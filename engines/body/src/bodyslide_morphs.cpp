#include "sns/body/bodyslide_morphs.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <set>

#include "sns/body/xml_lite.hpp"
#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/mo2/vfs.hpp"

namespace sns::body {

namespace {

// Little-endian reader with bounds checks: a short file is a parse error, never an out-of-range read.
class Reader {
 public:
  Reader(std::string_view bytes, std::string_view label) : b_(bytes), label_(label) {}
  [[nodiscard]] std::size_t remaining() const { return b_.size() - pos_; }
  template <class T>
  T read() {
    need(sizeof(T));
    T v;
    std::memcpy(&v, b_.data() + pos_, sizeof(T));
    pos_ += sizeof(T);
    return v;
  }
  std::string name() {
    const auto n = read<std::uint8_t>();
    need(n);
    std::string s(b_.substr(pos_, n));
    pos_ += n;
    return s;
  }
  void need(std::size_t n) const {
    if (remaining() < n) throw Error(ErrorKind::Parse, std::format("{}: archivo truncado en el byte {}", label_, pos_));
  }

 private:
  std::string_view b_;
  std::string label_;
  std::size_t pos_ = 0;
};

nif::Vec3 sub(const nif::Vec3& a, const nif::Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
nif::Vec3 add(const nif::Vec3& a, const nif::Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
float length(const nif::Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
nif::Vec3 normalized(const nif::Vec3& v) {
  const float l = length(v);
  return l > 1e-12f ? nif::Vec3{v.x / l, v.y / l, v.z / l} : nif::Vec3{0, 0, 0};
}

// The linear part of shape.to_root: an offset in vertex space becomes an offset in the space of shape.positions.
nif::Vec3 map_offset(const nif::Transform& t, const VertexDelta& d) { return sub(t.apply({d.x, d.y, d.z}), t.translation); }

// Area-weighted face normals summed per vertex (not welded: only the change matters).
std::vector<nif::Vec3> face_normal_sums(const nif::Shape& shape) {
  std::vector<nif::Vec3> sum(shape.positions.size());
  const auto n = shape.positions.size();
  for (std::size_t t = 0; t + 2 < shape.indices.size(); t += 3) {
    const auto i0 = shape.indices[t], i1 = shape.indices[t + 1], i2 = shape.indices[t + 2];
    if (i0 >= n || i1 >= n || i2 >= n) continue;
    const auto e1 = sub(shape.positions[i1], shape.positions[i0]);
    const auto e2 = sub(shape.positions[i2], shape.positions[i0]);
    const nif::Vec3 fn{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
    for (const auto i : {i0, i1, i2}) sum[i] = add(sum[i], fn);
  }
  return sum;
}

// Moves the stored normals by the change of the geometric normals, so lighting follows the new shape while the authored
// normal detail (seams, hard edges) is kept. Shapes without normals keep none.
void follow_geometry(nif::Shape& shape, const std::vector<nif::Vec3>& before) {
  if (shape.normals.size() != shape.positions.size()) return;
  const auto after = face_normal_sums(shape);
  for (std::size_t i = 0; i < shape.normals.size(); ++i) {
    const auto d = sub(normalized(after[i]), normalized(before[i]));
    if (std::abs(d.x) + std::abs(d.y) + std::abs(d.z) < 1e-6f) continue;
    const auto n = normalized(add(shape.normals[i], d));
    if (length(n) > 0.5f) shape.normals[i] = n;
  }
}

std::string normalize_separators(std::string s) {
  std::replace(s.begin(), s.end(), '/', '\\');
  return s;
}

std::optional<float> number(std::string_view s) {
  s = text::trim(s);
  if (s.empty()) return std::nullopt;
  float v = 0.0f;
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{}) return std::nullopt;
  return v;
}

bool attr_true(const XmlElement& e, std::string_view key) {  // BodySlide: StringsEqualNInsens(value, "true", 4)
  const auto v = e.attribute(key);
  return v && v->size() >= 4 && text::iequals_ascii(v->substr(0, 4), "true");
}

float attr_float(const XmlElement& e, std::string_view key) {  // tinyxml2 FloatAttribute: 0 when missing or unreadable
  const auto v = e.attribute(key);
  return v ? number(*v).value_or(0.0f) : 0.0f;
}

std::string child_text(const XmlElement& e, std::string_view a, std::string_view b = {}) {
  for (const auto& c : e.children) {
    if (c.name == a || (!b.empty() && c.name == b)) return std::string(text::trim(c.text));
  }
  return {};
}

}  // namespace

// ---------------------------------------------------------------- TRI

const MorphData* TriShape::find(std::string_view slider) const {
  const auto it = std::find_if(position.begin(), position.end(), [&](const auto& m) { return m.name == slider; });
  return it == position.end() ? nullptr : &*it;
}

const TriShape* TriFile::shape(std::string_view name) const {
  const auto it = std::find_if(shapes.begin(), shapes.end(), [&](const auto& s) { return s.name == name; });
  return it == shapes.end() ? nullptr : &*it;
}

TriFile parse_tri(std::string_view bytes, std::string_view label) {
  Reader r(bytes, label);
  r.need(4);
  if (bytes.substr(0, 4) != "PIRT") throw Error(ErrorKind::Parse, std::format("{}: no es un TRI de BodySlide (cabecera)", label));
  (void)r.read<std::uint32_t>();
  TriFile file;
  const auto shapes = r.read<std::uint16_t>();
  for (std::uint16_t s = 0; s < shapes; ++s) {
    TriShape shape;
    shape.name = r.name();
    const auto morphs = r.read<std::uint16_t>();
    for (std::uint16_t m = 0; m < morphs; ++m) {
      MorphData morph;
      morph.name = r.name();
      const auto mult = r.read<float>();
      const auto count = r.read<std::uint16_t>();
      morph.offsets.reserve(count);
      for (std::uint16_t k = 0; k < count; ++k) {
        const auto id = r.read<std::uint16_t>();
        const auto x = r.read<std::int16_t>();
        const auto y = r.read<std::int16_t>();
        const auto z = r.read<std::int16_t>();
        morph.offsets.push_back({id, x * mult, y * mult, z * mult});
      }
      shape.position.push_back(std::move(morph));
    }
    file.shapes.push_back(std::move(shape));
  }
  if (r.remaining() == 0) return file;
  // UV section: BodySlide reads it when the position section did not reach the end of the file.
  try {
    const auto uv_shapes = r.read<std::uint16_t>();
    for (std::uint16_t s = 0; s < uv_shapes; ++s) {
      const auto name = r.name();
      auto it = std::find_if(file.shapes.begin(), file.shapes.end(), [&](const auto& x) { return x.name == name; });
      if (it == file.shapes.end()) {
        file.shapes.push_back(TriShape{name, {}, {}});
        it = std::prev(file.shapes.end());
      }
      const auto morphs = r.read<std::uint16_t>();
      for (std::uint16_t m = 0; m < morphs; ++m) {
        MorphData morph;
        morph.name = r.name();
        const auto mult = r.read<float>();
        const auto count = r.read<std::uint16_t>();
        for (std::uint16_t k = 0; k < count; ++k) {
          const auto id = r.read<std::uint16_t>();
          const auto u = r.read<std::int16_t>();
          const auto v = r.read<std::int16_t>();
          morph.offsets.push_back({id, u * mult, v * mult, 0.0f});
        }
        it->uv.push_back(std::move(morph));
      }
    }
  } catch (const Error& e) {
    file.warnings.push_back(std::string("Sección UV del TRI ilegible: ") + e.what());
  }
  return file;
}

// ---------------------------------------------------------------- OSD / BSD

OsdFile parse_osd(std::string_view bytes, std::string_view label) {
  Reader r(bytes, label);
  r.need(4);
  // BodySlide writes the multi-character constant 'OSD\0' as a little-endian uint32: bytes 00 'D' 'S' 'O'.
  if (bytes.substr(0, 4) != std::string_view("\0DSO", 4)) {
    throw Error(ErrorKind::Parse, std::format("{}: no es un OSD de Outfit Studio (cabecera)", label));
  }
  (void)r.read<std::uint32_t>();
  OsdFile file;
  file.version = r.read<std::uint32_t>();
  const auto count = r.read<std::uint32_t>();
  for (std::uint32_t i = 0; i < count; ++i) {
    auto name = r.name();
    const auto diffs = r.read<std::uint16_t>();
    std::vector<VertexDelta> offsets;
    offsets.reserve(diffs);
    for (std::uint16_t k = 0; k < diffs; ++k) {
      VertexDelta d;
      d.index = r.read<std::uint16_t>();
      d.x = r.read<float>();
      d.y = r.read<float>();
      d.z = r.read<float>();
      offsets.push_back(d);
    }
    file.data[std::move(name)] = std::move(offsets);
  }
  return file;
}

std::vector<VertexDelta> parse_bsd(std::string_view bytes, std::string_view label) {
  Reader r(bytes, label);
  const auto count = r.read<std::uint32_t>();
  std::vector<VertexDelta> out;
  out.reserve(std::min<std::size_t>(count, bytes.size() / 16));
  for (std::uint32_t i = 0; i < count; ++i) {
    VertexDelta d;
    d.index = static_cast<std::uint16_t>(r.read<std::uint32_t>());
    d.x = r.read<float>();
    d.y = r.read<float>();
    d.z = r.read<float>();
    out.push_back(d);
  }
  return out;
}

// ---------------------------------------------------------------- OSP

std::string SliderProject::output_mesh(int variant) const {
  std::string path = normalize_separators(output_path);
  while (!path.empty() && path.back() == '\\') path.pop_back();
  path += "\\" + output_file + (variant >= 0 ? "_" + std::to_string(variant) : std::string()) + ".nif";
  auto key = text::data_path_key(path);
  if (!key.starts_with("meshes\\")) key = "meshes\\" + key;
  return key;
}

std::string SliderProject::source_mesh() const {
  return std::string(kShapeDataDir) + "\\" + normalize_separators(data_folder) + "\\" + normalize_separators(source_file);
}

const ProjectShape* SliderProject::shape_for_target(std::string_view target) const {
  const auto it = std::find_if(shapes.begin(), shapes.end(), [&](const auto& s) { return s.target == target; });
  return it == shapes.end() ? nullptr : &*it;
}

const ProjectShape* SliderProject::shape(std::string_view shape_name) const {
  const auto it = std::find_if(shapes.begin(), shapes.end(), [&](const auto& s) { return s.name == shape_name; });
  return it == shapes.end() ? nullptr : &*it;
}

std::vector<SliderProject> parse_slider_projects(std::string_view xml_text, std::string_view virtual_file) {
  const auto root = parse_xml(xml_text);
  std::vector<SliderProject> out;
  for (const auto& set : root.children) {
    if (set.name != "SliderSet") continue;
    SliderProject p;
    p.name = std::string(set.attribute("name").value_or(""));
    p.file = std::string(virtual_file);
    p.data_folder = normalize_separators(child_text(set, "DataFolder", "SetFolder"));
    p.source_file = child_text(set, "SourceFile");
    p.output_path = child_text(set, "OutputPath");
    for (const auto& c : set.children) {
      if (c.name != "OutputFile") continue;
      p.output_file = std::string(text::trim(c.text));
      if (const auto gw = c.attribute("GenWeights")) p.gen_weights = !(text::iequals_ascii(*gw, "false") || *gw == "0");
      p.keep_zapped_shapes = attr_true(c, "KeepZappedShapes");
      p.prevent_morph_file = attr_true(c, "PreventMorphFile");
    }
    for (const auto& c : set.children) {
      if (c.name != "Shape" && c.name != "BaseShapeName") continue;
      ProjectShape shape;
      shape.name = std::string(text::trim(c.text));
      if (shape.name.empty()) continue;
      shape.target = std::string(c.attribute("target").value_or(shape.name));
      if (const auto df = c.attribute("DataFolder")) {
        std::string_view rest = *df;
        while (!rest.empty()) {
          const auto semi = rest.find(';');
          const auto part = text::trim(rest.substr(0, semi));
          if (!part.empty()) shape.data_folders.push_back(normalize_separators(std::string(part)));
          rest = semi == std::string_view::npos ? std::string_view{} : rest.substr(semi + 1);
        }
      }
      if (shape.data_folders.empty()) shape.data_folders.push_back(p.data_folder);
      if (const auto smooth = c.attribute("SmoothSeamNormals")) shape.smooth_seam_normals = !(text::iequals_ascii(*smooth, "false") || *smooth == "0");
      shape.lock_normals = attr_true(c, "LockNormals");
      p.shapes.push_back(std::move(shape));
    }
    for (const auto& c : set.children) {
      if (c.name != "Slider") continue;
      ProjectSlider s;
      s.name = std::string(c.attribute("name").value_or(""));
      s.invert = attr_true(c, "invert");
      s.zap = attr_true(c, "zap");
      s.clamp = attr_true(c, "clamp");
      s.uv = attr_true(c, "uv");
      s.hidden = attr_true(c, "hidden");
      if (p.gen_weights) {
        s.default_small = attr_float(c, "small");
        s.default_big = attr_float(c, "big");
      } else {
        s.default_small = s.default_big = attr_float(c, "default");
      }
      for (const auto& d : c.children) {
        if (d.name != "Data" && d.name != "datafile") continue;
        ProjectSliderData data;
        data.name = std::string(d.attribute("name").value_or(s.name));
        data.target = std::string(d.attribute("target").value_or(""));
        data.local = attr_true(d, "local");
        data.file = std::string(text::trim(d.text));
        s.data.push_back(std::move(data));
      }
      p.sliders.push_back(std::move(s));
    }
    out.push_back(std::move(p));
  }
  return out;
}

// ---------------------------------------------------------------- values

std::optional<float> preset_value(const SliderSetting& setting, float weight) {
  if (!setting.small && !setting.big) return std::nullopt;
  const float small = setting.small.value_or(setting.big.value_or(0.0f));
  const float big = setting.big.value_or(setting.small.value_or(0.0f));
  const float w = std::clamp(weight / 100.0f, 0.0f, 1.0f);
  return (small + (big - small) * w) / 100.0f;
}

const SliderSetting* find_setting(const std::vector<SliderSetting>& preset, std::string_view slider) {
  const auto it = std::find_if(preset.begin(), preset.end(), [&](const auto& s) { return s.name == slider; });
  return it == preset.end() ? nullptr : &*it;
}

// ---------------------------------------------------------------- application

ShapeMorphReport apply_tri_morphs(nif::Shape& shape, const TriShape& morphs, const std::vector<SliderSetting>& preset, float weight) {
  ShapeMorphReport report;
  report.shape = shape.name;
  report.vertex_count = shape.positions.size();
  report.morphs_available = morphs.position.size();
  report.preset_sliders = preset.size();

  struct Pending {
    const MorphData* morph;
    float value;
  };
  std::vector<Pending> pending;
  for (const auto& setting : preset) {
    const auto* morph = morphs.find(setting.name);
    if (morph) ++report.compatible;
    const auto value = preset_value(setting, weight);
    if (!value || *value == 0.0f) continue;
    if (!morph) {
      const bool uv_only = std::any_of(morphs.uv.begin(), morphs.uv.end(), [&](const auto& m) { return m.name == setting.name; });
      if (uv_only) report.notes.push_back(setting.name + ": morph de UV, no mueve vértices (no aplicado)");
      else report.not_in_data.push_back(setting.name);
      continue;
    }
    pending.push_back({morph, *value});
  }
  for (const auto& p : pending) {
    for (const auto& d : p.morph->offsets) {
      if (d.index >= report.vertex_count) {
        report.error = std::format("el morph «{}» mueve el vértice {} pero la forma «{}» solo tiene {}: el TRI no corresponde a esta malla",
                                   p.morph->name, d.index, shape.name, report.vertex_count);
        return report;
      }
    }
  }
  if (pending.empty()) return report;

  const auto before = face_normal_sums(shape);
  for (const auto& p : pending) {
    SliderApplied applied;
    applied.slider = p.morph->name;
    applied.value = p.value;
    for (const auto& d : p.morph->offsets) {
      const auto o = map_offset(shape.to_root, d);
      const nif::Vec3 scaled{o.x * p.value, o.y * p.value, o.z * p.value};
      const float len = length(scaled);
      if (len <= 0.0f) continue;
      shape.positions[d.index] = add(shape.positions[d.index], scaled);
      ++applied.vertices;
      applied.max_displacement = std::max(applied.max_displacement, len);
    }
    report.max_displacement = std::max(report.max_displacement, applied.max_displacement);
    if (applied.vertices > 0) report.applied.push_back(std::move(applied));
  }
  follow_geometry(shape, before);
  return report;
}

SliderCoverage slider_coverage(const ShapeMorphReport& report) {
  SliderCoverage c;
  c.found = report.applied.size();
  c.considered = c.found + report.not_in_data.size();
  c.ratio = c.considered ? static_cast<float>(c.found) / static_cast<float>(c.considered) : 1.0f;
  return c;
}

std::vector<std::string> morph_names(const TriFile& tri) {
  std::set<std::string> names;
  for (const auto& s : tri.shapes) {
    for (const auto& m : s.position) names.insert(m.name);
  }
  return {names.begin(), names.end()};
}

VocabularyMatch compare_vocabulary(const std::vector<std::string>& body_sliders, const std::vector<std::string>& outfit_sliders) {
  VocabularyMatch m;
  const std::set<std::string> body(body_sliders.begin(), body_sliders.end());
  const std::set<std::string> outfit(outfit_sliders.begin(), outfit_sliders.end());
  m.body = body.size();
  m.outfit = outfit.size();
  for (const auto& s : outfit) m.shared += body.count(s);
  if (m.body == 0 || m.outfit == 0) {
    m.verdict = "unknown";
    m.reason = m.body == 0 ? "El cuerpo no tiene morphs (TRI) con los que comparar." : "La prenda no tiene morphs (TRI).";
    return m;
  }
  m.ratio = static_cast<float>(m.shared) / static_cast<float>(m.outfit);
  m.verdict = m.ratio >= 0.6f ? "compatible" : m.ratio >= kMinFamilyCoverage ? "partial" : "incompatible";
  m.reason = std::format("{} de {} sliders de la prenda existen en el cuerpo ({:.0f} %; el cuerpo tiene {}).", m.shared, m.outfit,
                         m.ratio * 100.0f, m.body);
  return m;
}

ProjectData load_project_data(const SliderProject& project, const DataReader& read) {
  ProjectData out;
  std::map<std::string, std::optional<OsdFile>> osd_cache;
  auto folder_path = [](const std::string& folder, const std::string& file) {
    return std::string(kShapeDataDir) + "\\" + normalize_separators(folder) + "\\" + file;
  };
  for (const auto& slider : project.sliders) {
    for (const auto& data : slider.data) {
      const auto file = normalize_separators(data.file);
      if (file.size() <= 4) continue;
      std::vector<std::string> folders;
      if (data.local) {
        folders.push_back(project.data_folder);
      } else if (const auto* shape = project.shape_for_target(data.target)) {
        folders = shape->data_folders;
      } else {
        folders.push_back(project.data_folder);
      }
      bool found = false;
      if (text::ends_with_icase(file, ".bsd")) {
        for (const auto& folder : folders) {
          const auto path = folder_path(folder, file);
          const auto bytes = read(path);
          if (!bytes) continue;
          try {
            out.sets[data.name] = DiffSet{data.target, parse_bsd(*bytes, path)};
          } catch (const std::exception& e) {
            out.errors.push_back(e.what());
          }
          found = true;
          break;
        }
      } else {
        const auto split = file.find_last_of('\\');
        if (split == std::string::npos) {
          out.errors.push_back(std::format("{}: dato «{}» sin «archivo.osd\\nombre»", slider.name, data.file));
          continue;
        }
        const auto osd_name = file.substr(0, split);
        const auto entry = file.substr(split + 1);
        for (const auto& folder : folders) {
          const auto path = folder_path(folder, osd_name);
          auto it = osd_cache.find(text::data_path_key(path));
          if (it == osd_cache.end()) {
            std::optional<OsdFile> parsed;
            if (const auto bytes = read(path)) {
              try {
                parsed = parse_osd(*bytes, path);
              } catch (const std::exception& e) {
                out.errors.push_back(e.what());
              }
            }
            it = osd_cache.emplace(text::data_path_key(path), std::move(parsed)).first;
          }
          if (!it->second) continue;
          found = true;
          // BodySlide stores the entry under its name inside the file; the slider then looks it up by <Data name>.
          if (const auto d = it->second->data.find(entry); d != it->second->data.end()) out.sets[entry] = DiffSet{data.target, d->second};
          break;
        }
      }
      if (!found) out.missing.push_back(std::format("{} ({})", file, slider.name));
    }
  }
  std::sort(out.missing.begin(), out.missing.end());
  out.missing.erase(std::unique(out.missing.begin(), out.missing.end()), out.missing.end());
  return out;
}

BuildReport apply_project_sliders(nif::Shape& shape, const SliderProject& project, const DiffSets& data,
                                  const std::vector<SliderSetting>& preset, bool big) {
  BuildReport build;
  auto& report = build.report;
  report.shape = shape.name;
  report.vertex_count = shape.positions.size();
  report.preset_sliders = preset.size();
  const auto* project_shape = project.shape(shape.name);
  if (!project_shape) {
    report.error = std::format("la forma «{}» no está en el proyecto «{}»", shape.name, project.name);
    return build;
  }
  const auto& target = project_shape->target;
  const auto targets_shape = [&](const ProjectSlider& s) {
    return std::any_of(s.data.begin(), s.data.end(), [&](const auto& d) { return d.target == target; });
  };
  for (const auto& s : project.sliders) {
    if (!targets_shape(s)) continue;
    ++report.morphs_available;
    if (find_setting(preset, s.name)) ++report.compatible;
  }
  for (const auto& setting : preset) {
    const bool known = std::any_of(project.sliders.begin(), project.sliders.end(), [&](const auto& s) { return s.name == setting.name; });
    const auto v = big ? setting.big : setting.small;
    if (!known && v && *v != 0.0f) report.not_in_data.push_back(setting.name);
  }
  // SliderManager::InitializeSliders: the preset's value for this size, else the slider's default.
  const auto value_of = [&](const ProjectSlider& s) {
    const auto* setting = find_setting(preset, s.name);
    const auto v = setting ? (big ? setting->big : setting->small) : std::nullopt;
    return v ? *v / 100.0f : (big ? s.default_big : s.default_small) / 100.0f;
  };

  const auto before = face_normal_sums(shape);
  std::set<std::uint16_t> zapped;
  std::size_t skipped_indices = 0;
  for (const auto& s : project.sliders) {
    float value = value_of(s);
    if (s.zap && !s.uv) {
      if (value > 0) {
        for (const auto& d : s.data) {
          const auto it = data.find(d.name);
          if (it == data.end() || it->second.target != target) continue;
          for (const auto& o : it->second.offsets) zapped.insert(o.index);
        }
        if (targets_shape(s)) report.notes.push_back(s.name + ": zap activo (vértices eliminados en el build)");
      }
      continue;
    }
    if (s.invert) value = 1.0f - value;
    if (value == 0.0f) continue;
    SliderApplied applied;
    applied.slider = s.name;
    applied.value = value;
    applied.inverted = s.invert;
    for (const auto& d : s.data) {
      const auto it = data.find(d.name);
      if (it == data.end() || it->second.target != target) continue;
      if (s.uv) {
        report.notes.push_back(s.name + ": slider de UV (no mueve vértices)");
        continue;
      }
      for (const auto& o : it->second.offsets) {
        if (o.index >= report.vertex_count) {
          ++skipped_indices;  // DiffDataSets::ApplyDiff skips them silently
          continue;
        }
        const auto m = map_offset(shape.to_root, o);
        const nif::Vec3 scaled{m.x * value, m.y * value, m.z * value};
        const float len = length(scaled);
        if (len <= 0.0f) continue;
        shape.positions[o.index] = add(shape.positions[o.index], scaled);
        ++applied.vertices;
        applied.max_displacement = std::max(applied.max_displacement, len);
      }
    }
    report.max_displacement = std::max(report.max_displacement, applied.max_displacement);
    if (applied.vertices > 0) report.applied.push_back(std::move(applied));
  }
  for (const auto& s : project.sliders) {
    if (!s.clamp || value_of(s) <= 0) continue;
    for (const auto& d : s.data) {
      const auto it = data.find(d.name);
      if (it == data.end() || it->second.target != target) continue;
      for (const auto& o : it->second.offsets) {
        if (o.index < report.vertex_count) shape.positions[o.index] = shape.to_root.apply({o.x, o.y, o.z});
      }
      report.notes.push_back(s.name + ": clamp aplicado");
    }
  }
  if (skipped_indices > 0) {
    report.notes.push_back(std::format("{} desplazamientos con índice fuera de la malla ignorados (como BodySlide)", skipped_indices));
  }
  follow_geometry(shape, before);
  build.zapped.assign(zapped.begin(), zapped.end());
  return build;
}

std::vector<const SliderProject*> ProjectIndex::for_mesh(std::string_view data_path) const {
  std::vector<const SliderProject*> out;
  auto key = text::data_path_key(data_path);
  if (key.starts_with("data\\")) key.erase(0, 5);
  const auto [first, last] = by_output.equal_range(key);
  for (auto it = first; it != last; ++it) out.push_back(&projects[it->second]);
  return out;
}

ProjectIndex build_project_index(std::vector<SliderProject> projects) {
  ProjectIndex index;
  index.projects = std::move(projects);
  for (std::size_t i = 0; i < index.projects.size(); ++i) {
    const auto& p = index.projects[i];
    if (p.output_file.empty()) continue;
    if (p.gen_weights) {
      index.by_output.emplace(p.output_mesh(0), i);
      index.by_output.emplace(p.output_mesh(1), i);
    } else {
      index.by_output.emplace(p.output_mesh(-1), i);
    }
  }
  return index;
}

ProjectIndex load_project_index(const mo2::VirtualFileSystem& vfs) {
  std::vector<SliderProject> projects;
  std::vector<std::string> errors;
  for (const auto& file : vfs.list_directory(kSliderSetsDir, /*recursive=*/true)) {
    if (!text::ends_with_icase(file, ".osp")) continue;
    const auto resolved = vfs.resolve(file);
    if (!resolved) continue;
    try {
      auto parsed = parse_slider_projects(text::decode_text(file_io::read_file(resolved->absolute, 32ull * 1024 * 1024)), file);
      for (auto& p : parsed) projects.push_back(std::move(p));
    } catch (const std::exception& e) {
      errors.push_back(file + ": " + e.what());
    }
  }
  auto index = build_project_index(std::move(projects));
  index.errors = std::move(errors);
  return index;
}

BaseBuildCheck check_base_build(const SliderProject& project, const DataReader& read, const nif::MeshFile& built, std::string_view mesh_path) {
  BaseBuildCheck check;
  check.mesh = std::string(mesh_path);
  check.project = project.name;
  check.osp = project.file;
  const auto lower = text::to_lower_ascii(mesh_path);
  const bool big = !text::ends_with_icase(lower, "_0.nif");
  const auto source_bytes = read(project.source_mesh());
  if (!source_bytes) {
    check.verdict = "error";
    check.detail = "Falta el NIF fuente del proyecto: " + project.source_mesh();
    return check;
  }
  nif::MeshFile source;
  try {
    source = nif::load_mesh(*source_bytes, project.source_mesh());
  } catch (const std::exception& e) {
    check.verdict = "error";
    check.detail = e.what();
    return check;
  }
  const auto data = load_project_data(project, read);
  check.missing_data = data.missing;
  std::vector<SliderSetting> zeroed;
  for (const auto& s : project.sliders) zeroed.push_back({s.name, 0.0f, 0.0f});
  struct Reference {
    const char* name;
    const std::vector<SliderSetting>* preset;
  };
  const std::vector<SliderSetting> defaults;
  float best = std::numeric_limits<float>::max();
  std::string best_name;
  std::size_t compared = 0;
  bool comparable = true;
  for (const Reference ref : {Reference{"defaults", &defaults}, Reference{"zeroed", &zeroed}}) {
    float worst = 0.0f;
    std::size_t n = 0;
    for (const auto& ps : project.shapes) {
      const auto src = std::find_if(source.shapes.begin(), source.shapes.end(), [&](const auto& s) { return s.name == ps.name; });
      const auto out = std::find_if(built.shapes.begin(), built.shapes.end(), [&](const auto& s) { return s.name == ps.name; });
      if (src == source.shapes.end() || out == built.shapes.end()) continue;
      auto shape = *src;
      const auto b = apply_project_sliders(shape, project, data.sets, *ref.preset, big);
      auto positions = shape.positions;
      if (!b.zapped.empty()) {
        const std::set<std::uint16_t> zap(b.zapped.begin(), b.zapped.end());
        std::vector<nif::Vec3> kept;
        for (std::size_t i = 0; i < positions.size(); ++i) {
          if (!zap.count(static_cast<std::uint16_t>(i))) kept.push_back(positions[i]);
        }
        positions = std::move(kept);
      }
      const auto d = compare_positions(positions, out->positions, 1e-3f);
      if (!d) {
        comparable = false;
        continue;
      }
      worst = std::max(worst, d->max);
      ++n;
    }
    if (n > 0 && worst < best) {
      best = worst;
      best_name = ref.name;
      compared = n;
    }
  }
  check.shapes_compared = compared;
  if (compared == 0) {
    check.verdict = "not-comparable";
    check.detail = comparable ? "Ninguna forma del proyecto «" + project.name + "» está en la malla."
                              : "La malla no tiene los mismos vértices que la fuente del proyecto «" + project.name + "» (otra versión de la prenda).";
    return check;
  }
  check.max_distance = best;
  if (best <= 1e-3f) {
    check.verdict = "base";
    check.reference = best_name;
    check.detail = std::format("Construida sin preset ({}; máx {:.6f} u en {} forma(s)): Body Change NG puede aplicar el preset con sus morphs.",
                               best_name == "zeroed" ? "Zeroed Sliders" : "valores por defecto del proyecto", best, compared);
  } else {
    check.verdict = "preset-baked";
    check.detail = std::format("La malla lleva un preset horneado (difiere hasta {:.3f} u de la base sin preset del proyecto «{}»). "
                               "En el juego, Body Change NG aplicará el preset otra vez encima: reconstrúyela en BodySlide con "
                               "Zeroed Sliders + Build Morphs.",
                               best, project.name);
  }
  return check;
}

std::optional<ShapeDistance> compare_positions(const std::vector<nif::Vec3>& a, const std::vector<nif::Vec3>& b, float tolerance) {
  if (a.size() != b.size()) return std::nullopt;
  ShapeDistance d;
  double sum = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const float l = length(sub(a[i], b[i]));
    d.max = std::max(d.max, l);
    sum += l;
    if (l > tolerance) ++d.over_tolerance;
  }
  d.mean = a.empty() ? 0.0f : static_cast<float>(sum / static_cast<double>(a.size()));
  return d;
}

}  // namespace sns::body
