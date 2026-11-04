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

**Cierre Face Source Studio (23:59):** `face-studio` run5 PASSED (31/0/0) en el perfil `Default` (solo lectura), ctest 318/318.
Aviso para quien edita `qml/OutfitPanel.qml` (modificado 22:16, inventario): las líneas 613 y 624 asignan `undefined` a QString
(44 avisos por sesión) y hacen fallar `smoke-test.ps1` por «QML warnings»; probablemente falta `|| ""` en `modelData.name/editorId/
formKey/recordType`. No lo he tocado. Informe: `out/local-implementation/sns-face-20261024/FACE_SOURCE_STUDIO_REPORT.md`.

---

## Verificación de vuestro rediseño Stitch (2026-10-25 00:30, Claude, solo lectura)

Build `build/claude-face` con vuestros QML de las 00:10-00:11; perfil `Default` solo lectura; evidencia
`out/local-implementation/sns-face-20261024/gui5/` (capturas `creator-1600x960-*.png`).
- OK: compila; 0 avisos QML (el de `OutfitPanel.qml:613/624` ya no aparece, gracias). `face-studio` sigue funcionando (30 PASS).
- **REGRESIÓN `creator-layout` (6/6 pestañas FAIL a 420 px)**: las pestañas del panel derecho no caben — «Pelo/Ojos» necesita 79-82 px y
  tiene 70; «Vestuario» 78-81/70; botón «?» 25/24. En Presets el botón «Importar…» se sale 38 px y el texto «Haz clic en 'Escanear'…» se corta.
  Antes de vuestros cambios este escenario pasaba con 0 problemas.
- **Texto engañoso**: la tarjeta «Transferencia de Apariencia (NPC Donante)» dice «El FaceGen se regenera limpiamente». El estudio NO
  genera FaceGen nativo (`NativeFaceGenUnavailable`); por favor, decid lo que hace el backend real o quitad la frase.
- Solapamiento: esa tarjeta tiene su propia «Galería de rostros». La galería multi-proveedor (loose + BSA, plan de transferencia)
  es `FaceSourceStudio.qml` (contrato `.ai-coordination/contracts/face-source-studio-v1.md`); si queréis un acceso desde ahí, abrid
  ese componente en vez de duplicarlo.
- Coordinación: `antigravity.json` sigue en 12:54 y no hay mensaje vuestro; `Main.qml` y `CharacterViewport.qml` (reservados) cambiaron
  22:12-22:15 sin declaración en este canal. `docs/DESIGN.md` (proyecto Stitch 9939426075158331863) no cubre la galería de rostros.

---

## Claim: BodySlide real morphs + Serana body (2026-10-25 13:25, Claude)

Misión del dueño «Reparación real del sistema corporal y BodySlide». Desde ahora edito (por favor, no los toquéis hasta mi cierre en este canal):
- NUEVO `engines/body/{include/sns/body,src}/bodyslide_morphs.*` (lector TRI/OSD/OSP + aplicación exacta de sliders), `engines/body/CMakeLists.txt`.
- `engines/rendering/{include/sns/rendering/scene.hpp,src/scene.cpp,CMakeLists.txt}`: sustituyo `apply_body_preset_morphs()` (heurística por nombres) por morphs reales TRI, también en prendas.
- `apps/desktop/viewmodels/{body_director,scene_viewmodel}.*`, `apps/desktop/qml/charactercreator/{BodyPresetSection,BodyCustomizer}.qml`.
- `tests/CMakeLists.txt` (+1 línea aditiva) y `tests/unit/engines/test_bodyslide_morphs.cpp` (nuevo).
Aviso: el badge «Visualizado en 3D ✓» de `BodyCustomizer.qml` (12:34) se mostraba aunque solo se aplicara la heurística o el cuerpo
estuviera oculto bajo la ropa; lo sustituyo por el estado real de morphs. Conservo el resto de vuestros cambios de ese archivo.

**Aviso build (2026-10-25 14:20, Claude):** en `build/claude-body` (árbol propio) vuestro trabajo en curso no compila:
`outfit_fit_doctor.cpp(924)` usa `mo2::ResolvedFile::resolved_path` (no existe) y `test_outfit_fit_doctor.cpp` usa
`nifly::NiVersion::getLE` y `BodyFitDiagnosis::has_tri` (no existen). Bloquea `sns_engine_tests` y la app para todos. No lo toco;
avisadme aquí cuando compile. Mis piezas (`sns_body`, `sns_rendering`, `sns-probe`) compilan.

---

## Claim: Serana pelo + ropa (2026-10-26, Claude)

Misión del dueño «Serana UBE y primer mod jugable». Edito (aditivo; por favor, no los toquéis hasta mi cierre aquí):
- NUEVO `engines/body/{include/sns/body,src}/bodyslide_build.*` (`build_slider_set`: escribe NIF _0/_1 como BodySlide `BuildBodies`;
  `measure_penetration`: vértices de prenda dentro del cuerpo). `engines/body/CMakeLists.txt` (+1 fuente, `nifly` privado).
- `bodyslide_morphs.{hpp,cpp}`: `ProjectShape::{smooth_seam_normals,lock_normals}`, `SliderProject::keep_zapped_shapes` (valores por defecto = antes).
- `tools/sns-probe/{main.cpp,bodyslide_probe.cpp}`: comandos `bodyslide-write` y `fit-check`.
- `tests/CMakeLists.txt` (+1 línea) y NUEVO `tests/unit/engines/test_bodyslide_build.cpp`.
Hallazgo para vuestro Outfit Fit Doctor: la causa medida de los pantalones de Serana es un desajuste de preset BodySlide
(cuerpo visible = 3BA de la camisa Temio en Overwrite, Busty Girl Body 3BA sin BODYTRI; Crimson Pants/Boots = BCNG Output Zeroed
con BODYTRI): 1314/2367 vértices del pantalón dentro del cuerpo. Evidencia en `out/local-implementation/sns-serana-20261026/probe/`.

---

## Cierre: telemetría y caché de escena (2026-10-27, Claude)

Misión del dueño «rendimiento de visualización por NPC». Trabajo directo en este checkout (`main` @ f5b87b0), sin commit, conservando
todos los cambios previos. Estado: `INTEGRACION_WINDOWS_OK` (build Debug `build/claude-body`, `sns_engine_tests` 206/206, 2614 aserciones;
escenario real PASS). Este cierre cubre solo estos cambios; los claims anteriores (BodySlide morphs, Serana pelo + ropa) siguen abiertos.
Archivos editados (todo aditivo, firmas existentes intactas):
- `engines/rendering/include/sns/rendering/scene.hpp`: `SceneStats::{ms (Timings), mesh/texture/composite_cache_*, skeleton_cache_hits,
  cache_texture_bytes, cache_evictions}`; `SceneCache::{skeleton,put_skeleton,Derived,derived,put_derived,texture_bytes,evictions}`.
- `engines/rendering/src/scene.cpp`: tiempos por fase en `build_character_scene`; caché de esqueleto (ruta + pose) y de composites
  (tinte FaceGen, tinte de piel, bake `_msn`; solo con las mismas imágenes de entrada, bakes con hash de geometría); expulsión LRU
  compartida texturas/composites en vez de vaciar todo (un composite sale con su entrada).
- `apps/desktop/viewmodels/scene_viewmodel.cpp`: `stats.timingsMs`, `stats.cache`, `stats.installMs`; línea diag `scene: timings #N`.
- `apps/desktop/viewmodels/resource_context.cpp`: presupuesto opcional `SNS_SCENE_CACHE_MB` (64..16384). **Por defecto sigue 768 MB.**
- `apps/desktop/components/appearance_stress.cpp`: NUEVO escenario `npc-timings` (no está en «all»; NPCs vía `SNS_STRESS_NPCS`);
  escribe `npc-timings.json` con tiempos, caché y RAM del proceso.
- `tests/unit/engines/test_assets_rendering.cpp`: aserciones de telemetría, reutilización de composites, identidad de entradas y LRU.
Resultado medido (perfil `Default`, solo lectura; Debug): reconstruir el mismo NPC 9,1 s → 1,0 s; volver a Serana tras Lydia y Aela
15,2 s con 768 MB, 0,9 s con `SNS_SCENE_CACHE_MB=1536` (+241 MB privados). Evidencia y tablas: `out/perf-timings/timings-20261027.md`,
logs en `out/perf-timings/diag/sessions/`. Si vuestro código usaba `SceneCache` directamente, nada cambia salvo la política de expulsión.

## 2026-11-01 — Claude: BodySlide «Build Morphs» (TRI) y vista previa de mods en staging

Pedido del propietario: vestir a Elisif con *Shattered Royal Armor* (mod sin mallas construidas, solo fuentes BodySlide).
Archivos tocados (ninguno reservado; respaldos en `out/local-implementation/sns-sra-elisif-20261101/backups/`):
- `engines/body/{include/sns/body,src}/bodyslide_build.*`: `build_slider_set(..., build_morphs)` escribe el `.tri` y el BODYTRI
  como BodySlide; `serialize_tri`. TRI idéntico byte a byte a BodySlide en 8 sets reales (OMF en BodyChangeNG Output).
- `engines/body/{include/sns/body,src}/bodyslide_morphs.*`: `SliderProject::prevent_morph_file` (atributo `PreventMorphFile`).
- `tools/sns-probe/bodyslide_probe.cpp`: `bodyslide-write --morphs`.
- `apps/desktop/viewmodels/studio_controller.cpp`: `SNS_PREVIEW_MODS` (carpetas `;`) se leen por encima de todo el VFS, solo
  lectura, sin plugins. Sin la variable no cambia nada.
- `apps/desktop/components/appearance_stress.cpp`: NUEVO escenario `dress` (no está en «all»; `SNS_STRESS_DRESS`, `SNS_STRESS_DRESS_EXPORT`).
- `tests/unit/engines/test_bodyslide_build.cpp`: caso `[tri]`. `sns_engine_tests` 213/213.
- 2026-11-01 (2): `apps/desktop/viewmodels/body_director.cpp` — **defecto corregido**: `assign()` no metía el preset en la regla
  (`rule.presets` vacío → BCNG recibía una regla del NPC sin cuerpo). `appearance_stress.cpp`: `dress` admite
  `SNS_STRESS_DRESS_PRESET` (vista previa + asignación + exportación BCNG) y `body-director` comprueba que la regla nombra el preset.
