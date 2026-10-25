#include "sns/appearance/source_matrix.hpp"

#include <algorithm>
#include <cstdio>

#include "sns/appearance/review_case.hpp"

namespace sns::appearance {
namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
  return s;
}

std::string quoted(const std::string& s) { return "«" + s + "»"; }

std::string joined(std::vector<std::string> items, bool sort) {
  if (sort) std::sort(items.begin(), items.end());
  std::string out;
  for (const auto& i : items) out += (out.empty() ? "" : ",") + i;
  return out;
}

std::string asset_line(const char* key, const AssetSource& a) {
  return std::string(key) + "=" + lower(a.path) + "|" + (a.found ? "found" : "missing") + "|" + a.kind + "|" + a.winner_mod + "|loose:" +
         joined(a.loose_mods, false);
}

std::string tri(const std::optional<bool>& v) { return v ? (*v ? "yes" : "no") : "unset"; }

bool is_face_field(const std::string& f) {
  static const std::vector<std::string> fields = {"HeadParts", "FaceMorph", "FaceParts", "Race", "Weight", "HeadTexture", "TextureLighting",
                                                  "TintLayers", "HairColor"};
  return std::find(fields.begin(), fields.end(), f) != fields.end();
}

AssetSource loose_source(const std::string& path, const std::string& mod) {
  AssetSource a;
  a.path = path;
  a.found = true;
  a.kind = "loose";
  a.winner_mod = mod;
  a.loose_mods = {mod};
  return a;
}

// Severity order of the findings: warnings first, then notes, then confirmations.
int severity_rank(const std::string& s) { return s == "warning" ? 0 : s == "info" ? 1 : 2; }

void fill_findings(SourceCandidate& c, const ControlReport& report) {
  auto findings = report.findings;
  std::stable_sort(findings.begin(), findings.end(),
                   [](const ControlFinding& a, const ControlFinding& b) { return severity_rank(a.severity) < severity_rank(b.severity); });
  for (const auto& f : findings) {
    c.codes.push_back(f.code);
    c.messages.push_back(f.message);
  }
  if (report.resolution) {
    c.resolution_available = true;
    c.requires_review = requires_review(*report.resolution);
  }
}

// The verdict of the control map on one candidate, in the matrix' terms.
SourceStatus judge(const ControlReport& report, const ControlInputs& ctl, const std::optional<bool>& readable) {
  if (!ctl.facegen.found) return SourceStatus::Missing;
  // A file the safe loader could not open is a hard conflict, whatever a person may decide about the record.
  if (readable == false) return SourceStatus::Conflict;
  if (report.resolution && requires_review(*report.resolution)) return SourceStatus::Ambiguous;
  const bool warning = std::any_of(report.findings.begin(), report.findings.end(), [](const ControlFinding& f) { return f.severity == "warning"; });
  if (warning) return SourceStatus::Conflict;
  return readable == true || ctl.regeneration_deviation ? SourceStatus::Valid : SourceStatus::Unverified;
}

const ProviderFaceGen* provider_of(const std::vector<ProviderFaceGen>& providers, const std::string& mod) {
  // Loose copies only: an archived copy of the same mod is a candidate of its own, never the winner's evidence.
  const auto it = std::find_if(providers.begin(), providers.end(), [&](const ProviderFaceGen& p) { return p.mod == mod && p.archive.empty(); });
  return it == providers.end() ? nullptr : &*it;
}

void copy_evidence(SourceCandidate& c, const ProviderFaceGen& p) {
  c.geometry_sha256 = p.geometry_sha256;
  c.tint_sha256 = p.tint_sha256;
  c.geometry_bytes = p.geometry_bytes;
  c.tint_bytes = p.tint_bytes;
}

}  // namespace

std::string_view to_string(SourceStatus status) {
  switch (status) {
    case SourceStatus::Valid: return "VALID";
    case SourceStatus::Unverified: return "UNVERIFIED";
    case SourceStatus::Conflict: return "CONFLICT";
    case SourceStatus::Ambiguous: return "AMBIGUOUS";
    case SourceStatus::Missing: return "MISSING";
  }
  return "UNVERIFIED";
}

std::optional<SourceStatus> source_status_from(std::string_view text) {
  for (const auto s : {SourceStatus::Valid, SourceStatus::Unverified, SourceStatus::Conflict, SourceStatus::Ambiguous, SourceStatus::Missing}) {
    if (to_string(s) == text) return s;
  }
  return std::nullopt;
}

const SourceCandidate* SourceMatrix::find(std::string_view id) const {
  const auto it = std::find_if(candidates.begin(), candidates.end(), [&](const SourceCandidate& c) { return c.id == id; });
  return it == candidates.end() ? nullptr : &*it;
}

const SourceCandidate* SourceMatrix::effective() const { return find("effective"); }

std::size_t SourceMatrix::renderable_count() const {
  return static_cast<std::size_t>(std::count_if(candidates.begin(), candidates.end(), [](const SourceCandidate& c) { return c.renderable; }));
}

std::string source_identity(const SourceInputs& in) {
  const auto& ctl = in.control;
  std::string out = "source-identity-v1\nnpc=" + in.npc_form_key + "\nprofile=" + in.profile_digest + "\n";
  for (const auto& link : ctl.chain) {
    std::vector<std::string> copies;
    for (const auto& [field, from] : link.copied_from) copies.push_back(field + "=" + from);
    out += "link=" + link.plugin + "|" + link.mod + "|" + (link.is_origin ? "origin" : "-") + "|" + (link.is_winner ? "winner" : "-") +
           "|changed:" + joined(link.changed_fields, true) + "|copied:" + joined(copies, true) + "|files:" + joined(link.plugin_mods, false) + "\n";
  }
  out += asset_line("facegen", ctl.facegen) + "\n" + asset_line("facetint", ctl.facetint) + "\n";
  char deviation[32] = "none";
  if (ctl.regeneration_deviation) std::snprintf(deviation, sizeof(deviation), "%.4f", *ctl.regeneration_deviation);
  out += std::string("deviation=") + deviation + "|expected=" + (ctl.facegen_expected ? "yes" : "no") + "|readable=" + tri(ctl.facegen_readable) +
         "|headparts=" + tri(ctl.facegen_has_record_head_parts) + "\n";
  out += "lost=" + joined(ctl.lost_fields, true) + "\n";
  for (const auto& p : in.providers) {
    out += "provider=" + p.mod + "|geometry:" + (p.has_geometry ? lower(p.geometry_file) : "-") + ":" + std::to_string(p.geometry_bytes) + ":" +
           p.geometry_sha256 + "|tint:" + (p.has_tint ? lower(p.tint_file) : "-") + ":" + std::to_string(p.tint_bytes) + ":" + p.tint_sha256 +
           "|readable=" + tri(p.readable) + "|headparts=" + tri(p.has_record_head_parts) +
           (p.archive.empty() ? std::string() : "|archive=" + lower(p.archive)) + "\n";
  }
  return out;
}

SourceMatrix build_source_matrix(const SourceInputs& in) {
  SourceMatrix m;
  m.npc_form_key = in.npc_form_key;
  m.profile_digest = in.profile_digest;
  m.identity = source_identity(in);
  const auto& base = in.control;

  // ---- the source the game loads
  {
    ControlInputs ctl = base;
    const auto* winner_files = base.facegen.found ? provider_of(in.providers, base.facegen.winner_mod) : nullptr;
    if (!ctl.facegen_readable && winner_files) ctl.facegen_readable = winner_files->readable;
    if (!ctl.facegen_has_record_head_parts && winner_files) ctl.facegen_has_record_head_parts = winner_files->has_record_head_parts;
    const auto report = analyze_appearance_control(ctl);
    SourceCandidate c;
    c.id = "effective";
    c.effective = true;
    c.record_plugin = report.face_plugin;
    c.record_mod = report.face_mod;
    c.geometry_path = ctl.facegen.path;
    c.tint_path = ctl.facetint.path;
    c.facegen_mod = ctl.facegen.found ? ctl.facegen.winner_mod : std::string();
    c.facetint_mod = ctl.facetint.found ? ctl.facetint.winner_mod : std::string();
    c.loose_mods = ctl.facegen.loose_mods;
    c.label = ctl.facegen.found ? "Lo que carga el juego: FaceGen de " + quoted(ctl.facegen.winner_mod) : "Lo que carga el juego (sin FaceGen propio)";
    c.status = judge(report, ctl, ctl.facegen_readable);
    fill_findings(c, report);
    if (winner_files) copy_evidence(c, *winner_files);
    // Drawn by the portrait renderer from the profile itself (no overlay): needs a FaceGen that is not damaged.
    c.renderable = ctl.facegen.found && ctl.facegen_readable != false;
    c.render_note = !ctl.facegen.found ? "El NPC no tiene FaceGen en el perfil: no hay geometría que dibujar."
                    : ctl.facegen_readable == false ? "El FaceGen está dañado: no se dibuja."
                                                    : "Aproximación del editor con los archivos que carga el juego.";
    m.candidates.push_back(std::move(c));
  }

  // ---- every other loose FaceGen provider (the higher MO2 priority first)
  const bool can_offer = base.facegen.found && !base.facegen.path.empty();
  for (auto it = in.providers.rbegin(); can_offer && it != in.providers.rend(); ++it) {
    const auto& p = *it;
    if (!p.archive.empty()) continue;  // archived copies come after the loose ones
    if (!p.has_geometry || p.mod == base.facegen.winner_mod) continue;
    ControlInputs ctl = base;
    ctl.facegen = loose_source(base.facegen.path, p.mod);
    if (p.has_tint) ctl.facetint = loose_source(base.facetint.path, p.mod);
    // Nothing was measured for this provider's files: the deviation of the effective FaceGen says nothing here.
    ctl.regeneration_deviation.reset();
    ctl.facegen_readable = p.readable;
    ctl.facegen_has_record_head_parts = p.has_record_head_parts;
    const auto report = analyze_appearance_control(ctl);
    SourceCandidate c;
    c.id = "facegen:" + lower(p.mod);
    c.record_plugin = report.face_plugin;
    c.record_mod = report.face_mod;
    c.geometry_path = ctl.facegen.path;
    c.tint_path = ctl.facetint.path;
    c.facegen_mod = p.mod;
    c.facetint_mod = ctl.facetint.found ? ctl.facetint.winner_mod : std::string();
    c.loose_mods = base.facegen.loose_mods;
    c.label = "FaceGen de " + quoted(p.mod);
    c.status = judge(report, ctl, p.readable);
    fill_findings(c, report);
    copy_evidence(c, p);
    c.renderable = !p.geometry_file.empty() && p.readable != false;
    if (p.geometry_file.empty()) {
      c.render_note = "No hay una copia suelta de este FaceGen que el editor pueda leer.";
    } else if (p.readable == false) {
      c.render_note = "El FaceGen de este mod está dañado: no se dibuja.";
    } else {
      c.overlay.push_back({ctl.facegen.path, p.geometry_file});
      if (p.has_tint && !p.tint_file.empty()) c.overlay.push_back({ctl.facetint.path, p.tint_file});
      c.render_note = p.has_tint ? "Aproximación del editor: geometría y tinte de este mod sobre el resto del perfil."
                                 : "Aproximación del editor: geometría de este mod; el tinte sigue siendo el que carga el juego.";
    }
    m.candidates.push_back(std::move(c));
  }

  // ---- copies inside BSA archives the game does not load (shadowed by a loose file or a later archive)
  for (auto it = in.providers.rbegin(); can_offer && it != in.providers.rend(); ++it) {
    const auto& p = *it;
    if (p.archive.empty() || !p.has_geometry) continue;
    ControlInputs ctl = base;
    ctl.facegen = {};
    ctl.facegen.path = base.facegen.path;
    ctl.facegen.found = true;
    ctl.facegen.kind = "bsa";
    ctl.facegen.winner_mod = p.mod;
    if (p.has_tint) {
      ctl.facetint = {};
      ctl.facetint.path = base.facetint.path;
      ctl.facetint.found = true;
      ctl.facetint.kind = "bsa";
      ctl.facetint.winner_mod = p.mod;
    }
    ctl.regeneration_deviation.reset();
    ctl.facegen_readable = p.readable;
    ctl.facegen_has_record_head_parts = p.has_record_head_parts;
    const auto report = analyze_appearance_control(ctl);
    SourceCandidate c;
    c.id = "bsa:" + lower(p.archive);
    c.archive = p.archive;
    c.record_plugin = report.face_plugin;
    c.record_mod = report.face_mod;
    c.geometry_path = ctl.facegen.path;
    c.tint_path = ctl.facetint.path;
    c.facegen_mod = p.mod;
    c.facetint_mod = ctl.facetint.found ? ctl.facetint.winner_mod : std::string();
    c.loose_mods = base.facegen.loose_mods;
    c.label = "FaceGen de " + quoted(p.mod) + " (BSA " + quoted(p.archive) + ")";
    c.status = judge(report, ctl, p.readable);
    fill_findings(c, report);
    copy_evidence(c, p);
    c.renderable = !p.geometry_file.empty() && p.readable != false;
    if (p.geometry_file.empty()) {
      c.render_note = "No se pudo extraer la copia del BSA para leerla.";
    } else if (p.readable == false) {
      c.render_note = "El FaceGen de este BSA está dañado: no se dibuja.";
    } else {
      c.overlay.push_back({ctl.facegen.path, p.geometry_file});
      if (p.has_tint && !p.tint_file.empty()) c.overlay.push_back({ctl.facetint.path, p.tint_file});
      c.render_note = p.has_tint ? "Aproximación del editor: geometría y tinte de este BSA sobre el resto del perfil."
                                 : "Aproximación del editor: geometría de este BSA; el tinte sigue siendo el que carga el juego.";
    }
    m.candidates.push_back(std::move(c));
  }

  // ---- versions that change the face but ship no FaceGen: shown, never drawn
  std::vector<std::string> seen;
  const auto effective_plugin = m.candidates.front().record_plugin;
  for (std::size_t i = 1; i < base.chain.size(); ++i) {
    const auto& link = base.chain[i];
    const bool changes_face = std::any_of(link.changed_fields.begin(), link.changed_fields.end(), is_face_field);
    if (!changes_face || link.plugin == effective_plugin) continue;
    if (link.mod == base.facegen.winner_mod || provider_of(in.providers, link.mod)) continue;
    if (std::find(seen.begin(), seen.end(), lower(link.plugin)) != seen.end()) continue;
    seen.push_back(lower(link.plugin));
    SourceCandidate c;
    c.id = "record:" + lower(link.plugin);
    c.record_plugin = link.plugin;
    c.record_mod = link.mod;
    c.label = "Registro de " + quoted(link.plugin);
    c.status = SourceStatus::Missing;
    c.codes = {"NoFaceGen"};
    c.messages = {quoted(link.plugin) + " (" + link.mod + ") cambia datos faciales pero no trae FaceGen propio."};
    c.render_note = "Sin FaceGen propio no hay nada que dibujar: el editor no regenera rostros para esta vista.";
    m.candidates.push_back(std::move(c));
  }
  return m;
}

}  // namespace sns::appearance
