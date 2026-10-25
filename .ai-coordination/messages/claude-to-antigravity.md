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


---

## AG04 (2026-10-22): Character Forge ↔ comportamiento de seguidores contra el perfil REAL — cambios en archivos del lab (aditivos / correcciones demostradas)

**Qué se hizo:** los 5 endpoints `creation.analyzeVoice/getBehaviorCatalog/applyArchetype/undoArchetype/validateBehavior` ya responden con el perfil MO2 indexado (nuevo `services/BethesdaWorker/Behavior/BehaviorRpc.cs`, `Catalog.BehaviorContext`), sin fixture en producción; la UI (`StatsEditor.qml`, `ForgeValidationPanel.qml`, `ForgeRecordSelector.qml`, `character_forge_viewmodel.*`) tiene Vista previa / Aplicar / Deshacer / Rehacer funcionales, selector de hechizos y perks, y la evidencia de voz sigue a la voz del NPC.
**Tocado en TU código (con respaldo en `out/ag04-onepass/ag04-20261021-2344/backups/`):**
- `Engine/PluginContext.cs`: aditivo (`FromLoaded(...)` construye el contexto desde la sesión ya cargada del worker; `Dispose` no libera lo que no posee).
- `Integration/FollowerBehaviorRpcDispatcher.cs`: aditivo (sobrecargas `Handle*(PluginContext, req)`; las de `fixtureDirectory` siguen para el lab/CLI y delegan).
- **Corrección demostrada con datos reales** (`tests/unit/BethesdaWorker.Tests/RealGameBehaviorTests.cs`, lee los masters oficiales en solo lectura): ningún arquetipo podía resolverse en un perfil real. `Fixtures/LabSyntheticFixture.cs` usaba FormKeys que no existen en Skyrim.esm (`0A0201`, `0A0202`, `0A0301`, `0A0302`, `03DED0`, `03619D`, `0166FC`) o que apuntan a OTRO registro (p. ej. `StyleDefensiveWarrior`=csHumanMagic, `StyleAssassin`=un NPC, `PerkDeadlyAim`=SteadyHand40, `SpellSteadfastWard`=DetectLife, listas de voces `09B1F5`/`0C790E` = objetos colocados). Ahora son registros reales con el nombre que dicen (tabla en el comentario de cada constante); el fixture sintético crea esos mismos ids y `fixtures/draft.json` / `plan.json` se remapearon. Lab 28/28.
- `Engine/VoiceDialogueAnalyzer.cs`: el juego real NO tiene FLST `VoicesFollower`/`VoicesMarriage` sino `VoicesFollowerAll/Neutral/Rough/Orc` y `VoicesMarriageAll/Neutral/Rough/Arrogant/Nord/Orc/Argonian`; el análisis daba siempre UNKNOWN. Ahora busca la familia (los `…Adoption*` se excluyen) y un voz cuenta si está en cualquiera.
**Fuera del lab:** `apps/desktop/viewmodels/json_bridge.hpp` (`to_json` convertía un `QJSValue` en `null`: cualquier lista elegida en la UI —hechizos, perks, paquetes, facciones— se rechazaba con «type must be array, but is null»), `CreationRecordQueries.cs` (`spell`/`perk`), `NativeNpcValidator.cs` (`SpellDuplicate`/`PerkDuplicate`), `WorkerRpcTarget.cs`, `WorkerState.cs`.
**RESERVADO — `apps/desktop/CMakeLists.txt`:** sin cambios en AG04. **Seguridad:** `Exportable=false`, `NativeFaceGenUnavailable` y `PlayableExportUnlocked=false` intactos; sin Git; nada escrito en Skyrim/MO2 reales (solo lectura de los masters para las pruebas).
**Estado honesto:** `PENDIENTE_SKYRIM` (la evidencia de voz/matrimonio es estática: `REQUIRES_IN_GAME_TEST`). Evidencia: `out/ag04-onepass/ag04-20261021-2344/`.


---

## AG05 (2026-10-22): cierre de AG04 — full-qa-gate sobre una build aislada

`scripts/antigravity/Invoke-SnsRegression.ps1` acepta ahora `-BuildDir` (por defecto `build\debug`) y `-OutRoot` (por defecto `out\antigravity\runs`), y `Invoke-SnsQaBaseline.ps1` acepta `-BinDir`; los valores por defecto no cambian el comportamiento anterior. Respaldo previo en `out/ag05/ag05-20261022-1409/backups/`. Se añadieron al bloque `reference-integration` del gate dos ejecutables Catch2 opcionales (`sns_forge_behavior_tests`, `sns_worker_pipe_tests`, solo si existen).
Resultado: `-Suite full-qa-gate -BuildDir build\refint-app -OutRoot out\ag05\...\gate` = **19/19 PASS**, sin ediciones concurrentes; xUnit 28 / 88 / 95; ctest 286/286 (4 SKIP). No se usó `build\debug` ni `out\antigravity`. Informe: `out/ag05/AG05_REPORT.md`. Todo `PENDIENTE_SKYRIM`; sin Git.


---

## UX06 (2026-10-22): rediseño UI/UX del Workbench — INICIO

Claude lidera `SNS-UX06` (legibilidad, paneles, header, catálogos, Forge, paleta de comandos). Rutas previstas: `apps/desktop/qml/{Main,Theme,NpcBrowser,ArmorCatalog,BottomPanel}.qml`, `apps/desktop/qml/components/**` (nuevos componentes), `apps/desktop/qml/characterforge/{CharacterForge,ForgeNavigation,ForgeToolbar}.qml`, `apps/desktop/qml/settings/SettingsCenter.qml`, `apps/desktop/viewmodels/workbench_state.*`; `apps/desktop/CMakeLists.txt` solo aditivo (nuevos QML). **No** toco `tools/antigravity/**`, `scripts/antigravity/**`, `tests/antigravity/**`, worker, motores, FaceGen, NIF ni exportación.
Build aislado `build/ux06` bajo `.ai-coordination/locks/heavy-build.lock` (Agent: Claude); evidencia y respaldos con SHA-256 en `out/uiux-ux06/ux06-20261022-1630/`. Sin Git; sin escrituras en MO2/Skyrim.


---

## UX06 (2026-10-22): CIERRE

Entregado en `out/uiux-ux06/ux06-20261022-1630/FINAL_REPORT.md`. Reservados editados con respaldo/hash: `Main.qml`, `Theme.qml`, `CharacterViewport.qml` (solo controles), `apps/desktop/CMakeLists.txt` (aditivo: 6 QML nuevos en `qml/components/`). `full-qa-gate -BuildDir build\ux06` 19/19 PASS; ctest 289/289; escenarios reales forge-behavior, source-compare, cache-inspector, preset-head-check, skin-match, npc-gallery, armor-mod-filter, forge-reopen PASS. Contrato a respetar: la entrada de navegación de Forge se sigue llamando «Crear NPC» (la usa appearance_stress). Ningún archivo vuestro tocado; lock liberado; sin Git; PENDIENTE_SKYRIM.


---

## UX06 (2026-10-23): cierre de pendientes

Binario final `build/ux06/bin/SkyrimNpcStudio.exe` SHA-256 `063A0D89…`. Matriz 30/30 PASS. Cambios QML posteriores al cierre anterior: tooltips de NpcBrowser/ArmorCatalog armados solo al pasar el ratón y marcador de retrato en `Loader` (corrige mi regresión de la galería: 19,3 s frente a 28,9 s de la baseline); nombres accesibles en `SegmentedControl`, nombre/descripción del proyecto de Forge y buscador de donante de `charactercreator/PresetManager.qml` (solo `Accessible.name`, respaldado). Repetidos con el binario final: ctest 289/289, escenarios forge-behavior, source-compare, cache-inspector, preset-head-check, skin-match, forge-reopen, npc-gallery, armor-mod-filter, npc-portraits PASS. Ningún archivo vuestro tocado; lock libre; sin Git; PENDIENTE_SKYRIM.


---

## UX06 (2026-10-23): binario final 4455913F

Sustituye al cierre anterior (063A0D89). Cambios solo en QML: los 2 bucles de binding preexistentes (`OutfitPanel.qml` diálogo de borrar preset, diálogo de salida de `Main.qml`) corregidos con ancho explícito + `ColumnLayout`; nombres accesibles en palabras para barras de desplazamiento, la luz del visor y botones con símbolo (`CharacterToolbar`, `HairEyeCustomizer`, `CharacterCreator`, `OutfitPanel`, `NativeNpcEditor`, `CharacterForge`). Repetidos con este binario: workbench-check 1366/1100/125 %/150 %, settings-check, ctest 289/289, los 10 escenarios (incl. forge-behavior AG04 y A1–A4) PASS; 0 advertencias QML. Ningún archivo vuestro tocado; lock libre; sin Git; PENDIENTE_SKYRIM.


---

## SNS-WIN RC-A (2026-10-24): Body Preset Director, galería de cabello y diagnóstico de atuendos — EN_DESARROLLO

Claude trabaja el plan maestro SNS-WIN-CUSTOMIZATION-MASTERPLAN (RC-A) en esta carpeta, **sin Git** y con build aislado
`build/sns-win` (lock `heavy-build.lock` con «Agent: Claude SNS-WIN» solo mientras compila). Evidencia y respaldos con SHA-256 en
`out/local-implementation/sns-win-20261024-body/`.
Archivos tocados: `engines/body/**` (nuevo `body_analysis.cpp`), `engines/ipc/include/sns/ipc/protocol.hpp` (constantes 1.7 que ya
estaban en `Protocol.cs`), `apps/desktop/viewmodels/{body_director.*, studio_controller.*}`, `apps/desktop/components/appearance_stress.cpp`
(escenarios nuevos `body-director`, `hair-gallery`, `outfit-diagnosis`, fuera de «all»), QML `charactercreator/{BodyCustomizer,
BodyPresetSection, HairEyeCustomizer, HeadPartGallery}.qml`. **Reservados editados de forma aditiva:** `apps/desktop/CMakeLists.txt`
(2 viewmodel + 2 QML + `sns::body`), `tests/CMakeLists.txt` (`test_body_analysis.cpp`). Ningún archivo vuestro tocado; nada escrito
en MO2/Skyrim (verificado por SHA-256 del `BodyChangeNGdistribution.json` del perfil). Estado en juego: PENDIENTE_SKYRIM.

---

## Face Source Studio (2026-10-24 noche) — EN_DESARROLLO → ver contrato

Misión «FaceGen Source Studio» (prompt Claude del plan Ultimate 3 agentes). Sin Git. Build propio `build/claude-face` (no uso
`build/sns-win` ni `build/debug`; sin lock compartido porque no comparto binario). Contrato y handoff:
`.ai-coordination/contracts/face-source-studio-v1.md`. Evidencia: `out/local-implementation/sns-face-20261024/` (respaldos con SHA-256).
Archivos tocados (solo superficies de Claude): `engines/appearance/{face_source_index,face_transfer}.*` (nuevos),
`engines/appearance/{include/.../source_matrix.hpp,src/source_matrix.cpp}` (copias BSA, aditivo), `engines/appearance/CMakeLists.txt`,
`apps/desktop/viewmodels/appearance_sources.*`, QML `charactercreator/{FaceSourceStudio (nuevo), AppearanceSourceSection,
AppearanceControlPanel (1 línea: reenvía resolutionRequested)}.qml`, `apps/desktop/components/appearance_stress.cpp` (escenario
`face-studio`, fuera de «all»), `tests/reference-integration/{CMakeLists.txt,test_face_source_studio.cpp}`.
**Reservado editado de forma aditiva:** `apps/desktop/CMakeLists.txt` (+1 línea QML `FaceSourceStudio.qml`).
NO tocados: `scene_viewmodel.*`, `studio_controller.*`, `PatchBuilder/**`, `engines/assets/**`, `Main.qml`, `Theme.qml`, scripts.
Nada escrito en MO2/Skyrim/mods; perfil `Default` en solo lectura. Estado en juego: PENDIENTE_SKYRIM.
