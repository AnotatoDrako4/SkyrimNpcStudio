# Cobertura D1–D36 (estado comprobado)

Fecha: 2026-10-17 · Carpeta: `SkyrimNpcStudio-ui2` · Fuente de las decisiones: `docs/IMPLEMENTATION_PLAN_36_DECISIONS.md`.

Cada fila dice lo que **existe y se comprobó** en esta carpeta (archivo, prueba, comando), no lo que la especificación
pide. Una base de código previa no completa una decisión: la decisión entera queda `EN_DESARROLLO` hasta cumplir su
alcance y sus gates. Las fases F1–F7 no empiezan hasta superar F0. Nada está `VERIFICADO_EN_SKYRIM`.

- Commit/PR: **ninguno** (Git en espera por orden expresa del propietario). Las evidencias están en
  `out/local-implementation/20261017-141652/`.
- Estados: `NO_INICIADO`, `EN_DESARROLLO`, `IMPLEMENTADO_NO_COMPILADO`, `COMPILADO`, `TESTS_UNITARIOS_OK`,
  `INTEGRACION_WINDOWS_OK`, `PAQUETE_MO2_VERIFICADO`, `PENDIENTE_SKYRIM`, `VERIFICADO_EN_SKYRIM`, `BLOQUEADO`, `REGRESION`.
- Regresión de referencia (2026-10-17, `scripts\build.ps1`, build final): .NET 119/119 PASS; CTest 180: 171 PASS / 0 FAIL /
  9 SKIP (3 Blender no instalado, 6 `[local]` sin variables de perfil local), incluida `scripts:test run manifest…`.

## F0 (RC 1.1.0)

| D | Decisión | Estado comprobado | Artefacto demostrable | Pruebas concretas | Limitaciones / riesgos | Gate humano |
| --- | --- | --- | --- | --- | --- | --- |
| D1 | Revisión individual de los ambiguos | `INTEGRACION_WINDOWS_OK` · revisiones humanas **0/53** | `engines/appearance/{include/sns/appearance/review_case.hpp,src/review_case.cpp}`; panel «Revisión individual» en Origen; bloqueo en asistente y exportación; `review-cases.json` (53 pending) | `sns_engine_tests "[review]"` (6 casos), «ambiguous resolution needs an individual approval» (`[easymod]`); arnés `face-resolution` (Bolli: pendiente/aplazado/rechazado bloquean la exportación, carpeta vacía) y `appearance-scan` | aprobación no copiada al manifiesto del mod; reconsulta sobre el snapshot indexado | aprobación por NPC (53) |
| D2 | RC 1.1.0 | `BLOQUEADO` (ZIP reproducible) · suites `TESTS_UNITARIOS_OK` | guards existentes: `FaceSourceProviderUnknown`, FaceGen efectivo reconsultado, `RemainingFaceDifferences` (`FacePatchBuilder.cs`) | `FaceResolutionExportTests` (`Resolution_RefusesWhenTheSourceProviderIsUnknown`, `…RefusesWhatWouldNotMatchTheEffectiveFaceGen`, …); concurrencia `CatalogStoreConcurrencyTests` (3 nuevas, ciclo 1) | `package.ps1`/`release.ps1`/`local-review.ps1` usan Git: no ejecutados; 2 ZIP reproducibles pendientes | autorizar Git y merge |
| D3 | Validación híbrida MO2 + Skyrim | `INTEGRACION_WINDOWS_OK` · `PENDIENTE_SKYRIM` | `scripts/TestRunManifest.psm1`, `scripts/test-run-manifest.ps1`, `scripts/acceptance-kits.ps1`; 5 kits en `acceptance-kits-final/` (Serana, Lydia, Shady Sam, Brand-Shei con ZIP; Bolli sin ZIP instalable por D1) con acta y manifiesto VALID | `tests/scripts/test_run_manifest.tests.ps1` (CTest `scripts:`, 31 comprobaciones); kits: 336/7/7/59/45 PASS, 0 FAIL; perfil MO2 igual antes/después | sidecar SHA-256 = integridad, no autoría; sin comparación A/B automática | prueba A/B en Skyrim con acta |
| D22 | Plataforma de pruebas verificadas | `INTEGRACION_WINDOWS_OK` (runner y verificador) | estados `NOT_RUN`/`PASS_STATIC`/`PASS_WINDOWS`/`WAITING_HUMAN`/`PASS_IN_GAME`/`FAIL_IN_GAME`; `Add-SnsHumanEvidence` | evidencia de otro build, perfil cambiado, captura ausente, ZIP cambiado, manifiesto editado, resultado sin evidencia: rechazados | sin dashboard de release (D22.7) | `PASS_IN_GAME` solo por acta humana |
| D35 | Fiabilidad y recuperación | `EN_DESARROLLO` (ciclos 1 y 4: `INTEGRACION_WINDOWS_OK`) | caché compartida por perfil: huella del orden de carga en `meta`, `IndexReplaced` (1010) al leer tras otro proceso; escaneo del perfil sin bloqueos largos de la interfaz (máx. 109 → 24 ms, tramos de 50 y presupuesto de ~20 ms); crash handler y minidump | `CatalogStoreConcurrencyTests`; guarda del arnés `longest UI-thread block < 60 ms`; CTest `app:crash report and minidump (×3)`; arnés `switch-npc-rapid` | sin `DiagnosticBundle`, sin journal de autosave | tiempos de recuperación medidos |
| D36 | Estrategia secuencial | `EN_DESARROLLO` | este documento y `docs/plans/2026-10-17-f0-f7-board.md` | — | tablero local (sin issues: Git/GitHub en espera) | aprobar F0 antes de F1 |

## F1–F7 (no iniciadas como fase; base existente donde se indica)

| D | Fase | Decisión | Estado comprobado | Base existente verificada | Pruebas existentes |
| --- | --- | --- | --- | --- | --- |
| D4 | F1 | Multi-NPC y exportación unificada | `EN_DESARROLLO` (base) | DTO `FaceAssignment`/`OutfitAssignment` en listas; UI y resolución de un NPC a la vez | `FaceResolutionExportTests`, ExportTests |
| D14 | F1 | Producción verificada de mods | `EN_DESARROLLO` (base) | `NpcSafePatch.cs`, `ModPackager.cs`, `FacePatchBuilder.cs`, `OutfitPatchBuilder.cs` | ExportTests (.NET), arnés `easy-mod*`, `face-resolution` |
| D24 | F1 | Compatibilidad NPC + FaceGen + MO2 | `EN_DESARROLLO` (base) | `control_map` (proveedores, `installAbove`, `LostFaceEdits`, resoluciones) | `test_control_map.cpp`, arnés `appearance-control`/`appearance-scan` |
| D18 | F1 | Procesamiento adaptativo | `EN_DESARROLLO` (medición F0-5) | instrumentación por página del escaneo (`listingPages`) y bloqueos máximos medidos; sin `JobDescriptor` ni cola persistente | arnés `appearance-scan` |
| D11 | F2 | Workbench adaptable | `EN_DESARROLLO` (base) | `workbench_state.cpp` (escala, fuentes, paneles) | `sns_workbench_tests` |
| D13 | F2 | Ecosistema de proyectos | `EN_DESARROLLO` (base) | `engines/projects` (`.snps`) | `test_projects_outfit.cpp`, arnés `project-cycle` |
| D16 | F2 | Motor no destructivo | `EN_DESARROLLO` (base) | `appearance_draft`, `edit_history` | `test_appearance_draft.cpp`, arnés `undo-redo*` |
| D26 | F2 | Procedencia e integridad | `EN_DESARROLLO` (base) | VFS de MO2 (proveedores suelto/BSA), FaceGen Integrity Guard | `test_vfs_load_order.cpp`, `test_facegen.cpp`, arnés `hair-safe` |
| D32 | F2 | Colaboración con Git | `BLOQUEADO` | — | — (Git en espera) |
| D33 | F2 | Accesibilidad y localización | `EN_DESARROLLO` (base) | escala de interfaz y fuentes; interfaz en español | `sns_workbench_tests`, arnés `creator-layout` |
| D5 | F3 | Editor High Poly | `EN_DESARROLLO` (protección) | bloqueos de operaciones por cabeza (`face-morphs`, `head-parts`) | arnés `project-blocked-face`; `test_compatibility_guardian.cpp` |
| D7 | F3 | Editor 3D next-gen | `EN_DESARROLLO` (visor) | `scene_viewmodel`, `CharacterViewport.qml` | `test_assets_rendering.cpp`, arnés `viewport-during-build` |
| D8 | F3 | Renderizado híbrido | `EN_DESARROLLO` (base) | `engines/rendering` | `test_assets_rendering.cpp`, `test_framing.cpp` |
| D17 | F3 | Fidelidad visual | `EN_DESARROLLO` (base) | `docs/plans/2026-10-16-render-fidelity-loop.md` | capturas del arnés |
| D28 | F3 | Rendimiento adaptativo | `NO_INICIADO` | — | métricas del arnés solamente |
| D9 | F4 | Animación | `NO_INICIADO` | — | — |
| D10 | F4 | Pipeline con Blender | `EN_DESARROLLO` (base) | `forge_blender_bridge.cpp` | `sns_forge_asset_tests` (3 SKIP: Blender no instalado) |
| D19 | F4 | Creación con IA | `NO_INICIADO` | — | — |
| D29 | F4 | Población procedural | `NO_INICIADO` | — | — |
| D6 | F5 | Plataforma IA | `NO_INICIADO` | sin integración de modelos en el código | — |
| D12 | F5 | Plataforma extensible | `NO_INICIADO` | — | — |
| D15 | F5 | Diagnóstico y recuperación | `EN_DESARROLLO` (base) | `apps/desktop/diagnostics`, `enable-crash-dumps.ps1` | CTest `app:crash report and minidump` |
| D23 | F5 | Orquestación de flujos | `NO_INICIADO` | — | — |
| D27 | F5 | Ejecución autónoma segura | `NO_INICIADO` | — | — |
| D30 | F5 | Orquestación de modelos | `NO_INICIADO` | — | — |
| D20 | F6 | Personalidad, diálogo, voz | `NO_INICIADO` | — | — |
| D21 | F6 | Mundo y misiones | `NO_INICIADO` | — | — |
| D25 | F6 | Puente SKSE de solo lectura | `NO_INICIADO` | — | — |
| D34 | F7 | Centro de publicación | `EN_DESARROLLO` (base) | `release.ps1`, `verify-release.ps1` | no ejecutados (Git) |
| D31 | transversal [A] | Skyrim 1.6.1170 + MO2 | `EN_DESARROLLO` (base) | `engines/mo2` (instancia, perfil, VFS); el manifiesto de pruebas lee `SkyrimSE.exe` = 1.6.1170.0 | `test_instance_profile.cpp`, `test_vfs_load_order.cpp` |
