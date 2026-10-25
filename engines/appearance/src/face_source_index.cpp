#include "sns/appearance/face_source_index.hpp"

#include <algorithm>
#include <set>

namespace sns::appearance {
namespace {

constexpr std::string_view kGeometryDir = "meshes\\actors\\character\\facegendata\\facegeom\\";
constexpr std::string_view kTintDir = "textures\\actors\\character\\facegendata\\facetint\\";

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
  return out;
}

std::string upper(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); });
  return out;
}

bool is_hex(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
}

bool contains(std::string_view haystack_lower, std::string_view needle_lower) { return haystack_lower.find(needle_lower) != std::string_view::npos; }

// The winner of one file and its distinct sources (loose mods, then archives).
struct Copies {
  std::string winner;
  bool archived = false;
  std::vector<std::string> sources;
};

Copies copies_of(const std::vector<std::string>& loose, const std::vector<ArchiveCopy>& archives) {
  Copies c;
  for (const auto& a : archives) c.sources.push_back(a.archive);
  for (const auto& m : loose) c.sources.push_back(m);
  if (!loose.empty()) {
    c.winner = loose.back();
  } else if (!archives.empty()) {
    c.winner = archives.back().archive;
    c.archived = true;
  }
  return c;
}

}  // namespace

std::optional<FaceFileIdentity> facegen_file_identity(std::string_view data_path) {
  std::string key = lower(data_path);
  std::replace(key.begin(), key.end(), '/', '\\');
  while (!key.empty() && key.front() == '\\') key.erase(key.begin());
  FaceFileIdentity id;
  std::string_view rest;
  std::string_view extension;
  if (key.rfind(kGeometryDir, 0) == 0) {
    id.geometry = true;
    rest = std::string_view(key).substr(kGeometryDir.size());
    extension = ".nif";
  } else if (key.rfind(kTintDir, 0) == 0) {
    rest = std::string_view(key).substr(kTintDir.size());
    extension = ".dds";
  } else {
    return std::nullopt;
  }
  const auto slash = rest.find('\\');
  if (slash == std::string_view::npos || slash == 0) return std::nullopt;
  const auto file = rest.substr(slash + 1);
  if (file.find('\\') != std::string_view::npos) return std::nullopt;  // FaceGen lives directly in the plugin folder
  if (file.size() != 8 + extension.size() || file.substr(8) != extension || !is_hex(file.substr(0, 8))) return std::nullopt;
  // Keep the plugin as written: same offset in the original text (the key only lowered ASCII and swapped slashes).
  std::string original(data_path);
  std::replace(original.begin(), original.end(), '/', '\\');
  while (!original.empty() && original.front() == '\\') original.erase(original.begin());
  const auto dir_size = (id.geometry ? kGeometryDir : kTintDir).size();
  id.plugin = original.substr(dir_size, slash);
  id.form_id = upper(file.substr(0, 8));
  id.form_key = id.form_id.substr(2) + ":" + id.plugin;
  return id;
}

FaceIndex build_face_index(const std::vector<FaceFileObservation>& files) {
  // By lower-case "plugin|formid" so a geometry and a tint written with different case meet.
  std::map<std::string, FaceIndexEntry> by_key;
  for (const auto& f : files) {
    const auto id = facegen_file_identity(f.data_path);
    if (!id || (f.loose_mods.empty() && f.archives.empty())) continue;
    auto& e = by_key[lower(id->plugin) + "|" + id->form_id];
    if (e.form_key.empty()) {
      e.form_key = id->form_key;
      e.plugin = id->plugin;
      e.form_id = id->form_id;
    }
    if (id->geometry) {
      e.geometry_path = f.data_path;
      e.geometry_loose = f.loose_mods;
      e.geometry_archives = f.archives;
    } else {
      e.tint_path = f.data_path;
      e.tint_loose = f.loose_mods;
      e.tint_archives = f.archives;
    }
  }

  FaceIndex index;
  std::set<std::string> all_sources;
  for (auto& [key, e] : by_key) {
    const auto geometry = copies_of(e.geometry_loose, e.geometry_archives);
    const auto tint = copies_of(e.tint_loose, e.tint_archives);
    e.geometry_winner = geometry.winner;
    e.geometry_winner_archived = geometry.archived;
    e.tint_winner = tint.winner;
    e.tint_winner_archived = tint.archived;
    e.alternatives = geometry.sources.empty() ? 0 : static_cast<int>(geometry.sources.size()) - 1;
    if (e.geometry_path.empty()) {
      e.flags.push_back("TintOnly");
    } else {
      if (e.alternatives > 0) e.flags.push_back("MultipleSources");
      if (e.tint_path.empty()) {
        e.flags.push_back("NoTint");
      } else if (lower(tint.winner) != lower(geometry.winner)) {
        // An archive and the mod that ships it count as the same source only when the names match; a loose tint over
        // an archived geometry (or the reverse) is exactly the mix that causes mismatched faces.
        e.flags.push_back("TintFromOtherSource");
      }
      if (geometry.archived && e.geometry_loose.empty()) e.flags.push_back("ArchivedOnly");
    }
    std::set<std::string> mine;
    for (const auto& s : geometry.sources) mine.insert(s);
    for (const auto& s : tint.sources) mine.insert(s);
    e.sources.assign(mine.begin(), mine.end());
    all_sources.insert(mine.begin(), mine.end());
    if (e.alternatives > 0) ++index.with_alternatives;
    index.entries.push_back(std::move(e));
  }
  std::stable_sort(index.entries.begin(), index.entries.end(), [](const FaceIndexEntry& a, const FaceIndexEntry& b) {
    const auto pa = lower(a.plugin), pb = lower(b.plugin);
    return pa != pb ? pa < pb : a.form_id < b.form_id;
  });
  index.sources.assign(all_sources.begin(), all_sources.end());
  return index;
}

std::vector<const FaceIndexEntry*> query_face_index(const FaceIndex& index, const FaceIndexQuery& query,
                                                    const std::map<std::string, std::string>* names, std::size_t* total) {
  std::vector<const FaceIndexEntry*> out;
  std::size_t matches = 0;
  const auto needle = lower(query.text);
  for (const auto& e : index.entries) {
    if (query.only_alternatives && e.alternatives <= 0) continue;
    if (!query.flag.empty() && std::find(e.flags.begin(), e.flags.end(), query.flag) == e.flags.end()) continue;
    if (!query.source.empty() && std::find(e.sources.begin(), e.sources.end(), query.source) == e.sources.end()) continue;
    if (!needle.empty()) {
      bool hit = contains(lower(e.form_key), needle) || contains(lower(e.form_id), needle);
      for (const auto& s : e.sources) hit = hit || contains(lower(s), needle);
      if (!hit && names) {
        const auto it = names->find(e.form_key);
        hit = it != names->end() && contains(lower(it->second), needle);
      }
      if (!hit) continue;
    }
    ++matches;
    if (out.size() < query.limit) out.push_back(&e);
  }
  if (total) *total = matches;
  return out;
}

}  // namespace sns::appearance
