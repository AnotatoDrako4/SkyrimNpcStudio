#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sns/appearance/control_map.hpp"

// Appearance Source Matrix: every source the profile offers for the face of ONE NPC (one FormKey), side by
// side. A source is a pair "record version + FaceGen provider": the one the game loads (the effective one)
// and, for every other MO2 mod that ships its own loose FaceGen for the NPC, what the NPC would look like
// with that mod's files. Each candidate is judged by the same rules as the appearance control map
// (analyze_appearance_control run on the candidate's inputs), so the matrix never invents a verdict.
//
// Pure, no I/O: the studio gathers the facts (VFS providers, file readability) and renders the candidates.
// Nothing here changes load order, projects, plugins or FaceGen; a candidate is only something to look at.
// Inspired by the idea of choosing the appearance provider per NPC in NPC Plugin Chooser 2 (GPL-3); no code
// of it was used (see docs/REFERENCE_INTEGRATION.md).
namespace sns::appearance {

enum class SourceStatus {
  Valid,       // no warning, and the FaceGen was opened (or its deviation measured): record and files fit
  Unverified,  // no warning, but nothing proves it: the file was not opened and no deviation was measured
  Conflict,    // the control map warns (record/FaceGen mismatch, damaged file, tint from another mod...)
  Ambiguous,   // fitting it would discard a mod's own face edits: only a human decision (D1) can settle it
  Missing,     // there is no FaceGen of this source the editor could draw
};
[[nodiscard]] std::string_view to_string(SourceStatus status);
[[nodiscard]] std::optional<SourceStatus> source_status_from(std::string_view text);

// The FaceGen files ONE MO2 mod ships for the NPC (loose files only: a BSA copy cannot be told apart per mod).
struct ProviderFaceGen {
  std::string mod;
  bool has_geometry = false;
  bool has_tint = false;
  std::string geometry_file;  // absolute path of this mod's copy (what the game would load from this mod)
  std::string tint_file;
  std::int64_t geometry_bytes = -1;
  std::int64_t tint_bytes = -1;
  // Opened by the studio's safe NIF loader (audit F-10/F-11); unset when it was not opened.
  std::optional<bool> readable;
  // Every head part of the record has a shape of its name in this FaceGen; unset when not checked.
  std::optional<bool> has_record_head_parts;
  // Lower-case hex, filled by the studio (the engine has no hash): evidence of the exact files compared.
  std::string geometry_sha256;
  std::string tint_sha256;
};

struct SourceInputs {
  std::string npc_form_key;
  // Digest of the MO2 profile and its active plugins (portrait cache's snapshot digest): a different profile
  // makes every candidate and every draft made on it stale.
  std::string profile_digest;
  ControlInputs control;                  // the effective inputs, exactly as the control map gets them
  std::vector<ProviderFaceGen> providers;  // loose FaceGen providers, ascending MO2 priority (winner last)
};

// A Data path the candidate replaces for one render, and the file whose bytes replace it.
struct OverlayFile {
  std::string data_path;  // e.g. meshes\actors\character\facegendata\facegeom\skyrim.esm\000a2c8e.nif
  std::string file;       // absolute path of the candidate provider's copy
};

struct SourceCandidate {
  std::string id;  // "effective", "facegen:<mod>" or "record:<plugin>" (lower case; stable across runs)
  std::string label;  // Spanish, for the card
  bool effective = false;
  SourceStatus status = SourceStatus::Unverified;
  std::string record_plugin;  // version whose face data this source uses
  std::string record_mod;
  std::string facegen_mod;    // provider of the geometry (empty when there is none)
  std::string facetint_mod;
  std::string geometry_path;  // Data paths
  std::string tint_path;
  std::vector<std::string> codes;     // control map finding codes of this candidate, warnings first
  std::vector<std::string> messages;  // Spanish, one per finding (what was found)
  // The editor can draw it: a FaceGen of its own exists, is a loose file and was not found damaged.
  bool renderable = false;
  std::string render_note;  // Spanish: why it cannot be drawn, or what the render uses
  // Files that replace the effective ones to draw this candidate (empty for the effective candidate).
  std::vector<OverlayFile> overlay;
  // «Usar el rostro de X» (the existing assisted fix) applies to this candidate's mod.
  bool resolution_available = false;
  bool requires_review = false;
  // Evidence per option: provider files compared (size and SHA-256 when the studio filled them).
  std::string geometry_sha256;
  std::string tint_sha256;
  std::int64_t geometry_bytes = -1;
  std::int64_t tint_bytes = -1;
  std::vector<std::string> loose_mods;  // every MO2 mod with a copy of the geometry, ascending priority
};

struct SourceMatrix {
  std::string npc_form_key;
  std::string profile_digest;
  std::string identity;  // canonical text of every input; the studio stores its SHA-256 as the input digest
  std::vector<SourceCandidate> candidates;  // effective first, then providers by descending MO2 priority, then record-only
  [[nodiscard]] const SourceCandidate* find(std::string_view id) const;
  [[nodiscard]] const SourceCandidate* effective() const;
  // Candidates the editor can draw (A/B needs two of them).
  [[nodiscard]] std::size_t renderable_count() const;
};

[[nodiscard]] SourceMatrix build_source_matrix(const SourceInputs& in);

// Canonical, deterministic text of everything a matrix and a draft made on it depend on.
[[nodiscard]] std::string source_identity(const SourceInputs& in);

}  // namespace sns::appearance
