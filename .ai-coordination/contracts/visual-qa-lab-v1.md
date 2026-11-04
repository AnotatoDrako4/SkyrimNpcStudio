# Coordination Contract: Skyrim NPC Studio — Visual QA Lab (`visual-qa-lab-v1`)

**Owner:** Antigravity AG03  
**Status:** Active / Implemented  
**Module Root:** `tools/visual-qa-lab/`  
**Test Suite Root:** `tests/visual-qa/`  
**Documentation Root:** `docs/visual-qa/`  
**Output Directory:** `out/visual-qa/<run-id>/`

---

## 1. Purpose & Non-Interference Guarantee

`Skyrim NPC Studio — Visual QA Lab` provides independent C++ geometric validation, clipping detection, anatomical seam analysis, BodySlide morph verification (`PIRT` `.tri` and `OSD\0` `.osd`), reproducible 8-view image capture, and automated visual regression testing (`VQA-01` .. `VQA-10`).

- **Read-Only Consumption of Shared Engines:** Consumes `sns::rendering::CharacterScene`, `sns::rendering::RenderPart`, `sns::nif::MeshFile`, `sns::body::PresetLibrary`, and `sns::assets::AssetResolver` without modifying `engines/body/`, `engines/rendering/`, or `apps/desktop/`.
- **Strict Read-Only on Game & MO2:** Never writes to `Skyrim/Data`, `F:\Mods Skyrim`, or `D:\Apps\ModOrganizer.ini`.
- **Isolated Build Tree:** Builds in `build/ag03-vqa/` (`sns_visual_qa.lib`, `sns-visual-qa.exe`, `sns_visual_qa_tests.exe`).

---

## 2. Public C++ Headers & Namespace (`sns::visual_qa`)

1. `tools/visual-qa-lab/include/sns/visual_qa/visual_qa_types.hpp`
   - `QaVerdict` (`Pass`, `Fail`, `Skip`, `Inconclusive`)
   - `AnatomicalZone` (20 view-space anatomical zones)
   - `SurfaceRole` (`VisibleBodySkin`, `EmbeddedBodyInOutfit`, `VisibleHeadSkin`, `VisibleGarment`, `VisibleHeadPart`, `CoveredBodyBySlot`, `HiddenNifShape`, `SmpCollisionProxy`, `UnequippedOutfitPiece`)
   - `ClippingClassification` (4 severity levels + 5 explicit false-positive exclusion categories)
   - `VisualQaIssue`
2. `tools/visual-qa-lab/include/sns/visual_qa/clipping_analyzer.hpp`
   - `test_triangle_triangle_intersection(...)`
   - `analyze_scene_clipping(const rendering::CharacterScene&, const ClippingAnalysisOptions&)`
3. `tools/visual-qa-lab/include/sns/visual_qa/seam_analyzer.hpp`
   - `analyze_scene_seams(const rendering::CharacterScene&, const SeamAnalysisOptions&)`
   - Inspects `NeckHead`, `WristHandLeft`, `WristHandRight`, `AnkleFootLeft`, `AnkleFootRight`
4. `tools/visual-qa-lab/include/sns/visual_qa/morph_analyzer.hpp`
   - `parse_pirt_tri(...)`, `parse_osd_morphs(...)`
   - `compare_part_morph(...)`, `compare_scene_morphs(...)`
   - Distinguishes `ExactBodySlideMorph`, `ApproximateGeometricMorph`, `LegitimateZeroChange`, `FalseNoChange`, `OutfitMorphNotApplied`, `AbnormalDeformation`
5. `tools/visual-qa-lab/include/sns/visual_qa/image_comparator.hpp`
   - `build_controlled_camera_set(...)`, `render_controlled_view(...)`, `capture_controlled_scene_views(...)`, `compare_images(...)`
6. `tools/visual-qa-lab/include/sns/visual_qa/scene_inspector.hpp`
   - `analyze_scene_integrity(...)`, `evaluate_character_scene(...)`
7. `tools/visual-qa-lab/include/sns/visual_qa/regression_suite.hpp`
   - `run_visual_regression_suite(...)` (`VQA-01` .. `VQA-10`)
8. `tools/visual-qa-lab/include/sns/visual_qa/report_writer.hpp`
   - `write_visual_qa_report_bundle(...)`

---

## 3. Output Bundle Schema (`out/visual-qa/<run-id>/`)

Every execution produces:
- `report.json` — Full structured telemetry, build metadata, input hashes, primary NPC evaluation, and `VQA-01`..`VQA-10` suite results (`in_game_verification: "PENDIENTE_SKYRIM"`).
- `summary.md` — Executive summary distinguishing `PASS`, `FAIL`, `SKIP`, `INCONCLUSIVE`, and `PENDIENTE_SKYRIM`.
- `geometry-diff.json` — Per-part integrity, per-garment clipping & fit, per-junction seam distances, and per-shape morph metrics.
- `issues.json` — Traceable issues with `severity`, `code`, `category`, `meshPath`, `shapeName`, `pieceLabel`, `armorFormKey`, `provider`, and `zone`.
- `artifact-hashes.json` — SHA-256 digests and byte sizes of all generated files.
- `captures/` — 8 controlled PNG views and before/after visual diff heatmaps.
