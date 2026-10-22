# PLAN_INTEGRATION_AG03 — Character Forge Follower Behavior Studio Integration

## Context
The Follower Behavior & Voice Intelligence Studio (`FollowerBehaviorLab`) has been implemented in `tools/antigravity/follower-behavior-lab/` and verified with 28 tests in .NET 10.
The integration drop-in components have been designed and validated in `tools/antigravity/follower-behavior-lab/DropIn/`.
The goal of this plan is to integrate the behavior intelligence contracts, worker RPC handlers, and UI panels into Character Forge while preserving all existing stability, zero regressions on the 17 QA suites, and preserving the immutable security invariants:
1. `PlayableExportUnlocked = false` and `NativeFaceGenUnavailable` remain locked.
2. Static records never emit `recruitable: true`, `marriageWorks: true`, or `VERIFICADO_EN_SKYRIM`.
3. Identity, appearance, morphs, textures, and placement are 100% protected.

---

## Global Constraints
- **NO GIT / NO GITHUB:** All tracking is performed via cryptographic SHA-256 fingerprints and local verification reports.
- **Additive Only:** Do not break existing wire contracts or remove any fields from `NativeNpcDraft` or QML models. Bump `Minor` protocol version from 5 to 6.
- **FaceGen Block:** `CreationValidateResponse.Exportable` must remain `false` while native FaceGen is pending.
- **Verification Gates:** Each task must compile cleanly and pass its relevant test suites (`BethesdaWorker.Tests`, `ExportTests`, `FollowerBehaviorLab.Tests`, and Catch2 reference tests).

---

## Task 1: Wire Protocol & C++ Engine Contracts
Update `engines/ipc/include/sns/ipc/protocol.hpp` to bump `kMinor` to 6 and declare the 5 new RPC method constants (`kCreationAnalyzeVoice`, `kCreationGetBehaviorCatalog`, `kCreationApplyArchetype`, `kCreationUndoArchetype`, `kCreationValidateBehavior`).
Ensure `engines/creation/include/sns/creation/native_npc_draft.hpp` can optionally serialize `spell_form_keys` and `perk_form_keys` in `nlohmann::json` without breaking backwards compatibility.
Verify by compiling C++ engine targets or running `ctest`.

## Task 2: Worker .NET Protocol & DTOs
Update `services/BethesdaWorker/Contracts/Protocol.cs` to bump `Minor` to 6 and declare the matching RPC constants.
Update `services/BethesdaWorker/Contracts/CreationDtos.cs` to declare:
- `SpellFormKeys` and `PerkFormKeys` in `NativeNpcDraft` (defaulting to empty lists).
- DTOs for `creation.analyzeVoice`, `creation.getBehaviorCatalog`, `creation.applyArchetype`, `creation.undoArchetype`, and `creation.validateBehavior`.
Verify compilation of `services/BethesdaWorker/BethesdaWorker.csproj`.

## Task 3: Worker .NET Behavior Service & RPC Target
Incorporate the behavior intelligence queries into `services/BethesdaWorker/`:
- Add `services/BethesdaWorker/Behavior/` with `VoiceDialogueService.cs`, `CombatClassService.cs`, and `ArchetypeService.cs` (adapted from the lab engine to work directly with `LoadOrderSession`).
- Extend `services/BethesdaWorker/Rpc/WorkerRpcTarget.cs` with the 5 JSON-RPC methods (`AnalyzeVoice`, `GetBehaviorCatalog`, `ApplyArchetype`, `UndoArchetype`, `ValidateBehavior`) and add them to `Capabilities`.
Verify with `dotnet test tests/unit/BethesdaWorker.Tests/BethesdaWorker.Tests.csproj` and `tests/export/SkyrimNpcStudio.ExportTests/SkyrimNpcStudio.ExportTests.csproj`.

## Task 4: Character Forge QML Panels Integration
Integrate the drop-in QML updates:
- Update `apps/desktop/qml/characterforge/StatsEditor.qml` with the archetype selector bar (preview dry-run, apply, undo) and the repeaters for `spellFormKeys` and `perkFormKeys`.
- Update `apps/desktop/qml/characterforge/ForgeValidationPanel.qml` with the follower voice diagnostic rows and the explicit `REQUIRES_IN_GAME_TEST` warning.
Verify via headless smoke-test to ensure QML parses without warnings or syntax errors.

## Task 5: Full QA Gate & Evidence Audit
Execute the entire 17-suite regression gate (`scripts/antigravity/Invoke-SnsRegression.ps1 -Suite full-qa-gate`).
Verify that all 17 suites pass (100% PASS), `HasDifferences = false`, and run `Test-SnsEvidence.ps1` to confirm cryptographic evidence integrity.
Update `CHECKPOINT.md` and coordination files with the final status.
