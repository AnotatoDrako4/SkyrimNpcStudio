#include "sns/presets/export_head_match.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace sns::presets {
namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
  return out;
}

bool is_alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'); }

// Words that say nothing about WHICH preset a file belongs to.
const std::set<std::string>& stop_words() {
  static const std::set<std::string> words = {"head", "preset", "export", "exported", "sculpt", "ube", "hph", "female", "male",
                                              "nif",  "jslot",  "the",    "and",      "follower", "replacer", "v2", "v3"};
  return words;
}

std::set<std::string> words_of(std::string_view s) {
  // "AikaHead" -> "Aika Head": split a lower->upper change before lower-casing.
  std::string spaced;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i > 0 && s[i - 1] >= 'a' && s[i - 1] <= 'z' && s[i] >= 'A' && s[i] <= 'Z') spaced += ' ';
    spaced += s[i];
  }
  const auto text = lower(spaced);
  std::set<std::string> out;
  std::string word;
  const auto flush = [&] {
    const bool digits = !word.empty() && std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; });
    // Timestamps and numbers ("5-1-2025") are not names.
    if (word.size() >= 3 && !digits && !stop_words().count(word)) out.insert(word);
    word.clear();
  };
  for (const char c : text) {
    if (is_alnum(c)) word += c;
    else flush();
  }
  flush();
  return out;
}

bool overlaps(const std::set<std::string>& a, const std::set<std::string>& b) {
  return std::any_of(a.begin(), a.end(), [&](const std::string& w) { return b.count(w) > 0; });
}

long rounded(double score) { return std::lround(score / 0.05); }

}  // namespace

std::string_view to_string(HeadVerdict verdict) {
  switch (verdict) {
    case HeadVerdict::ReadyForReview: return "READY_FOR_REVIEW";
    case HeadVerdict::NeedsAsset: return "NEEDS_ASSET";
    case HeadVerdict::NeedsChoice: return "NEEDS_CHOICE";
    case HeadVerdict::FaceGenUnreadable: return "FACEGEN_UNREADABLE";
    case HeadVerdict::HeadPartMismatch: return "HEAD_PART_MISMATCH";
    case HeadVerdict::UnsupportedTopology: return "UNSUPPORTED_TOPOLOGY";
  }
  return "NEEDS_ASSET";
}

std::string normalize_head_stem(std::string_view file_name) {
  const auto slash = file_name.find_last_of("\\/");
  std::string name(slash == std::string_view::npos ? file_name : file_name.substr(slash + 1));
  while (name.size() > 4 && lower(name.substr(name.size() - 4)) == ".nif") name.resize(name.size() - 4);
  return name;
}

HeadMatch match_export_head(const PresetHeadFacts& facts, const std::vector<HeadCandidate>& candidates, std::optional<std::size_t> manual_choice) {
  HeadMatch m;
  m.caveats = {"UNVERIFIED_IN_SKYRIM"};
  const auto finish = [&](HeadVerdict verdict, const char* code, std::string message) {
    m.verdict = verdict;
    if (code) m.codes.push_back(code);
    m.message = std::move(message);
    return m;
  };

  std::set<std::string> names;
  for (const auto& n : facts.shape_names) {
    if (!n.empty()) names.insert(lower(n));
  }
  const auto face = lower(facts.face_part);
  if (!facts.unresolved.empty()) m.codes.push_back("HeadPartsUnresolved");
  if (names.empty() || face.empty()) {
    return finish(HeadVerdict::HeadPartMismatch, "NoHeadPartsResolved",
                  "No se pudo resolver ninguna pieza de cabeza con malla (ni la de tipo Face) del preset: no hay con qué casar una cabeza exportada.");
  }
  if (facts.race_ok == false) {
    return finish(HeadVerdict::HeadPartMismatch, "RaceIncompatible", "Alguna pieza de cabeza del preset no es válida para la raza de destino.");
  }
  if (candidates.empty()) {
    return finish(HeadVerdict::NeedsAsset, "NoExportHead", "El perfil no tiene ninguna cabeza exportada de RaceMenu (Export Head).");
  }

  // ---- rank the heads that can belong to the preset
  const auto preset_stem = normalize_head_stem(facts.preset_name);
  const auto preset_words = words_of(preset_stem);
  std::optional<std::size_t> unreadable_named, unreadable_exact, faceless_named;
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& c = candidates[i];
    const auto stem = normalize_head_stem(c.stem);
    RankedHead r;
    r.index = i;
    r.exact_name = lower(stem) == lower(preset_stem);
    r.near_name = !r.exact_name && overlaps(words_of(stem), preset_words);
    const bool named = r.exact_name || r.near_name;
    if (!c.readable) {
      if (named && !unreadable_named) unreadable_named = i;
      if (r.exact_name && !unreadable_exact) unreadable_exact = i;
      continue;
    }
    std::set<std::string> shapes;
    for (const auto& s : c.shapes) shapes.insert(lower(s.name));
    if (!shapes.count(face)) {
      if (named && !faceless_named) faceless_named = i;
      continue;
    }
    for (const auto& n : names) r.common += shapes.count(n) ? 1 : 0;
    // Coverage of the preset's parts, not Jaccard: an Export Head also carries the whole hairstyle (real exports hold 100+ hair
    // shapes) and scars/extras, which say nothing against the pairing and would sink a symmetric ratio.
    r.overlap = static_cast<double>(r.common) / static_cast<double>(names.size());
    // Without a name hint almost every head shares the player's parts: it takes an almost complete overlap.
    if (r.overlap < (r.exact_name ? 0.3 : r.near_name ? 0.5 : 0.85)) continue;
    r.score = r.overlap + (r.near_name ? 1.0 : 0.0) + (r.exact_name ? 2.0 : 0.0);
    char text[160];
    std::snprintf(text, sizeof(text), "%d de %zu piezas del preset están en la cabeza (%.0f %%)%s", r.common, names.size(), r.overlap * 100.0,
                  r.exact_name ? ", nombre exacto" : r.near_name ? ", nombre próximo" : "");
    r.reason = text;
    m.ranked.push_back(std::move(r));
  }
  // Better score first (to 0.05); among equals with a name, the newer export (a new export of one preset wins over the old one).
  std::stable_sort(m.ranked.begin(), m.ranked.end(), [&](const RankedHead& a, const RankedHead& b) {
    if (rounded(a.score) != rounded(b.score)) return rounded(a.score) > rounded(b.score);
    const bool named = a.exact_name || a.near_name;
    return named && candidates[a.index].modified > candidates[b.index].modified;
  });

  // ---- choose
  if (manual_choice && *manual_choice < candidates.size()) {
    m.chosen = *manual_choice;
  } else if (unreadable_exact && (m.ranked.empty() || !m.ranked.front().exact_name)) {
    // The head that carries the preset's own name is damaged: another head that merely fits is not its replacement.
    m.chosen = unreadable_exact;
    return finish(HeadVerdict::FaceGenUnreadable, "HeadUnreadable",
                  "La cabeza exportada con el nombre exacto del preset (" + candidates[*unreadable_exact].path + ") está dañada o no es un NIF legible.");
  } else if (m.ranked.empty()) {
    if (unreadable_named) {
      m.chosen = unreadable_named;
      return finish(HeadVerdict::FaceGenUnreadable, "HeadUnreadable",
                    "La cabeza exportada que lleva el nombre del preset (" + candidates[*unreadable_named].path + ") está dañada o no es un NIF legible.");
    }
    if (faceless_named) {
      m.chosen = faceless_named;
      return finish(HeadVerdict::HeadPartMismatch, "FaceShapeMissing",
                    "La cabeza exportada de nombre parecido (" + candidates[*faceless_named].path + ") no contiene la pieza Face del preset («" + facts.face_part + "»).");
    }
    return finish(HeadVerdict::NeedsAsset, "NoExportHead", "Ninguna cabeza exportada del perfil corresponde a este preset (ni por nombre ni por sus piezas).");
  } else {
    const auto& top = m.ranked.front();
    const bool tie = m.ranked.size() > 1 && rounded(top.score) == rounded(m.ranked[1].score);
    const bool date_decides = tie && (top.exact_name || top.near_name) && candidates[top.index].modified != candidates[m.ranked[1].index].modified;
    if (tie && !date_decides) {
      m.needs_manual_choice = true;
      const auto equal = std::count_if(m.ranked.begin(), m.ranked.end(), [&](const RankedHead& r) { return rounded(r.score) == rounded(top.score); });
      return finish(HeadVerdict::NeedsChoice, "AmbiguousHead",
                    "Hay " + std::to_string(equal) + " cabezas igual de probables para este preset: elige una a mano.");
    }
    m.chosen = top.index;
    // Nothing but the parts fit (the name says nothing): other presets can fit the same head, so it is flagged for a person.
    if (!top.exact_name && !top.near_name) m.codes.push_back("PairedByContentOnly");
  }

  // ---- the chosen head is checked like any other
  const auto& c = candidates[*m.chosen];
  if (!c.readable) {
    return finish(HeadVerdict::FaceGenUnreadable, "HeadUnreadable", "La cabeza elegida (" + c.path + ") está dañada o no es un NIF legible.");
  }
  const HeadShape* face_shape = nullptr;
  for (const auto& s : c.shapes) {
    if (lower(s.name) == face) face_shape = &s;
  }
  if (!face_shape) {
    return finish(HeadVerdict::HeadPartMismatch, "FaceShapeMissing", "La cabeza elegida (" + c.path + ") no contiene la pieza Face del preset («" + facts.face_part + "»).");
  }
  if (facts.face_vertices && face_shape->vertices != 0) {
    if (*facts.face_vertices != face_shape->vertices) {
      return finish(HeadVerdict::UnsupportedTopology, "TopologyDiffers",
                    "La malla de la cara de la cabeza elegida tiene " + std::to_string(face_shape->vertices) + " vértices y la pieza del preset " +
                        std::to_string(*facts.face_vertices) + ": no se pueden sustituir (¿otra cabeza, p. ej. High Poly Head frente a la vanilla?).");
    }
  } else {
    m.codes.push_back("TopologyUnchecked");
  }
  if (!c.missing_textures.empty()) {
    return finish(HeadVerdict::NeedsAsset, "TexturesMissing",
                  "A la cabeza elegida le faltan " + std::to_string(c.missing_textures.size()) + " texturas en el perfil (p. ej. «" + c.missing_textures.front() + "»).");
  }
  const bool content_only = std::find(m.codes.begin(), m.codes.end(), "PairedByContentOnly") != m.codes.end();
  return finish(HeadVerdict::ReadyForReview, nullptr,
                "La cabeza «" + c.path + "» es legible y contiene la pieza Face del preset; falta la comprobación dentro de Skyrim." +
                    (content_only ? " Se emparejó solo por el contenido (el nombre no ayuda): confirma que es la de este preset." : ""));
}

}  // namespace sns::presets
