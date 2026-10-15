# Traspaso de trabajo (Claude → Antigravity)

Archivo de trabajo, **no versionar**. Última actualización: 2026-10-14, sesión de Claude.
Tarea: «HIGH POLY HEAD PROTECTION + EASY MOD CREATOR» (Serana, `Priscilla.Serana.Replacer.esp`).

## Ramas y PR

- `fix/forge-click-selection` → PR #6 (abierto): la prueba `--forge-preview` hace clic con
  `QTest::mouseClick`; 63 PASS / 0 FAIL. No mezclar con Serana.
- `fix/serana-hair-color-safe-mode` (desde `main` 03c9c9c): trabajo de Serana. Contenía cambios sin
  confirmar de otra sesión (Hair Color Safe Mode inicial, `verify_hair_tint_integrity`, docs nuevos);
  se conservan y se están revisando.
- Nunca: commit directo en `main`, force push, escribir en la instancia MO2 o en Skyrim/Data.
  Exportaciones locales de prueba: `out\local-exports`.

## Entorno de pruebas locales (solo lectura del perfil)

```powershell
. .\scripts\dev-env.ps1
$env:SNS_LOCAL_MO2_INI = 'D:\Apps\ModOrganizer.ini'
$env:SNS_LOCAL_PROFILE = 'Default'
$env:SNS_LOCAL_CACHE_DIR = 'D:\.Proyectos Games\SkyrimNpcStudio\out\local-cache'
.\build\debug\bin\sns_integration_tests.exe "[serana]"
```

## P0 — hechos verificados con los datos reales (perfil `Default`)

- Mod MO2: `ESP-FE Servant Priscilla high poly follower healer- ... replacer- Vampire form`.
- `DLC1Serana` `002B6C:Dawnguard.esm`; cadena: Dawnguard.esm → USSEP → **Priscilla.Serana.Replacer.esp
  (ganador, índice 412)**.
- FaceGen ganador: suelto, del mismo mod, 3.726.729 B; FaceTint suelto 1.048.704 B, mismo mod.
- Shapes del FaceGen: AAA_GirlHeads 3832 v, AAA_Hair 23320, AAA_Hairline 23320, AAA_Scalp 897,
  FemaleEyesHumanHazelBrown 176, AAA_FemaleBrows 371, AAA_Horn 1162, FemaleMouthHumanoidDefault 141.
- `AAA_GirlHeads` (malla `meshes\Priscilla body\Body\FemaleHead.nif`, 3832 v) usa TRI de raza y de
  chargen **vanilla** (`FemaleHeadRaces.tri`, `FemaleHeadCharGen.tri`, 996 v) y expresión propia
  (`femalehead.tri`): morphs de raza/sliders incompatibles con la malla.
- Análisis de la app (regenerar y comparar): `same_topology=1`, desviación máx. **2,797 u** →
  modo `preserve`.
- Ruta antigua del primer cambio en modo `PreserveExisting`: posiciones idénticas (máx. 0 en las 8
  shapes), pero el NIF es otro grafo (orden de shapes/raíz nuevos) → el guard lo marca.
- Ruta `Regenerate` (medición en curso en `out\serana-p0b.log`).
- Hair Color Safe Mode (parche del FaceGen ganador): 4 shapes HairTint recoloreadas (AAA_Hair,
  AAA_Hairline, AAA_Scalp, AAA_FemaleBrows), máx. 0, guard OK, 20 colores OK.

## Causa raíz (en confirmación)

`FaceState::geometry_mode` vale `"regenerate"` por defecto y solo pasa a `"preserve"` cuando termina
`analyse_existing_facegen()` (asíncrono, regenera la cabeza entera para comparar). `start_build()`
captura el modo al editar: un cambio de color hecho antes de que acabe el análisis, o restaurado por
deshacer/rehacer desde el historial (que guarda `geometry_mode`), se construía con `Regenerate`,
que reconstruye la cabeza desde los Head Parts y TRI y pierde el esculpido (≈2,8 u). Además
`only_hair_color_differs()` compara `geometry_mode` y `source`, así que esas diferencias sacaban el
cambio de la ruta de color.

## Implementado en `fix/serana-hair-color-safe-mode` (sin confirmar aún)

- `engines/appearance`: `same_face_record`, `only_hair_color_differs`, `face_build_kind`
  (Unchanged / WaitForAnalysis / HairColorSafe / Full), `kGeometryPending`.
- `AppearanceEditor`: baseline `pending` hasta el análisis; `edit()` rechaza ediciones mientras
  analiza (propiedad QML `analysing`); `start_build` usa `face_build_kind`; modo seguro sin fallback;
  al reabrir un proyecto el modo lo decide el análisis (aviso si pedía «regenerate»); exportación con
  `onlyFields` (`HCLF`, `NAM6`) y guard repetido sobre los bytes a exportar.
- `engines/face`: `patch_hair_tint` reescribe solo los 12 bytes del color en una copia del NIF
  (offsets por tamaños de bloque del encabezado, validados contra nifly); errores
  `HAIR_TINT_LAYOUT_UNVERIFIED`, `HAIR_TINT_NOT_APPLIED`, `FACEGEN_GEOMETRY_CHANGED`;
  `verify_hair_tint_integrity` con `FaceGenDifference{shape, property, before, after}`.
- Worker: `FaceAssignment.OnlyFields`, `ApplyOnlyFields`, `MinimalChangeViolated`,
  `OnlyFieldsUnsupported`, verificación tras escribir contra el registro ganador.
- Pruebas: `[face]`+`[appearance]` 20 casos/361 aserciones OK; .NET 67/67 OK (`dotnet test
  --solution SkyrimNpcStudio.slnx --no-build`; con build hubo un «no tests ran» puntual sin explicar).
- Incidente: detuve por error el worker (PID 24984) de la app `dist` abierta por el usuario (PID 13824).
- Diagnóstico facial `face::diagnose_face_compatibility` + `AppearanceEditor.faceCompatibility`
  (procedencia MO2, conflicto ESP/FaceGen) + `FaceProtectionCard.qml` en Pelo/Ojos y Rostro;
  ediciones bloqueadas se rechazan (`refuse_blocked`).
- Arnés real: escenario `hair-safe` en `apps/desktop/components/appearance_stress.cpp`.
  Primera ejecución sobre Serana: exportación OK, NIF exportado = ganador salvo HairTint, sin
  FaceTint, `changedFields = [HCLF]`; únicas fallas = comparación de QColor HSV de la prueba
  (corregida: color de 8 bits).
- Docs actualizados: HIGH_POLY_HEAD_COMPATIBILITY (reescrito), DEVELOPMENT_PLAN (tabla de estados),
  ARCHITECTURE (tabla arreglada + flujo), CRASH_DIAGNOSTICS, COMPATIBILITY, README,
  EASY_MOD_CREATOR (marcado PENDIENTE: no hay UI todavía).

## ABIERTO 1 — corrupción del montón (prioridad máxima)

Segunda ejecución del arnés de Serana (build Debug con los cambios de esta rama):
`hair-single, replacer-facegen, hair-safe, early-edit, sliders, undo-redo, export-reopen`. Tras el
resumen, al cerrar, el CRT de depuración mostró «HEAP CORRUPTION DETECTED: after Normal block
(#266487) … wrote to memory after end of heap buffer» y la app quedó colgada en ese diálogo.
Pilas (CRT fiable; símbolos del exe NO fiables porque un enlace fallido reescribió el PDB):
`out\hang\stacks-19368.txt`; minidump pequeño `out\hang\exit-hang-19368.dmp`. Último trabajo antes
del cierre en el registro de sesión: `facegen.full #71` (build completo tras reabrir el proyecto,
con `compose FaceTint`) y `scene.build #72`. No se sabe si es previo a esta rama.
Siguiente: repetir con el preset `asan` (ver docs/CRASH_DIAGNOSTICS.md) el mismo escenario para
localizar la escritura; y/o bisecar escenarios. No marcar nada como VERIFICADO EN WINDOWS hasta
resolverlo.

## ABIERTO 2 — Caso C (guardar/reabrir) no queda en Hair Color Safe Mode

Tras reabrir el proyecto en `export-reopen`, el cambio solo de color de Serana se construyó como
`full` (PreserveExisting + FaceTint recompuesto) en vez de `hair-safe`: el ciclo
`to_face_config` → `from_face_config` altera algún dato del registro de Serana (sospecha: tintes o
peso/presets). La prueba unitaria con datos tipo Lydia sí da `HairColorSafe`. Siguiente: volcar
`recipe` y el `.snps` (`out\stress-work\…\StressProject.snps`) y comparar campo a campo; añadir
`check_hair_safe` al final de `export-reopen`.

## Siguiente paso exacto

1. Recompilar (`cmake --build --preset debug`) y repetir el arnés de Serana (comando en
   HIGH_POLY_HEAD_COMPATIBILITY.md §6) → esperar `summary: PASSED`.
2. Regresión Lydia: `smoke-test.ps1 -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc
   0A2C8E:Skyrim.esm -StressAppearance all -ShowWindow`.
3. `ctest --preset debug`, `dotnet test --solution SkyrimNpcStudio.slnx --no-build`,
   `sns_integration_tests "[serana]"`, `scripts/local-review.ps1`.
4. Commit en `fix/serana-hair-color-safe-mode`, push, PR contra `main` (no merge).
5. Después: P5 Easy Mod Creator según `docs/EASY_MOD_CREATOR.md`.

## Plan en ejecución (orden)

1. P1 `AppearanceEditor`: no construir hasta que el análisis termine; decisión de build pura y
   probada en `engines/appearance` que ignora `geometry_mode`/`source` para «solo color»; modo seguro
   desde el FaceGen ganador; sin fallback a `build_facegen`.
2. P2 guard semántico con detalle (pieza, propiedad, antes, después, operación) en `engines/face`.
3. P3 worker: alcance `hair-color` en `FacePatchBuilder` (solo HCLF; error si cambia otro campo);
   informe de campos cambiados al estudio.
4. P4 pruebas: unitarias sintéticas, .NET, locales `[serana]` (exportar a `out\local-exports`,
   releer plugin y NIF), regresión Lydia, `local-review.ps1`.
5. Docs (`HIGH_POLY_HEAD_COMPATIBILITY.md`, `CRASH_DIAGNOSTICS.md`, plan, arquitectura) y PR.
6. Solo después: diagnóstico de compatibilidad facial en UI y Easy Mod Creator (P5–P6).

## Advertencias sobre el trabajo previo sin confirmar

- `docs/DEVELOPMENT_PLAN.md` marcaba como hechos puntos no probados (worker «override mínimo HCLF»,
  fixtures de TRI incompatible/shader incompatible). Corregir antes de confirmar.
- `docs/ARCHITECTURE.md` insertó una sección dentro de la tabla de módulos (rompe la tabla).
