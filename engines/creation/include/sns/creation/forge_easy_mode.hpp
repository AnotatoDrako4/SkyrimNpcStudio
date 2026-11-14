#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// Character Forge — Modo Fácil. A guided front for operations the studio already has: it never adds an operation
// of its own. Every verdict comes from facts the backends report (project, Blender, worker, indexed profile, the
// Blender report classified by skyrim::classify, the nifly re-read, the ModPackager result), so the same facts
// always produce the same screen. The UI states here describe the screen; they do not replace the delivery
// states of AGENTS.md (an exported mod stays PENDIENTE_SKYRIM until a human sees it in game).
namespace sns::creation::easy {

struct Facts {
  // Project and tools
  bool project_open = false;
  bool project_saved = false;          // the .sncf has a folder (jobs and exports are written inside it)
  bool blender_found = false;          // a validated blender.exe in the workspace
  bool busy = false;                   // a job (Blender, worker) is running
  // Load order: the BethesdaWorker answers and the MO2 profile is indexed
  bool worker_ready = false;
  bool profile_indexed = false;
  std::string worker_message;          // why the worker is not ready (its own status text)
  // Character Creator (other workspace): an NPC of the indexed profile is loaded there
  bool npc_loaded = false;
  // Model and its pipeline
  bool model_selected = false;
  std::string model_format;            // glb, gltf, fbx, obj, blend
  bool preview_loaded = false;         // the 3D preview shows the model (a GLB, never the export source)
  std::string preview_state;           // ForgeSceneViewModel state: idle, loading, loaded, error
  std::string conversion_state = "none";  // none, running, analyzed, exported, verificationFailed, error
  std::string conversion_operation;    // inspect, exportNif
  std::string profile = "static";      // static, skinned
  nlohmann::json classification;       // skyrim::classify output of the last analysis (or null)
  bool nif_verified = false;           // verification.passed of the exported NIF
  std::string mod_state;               // "", running, done, error
  std::string mod_message;
};

// UI states of one diagnosis finding.
inline constexpr std::string_view kCompatible = "COMPATIBLE";
inline constexpr std::string_view kSafeFix = "CORRECCION_SEGURA_DISPONIBLE";
inline constexpr std::string_view kManualReview = "REVISION_MANUAL";
inline constexpr std::string_view kBlocking = "BLOQUEANTE";
inline constexpr std::string_view kNotVerified = "NO_VERIFICADO";

// Maps one skyrim::classify issue to its UI state:
//   blocking -> BLOQUEANTE; autoFixable with fixAvailable -> CORRECCION_SEGURA_DISPONIBLE (without it: REVISION_MANUAL);
//   warning, manualReview -> REVISION_MANUAL; compatible -> COMPATIBLE; anything else -> NO_VERIFICADO.
std::string_view ui_state(const nlohmann::json& issue);

// The classification explained for a person: one entry per issue with
//   {code, uiState, resource, problem, whySkyrim, operation, reversible, requiresUser, blocksExport, severity}
// plus the checks the studio cannot make (in game, collision) as NO_VERIFICADO. Returns
//   {state, summary, counts: {<uiState>: n}, findings: [...], exportable}
// where state is the worst UI state present. A null or empty classification gives state NO_VERIFICADO.
nlohmann::json explain(const nlohmann::json& classification);

// The seven actions of the main screen:
//   [{id, title, description, state: "available" | "partial" | "blocked", reason, missing: [..], next, target}]
// ids: createCharacter, editNpc, bodyAppearance, outfits, importBlender, prepareResource, exportMod.
// target names where the action continues: "forge.npc", "creator", "creator.body", "outfit", "forge.import",
// "forge.skyrim". "partial" means the action opens, but part of it is not certified (reason says which part).
// Export is never "available" unless every check of its route passed.
nlohmann::json actions(const Facts& facts);

// The guided flow SELECCIONAR -> PREVISUALIZAR -> DIAGNOSTICAR -> PREPARAR -> VALIDAR -> EXPORTAR:
//   {steps: [{id, title, state: "done" | "current" | "pending" | "blocked" | "running", detail}], current, canExport}
// ids: select, preview, diagnose, prepare, validate, export. At most one step is current (or running); a step after
// a blocked one stays pending.
nlohmann::json flow(const Facts& facts);

// The NPC loaded in the Character Creator, summarised for the Modo Fácil from results the studio already computed. It runs no
// analysis of its own. Input (every key optional):
//   {indexed,
//    npc: BodyDirector.npc {state, formKey, name, family, familyLabel, familyMask, body: {path, provider, tri}, issues: [{severity,
//         code, message}], changes: [{target, label, verdict, verdictLabel, issues}]},
//    environment: BodyDirector.environment {state, bcng, obody, installedFamilies, presets, duplicates},
//    presets: BodyDirector.presets [{verdict}] (filtered list), presetCounts: BodyDirector.preset_verdicts (all presets; wins),
//    outfit: {formKey, pieces: [{formKey, name, plugin}], localConflicts: [{slot, first, second}], validating,
//             validation: {exportable, issues: [{severity, code, message}]}},
//    fit: Outfit Fit Doctor of the same session {formKey, bodyFamily, analyzing, fixableAlternatives,
//         pieces: [{armorName, detectedFamily, confidence, topologyCompatible, skeletonCompatible, isOverwrite, winningProvider}]}}
// Output:
//   {state: "none" | "loading" | "ready" | "error", message, npc: {formKey, name},
//    body: {family, familyLabel, provider, path, tri, uiState, detail},
//    presets: {total, compatible, experimental, needsResources, blocked},
//    familyChanges: [{target, label, verdict, verdictLabel}]   (read-only evaluation, never exported),
//    outfits: {verified: bool, compatible: [name], needsConversion: [{name, reason}], unverified: [name]},
//    conflicts: [{kind: "slot" | "file", text}], missing: [text],
//    findings: [{code, uiState, resource, problem, operation, blocksExport}],
//    exports: [{id, label, state: "available" | "partial" | "blocked" | "notNeeded", reason, target}]}
// A garment is "compatible" only when the Fit Doctor analysed THIS NPC and confirmed family, topology and skeleton; anything it did
// not analyse is "unverified". CBBE/3BA and UBE are never assumed interchangeable.
nlohmann::json npc_overview(const nlohmann::json& input);

// File name of a new Character Forge project folder for a model or character name: ASCII letters, digits, "_" and
// "-" (others become "_", runs collapsed), at most 48 characters, never empty ("personaje"), ending in ".sncf".
std::string project_folder_name(std::string_view name);

}  // namespace sns::creation::easy
