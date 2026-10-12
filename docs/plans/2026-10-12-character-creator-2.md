# Character Creator 2.0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform Skyrim NPC Studio Ultimate into a professional NPC visual customization studio (Character Creator 2.0) supporting real-time 3D preview, RaceMenu .jslot presets, NPC replacer donor appearances, full facial editing, modular wardrobe customization, .snps v2 projects, and complete in-game export (Modo A: Outfit, Modo B: Face Overhaul, Modo C: Complete Overhaul, Modo D: Multi-NPC).

**Architecture:** Extend the existing dual-process architecture (C++20 Qt 6 GUI engine + .NET 10 Mutagen worker). Unify face and outfit state into `engines/appearance/` with an undo/redo command stack. Implement `.jslot` parsing and donor extraction in `engines/presets/`, facial morph/headpart logic in `engines/face/`, expand `engines/projects/` to format v2, extend `BethesdaWorker` contracts and RPC methods for face overhaul and complete overhaul packaging, and build a dedicated modular workspace UI in `apps/desktop/qml/charactercreator/`.

**Tech Stack:** C++20, Qt 6.11.2 (QML / Qt Quick 3D), .NET 10 (Mutagen.Bethesda, Microsoft.Data.Sqlite), Catch2, xUnit, CMake.

## Global Constraints

- Never destroy or rebuild existing working modules from scratch; reuse and extend `engines/mo2`, `engines/nif`, `engines/rendering`, `engines/textures`, `engines/projects`, `engines/outfit`, `services/BethesdaWorker`.
- Target C++20 with MSVC `/W4 /WX` compatibility.
- Ensure .snps format v2 cleanly auto-migrates and loads v1 projects without losing outfit data.
- Never write directly to game Data or overwrite user mods; exports must remain staged, verified, atomic, and MO2-installable.
- FaceGen files must strictly target origin plugin and 8-digit hex FormID paths (`meshes/actors/character/facegendata/facegeom/<origin_plugin>/<formid>.nif` and `textures/.../facetint/<origin_plugin>/<formid>.dds`).
- Easy and Advanced modes share the exact same underlying appearance draft model.

---

### Task 1: Unified Appearance Engine & .snps v2 Model (engines/appearance & engines/projects)

**Files:**
- Create: `engines/appearance/include/sns/appearance/appearance_draft.hpp`
- Create: `engines/appearance/src/appearance_draft.cpp`
- Create: `engines/appearance/CMakeLists.txt`
- Modify: `engines/projects/include/sns/projects/project.hpp`
- Modify: `engines/projects/src/project.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/unit/engines/test_appearance_draft.cpp`

**Interfaces:**
- Produces: `struct FaceDraft`, `struct AppearanceDraft`, `class AppearanceCommand`, `enum class AppearanceSourceKind`
- Extends: `struct NpcEdit` with `std::optional<FaceConfig> face;`
- Maintains backward compatibility with `kProjectFormatVersion = 2`

- [ ] **Step 1: Write failing tests for AppearanceDraft and Project v2 migration**
- [ ] **Step 2: Run C++ test to verify it fails**
- [ ] **Step 3: Implement engines/appearance and project.hpp/cpp updates**
- [ ] **Step 4: Run C++ test to verify it passes**
- [ ] **Step 5: Commit changes**

---

### Task 2: BethesdaWorker Contracts & Face Data Queries Expansion

**Files:**
- Modify: `services/BethesdaWorker/Contracts/AppearanceDtos.cs`
- Modify: `services/BethesdaWorker/Contracts/CatalogDtos.cs`
- Modify: `services/BethesdaWorker/NpcCatalog/RecordExtractor.cs`
- Modify: `services/BethesdaWorker/NpcCatalog/AppearanceQueries.cs`
- Modify: `services/BethesdaWorker/NpcCatalog/CatalogQueries.cs`
- Modify: `services/BethesdaWorker/Rpc/WorkerRpcTarget.cs`
- Test: `tests/unit/BethesdaWorker.Tests/AppearanceTests.cs`

**Interfaces:**
- Produces: Detailed face morphs, face parts, tint layers, head parts with mesh paths, donor appearance query `appearance.getDonorAppearance`, headpart search `catalog.searchHeadParts`

- [ ] **Step 1: Write failing xUnit tests for donor appearance and detailed face data**
- [ ] **Step 2: Run dotnet test to verify failure**
- [ ] **Step 3: Implement contracts, RecordExtractor, AppearanceQueries, and Rpc methods**
- [ ] **Step 4: Run dotnet test to verify success**
- [ ] **Step 5: Commit changes**

---

### Task 3: Presets Engine — RaceMenu .jslot Parser & Donor Appearance (engines/presets)

**Files:**
- Create: `engines/presets/include/sns/presets/jslot_parser.hpp`
- Create: `engines/presets/src/jslot_parser.cpp`
- Create: `engines/presets/include/sns/presets/preset_model.hpp`
- Create: `engines/presets/src/preset_model.cpp`
- Create: `engines/presets/CMakeLists.txt`
- Test: `tests/unit/engines/test_presets.cpp`

**Interfaces:**
- Produces: `JslotPreset parse_jslot(std::string_view json_content)`, `CompatibilityReport evaluate_compatibility(const JslotPreset&, const NpcTarget&)`

- [ ] **Step 1: Write failing tests for RaceMenu .jslot parsing and validation**
- [ ] **Step 2: Run C++ tests to verify failure**
- [ ] **Step 3: Implement engines/presets parser and validator**
- [ ] **Step 4: Run C++ tests to verify success**
- [ ] **Step 5: Commit changes**

---

### Task 4: Face Engine — Morphs, Sliders, HeadParts & Compatibility (engines/face)

**Files:**
- Create: `engines/face/include/sns/face/face_morphs.hpp`
- Create: `engines/face/src/face_morphs.cpp`
- Create: `engines/face/include/sns/face/head_parts.hpp`
- Create: `engines/face/src/head_parts.cpp`
- Create: `engines/face/CMakeLists.txt`
- Test: `tests/unit/engines/test_face_engine.cpp`

**Interfaces:**
- Produces: Slider bounds, normalization, FaceMorph key mapping, head part validation and slot rules

- [ ] **Step 1: Write failing test for Face morph calculations and headpart cataloging**
- [ ] **Step 2: Run C++ test to verify failure**
- [ ] **Step 3: Implement engines/face logic**
- [ ] **Step 4: Run C++ test to verify success**
- [ ] **Step 5: Commit changes**

---

### Task 5: FaceGen Export & Mod Packager Expansion (Services & Export)

**Files:**
- Create: `services/BethesdaWorker/PatchBuilder/FacePatchBuilder.cs`
- Modify: `services/BethesdaWorker/PatchBuilder/ModPackager.cs`
- Modify: `services/BethesdaWorker/Contracts/Protocol.cs`
- Modify: `services/BethesdaWorker/Rpc/WorkerRpcTarget.cs`
- Test: `tests/export/SkyrimNpcStudio.ExportTests/FacePatchExportTests.cs`

**Interfaces:**
- Produces: `export.faceOverhaul`, `export.completeOverhaul`, FaceGen mesh/tint packaging to `meshes/.../facegeom/<origin>/<formid>.nif` and `textures/.../facetint/<origin>/<formid>.dds`

- [ ] **Step 1: Write failing integration/export tests for Face Overhaul and Complete Overhaul**
- [ ] **Step 2: Run dotnet test to verify failure**
- [ ] **Step 3: Implement FacePatchBuilder and ModPackager multi-mode exporter**
- [ ] **Step 4: Run dotnet test to verify success**
- [ ] **Step 5: Commit changes**

---

### Task 6: ViewModels — Character Creator & Appearance ViewModel

**Files:**
- Create: `apps/desktop/viewmodels/appearance_editor.hpp`
- Create: `apps/desktop/viewmodels/appearance_editor.cpp`
- Modify: `apps/desktop/viewmodels/studio_controller.hpp`
- Modify: `apps/desktop/viewmodels/studio_controller.cpp`
- Modify: `apps/desktop/viewmodels/scene_viewmodel.hpp`
- Modify: `apps/desktop/viewmodels/scene_viewmodel.cpp`
- Modify: `apps/desktop/CMakeLists.txt`

**Interfaces:**
- Produces: `AppearanceEditor` exposed to QML with preset management, sliders, donor switching, face preview hooks, comparator toggles, and unified export triggers

- [ ] **Step 1: Implement AppearanceEditor with undo/redo, donor preset application, and export hooks**
- [ ] **Step 2: Connect AppearanceEditor to SceneViewModel and StudioController**
- [ ] **Step 3: Compile and verify clean build with MSVC**
- [ ] **Step 4: Commit changes**

---

### Task 7: Character Creator QML UI Suite (apps/desktop/qml/charactercreator/)

**Files:**
- Create: `apps/desktop/qml/charactercreator/CharacterCreator.qml`
- Create: `apps/desktop/qml/charactercreator/CharacterToolbar.qml`
- Create: `apps/desktop/qml/charactercreator/CharacterLibrary.qml`
- Create: `apps/desktop/qml/charactercreator/CharacterPreview.qml`
- Create: `apps/desktop/qml/charactercreator/FaceEditor.qml`
- Create: `apps/desktop/qml/charactercreator/HairEditor.qml`
- Create: `apps/desktop/qml/charactercreator/EyeEditor.qml`
- Create: `apps/desktop/qml/charactercreator/SkinEditor.qml`
- Create: `apps/desktop/qml/charactercreator/BodyEditor.qml`
- Create: `apps/desktop/qml/charactercreator/OutfitDesigner.qml`
- Create: `apps/desktop/qml/charactercreator/PresetGallery.qml`
- Create: `apps/desktop/qml/charactercreator/PresetImportDialog.qml`
- Create: `apps/desktop/qml/charactercreator/PresetComparison.qml`
- Create: `apps/desktop/qml/charactercreator/AppearanceInspector.qml`
- Create: `apps/desktop/qml/charactercreator/CharacterExportWizard.qml`
- Modify: `apps/desktop/qml/Main.qml`
- Modify: `apps/desktop/CMakeLists.txt`

**Interfaces:**
- Produces: Integrated "PERSONALIZAR PERSONAJE" workspace with responsive layout, 3D viewport controls, 8 customization tabs, before/after comparison, preset gallery, wardrobe designer, and export wizard

- [ ] **Step 1: Create all QML components following dark theme standards**
- [ ] **Step 2: Integrate CharacterCreator into Main.qml with workspace switching**
- [ ] **Step 3: Compile desktop application and verify QML registration**
- [ ] **Step 4: Commit changes**

---

### Task 8: Verification, Test Suite & Documentation

**Files:**
- Modify: `tests/integration/test_worker_roundtrip.cpp`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/MOD_EXPORT_FORMAT.md`
- Run full test suite: `.\scripts\build.ps1`

- [ ] **Step 1: Run complete .NET test suite and verify 100% pass rate**
- [ ] **Step 2: Run complete CTest suite and verify 100% pass rate**
- [ ] **Step 3: Update documentation and export guidelines**
- [ ] **Step 4: Verify end-to-end Lydia synthetic case study**
