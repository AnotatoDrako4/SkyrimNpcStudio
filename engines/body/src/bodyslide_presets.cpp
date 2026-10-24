#include "sns/body/bodyslide_presets.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <format>
#include <set>
#include <unordered_map>

#include "sns/body/xml_lite.hpp"
#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/mo2/vfs.hpp"

namespace sns::body {

std::string_view family_id(BodyFamily family) {
  switch (family) {
    case BodyFamily::Cbbe3ba: return "3ba";
    case BodyFamily::Cbbe: return "cbbe";
    case BodyFamily::Ube: return "ube";
    case BodyFamily::Other: return "other";
    case BodyFamily::Unknown: break;
  }
  return "unknown";
}

std::string_view family_label(BodyFamily family) {
  switch (family) {
    case BodyFamily::Cbbe3ba: return "CBBE 3BA";
    case BodyFamily::Cbbe: return "CBBE";
    case BodyFamily::Ube: return "UBE";
    case BodyFamily::Other: return "Otra reconocida";
    case BodyFamily::Unknown: break;
  }
  return "Desconocida";
}

std::optional<BodyFamily> family_from_id(std::string_view id) {
  for (const auto f : {BodyFamily::Cbbe3ba, BodyFamily::Cbbe, BodyFamily::Ube, BodyFamily::Other, BodyFamily::Unknown}) {
    if (text::iequals_ascii(id, family_id(f))) return f;
  }
  return std::nullopt;
}

namespace {

std::optional<float> parse_number(std::string_view s) {
  s = text::trim(s);
  if (s.empty()) return std::nullopt;
  float v = 0.0f;
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || end != s.data() + s.size()) return std::nullopt;
  return v;
}

// --- Port of BCNG v1.3.7 BodyFamilyRules.cpp (Tokenize/DetectText/ClassifyText/ClassifyPreset) ---

// Lower-case ASCII alphanumeric tokens: "CBBE 3BBB Body Amazing" -> cbbe, 3bbb, body, amazing.
std::vector<std::string> tokenize(std::string_view s) {
  std::vector<std::string> out;
  std::string cur;
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x80 && std::isalnum(u)) {
      cur += static_cast<char>(std::tolower(u));
    } else if (!cur.empty()) {
      out.push_back(std::move(cur));
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

bool has_token(const std::vector<std::string>& t, std::string_view v) { return std::find(t.begin(), t.end(), v) != t.end(); }
bool has_fragment(const std::vector<std::string>& t, std::string_view v) {
  return std::any_of(t.begin(), t.end(), [&](const auto& tok) { return tok.find(v) != std::string::npos; });
}
bool has_sequence(const std::vector<std::string>& t, std::initializer_list<std::string_view> seq) {
  if (seq.size() == 0 || seq.size() > t.size()) return false;
  for (std::size_t off = 0; off + seq.size() <= t.size(); ++off) {
    if (std::equal(seq.begin(), seq.end(), t.begin() + static_cast<std::ptrdiff_t>(off))) return true;
  }
  return false;
}

unsigned detect_female(const std::vector<std::string>& t) {
  unsigned m = 0;
  if (has_fragment(t, "cbbe") || has_fragment(t, "3ba") || has_sequence(t, {"3bbb", "body", "amazing"})) m |= kFamCbbe;
  if (has_fragment(t, "bhunp") || has_fragment(t, "uunp") || has_fragment(t, "unpb") || has_token(t, "unp")) m |= kFamUnp;
  if (has_token(t, "ube") || has_token(t, "ube2") || has_fragment(t, "ubebody")) m |= kFamUbe;
  return m;
}

unsigned detect_male(const std::vector<std::string>& t) {
  unsigned m = 0;
  if (has_fragment(t, "himbo")) m |= kFamHimbo;
  if (has_token(t, "sam") || has_token(t, "samlight") || has_token(t, "samhighpoly")) m |= kFamSam;
  return m;
}

// "UBE Anus" in 3BA set names denotes only the borrowed anus part, not a UBE body.
std::string without_ube_anus(std::string_view text) {
  const auto t = tokenize(text);
  std::string out;
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i] == "ube" && i + 1 < t.size() && t[i + 1] == "anus") {
      ++i;
      continue;
    }
    out += t[i];
    out += ' ';
  }
  return out;
}

struct TextClass {
  unsigned mask = 0;
  bool conflict = false;
  bool male = false;
};

TextClass classify_text(std::string_view text) {
  const auto t = tokenize(text);
  const auto female = detect_female(t), male = detect_male(t);
  if (female && male) return {0, true, false};
  if (female) return {female, false, false};
  if (male) return {male, false, true};
  return {};
}

bool names_3ba(std::string_view text) {
  const auto t = tokenize(text);
  return has_fragment(t, "3ba") || has_fragment(t, "3bbb") || has_sequence(t, {"body", "amazing"});
}

}  // namespace

std::string mask_label(unsigned mask) {
  std::string label;
  auto append = [&](std::string_view v) {
    if (!label.empty()) label += " / ";
    label += v;
  };
  if (mask & kFamCbbe) append("CBBE 3BA");
  if (mask & kFamUnp) append("BHUNP / UNP");
  if (mask & kFamUbe) append("UBE");
  if (mask & kFamHimbo) append("HIMBO");
  if (mask & kFamSam) append("SAM");
  return label.empty() ? std::string("Unclassified") : label;
}

FamilyGuess classify_family(const BodySlidePreset& preset) {
  std::string groups;
  for (const auto& g : preset.groups) {
    if (g.empty()) continue;
    if (!groups.empty()) groups += ' ';
    groups += g;
  }
  struct Evidence {
    std::string text;
    std::string label;
    bool metadata;
  };
  const Evidence order[] = {
      {without_ube_anus(preset.set), std::format("set «{}»", preset.set), false},
      {without_ube_anus(groups), std::format("grupos «{}»", groups), false},
      {preset.name + ' ' + (preset.source.empty() ? preset.file : preset.source),
       std::format("nombre y archivo «{}» / «{}»", preset.name, preset.source.empty() ? preset.file : preset.source), true},
  };
  FamilyGuess g;
  const Evidence* decided = nullptr;
  TextClass c;
  for (const auto& e : order) {
    c = classify_text(e.text);
    if (c.mask != 0 || c.conflict) {
      decided = &e;
      break;
    }
  }
  if (!decided) {
    g.reason = "ni el set, ni los grupos, ni el nombre o el archivo nombran un cuerpo (BCNG: Unclassified)";
    return g;
  }
  g.mask = c.mask;
  g.conflict = c.conflict;
  g.male = c.male;
  g.metadata_fallback = decided->metadata;
  if (c.conflict) {
    g.reason = std::format("{}: nombra familias femeninas y masculinas a la vez (BCNG: Unclassified)", decided->label);
    return g;
  }
  g.reason = decided->label;
  if (std::popcount(c.mask) > 1) {
    g.reason += std::format(": varias familias ({})", mask_label(c.mask));
    return g;
  }
  switch (c.mask) {
    case kFamCbbe: g.family = names_3ba(decided->text) ? BodyFamily::Cbbe3ba : BodyFamily::Cbbe; break;
    case kFamUbe: g.family = BodyFamily::Ube; break;
    default:
      g.family = BodyFamily::Other;
      g.other_name = mask_label(c.mask);
      break;
  }
  return g;
}

PresetFileResult parse_slider_presets(std::string_view xml_text, std::string_view virtual_file,
                                      std::string_view provider) {
  const auto root = parse_xml(xml_text);
  if (root.name != "SliderPresets") {
    throw Error(ErrorKind::Parse,
                std::format("La raíz es <{}>, no <SliderPresets>: no es un archivo de presets de BodySlide.", root.name));
  }
  PresetFileResult out;
  const auto file_key = text::data_path_key(virtual_file);
  // BCNG's preset "file": the path relative to SliderPresets, generic separators.
  auto source = std::string(virtual_file);
  if (const auto dir_key = text::data_path_key(kSliderPresetsDir) + '\\'; file_key.starts_with(dir_key)) {
    source.erase(0, dir_key.size());
  }
  std::replace(source.begin(), source.end(), '\\', '/');
  std::set<std::string> seen_in_file;
  for (const auto& p : root.children) {
    if (p.name != "Preset") continue;
    const auto name = p.attribute("name");
    if (!name || text::trim(*name).empty()) {
      out.warnings.push_back("Un <Preset> sin atributo name se ignoró.");
      continue;
    }
    BodySlidePreset preset;
    preset.name = std::string(*name);
    preset.set = std::string(p.attribute("set").value_or(""));
    preset.file = std::string(virtual_file);
    preset.source = source;
    preset.provider = std::string(provider);
    preset.uid = file_key + "|" + preset.name;
    preset.refit = preset.name.ends_with("-Refit");
    // BCNG keys presets by file + exact name; a repeated exact name in one file keeps the first.
    if (!seen_in_file.insert(preset.name).second) {
      out.warnings.push_back(std::format("El preset «{}» aparece dos veces en el mismo archivo; se conserva el primero.", preset.name));
      continue;
    }
    std::unordered_map<std::string, std::size_t> slider_index;
    for (const auto& c : p.children) {
      if (c.name == "Group") {
        if (const auto g = c.attribute("name"); g && !g->empty()) preset.groups.emplace_back(*g);
      } else if (c.name == "SetSlider") {
        const auto sname = c.attribute("name");
        const auto size = c.attribute("size");
        const auto value = c.attribute("value");
        if (!sname || sname->empty() || !size || !value) {
          out.warnings.push_back(std::format("«{}»: SetSlider incompleto ignorado.", preset.name));
          continue;
        }
        const auto v = parse_number(*value);
        if (!v) {
          out.warnings.push_back(std::format("«{}»: valor no numérico en el slider {}.", preset.name, *sname));
          continue;
        }
        auto [it, inserted] = slider_index.try_emplace(std::string(*sname), preset.sliders.size());
        if (inserted) preset.sliders.push_back({std::string(*sname), {}, {}});
        auto& s = preset.sliders[it->second];
        // Like BCNG/OBody: a repeated endpoint only replaces a zero with a non-zero value.
        auto set_endpoint = [&](std::optional<float>& endpoint) {
          if (!endpoint || (*endpoint == 0.0f && *v != 0.0f)) endpoint = *v;
        };
        if (text::iequals_ascii(*size, "small")) set_endpoint(s.small);
        else if (text::iequals_ascii(*size, "big")) set_endpoint(s.big);
        else out.warnings.push_back(std::format("«{}»: tamaño «{}» desconocido en el slider {}.", preset.name, *size, *sname));
      }
    }
    preset.family = classify_family(preset);
    out.presets.push_back(std::move(preset));
  }
  return out;
}

const BodySlidePreset* PresetLibrary::find(std::string_view uid) const {
  const auto it = std::find_if(presets.begin(), presets.end(), [&](const auto& p) { return p.uid == uid; });
  return it == presets.end() ? nullptr : &*it;
}

std::vector<const BodySlidePreset*> PresetLibrary::by_name(std::string_view name) const {
  std::vector<const BodySlidePreset*> out;
  for (const auto& p : presets) {
    if (p.name == name) out.push_back(&p);
  }
  return out;
}

const DuplicateName* PresetLibrary::duplicate_of(std::string_view name) const {
  const auto it = std::find_if(duplicates.begin(), duplicates.end(), [&](const auto& d) { return d.name == name; });
  return it == duplicates.end() ? nullptr : &*it;
}

namespace {

using SliderKey = std::map<std::string, std::pair<std::optional<float>, std::optional<float>>>;

SliderKey slider_key(const BodySlidePreset& p) {
  SliderKey m;
  for (const auto& s : p.sliders) m[text::to_lower_ascii(s.name)] = {s.small, s.big};
  return m;
}

}  // namespace

PresetLibrary build_library(std::vector<PresetFileEntry> files, std::vector<BodySlidePreset> presets) {
  PresetLibrary lib;
  // Display order is case-insensitive; exact names stay adjacent because BCNG matches names case-sensitively.
  std::sort(presets.begin(), presets.end(), [](const auto& a, const auto& b) {
    const auto an = text::to_lower_ascii(a.name), bn = text::to_lower_ascii(b.name);
    if (an != bn) return an < bn;
    if (a.name != b.name) return a.name < b.name;
    return text::data_path_key(a.file) < text::data_path_key(b.file);
  });
  std::sort(files.begin(), files.end(),
            [](const auto& a, const auto& b) { return text::data_path_key(a.file) < text::data_path_key(b.file); });
  for (std::size_t i = 0; i < presets.size();) {
    std::size_t j = i + 1;
    while (j < presets.size() && presets[j].name == presets[i].name) ++j;
    if (j - i > 1) {
      DuplicateName d;
      d.name = presets[i].name;
      d.identical = true;
      const auto reference = slider_key(presets[i]);
      for (std::size_t k = i; k < j; ++k) {
        d.uids.push_back(presets[k].uid);
        if (slider_key(presets[k]) != reference) d.identical = false;
      }
      lib.duplicates.push_back(std::move(d));
    }
    i = j;
  }
  for (const auto& p : presets) ++lib.family_counts[p.family.family];
  lib.presets = std::move(presets);
  lib.files = std::move(files);
  return lib;
}

PresetLibrary load_preset_library(const mo2::VirtualFileSystem& vfs, const FileReader& read_file) {
  std::vector<PresetFileEntry> files;
  std::vector<BodySlidePreset> presets;
  const auto& providers = vfs.providers();
  for (const auto& file : vfs.list_directory(kSliderPresetsDir, /*recursive=*/true)) {
    if (!text::ends_with_icase(file, ".xml")) continue;
    const auto resolved = vfs.resolve(file);
    if (!resolved) continue;
    PresetFileEntry entry;
    entry.file = file;
    entry.provider = providers[resolved->provider].name;
    for (const auto p : vfs.providers_of(file)) {
      if (p != resolved->provider) entry.hidden_providers.push_back(providers[p].name);
    }
    try {
      const auto bytes = read_file ? read_file(text::path_to_utf8(resolved->absolute))
                                   : file_io::read_file(resolved->absolute, 8ull * 1024 * 1024);
      auto parsed = parse_slider_presets(text::decode_text(bytes), file, entry.provider);
      entry.presets = parsed.presets.size();
      entry.warnings = std::move(parsed.warnings);
      for (auto& p : parsed.presets) presets.push_back(std::move(p));
    } catch (const std::exception& e) {
      entry.error = e.what();
    }
    files.push_back(std::move(entry));
  }
  return build_library(std::move(files), std::move(presets));
}

}  // namespace sns::body
