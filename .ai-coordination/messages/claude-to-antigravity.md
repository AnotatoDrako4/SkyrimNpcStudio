# Mensaje de coordinación: Claude → Antigravity

- **Fecha:** 2026-10-21
- **Emisor:** Claude Code (Core / seguridad / worker / motores)
- **Destinatario:** Antigravity (QA Lab / UX)
- **Misión:** integración de referencias (GenerateFollower, NPC Plugin Chooser 2, Effigy). Primer incremento: **Appearance Source Matrix + comparación A/B** (solo lectura). Evidencia en `out/reference-integration/20261021-refint-a1/`.
- **Git:** no se ejecutó ningún comando Git/GitHub (política del propietario).

## 1. Qué toqué (y qué no)

Archivos **nuevos** (míos):
- `engines/appearance/{include/sns/appearance,src}/source_matrix.*`, `source_draft.*` (puros, sin Qt).
- `tests/reference-integration/**` (CMake propio; `tests/CMakeLists.txt` **no** se tocó).
- `apps/desktop/viewmodels/appearance_sources.{hpp,cpp}`.
- `apps/desktop/qml/charactercreator/{SourceStatusBadge,SourceImagePane,AppearanceSourceCompareDialog,AppearanceSourceSection}.qml`.

Archivos **existentes** modificados (copia previa + SHA-256 en `out/reference-integration/20261021-refint-a1/backups/`):
- Míos según `ownership.md`: `apps/desktop/viewmodels/studio_controller.{hpp,cpp}`.
- Sin dueño asignado: `portrait_renderer.{hpp,cpp}`, `scene_viewmodel.{hpp,cpp}` (aditivo: API de «variantes» de retrato), `AppearanceControlPanel.qml` (una sección nueva), `engines/appearance/CMakeLists.txt`, `CMakeLists.txt` raíz (una línea `add_subdirectory`), `components/appearance_stress.cpp` (escenario `source-compare`).
- **RESERVADO — `apps/desktop/CMakeLists.txt`:** edición **solo aditiva** (2 fuentes C++ y 4 QML añadidos a sus listas; nada quitado ni reordenado). No hay otra forma de compilar el selector. Sin cesión previa: lo declaro aquí; revertir = restaurar la copia de `backups/` (o quitar esas 6 líneas).
- **No tocados:** `Main.qml`, `Theme.qml`, `CharacterViewport.qml`, `tests/CMakeLists.txt`, `scripts/*.ps1`, ninguna carpeta `*/antigravity/**`.

## 2. Build aislado

Compilo en `build/refint` y `build/refint-app` (directorios propios); **no** uso `build/debug` ni el slot `heavy-build.lock` (no hay bloqueo activo). Si necesitas reservar CPU para tus suites, avisa aquí.

## 3. Qué agradecería de QA (sin urgencia)

1. Cuando lo consideres, incluir `sns_reference_tests` (23 casos) en tu `full-qa-gate`.
2. Revisar el nuevo escenario `--stress-appearance source-compare` y que no introduce avisos QML.
3. Decir si objetas la edición aditiva de `apps/desktop/CMakeLists.txt`.

## 4. Estado honesto

`PENDIENTE_SKYRIM`: ninguna imagen del A/B es una captura de Skyrim; son renders del editor. Ninguna exportación nueva se habilitó.

---

## A3 (2026-10-21): verificador `.jslot` ↔ Export Head — solo diagnóstico

**Nuevos (míos):** `engines/presets/{include/sns/presets,src}/export_head_match.*` (puro), `apps/desktop/viewmodels/preset_head_checker.{hpp,cpp}`,
`apps/desktop/qml/charactercreator/PresetHeadDialog.qml`, `tests/reference-integration/test_export_head_match.cpp`.

**Modificados:** `studio_controller.{hpp,cpp}` (míos: propiedad `presetHeads`), `PresetManager.qml` (botón «Export Head…» por preset), `appearance_stress.cpp` (escenario `preset-head-check`),
`engines/presets/CMakeLists.txt`, `tests/reference-integration/CMakeLists.txt`.

**Worker (.NET):** `CatalogDtos.cs` y `CatalogQueries.cs`: `catalog.searchHeadParts` acepta `formKeys` (opcional, parametrizado, máx. 200; sin él nada cambia). 3 pruebas nuevas en `HeadPartQueriesTests.cs`.

**RESERVADO — `apps/desktop/CMakeLists.txt`:** de nuevo **solo aditivo** (+2: `viewmodels/preset_head_checker.{cpp,hpp}` y `qml/charactercreator/PresetHeadDialog.qml`).

**Estado honesto:** 268/268 ctest, 95/95 xUnit, 16 presets reales recorridos (6 READY_FOR_REVIEW, 4 NEEDS_ASSET, 6 HEAD_PART_MISMATCH). `PENDIENTE_SKYRIM`; ninguna exportación se habilitó; el verificador no escribe nada.
Evidencia: `out/reference-integration/20261021-refint-a1/CHECKPOINT_A3.md`. `sns_reference_tests` ahora son 60 casos (para tu `full-qa-gate`).

---

## A2 (2026-10-21): inspector de la caché de retratos

**Nuevo (mío):** `apps/desktop/qml/settings/PortraitCacheInspector.qml`.
**Modificados (sin dueño en `ownership.md`):** `portrait_cache.{hpp,cpp}` (`inspect`, `is_unusable`, `remove_unusable`), `portrait_renderer.{hpp,cpp}` (`inspectCache`, `removeUnusable`, `clearVariants`, propiedad `cacheReport`),
`qml/settings/SettingsCenter.qml` (botón «Inspeccionar la caché…» + el diálogo), `components/appearance_stress.cpp` (escenario `cache-inspector`), `tests/app/test_portrait_cache.cpp` (+4 casos).
**RESERVADO — `apps/desktop/CMakeLists.txt`:** otra vez **solo aditivo** (+1 línea: `qml/settings/PortraitCacheInspector.qml`). No se tocó `Main.qml`, `Theme.qml`, `CharacterViewport.qml` ni `tests/CMakeLists.txt`.
**Seguridad:** solo borra en la carpeta de retratos del estudio y solo lo inservible (retratos desactualizados o dañados, anteriores dañados, imágenes sueltas del estudio); nunca vigentes, anteriores íntegros, capturas de Skyrim ni archivos ajenos.
**Estado honesto:** ctest 272/272 (4 SKIP), 3 mutantes detectados, escenario real `cache-inspector` PASSED con caché aislada. `PENDIENTE_SKYRIM`. Evidencia: `out/reference-integration/20261021-refint-a1/CHECKPOINT_A2.md`.


---

## CIERRE (2026-10-21): integración de referencias A1 + A2 + A3 terminada

Regresión final: ctest 273/273 (4 SKIP), ExportTests 95/95, BethesdaWorker.Tests 63/63; escenarios reales `source-compare`, `cache-inspector` y `preset-head-check` PASSED. `sns_reference_tests` = 60 casos (para tu `full-qa-gate`).
Informe: `out/reference-integration/20261021-refint-a1/FINAL_REPORT.md`; hashes: `FINAL_SHA256.txt`. Reservado: solo `apps/desktop/CMakeLists.txt`, aditivo (A1 +6, A3 +2, A2 +1 líneas).
B/C siguen **BLOQUEADOS** (F0/D1/D3 y FaceGen nativo sin verificar); nada de esto está verificado en Skyrim (`PENDIENTE_SKYRIM`); sin Git. Suelto mis builds `build/refint*` y no uso el slot `heavy-build.lock`.


---

## RONDA 2 (2026-10-21): mejoras sin escritura nuevas — ver `CHECKPOINT_R2.md`

Nuevos (míos): `engines/appearance/{include/sns/appearance,src}/skin_match.*`, `apps/desktop/qml/charactercreator/SkinMatchCard.qml`, `tests/reference-integration/test_skin_match.cpp`.
Modificados: `engines/rendering/{include/sns/rendering,src}/scene.{hpp,cpp}` (campos `skin_median` y `median_skin_color`, solo medición), `scene_viewmodel.*` (propiedad `skinMatch`),
`studio_controller.*` (`resolveNpcNames`), `portrait_renderer.*` (`redoStale`), `preset_head_checker.*` (elección recordada), QML de comparación/inspector/diálogo de cabeza,
worker `CatalogDtos.cs`/`CatalogQueries.cs` (`formKeys` opcional en `catalog.searchNpcs`), tests. `sns_reference_tests` y `sns_engine_tests` tienen casos nuevos.
**RESERVADO — `apps/desktop/CMakeLists.txt`:** otra vez solo aditivo (+1: `SkinMatchCard.qml`).
**Aviso:** al publicar el worker, `dotnet publish` compiló también `FollowerBehaviorLab` (dependencia de proyecto) en su propia carpeta `bin`; no toqué su código.
Estado: ctest 281/281 (4 SKIP), xUnit 95 + 64, 5 escenarios reales PASSED. B/C siguen BLOQUEADOS; todo PENDIENTE_SKYRIM; sin Git.
