# Estado de las mejoras investigadas — actualizado 2026-11-05

Checkout: `D:\.Proyectos Games\SkyrimNpcStudio-ui2`, rama `main` @ `66096f5` + cambios locales **sin commit**.
Plan: [`docs/plans/2026-11-04-mejoras-investigadas.md`](../plans/2026-11-04-mejoras-investigadas.md).

| Fase | Estado |
|---|---|
| M1 PyNifly 30.x | `INTEGRACION_WINDOWS_OK` — ver [`PYNIFLY_30_COMPATIBILITY.md`](PYNIFLY_30_COMPATIBILITY.md) |
| M2 Kit de prueba en juego | `EN_DESARROLLO` — motor y worker hechos; falta la integración en la app (abajo) |
| M3–M7 | `NO_INICIADO` |

## M2 — hecho

| Pieza | Archivos | Evidencia |
|---|---|---|
| Motor Qt-free: FormID en tiempo de ejecución (plugins completos `IILLLLLL`, ligeros `FEIIILLL`, ausentes sin índice, cabecera ilegible bloquea, límites 0xFD/0xFFF), lectura del `manifest.json` de exportación (con BOM), kit (`SNS_Test_<plugin>.txt` + `ACTA_SNS_Test_<plugin>.md`), comprobación de que el plugin activo es byte a byte el exportado (SHA-256), nunca `coc` | `engines/mo2/include/sns/mo2/ingame_test_kit.hpp`, `engines/mo2/src/ingame_test_kit.cpp`, `engines/mo2/CMakeLists.txt` (fuente + `nlohmann_json` PRIVATE) | `sns_engine_tests "[ingame-test]"`: 6 casos, 79 aserciones, PASS (exit 0) |
| Pruebas del motor | `tests/unit/engines/test_ingame_test_kit.cpp`, `tests/CMakeLists.txt` | ídem |
| Worker: `export.bodyDistribution` acepta `kind` opcional (`body-distribution` por defecto, `ingame-test-kit`); el manifiesto declara `skyrim-npc-studio/<kind>`; un `kind` desconocido se rechaza antes de escribir | `services/BethesdaWorker/Contracts/BodyDtos.cs`, `services/BethesdaWorker/PatchBuilder/BodyDistributionPackager.cs` | `BodyDistributionPackagerTests`: 39/39 PASS (2 nuevas) |
| Protocolo 1.7 → 1.8 (cambio aditivo) | `services/BethesdaWorker/Contracts/Protocol.cs`, `engines/ipc/include/sns/ipc/protocol.hpp`, `ProtocolContractTests.cs:33-38` (`TheWorkerSpeaksProtocol1_8_…`, espera `8`) | `ProtocolContractTests` 4/4 PASS |

### Verificación del 2026-11-05 (pasos 1–3 cerrados)

La nota anterior de «FAIL abierto» estaba desactualizada: el test ya esperaba `8` y ya se llamaba `…Protocol1_8…`.

SDK .NET: `D:\DevTools\dotnet` (10.0.401, `DOTNET_ROOT`). El `dotnet` de `C:\Program Files\dotnet` no tiene SDK:
anteponer `D:\DevTools\dotnet` al `PATH`. C++: `vcvars64.bat` de VS 2022 BuildTools + `cmake --build build/claude-forge`.

| Comando | Exit | Resultado |
|---|---|---|
| `dotnet test --project tests/unit/BethesdaWorker.Tests/…csproj` | 0 | 98 PASS, 0 FAIL, 0 SKIP |
| `dotnet test --project tests/export/SkyrimNpcStudio.ExportTests/…csproj` | 0 | 177 PASS, 0 FAIL, 0 SKIP |
| `dotnet test --project tests/antigravity/follower-behavior-lab/…csproj` | 0 | 28 PASS, 0 FAIL, 0 SKIP |
| `cmake --build build/claude-forge` (árbol completo, incluye `SkyrimNpcStudio.exe`) | 0 | recompilación posterior: `ninja: no work to do` |
| `build/claude-forge/bin/sns_engine_tests.exe` | 0 | 245 casos, 3253 aserciones, PASS |
| `build/claude-forge/bin/sns_worker_pipe_tests.exe` (handshake real, `kMinor`) | 0 | 1 caso, 19 aserciones, PASS |

Estado de M2: sigue en `EN_DESARROLLO`. Motor y worker `COMPILADO` + `TESTS_UNITARIOS_OK`; falta la integración en la app.

## M2 — falta (en este orden)

1. ~~FAIL de `ProtocolContractTests`~~: cerrado (ver arriba).
2. ~~Suites .NET completas~~: cerrado.
3. ~~Recompilar C++ y ejecutar `sns_engine_tests` y la prueba del worker~~: cerrado.
4. ~~Viewmodel~~: **hecho el 2026-11-05** (`COMPILADO` + `TESTS_UNITARIOS_OK`; sin ejecutar en la app con un perfil real):
   - `apps/desktop/viewmodels/ingame_test_request.{hpp,cpp}`: función pura `plan_ingame_test` (Qt Core + motor, sin worker).
     Acepta la carpeta de la exportación o su `manifest.json` (ruta o `file://`), límite de 8 MiB, `parse_export_manifest`,
     SHA-256 en streaming del plugin activo del perfil, `build_ingame_test_kit`, salida = carpeta padre de la exportación
     pasada por `refused_export_directory`, `modName = <kit>_<yyyyMMdd-HHmmss UTC>` y la petición
     `export.bodyDistribution` (`kind: "ingame-test-kit"`, `createZip: true`, los dos archivos y
     `metadata.inGameVerification = PENDIENTE_SKYRIM`). Si algo falla, no hay petición (`params` nulo) y los errores
     del motor se muestran tal cual.
   - `StudioController::prepareInGameTest(QString)` (`Q_INVOKABLE`), propiedad `inGameTest` y señal
     `inGameTestChanged` (`state`: `preparing` → `exporting` → `done` | `error`; `batchCommand`, `zipPath`, `modDirectory`,
     `lines`, `files`, `verification`, `status = PAQUETE_MO2_VERIFICADO`, `inGame = PENDIENTE_SKYRIM`). El plan se calcula
     en `QThreadPool` y un número de serie descarta resultados viejos. Todo queda registrado en la consola (`ingame-test`).
   - Prueba: `tests/app/test_ingame_test_request.cpp` → target `sns_ingame_test_request_tests` (5 casos, 45 aserciones:
     caso válido con `player.placeatme FE000800 1`, `manifest.json` directo, SHA distinto, sin manifiesto/ajeno/demasiado
     grande, exportación dentro de una raíz prohibida).
   - Evidencia: `cmake --build build/claude-forge` exit 0, sin avisos; `ctest -R "^app:"` exit 0: 64 PASS, 10 SKIP
     (Blender no configurado), 0 FAIL; `sns_engine_tests` 245 casos, PASS. Logs en
     `out/local-implementation/m2-ingame-kit-20261104/logs-20261105/`.
   - Sin probar todavía: la llamada real al worker desde la app (se cubre en el paso 6).
5. **QML** (no escrito): botón «Preparar prueba en Skyrim» por exportación en `BottomPanel.qml`; acción en el menú
   `&Archivo` de `Main.qml` con `FolderDialog` para exportaciones de sesiones anteriores, incluidas las de Character Forge;
   un diálogo de resultado con la línea `bat SNS_Test_…`, la ruta del ZIP y «Abrir carpeta». Registrar cualquier QML nuevo
   en `apps/desktop/CMakeLists.txt`.
6. Prueba de integración en Windows con un perfil real (lectura): exportación ya instalada en un **perfil de prueba**
   → kit generado → hashes. Instalarlo en MO2 y probarlo en el juego le corresponde al dueño (`PENDIENTE_SKYRIM`).
7. Sustituir el texto manual `help … 4` / `player.placeatme` de `ModPackager.cs:416-419` y `forge_easy_mode.cpp:132` por una
   referencia a la nueva acción.
8. Documentación de usuario: sección en `docs/EASY_MOD_CREATOR.md` y en `docs/SKYRIM_ACCEPTANCE_TEST.md`.

Inferencia aún no observada en el juego: el formato `FE` + índice ligero de 3 dígitos + id local de 3 dígitos procede
de la comunidad. La primera prueba real en Skyrim lo confirma o lo corrige.

## Diseño de agentes para los pasos 4–8 (skill `agent-designer`, en local)

`agent_planner.py` con los requisitos de M2 (checkout único, `concurrent_tasks: 1`, seguridad alta, 3 roles) propone el
patrón **supervisor** con 3 agentes. Entrada y salida en `out/local-implementation/m2-ingame-kit-20261104/agent-design/`.
La duración que estima el script (8–12 semanas) es una plantilla genérica y no se tiene en cuenta.

| Rol del planner | Asignación concreta | Escribe en |
|---|---|---|
| `supervisor_agent` | La sesión principal: orden de pasos, gates y evidencia | `docs/qa/` (estado) |
| `development_specialist` | Implementación del paso 4 (viewmodel) y después del 5 (QML), uno tras otro | `apps/desktop/viewmodels/studio_controller.*`, `apps/desktop/qml/**`, `apps/desktop/CMakeLists.txt` |
| `file_specialist` | Pasos 7–8 (textos de `ModPackager.cs`, `forge_easy_mode.cpp`, guías) y, en solo lectura, compilar, probar y revisar el diff | sus propios archivos; nunca los del especialista de desarrollo |

Reglas (AGENTS.md): un solo escritor por archivo y por caché de build (`build/claude-forge`), sin worktrees ni sesiones en la
nube; el resultado de un subagente no cuenta como evidencia hasta repetir build y pruebas. El paso 6 (perfil MO2 de prueba)
y la prueba en Skyrim requieren al dueño.
No se generaron esquemas de herramientas (`tool_schema_generator.py`): los agentes usan las herramientas de Claude Code y
no herramientas propias. `agent_evaluator.py` queda pendiente: todavía no hay logs de ejecución que evaluar.

## Después de M2

- **M3** plugin MO2 (`installMod`): requiere F0 y tu aprobación para tocar un perfil real.
- **M4** SkyPatcher: bloqueado hasta tener el readme oficial.
- **M5** snapshots de ESP, **M6** BodySlide como oráculo y **M7** MorphTarget (con medición previa): sin empezar.

## Para el dueño

- Nada está commiteado. Los cambios de M1/M2 conviven con el trabajo local previo de otras sesiones en `main`.
- Archivos tocados en M2 (para revisar o revertir): los 4 nuevos de `engines/mo2` y `tests/unit/engines`, más
  `engines/mo2/CMakeLists.txt`, `tests/CMakeLists.txt`, `BodyDtos.cs`, `BodyDistributionPackager.cs`,
  `BodyDistributionPackagerTests.cs`, `Protocol.cs` y `protocol.hpp`.
- Evidencia: `out/local-implementation/m1-pynifly30-20261104/` (M1) y `out/local-implementation/m2-ingame-kit-20261104/` (artefactos .NET de M2).
