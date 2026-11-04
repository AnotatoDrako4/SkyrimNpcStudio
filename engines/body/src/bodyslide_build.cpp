#include "sns/body/bodyslide_build.hpp"

#include <NifFile.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>
#include <format>
#include <optional>
#include <sstream>

#include "sns/core/error.hpp"
#include "sns/nif/safe_load.hpp"

namespace sns::body {

namespace {

// SliderManager::InitializeSliders: the preset's value for this size, else the slider's default (percent).
float slider_value(const ProjectSlider& s, const std::vector<SliderSetting>& preset, bool big) {
  const auto* setting = find_setting(preset, s.name);
  const auto v = setting ? (big ? setting->big : setting->small) : std::nullopt;
  return v ? *v : (big ? s.default_big : s.default_small);
}

// UV sliders, as DiffDataSets::ApplyUVDiff: the offset's x and y move u and v by the slider's value (inverted like any slider).
void apply_uv_sliders(nifly::NifFile& nif, nifly::NiShape* shape, const std::string& name, const SliderProject& project, const DiffSets& data,
                      const std::vector<SliderSetting>& preset, bool big) {
  const auto* project_shape = project.shape(name);
  std::vector<nifly::Vector2> uvs;
  if (!project_shape || !nif.GetUvsForShape(shape, uvs)) return;
  bool changed = false;
  for (const auto& s : project.sliders) {
    if (!s.uv) continue;
    float value = slider_value(s, preset, big) / 100.0f;
    if (s.invert) value = 1.0f - value;
    if (value == 0.0f) continue;
    for (const auto& d : s.data) {
      const auto it = data.find(d.name);
      if (it == data.end() || it->second.target != project_shape->target) continue;
      for (const auto& o : it->second.offsets) {
        if (o.index >= uvs.size()) continue;
        uvs[o.index].u += o.x * value;
        uvs[o.index].v += o.y * value;
        changed = true;
      }
    }
  }
  if (changed) nif.SetUvsForShape(shape, uvs);
}

// The sliders on the shape's own vertices: apply_project_sliders on a shape whose positions are the raw vertex data
// (to_root identity), which is what BodySlide's ApplySliders edits.
std::optional<BuildReport> apply_raw(nifly::NifFile& nif, const std::string& name, const SliderProject& project, const DiffSets& data,
                                     const std::vector<SliderSetting>& preset, bool big) {
  auto* shape = nif.FindBlockByName<nifly::NiShape>(name);
  std::vector<nifly::Vector3> verts;
  if (!shape || !nif.GetVertsForShape(shape, verts)) return std::nullopt;
  nif::Shape raw;
  raw.name = name;
  raw.positions.reserve(verts.size());
  for (const auto& v : verts) raw.positions.push_back({v.x, v.y, v.z});
  auto report = apply_project_sliders(raw, project, data, preset, big);
  for (std::size_t i = 0; i < verts.size(); ++i) verts[i] = {raw.positions[i].x, raw.positions[i].y, raw.positions[i].z};
  nif.SetVertsForShape(shape, verts);
  apply_uv_sliders(nif, shape, name, project, data, preset, big);
  return report;
}

// Phase 3 of BuildBodies for one file: normals, tangents, zap.
void finish_shape(nifly::NifFile& nif, const ProjectShape& ps, const std::vector<std::uint16_t>& zapped, bool keep_zapped,
                  SliderSetShapeBuild& report) {
  auto* shape = nif.FindBlockByName<nifly::NiShape>(ps.name);
  std::vector<nifly::Vector3> verts;
  if (!shape || !nif.GetVertsForShape(shape, verts)) return;
  if (!ps.lock_normals) {
    nif.CalcNormalsForShape(shape, false, ps.smooth_seam_normals);
    report.normals_recalculated = true;
  }
  nif.CalcTangentsForShape(shape);
  if (keep_zapped && zapped.size() == verts.size()) {
    shape->flags |= 1;
    report.deleted = true;
  } else if (!zapped.empty() && nif.DeleteVertsForShape(shape, zapped)) {
    nif.DeleteShape(shape);
    report.deleted = true;
  }
}

// BodySlideApp::WriteMorphTRI for one built file: per project shape, every slider that is not clamp or zap contributes the
// offsets DiffDataSets::ApplyDiff leaves at 1.0 on a zero vector (the data of the shape's target), the zapped vertices erased
// (later indices shift down) and the vertices whose offset is zero within nifly's epsilon dropped. UV sliders go to the UV
// section. Called before the zap is applied, so `nif` still has every vertex.
TriFile morph_tri(nifly::NifFile& nif, const SliderProject& project, const DiffSets& data, const std::vector<std::vector<std::uint16_t>>& zap) {
  TriFile tri;
  for (std::size_t i = 0; i < project.shapes.size(); ++i) {
    const auto& ps = project.shapes[i];
    auto* shape = nif.FindBlockByName<nifly::NiShape>(ps.name);
    if (!shape) continue;
    const std::size_t count = shape->GetNumVertices();
    std::vector<int> remap(count, 0);
    for (const auto z : zap[i]) {
      if (z < count) remap[z] = -1;
    }
    for (int next = 0; auto& r : remap) r = r < 0 ? -1 : next++;
    TriShape out{ps.name, {}, {}};
    for (const auto& s : project.sliders) {
      if (s.clamp || s.zap) continue;
      std::vector<nifly::Vector3> offsets(count);
      // SliderData::TargetDataName: the slider's first data entry for this shape's target.
      const auto d = std::find_if(s.data.begin(), s.data.end(), [&](const auto& x) { return x.target == ps.target; });
      if (d == s.data.end()) continue;
      const auto set = data.find(d->name);
      if (set == data.end() || set->second.target != ps.target) continue;
      for (const auto& o : set->second.offsets) {
        if (o.index < count) offsets[o.index] += nifly::Vector3(o.x, o.y, o.z);
      }
      MorphData morph{s.name, {}};
      for (std::size_t v = 0; v < count; ++v) {
        if (remap[v] < 0 || offsets[v].IsZero(true)) continue;
        morph.offsets.push_back({static_cast<std::uint16_t>(remap[v]), offsets[v].x, offsets[v].y, offsets[v].z});
      }
      if (!morph.offsets.empty()) (s.uv ? out.uv : out.position).push_back(std::move(morph));
    }
    if (!out.position.empty() || !out.uv.empty()) tri.shapes.push_back(std::move(out));
  }
  // TriFile keeps its shapes in a std::map: the file lists them by name (byte order), whatever the project's order.
  std::stable_sort(tri.shapes.begin(), tri.shapes.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
  return tri;
}

// The BODYTRI value: the TRI path with single backslashes, relative to the meshes folder (BuildBodies' triPathTrimmed).
std::string body_tri_value(std::string path) {
  std::replace(path.begin(), path.end(), '/', '\\');
  std::string collapsed;
  for (const char c : path) {
    if (c == '\\' && !collapsed.empty() && collapsed.back() == '\\') continue;
    collapsed += c;
  }
  std::string lower = collapsed;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower.starts_with("meshes\\")) return collapsed.substr(7);
  if (const auto at = lower.find("\\meshes\\"); at != std::string::npos) return collapsed.substr(at + 8);
  return collapsed;
}

// BuildBodies hangs one BODYTRI string on the first shape of the project that the built file still has (measured on files
// BodySlide wrote: Heels -> "Feet", Top Hoodless -> "3BA", whatever the NIF's block order); an existing one is rewritten.
void set_body_tri(nifly::NifFile& nif, const SliderProject& project, const std::string& value) {
  nifly::NiShape* carrier = nullptr;
  for (const auto& ps : project.shapes) {
    if ((carrier = nif.FindBlockByName<nifly::NiShape>(ps.name))) break;
  }
  if (!carrier) return;
  for (const auto& ref : carrier->extraDataRefs) {
    auto* extra = nif.GetHeader().GetBlock<nifly::NiStringExtraData>(ref);
    if (extra && extra->name.get() == "BODYTRI") {
      extra->stringData.get() = value;
      return;
    }
  }
  auto extra = std::make_unique<nifly::NiStringExtraData>();
  extra->name.get() = "BODYTRI";
  extra->stringData.get() = value;
  nif.AssignExtraData(carrier, std::move(extra));
}

std::string save(nifly::NifFile& nif, std::vector<std::string>& errors, std::string_view label) {
  nifly::NifSaveOptions options;
  options.optimize = false;  // as BodySlide
  std::ostringstream stream(std::ios::out | std::ios::binary);
  if (nif.Save(stream, options) != 0) {
    errors.push_back(std::format("nifly no pudo escribir {}", label));
    return {};
  }
  return std::move(stream).str();
}

using V = nif::Vec3;
V sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V mul(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Closest point of triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5).
V closest_on_triangle(V p, V a, V b, V c) {
  const V ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  const V bp = sub(p, b);
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(a, mul(ab, d1 / (d1 - d3)));
  const V cp = sub(p, c);
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(a, mul(ac, d2 / (d2 - d6)));
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return add(b, mul(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
  const float denom = 1.0f / (va + vb + vc);
  return add(a, add(mul(ab, vb * denom), mul(ac, vc * denom)));
}

}  // namespace

const SliderProject* family_reference_project(const std::vector<SliderProject>& projects, const std::vector<SliderSetting>& preset) {
  const SliderProject* best = nullptr;
  std::size_t best_score = 0;
  for (const auto& project : projects) {
    std::size_t score = 0;
    for (const auto& s : project.sliders) {
      if (slider_value(s, preset, true) != 0.0f || slider_value(s, preset, false) != 0.0f) ++score;
    }
    if (score > best_score) {
      best = &project;
      best_score = score;
    }
  }
  return best;
}

BatchDecision decide_batch_set(const SliderProject& project, const SliderProject& reference, const std::vector<SliderSetting>& preset) {
  std::vector<std::string> family_sliders;
  for (const auto& s : reference.sliders) family_sliders.push_back(s.name);
  const auto top_folder = [](const SliderProject& p) {
    const auto mesh = p.output_mesh(-1);  // "meshes\\<top>\\..."
    const auto start = mesh.find('\\') + 1;
    return mesh.substr(start, mesh.find('\\', start) - start);
  };
  std::vector<std::string> names;
  bool moved = false;
  for (const auto& s : project.sliders) {
    names.push_back(s.name);
    moved = moved || slider_value(s, preset, true) != 0.0f || slider_value(s, preset, false) != 0.0f;
  }
  const auto match = compare_vocabulary(family_sliders, names);
  if (match.verdict != "compatible") return {"other-family", match.reason};
  if (!moved) return {"untouched", "Ningún slider de este proyecto tiene valor en el preset."};
  if (names.size() < kMinFamilyProofSliders && top_folder(project) != top_folder(reference)) {
    return {"ambiguous", std::format("Solo {} sliders y fuera de la carpeta de la familia: sus sliders no prueban a qué cuerpo pertenece. {}", names.size(), match.reason)};
  }
  return {"build", match.reason};
}

PenetrationReport measure_penetration(const nif::Shape& body, const nif::Shape& garment, float reach, float tolerance) {
  PenetrationReport r;
  r.garment_vertices = garment.positions.size();
  struct Tri {
    V a, b, c, n, lo, hi;
  };
  std::vector<Tri> tris;
  for (std::size_t t = 0; t + 2 < body.indices.size(); t += 3) {
    const auto i0 = body.indices[t], i1 = body.indices[t + 1], i2 = body.indices[t + 2];
    if (i0 >= body.positions.size() || i1 >= body.positions.size() || i2 >= body.positions.size()) continue;
    Tri tri{body.positions[i0], body.positions[i1], body.positions[i2], {}, {}, {}};
    const V n = cross(sub(tri.b, tri.a), sub(tri.c, tri.a));
    const float len = std::sqrt(dot(n, n));
    if (len <= 1e-12f) continue;
    tri.n = mul(n, 1.0f / len);
    tri.lo = {std::min({tri.a.x, tri.b.x, tri.c.x}) - reach, std::min({tri.a.y, tri.b.y, tri.c.y}) - reach, std::min({tri.a.z, tri.b.z, tri.c.z}) - reach};
    tri.hi = {std::max({tri.a.x, tri.b.x, tri.c.x}) + reach, std::max({tri.a.y, tri.b.y, tri.c.y}) + reach, std::max({tri.a.z, tri.b.z, tri.c.z}) + reach};
    tris.push_back(tri);
  }
  double gap_sum = 0.0;
  for (const auto& p : garment.positions) {
    float best = reach * reach;
    float signed_distance = 0.0f;
    bool found = false;
    for (const auto& tri : tris) {
      if (p.x < tri.lo.x || p.y < tri.lo.y || p.z < tri.lo.z || p.x > tri.hi.x || p.y > tri.hi.y || p.z > tri.hi.z) continue;
      const V q = closest_on_triangle(p, tri.a, tri.b, tri.c);
      const V d = sub(p, q);
      const float d2 = dot(d, d);
      if (d2 >= best) continue;
      best = d2;
      signed_distance = dot(d, tri.n) < 0 ? -std::sqrt(d2) : std::sqrt(d2);
      found = true;
    }
    if (!found) continue;
    ++r.near_body;
    gap_sum += signed_distance;
    if (signed_distance < -tolerance) {
      ++r.inside;
      r.max_depth = std::max(r.max_depth, -signed_distance);
    }
  }
  if (r.near_body) r.mean_gap = static_cast<float>(gap_sum / static_cast<double>(r.near_body));
  return r;
}

bool SliderSetBuild::vertex_count_changed() const {
  // A shape deleted whole leaves the other shapes' morphs valid (RaceMenu matches TRI morphs by shape name).
  return std::any_of(shapes.begin(), shapes.end(), [](const auto& s) { return s.zapped > 0 && !s.deleted; });
}

std::string serialize_tri(const TriFile& tri) {
  std::string out("PIRT");
  const auto put = [&out](const auto value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
  const auto put_name = [&](const std::string& name) {
    put(static_cast<std::uint8_t>(name.size()));
    out += name;
  };
  // One section: shapes with morphs of this kind, each morph quantized to int16 against its largest component.
  const auto section = [&](auto morphs_of, bool uv) {
    std::uint16_t shapes = 0;
    for (const auto& s : tri.shapes) shapes += morphs_of(s).empty() ? 0 : 1;
    put(shapes);
    for (const auto& s : tri.shapes) {
      const auto& morphs = morphs_of(s);
      if (morphs.empty()) continue;
      put_name(s.name);
      put(static_cast<std::uint16_t>(morphs.size()));
      for (const auto& m : morphs) {
        put_name(m.name);
        float mult = 0.0f;
        for (const auto& o : m.offsets) mult = std::max({mult, std::fabs(o.x), std::fabs(o.y), uv ? 0.0f : std::fabs(o.z)});
        mult /= 0x7FFF;
        put(mult);
        put(static_cast<std::uint16_t>(m.offsets.size()));
        for (const auto& o : m.offsets) {
          put(o.index);
          put(static_cast<std::int16_t>(o.x / mult));
          put(static_cast<std::int16_t>(o.y / mult));
          if (!uv) put(static_cast<std::int16_t>(o.z / mult));
        }
      }
    }
  };
  section([](const TriShape& s) -> const std::vector<MorphData>& { return s.position; }, false);
  section([](const TriShape& s) -> const std::vector<MorphData>& { return s.uv; }, true);
  return out;
}

SliderSetBuild build_slider_set(const SliderProject& project, std::string_view source_nif, const DiffSets& data,
                                const std::vector<SliderSetting>& preset, bool build_morphs) {
  SliderSetBuild build;
  build_morphs = build_morphs && !project.prevent_morph_file;
  nifly::NifFile big;
  try {
    nif::load_nifly(big, source_nif, project.source_mesh());
  } catch (const Error& e) {
    build.errors.push_back(std::format("NIF fuente ilegible ({}): {}", project.source_mesh(), e.what()));
    return build;
  }
  nifly::NifFile small;
  if (project.gen_weights) small.CopyFrom(big);

  // BuildBodies keeps one zap list per shape, from its last ApplySliders call (the _0 one with GenWeights), for both files.
  std::vector<std::vector<std::uint16_t>> zap(project.shapes.size());
  for (std::size_t i = 0; i < project.shapes.size(); ++i) {
    const auto& ps = project.shapes[i];
    SliderSetShapeBuild report;
    report.shape = ps.name;
    const auto high = apply_raw(big, ps.name, project, data, preset, true);
    if (!high) {
      build.shapes.push_back(report);  // BuildBodies skips a project shape the NIF does not have
      continue;
    }
    if (!high->report.error.empty()) build.errors.push_back(ps.name + ": " + high->report.error);
    report.vertices = high->report.vertex_count;
    report.sliders_applied = high->report.applied.size();
    report.max_displacement = high->report.max_displacement;
    zap[i] = high->zapped;
    if (project.gen_weights) {
      const auto low = apply_raw(small, ps.name, project, data, preset, false);
      zap[i] = low ? low->zapped : std::vector<std::uint16_t>{};
    }
    report.zapped = zap[i].size();
    build.shapes.push_back(std::move(report));
  }
  if (!build.errors.empty()) return build;

  if (build_morphs) {
    build.tri_path = project.output_path + "\\" + project.output_file + ".tri";
    build.tri = serialize_tri(morph_tri(big, project, data, zap));
  }
  for (std::size_t i = 0; i < project.shapes.size(); ++i) {
    finish_shape(big, project.shapes[i], zap[i], project.keep_zapped_shapes, build.shapes[i]);
    if (project.gen_weights) {
      SliderSetShapeBuild ignored;
      finish_shape(small, project.shapes[i], zap[i], project.keep_zapped_shapes, ignored);
    }
  }
  if (build_morphs) {
    const auto value = body_tri_value(build.tri_path);
    set_body_tri(big, project, value);
    if (project.gen_weights) set_body_tri(small, project, value);
  }
  build.big = save(big, build.errors, project.output_mesh(project.gen_weights ? 1 : -1));
  if (project.gen_weights) build.small = save(small, build.errors, project.output_mesh(0));
  if (!build.errors.empty()) {
    build.big.clear();
    build.small.clear();
    build.tri.clear();
  }
  return build;
}

}  // namespace sns::body
