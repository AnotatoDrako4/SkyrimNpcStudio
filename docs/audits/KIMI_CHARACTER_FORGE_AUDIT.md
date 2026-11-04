# Auditoría técnica independiente — Skyrim NPC Studio / Character Forge

- **Auditor:** Kimi (agente), a petición del propietario (misión «Auditoría completa de Skyrim NPC Studio y Character Forge», 2026-11-03).
- **Repositorio:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2` · rama `main` · SHA `f5b87b0599aa4e3c6fa0397cb40e884d586efd7c` · `origin/main` sincronizado (0 commits por delante / por detrás).
- **Método:** inspección directa del código, ejecución real de builds y suites, verificación de herramientas instaladas. Nada se afirma por documentación sola; cada resultado de pruebas citado fue ejecutado por el auditor salvo que se indique «declarado por otro agente».
- **Alcance y permisos:** solo lectura en Git (sin pull/reset/commit/merge); escritura limitada a `out/kimi-audit/` (evidencia) y este informe. No se tocó MO2, `Skyrim/Data`, perfiles ni código de otros agentes. Coordinación multiagente respetada según `.ai-coordination/claims/ownership.md`.

## I. Resumen ejecutivo

El proyecto es **real, maduro en su núcleo y con una disciplina de verificación inusualmente alta** (read-back binario en cada exportación, clasificación determinista de hallazgos, staging atómico, guardias de integridad FaceGen). La arquitectura documentada (app Qt 6.11 → viewmodels → motores C++ sin Qt → worker .NET 10/Mutagen por JSON-RPC sobre named pipe) coincide con el código.

**Character Forge ya tiene un primer pipeline Blender → Skyrim funcional y verificado**: importación GLB/glTF validada, conversión FBX/OBJ/BLEND → GLB con Blender real, diagnóstico de compatibilidad determinista, exportación NIF vía PyNifly 29.1 con relectura independiente (nifly propio), y empaquetado de un mod MO2 con ESP-ESL + un registro STAT releído con Mutagen. Lo verifiqué ejecutando las suites con Blender 5.2.3 y PyNifly reales: 17/18 casos PASS (143 aserciones), y .NET ExportTests 175/175.

**Los tres problemas estructurales más serios no son de código sino de proceso:**

1. **La suite unitaria .NET está roja en `main`** y nadie lo había notado, porque los agentes recientes solo ejecutaron subconjuntos (el agente anterior omitió `BethesdaWorker.Tests` por «obj/ compartido»). `AppearanceTests.UnresolvedAndTemplatedAppearancesFailExplicitly` falla de forma reproducible: el commit HEAD `f5b87b0` añadió resolución de cadenas de templates (`AppearanceQueries.cs:16-26`) y el test sigue esperando que lance excepción. Test obsoleto o regresión de comportamiento: hay que decidirlo, no ignorarlo.
2. **84 archivos modificados o nuevos sin commit en `main`**, producidos por al menos tres agentes (Character Forge→Skyrim, BodySlide, Outfit Fit Doctor, visual-qa). Política «git deshabilitado por el propietario» + trabajo concurrente = riesgo alto de pérdida, colisión y de que «verde» signifique cosas distintas para cada agente.
3. **El valor central del producto sigue bloqueado**: la exportación final de NPC está vedada hasta certificar FaceGen nativo (bloqueo declarado en código: `NativeNpcBuilder.cs:26`). Todo lo demás — incluido Character Forge — es preparación para ese momento.

Estado global por fases AGENTS.md: núcleo F0 `INTEGRACION_WINDOWS_OK` (con la salvedad del test rojo); Character Forge Tipo A `PAQUETE_MO2_VERIFICADO` (declarado por el agente anterior y consistente con mis ejecuciones); **nada está `VERIFICADO_EN_SKYRIM`** — ningún mod producido ha sido cargado en el juego con observación humana.

## II. Inventario de funcionalidades

Leyenda: IMPLEMENTADO (verificado por mis ejecuciones) · PARCIAL · EXPERIMENTAL · NO IMPLEMENTADO · ROTO · NO VERIFICADO. «Declarado» = afirmado por otro agente con evidencia archivada que no re-ejecuté.

### A. Arquitectura general

| Sistema | Estado | Evidencia |
|---|---|---|
| Ejecutable `SkyrimNpcStudio.exe` (Qt 6.11.2, MSVC 14.44) | IMPLEMENTADO | `build/claude-forge/bin` compilado hoy 19:51 del árbol actual; binarios de test corren |
| 14 motores C++ sin Qt (`engines/*`) | IMPLEMENTADO | `sns_engine_tests`: **219 casos / 2.801 aserciones PASS** (mi ejecución, 11 s) |
| Worker .NET 10 + Mutagen 0.54.4 | IMPLEMENTADO | ExportTests **175/175 PASS** en build fresco (mi ejecución) |
| JSON-RPC named pipe, handshake, cancelación | IMPLEMENTADO | `sns_worker_pipe_tests` PASS (19 aserciones) con `DOTNET_ROOT` correcto |
| Índice SQLite incremental por perfil | IMPLEMENTADO (rendimiento declarado: 7,9 s completo / 1,4 s incremental con 415 plugins; NO REMEDIDO por mí) | `docs/ARCHITECTURE.md`; tests de integración pasan |
| Persistencia proyectos `.snps` / `.sncf` | IMPLEMENTADO | tests `sns_forge_asset_tests`, `sns_engine_tests [forge]` |
| Modelo de comandos undo/redo | IMPLEMENTADO | `engines/projects`, tests |
| Manejo de errores tipado (1001–1099) | IMPLEMENTADO | contratos `protocol.hpp` ↔ `Contracts/*.cs` |

### B. Character Forge (prioridad)

| Capacidad | Estado | Evidencia |
|---|---|---|
| Proyecto `.sncf`, biblioteca de modelos con SHA-256, deduplicación | IMPLEMENTADO | `sns_forge_asset_tests`, `engines/creation` |
| Visor 3D GLB/glTF (RuntimeLoader) con picking, caja 3D, ocultación jerárquica | IMPLEMENTADO (declarado + tests de arnés `--forge-preview` ejecutados por el agente anterior; mi ejecución de las suites asociadas PASS) | `ForgeViewport.qml`, `forge_preview_check.cpp` |
| Validador glTF propio (índices, rangos, rutas confinadas, límites) | IMPLEMENTADO | `engines/gltf`, 8 casos/112 aserciones en `sns_engine_tests` |
| Puente Blender FBX/OBJ/BLEND → GLB (`forge_bridge.py`, QProcess, timeout, cancelación) | IMPLEMENTADO | **mi ejecución con Blender 5.2.3 real**: casos de puente PASS en `sns_forge_asset_tests` |
| Diagnóstico de compatibilidad Skyrim (`skyrim_conversion`, 5 severidades) | IMPLEMENTADO | `sns_engine_tests [skyrim]` 25 casos PASS |
| Exportación NIF estático (Tipo A) vía PyNifly + relectura nifly + DDS BC1/BC3 | IMPLEMENTADO | **mi ejecución extremo a extremo PASS** (`test_forge_skyrim.cpp`, fixture `skyrim_prop.blend` generado hoy) |
| Empaquetado mod MO2 Tipo A (ESP-ESL + STAT + ZIP, `export.forgeStaticResource`) | IMPLEMENTADO | ExportTests 175/175 incluye 21 tests `ForgeStaticResourceExportTests` (mi ejecución); ventana real declarada 39/0 PASS |
| Importación de modelo del usuario real (98 MB, rig no-Skyrim) | IMPLEMENTADO (diagnóstico correcto: 5 bloqueantes detectados, exportación correctamente denegada) | declarado por agente anterior con artefactos |
| Creación de NPC nativo (registros NPC_/OTFT/ACHR, FaceMorph, tintas) | PARCIAL — exportación técnica sí; **exportación final BLOQUEADA por FaceGen** | `NativeNpcBuilder.cs` (read-back exhaustivo); `ForgeExportWizard.qml` |
| Comportamiento de seguidor (arquetipos, clases, voces del perfil) | IMPLEMENTADO | `sns_forge_behavior_tests` 4/4 PASS |
| Generación con IA (Meshy/Tripo/Rodin) | NO IMPLEMENTADO (UI honesta: «adaptador no configurado», botón deshabilitado) | `AIGenerationPanel.qml:105-109` |
| Editor de esqueleto / adaptación de rig | NO IMPLEMENTADO («Adaptar rig · motor pendiente», deshabilitado) | `SkeletonEditor.qml:44-48` |
| Visualización de huesos en el visor | NO IMPLEMENTADO (RuntimeLoader 6.11 no expone articulaciones; deshabilitado con motivo) | `CHARACTER_FORGE_IMPLEMENTATION_LOG.md` |
| Editor de materiales | PARCIAL (inspector existe; conversión PBR→Skyrim incompleta: BC5/BC7 no soportados, roughness/metallic se descartan con aviso) | `MaterialsEditor.qml`, plan §5 |
| Pipeline equipado (armadura con skinning, slots, ARMO/ARMA) | NO IMPLEMENTADO (incremento 3 `NO_INICIADO`) | `docs/plans/CHARACTER_FORGE_BLENDER_TO_SKYRIM.md` |
| Pipeline NPC humanoide completo (FaceGen desde Blender) | BLOQUEADO (depende de FaceGen nativo; incremento 4) | ídem |
| Criaturas | NO IMPLEMENTADO | — |
| Colisión `bhkCollisionObject` en NIF exportados | NO IMPLEMENTADO | plan §5, §10 |
| MCP Blender (`blender_mcp`) | NO IMPLEMENTADO (decisión deliberada y correcta: socket de ejecución Python innecesario) | plan §4 |

### C. Editor de NPC / Outfit Studio / reutilizables

| Sistema | Estado | Evidencia |
|---|---|---|
| Catálogo NPC/armaduras con resolución MO2 (VFS + BSA) | IMPLEMENTADO | suites de integración; `ArmorCatalog.qml` |
| Visor de personaje con poses, SMP diagnóstico | IMPLEMENTADO | `OUTFIT_STUDIO_STATUS.md` + `sns_engine_tests` |
| Outfit Studio: borrador, validación, exportación OTFT atómica | IMPLEMENTADO | ExportTests (OutfitPatchBuilder) |
| BodySlide: presets, morphs, build `_0/_1` + TRI (`bodyslide_build/morphs`) | IMPLEMENTADO (sin commit; tests 9/9 SERANA + bodyslide PASS en mi ejecución de `sns_engine_tests`) | `engines/body/*`, `test_bodyslide_*.cpp` |
| Outfit Fit Doctor (familias CBBE/3BA/UBE/BHUNP) | IMPLEMENTADO (sin commit; 9/9 declarado, 257 aserciones; suite completa pasa en mi ejecución) | `engines/outfit/outfit_fit_doctor.*` |
| Conversión de atuendo entre familias (`export.convertedOutfit`) | IMPLEMENTADO (sin commit; 8 [Fact] en `ConvertedOutfitExportTests.cs`, dentro de los 175 PASS) | `ConvertedOutfitBuilder.cs` |
| FaceGen: Safe Patch (HCLF), guard de integridad, diagnóstico | IMPLEMENTADO (solo campos seguros; regeneración completa NO certificada) | `FacePatchBuilder.cs`, `engines/face` |
| Face Source Studio (índice suelto+BSA, plan de transferencia) | IMPLEMENTADO (declarado por Claude, INTEGRACION_WINDOWS_OK) | `.ai-coordination/status/claude.json` |
| Resolución de apariencia con templates de NPC | IMPLEMENTADO — **pero rompe un test heredado** (ver defecto D-01) | `AppearanceQueries.cs:16-26`, commit `f5b87b0` |

## III. Defectos críticos (encontrados y reproducidos por esta auditoría)

### D-01 · P1 — Suite .NET unitaria roja en `main`: test obsoleto tras `f5b87b0`

- **Archivo/función:** `tests/unit/BethesdaWorker.Tests/AppearanceTests.cs:37` (`UnresolvedAndTemplatedAppearancesFailExplicitly`) vs `services/BethesdaWorker/NpcCatalog/AppearanceQueries.cs:16-26`.
- **Causa demostrada:** el commit HEAD (`f5b87b0`, «unify traits template appearance resolution») añadió resolución de cadenas de templates Traits; el test sigue exigiendo `WorkerException` para un NPC con template (`TemplatedGuard`). El binario actual **no lanza** (resuelve), `Assert.Throws` falla: 91 totales, 1 FAIL, reproducible 2/2 ejecuciones mías.
- **Reproducir:** compilar `BethesdaWorker.Tests` y ejecutar `SkyrimNpcStudio.BethesdaWorker.Tests.exe`.
- **Impacto:** la suite de regresión del worker está roja; cualquier regresión futura real quedará enmascarada por el fallo crónico. Indica que ningún agente corre la suite .NET completa.
- **Corrección recomendada:** decidir la semántica correcta (resolución de templates = comportamiento deseado) y reescribir el test: templated resoluble → OK; templated **irresoluble** → `WorkerException`. Añadir gate que ejecute ambas suites .NET completas.
- **Prueba de aceptación:** `BethesdaWorker.Tests` 100 % PASS incluyendo un caso de template circular (el código ya corta con `visited`, sin test que lo cubra).

### D-02 · P1 — 84 archivos sin commit en `main` de ≥3 agentes concurrentes

- **Causa demostrada:** `git status --short` (84 entradas), política `gitUsage: DISABLED_PER_OWNER_POLICY` en los status de coordinación.
- **Impacto:** pérdida de trabajo ante cualquier error humano; imposibilidad de bisectar; los estados «verde» de cada agente se refieren a árboles ligeramente distintos; los CMakeLists ya reciben «altas aditivas» de varios agentes (patrón frágil).
- **Corrección recomendada:** consolidación autorizada por el propietario en commits por ámbito (forge-skyrim, bodyslide, outfit-fit-doctor, visual-qa), previa ejecución de la regresión completa. Sin esto, cada incremento nuevo hereda riesgo desconocido.
- **Aceptación:** `git status` limpio salvo trabajo activo; cada commit con su suite verde registrada.

### D-03 · P1 — Exportación final de NPC bloqueada por FaceGen nativo no certificado

- **Archivo:** `NativeNpcBuilder.cs:26` («final export requires verified native FaceGen»), `ForgeExportWizard.qml` (botón final deshabilitado).
- **Estado:** bloqueo deliberado y correcto (no un bug), pero es **el** cuello de botella del producto: sin FaceGen no hay NPC nuevos jugables, y Character Forge incremento 4 queda en `BLOQUEADO`.
- **Corrección:** plan de certificación FaceGen (ver §V): generación nativa completa + guard + readback + verificación humana en juego sobre un perfil aislado.

### D-04 · P2 — Toolchain .NET frágil fuera del camino exacto de los agentes previos

Cuatro fricciones demostradas en mi entorno (misma máquina):
1. `dotnet test` rechazado: `global.json` exige runner Microsoft.Testing.Platform pero los proyectos xunit.v3 se detectan como VSTest en SDK 10.0.401 → ninguna invocación `dotnet test` funciona. Workaround mío: compilar y ejecutar los exe xunit.v3 directamente. (El agente anterior logró correrlas; su método exacto no está documentado — los logs muestran salida estilo VSTest.)
2. `dotnet restore` masivo (slnx) falla con `Value cannot be null (Parameter 'path1')` sobre grafos `obj/` obsoletos; la restauración proyecto a proyecto sí funciona.
3. El apphost del worker exige runtime .NET 10: `C:\Program Files\dotnet` solo tiene 8.0.28 → `sns_worker_pipe_tests` y `sns_integration_tests` **fallan si `DOTNET_ROOT` no apunta a `D:\DevTools\dotnet`**. El paquete `dist` va autocontenido, así que afecta solo a desarrollo.
4. `scripts/dev-env.ps1` depende de `${env:ProgramFiles(x86)}`; falla en shells saneados (el mío no la tenía).
- **Corrección:** fijar el runner en `global.json` (o migrar a MTP), script de restore robusto, documentar `DOTNET_ROOT`, endurecer `dev-env.ps1` con fallback por `vswhere`.
- **Aceptación:** desde un shell limpio, `dev-env.ps1` + build + `dotnet test` completos sin workarounds.

### D-05 · P2 — NIF Tipo A sin colisión y mod sin verificación en juego

- `bhkCollisionObject` ausente (plan §5): un STAT colocado se atraviesa. Estado `PENDIENTE_SKYRIM` honesto pero pendiente: ningún artefacto del estudio ha sido cargado en Skyrim por un humano.
- **Aceptación:** kit MO2 aislado + `player.placeatme` + capturas + hashes → `VERIFICADO_EN_SKYRIM`.

### D-06 · P3 — Fallo intermitente no reproducido en `[skyrim],[forge],[gltf]`

- **Evidencia:** una ejecución de `sns_engine_tests` falló con 2 casos (`"autoFixable" == "warning"`, 267 aserciones vs 270 habituales); 26 ejecuciones posteriores (incl. ráfaga de 20) 100 % PASS. Hipótesis: interferencia del `ctest -j4` que había matado segundos antes (fixtures regenerándose concurrentemente, patrón ya documentado en plan §8 con las copias de fixtures). **No reproducido; sin causa raíz confirmada.**
- **Corrección:** si reaparece, aislar qué test escribe el fixture `goblet_report.json` y serializarlo; registrar el seed de Catch2 en CI.

### D-07 · P3 — 44 avisos QML en `OutfitPanel.qml:613/624`

- **Evidencia:** reportado por `status/claude.json` (undefined → QString tras cambio de otro agente). No re-ejecutado por mí; se lista porque el emisor es identificable y el archivo sigue modificado sin commit.
- **Corrección:** tipar los bindings afectados; gate de smoke test con «0 avisos QML».

### D-08 · P3 — Higiene del repositorio

- 12 worktrees registrados, 4 `prunable`; uno `locked`; ramas locales abandonadas (`local/character-forge-review-20261013`, etc.). Riesgo de confusión sobre «cuál es la verdad».
- **Corrección:** poda autorizada (`git worktree prune` manual por el propietario, nunca por un agente).

### D-09 · P2 — Pipeline de materiales limitado

- PNG→DDS solo BC1/BC3; sin BC5 (normales) ni BC7 (alfa de calidad); roughness/metallic se descartan (`pbrChannelDropped`); rutas de textura en minúsculas (PyNifly) vs mayúsculas en mallas — Windows/Skyrim toleran, un futuro BSA no.
- **Corrección:** mapeo PBR→BSLightingShader (normal map BC5/BC7, especular, emisivo) con readback; normalización de mayúsculas en el empaquetado.

### D-10 · P2 — Rendimiento mayormente NO MEDIDO

- Arranque, RAM/VRAM del visor, conversión BLEND→GLB de modelos grandes, indexación MO2: solo existen dos cifras documentadas (7,9 s / 1,4 s, 415 plugins) y el dato de que un `.blend` de 98 MB se analizó correctamente. No hay telemetría de la app ni benchmarks repetibles. `sns_nif_load_probe` existe (tiempo + pico de memoria por NIF) pero sin serie de mediciones archivada.

## IV. Arquitectura recomendada

La arquitectura actual es sólida y no necesita reestructuración. Recomendaciones incrementales:

1. **Mantener la regla «Blender recoge hechos, C++ decide»** (plan §2). Es la decisión correcta: veredictos deterministas y testeables fuera del proceso de Blender. Extenderla al perfil equipado: `forge_skyrim.py` recoge pesos/huesos/particiones; `skyrim_conversion` clasifica; el worker escribe registros.
2. **No crear exportador NIF propio.** La combinación PyNifly (export) + nifly propio (readback) + Mutagen (registros) cubre la cadena con tres verificaciones independientes. Un exportador propio duplicaría años de edge-cases (particiones, skinning, BSSkin).
3. **Extraer el conocimiento de familias de cuerpo a un único motor:** hoy `outfit_fit_doctor` (Cbbe/Cbbe3ba/Bhunp/Unp/Ube) vive en `engines/outfit` y el pipeline Forge en `engines/creation`. El incremento 3 necesitará ambos: exponer `MeshFamily` y perfiles CBBE/3BA/UBE como API compartida en lugar de duplicar heurísticas.
4. **Esqueleto:** `sns::nif` ya lee jerarquías; construir la visualización/adaptación XPMSSE sobre `engines/skeleton` (hoy vacío, 0 archivos) en lugar de depender de RuntimeLoader (que no expone joints en Qt 6.11).
5. **Worker:** no sustituir (contrato con el propietario) y no ampliar su superficie sin versionado de protocolo. Los métodos nuevos (`export.forgeStaticResource`, `export.convertedOutfit`) se añadieron sin subir versión menor — aceptable mientras app y worker se distribuyan juntos, pero conviene registrarlo en `Protocol.cs` como política escrita.

## V. Plan de mejora de Character Forge (incrementos pequeños y comprobables)

| # | Incremento | Entregable comprobable | Estado inicial |
|---|---|---|---|
| 0 | Cerrar deuda roja: resolver D-01 (test templates) + script de regresión completa única (C++ + ambas .NET) que registre exit codes | `regresion_completa.ps1` con log; ambas suites verdes | Propuesta de esta auditoría |
| 1 | Verificación en juego del mod Tipo A (perfil MO2 aislado autorizado) | capturas + hashes + acta → `VERIFICADO_EN_SKYRIM` | Requiere humano |
| 2 | Colisión estática: `bhkCollisionObject` (hkxcmd de PyNifly o nifly) en `forge_skyrim.py` + check `hasCollision` en `verify_nif` | test con fixture sintético + readback | Pequeño |
| 3 | Registros MISC/ACTI además de STAT en `StaticResourceBuilder` | tests .NET + readback | Pequeño |
| 4 | Perfil equipado (incremento 3 del plan): esqueleto referencia → transferencia de pesos → particiones BSDismember → slots → ARMO/ARMA vía worker | pipeline extremo a extremo con fixture sintético + kit en juego | Grande; subdividir |
| 5 | Materiales: normal maps BC5/BC7, mapeo PBR→BLS, normalización de rutas | tests de `verify_nif` ampliados | Medio |
| 6 | FaceGen nativo certificado (desbloquea NPC final e incremento 4) | generación + guard + readback + verificación humana | El más valioso y el más arriesgado |
| 7 | Visualización de esqueleto en visor (`engines/skeleton`) | render de joints sobre NIF real | Medio |
| 8 | TRI/morphs desde Blender (reutilizando `bodyslide_morphs`/`bodyslide_build`) | morph visible en RaceMenu (humano) | Medio |
| 9 | Panel Blender «Character Forge Tools` (comunicación de vuelta: informe dentro de Blender) | add-on propio leyendo `report.json` | Opcional |

## VI. Integración de REA

REA está disponible en este entorno (skill de ingeniería inversa). Evaluación honesta: **su valor para este proyecto es bajo hoy**, porque las cinco herramientas objetivo son de código abierto o ya integradas:

| Herramienta | Vía correcta de estudio | Qué aportaría | Licencia |
|---|---|---|---|
| PyNifly 29.1 | Código fuente (add-on Python instalado) + operadores documentados | Ya consumida: `export_scene.pynifly`, readback | Sin archivo de licencia en la instalación local — **verificar antes de redistribuir nada**; el estudio solo invoca la instalación del usuario (correcto) |
| nifly | Fuente (FetchContent `build/debug/_deps/nifly-src`) | Ya integrada (lectura) | MIT |
| Outfit Studio / BodySlide | Fuente (GPL): `bodyslide_build/morphs` ya replica `BodySlideApp::BuildBodies`/`WriteMorphTRI` | Ya integrada la parte útil (build de cuerpos, TRI) | GPL — compatible con el proyecto (GPL-3.0) |
| NifSkope | Fuente (para formatos oscuros: particiones, BSSkin) | Referencia de formato al implementar incremento 4 | OSS |
| Morphbench | Solo si se necesita comparar morphs TRI a bajo nivel sin spec | Inspección TRI puntual | Comprobar |

Reglas: no usar REA sobre ejecutables propietarios con protección; no copiar código ni recursos; preferir siempre fuente/documentación. **Conclusión: cero integración REA justificada ahora; re-evaluar solo si aparece un formato cerrado sin spec (p. ej. HKX de colisiones complejas).**

## VII. Primer pipeline Blender → Skyrim: estado real

Cadena: Importar → Diagnosticar → Preparar → Adaptar → Exportar → Verificar → Empaquetar → Juego.

| Etapa | Estado | Verificación |
|---|---|---|
| Importar (GLB directo; FBX/OBJ/BLEND vía Blender) | IMPLEMENTADO | mis ejecuciones con Blender real |
| Diagnosticar (hechos Blender → veredictos C++) | IMPLEMENTADO | 25 casos `[skyrim]` PASS |
| Preparar (transforms seguras, staging de texturas, DDS) | IMPLEMENTADO | extremo a extremo PASS |
| Adaptar (rig/materiales Skyrim) | NO IMPLEMENTADO (bloquea skinned con motivo claro) | `skinnedStatic` bloqueante |
| Exportar NIF (PyNifly) + verificar (nifly + PyNifly cruzados) | IMPLEMENTADO | PASS; NIF reproducible byte a byte (declarado) |
| Empaquetar mod (ESP-ESL + STAT + ZIP atómico) | IMPLEMENTADO | 21 tests .NET PASS (mis ejecuciones) |
| Cargar en Skyrim | NO VERIFICADO | requiere humano |
| Pipelines equipado / NPC / criatura | NO IMPLEMENTADO | incrementos 3–4 del plan |

## VIII. Rendimiento

- **Medido por mí:** suites C++ completas en 11 s; ExportTests .NET en 13,5 s; análisis Blender de fixture sintético en ~10 s; suite completa `ctest` **no cabe en 300 s** (timeout alcanzado; causa probable: pruebas de ventana/estrés; NO MEDIDO qué la domina).
- **Documentado (no remedido):** indexación 415 plugins 7,9 s / 1,4 s incremental; `.blend` de 98 MB analizado sin bloqueo.
- **NO MEDIDO:** arranque de la app, RAM/VRAM del viewport, conversión de modelos >100 MB, regeneración FaceGen, caché de retratos.
- Mejora prioritaria: instrumentar `sns_nif_load_probe` y el arranque en una serie archivada por build (el harness ya existe).

## IX. Riesgos técnicos

1. **FaceGen:** el riesgo mayor del producto; generación nativa incorrecta = NPCs con cara rota en juego. Mitigación actual correcta (bloqueo total hasta certificación).
2. **Licencias:** PyNifly sin archivo de licencia local (verificar antes de cualquier redistribución); nifly/DirectXTex MIT (OK); Mutagen/StreamJsonRpc GPL/compatibles; código derivado de BodySlide bajo GPL (proyecto GPL-3.0, compatible).
3. **Versiones acopladas:** PyNifly 29.1 + Blender 5.2.x fijados por contrato (`PYNIFLY_TESTED_VERSIONS`); una actualización de Blender del usuario puede romper el pipeline — el diseño ya lo degrada a «pyniflyUntested» (bien).
4. **Pérdida de topología:** cualquier decimación/arreglo automático invalida TRI existentes; el clasificador ya lo detecta (`shapeTooBig` autoFixable solo sin morphs/rig/padre). Mantener esa regla.
5. **Rigging:** transferencia de pesos automática (incremento 3) es donde los pipelines de este tipo fallan en producción; exigir fixture sintético + prenda real de referencia + verificación en juego antes de habilitarlo para usuarios.
6. **Concurrencia multiagente:** sin locks de archivos efectivos (registro de claims es honorífico) y 84 archivos calientes; dos agentes editando `CMakeLists.txt` a la vez ya ocurrió (patrón «altas aditivas»).
7. **Regresión silenciosa:** suite roja crónica (D-01) + ausencia de gate = las regresiones futuras se acumulan sin señal.

## X. Resultados de pruebas (todo ejecutado por el auditor el 2026-11-03)

| Comando | Resultado |
|---|---|
| `sns_engine_tests.exe` (completo) | **PASS** — 219 casos, 2.801 aserciones, 11 s |
| `sns_engine_tests.exe "[skyrim],[forge],[gltf]"` ×27 | PASS 26/27; 1 fallo intermitente no reproducido (D-06) |
| `sns_forge_asset_tests.exe` con `SNS_LOCAL_BLENDER`=Blender 5.2.3 real + fixtures frescos | **17 PASS / 1 SKIP** (143 aserciones) — incluye BLEND/FBX/OBJ→GLB y BLEND→NIF(PyNifly)→verificación |
| `sns_forge_behavior_tests`, `sns_export_destination_tests`, `sns_portrait_cache_tests` (1.218 aserciones), `sns_settings_tests` (743), `sns_workbench_tests` (195) | PASS |
| `sns_integration_tests.exe` | 4 PASS / 6 SKIP (requieren MO2 local) |
| `sns_worker_pipe_tests.exe` | FAIL sin `DOTNET_ROOT` (solo .NET 8 global) → **PASS** (19 aserciones) con `DOTNET_ROOT=D:\DevTools\dotnet` |
| `.NET ExportTests` (build fresco, exe xunit.v3 directo) | **175/175 PASS** (incluye 21 `ForgeStaticResourceExportTests`) |
| `.NET BethesdaWorker.Tests` (build fresco) | **90/91 — 1 FAIL**: `UnresolvedAndTemplatedAppearancesFailExplicitly` (D-01, reproducible) |
| `ctest` completo (`build/claude-forge`, 374 tests) | **NO COMPLETADO** — supera 300 s en este entorno; las suites se ejecutaron por binarios |
| `dotnet test --solution` | BLOQUEADO por `global.json` MTP vs VSTest (D-04); suites ejecutadas vía exe |
| Fixtures `make_test_models.py` (`bridge_sources`, `skyrim_prop`) | Generados con Blender 5.2.3 (5 archivos) |
| Arranque de la app / UI smoke | NO EJECUTADO por el auditor (cubierto por arnés del agente anterior: run6-mod 39/0 declarado) |

Entorno verificado: MSVC 14.44.35207 · Qt 6.11.2 (`D:\DevTools\Qt`) · .NET SDK 10.0.401 (`D:\DevTools\dotnet`; global solo runtime 8.0.28) · Blender 5.2.3 LTS · PyNifly 29.1.0 (add-on usuario) · cmake 4.3.2 · Ninja · build auditado: `build/claude-forge` (19:51 hoy, sin fuentes más recientes).

---

# Entregables solicitados

## 1. Los diez problemas más importantes (por impacto)

1. FaceGen nativo no certificado → sin NPC nuevos jugables; Character Forge toca techo (D-03).
2. Suite .NET unitaria roja en `main` sin que nadie lo detectara (D-01).
3. 84 archivos sin commit de ≥3 agentes: riesgo de pérdida y de «verdes» incomparables (D-02).
4. Pipeline Blender→Skyrim limitado a estáticos: equipado/NPC/criatura sin iniciar (§VII).
5. Nada verificado dentro de Skyrim: todos los artefactos están `PENDIENTE_SKYRIM` (D-05).
6. NIF sin colisión `bhkCollisionObject` (D-05).
7. Toolchain .NET/Qt frágil fuera del camino feliz (D-04).
8. Materiales: sin normal maps BC5/BC7 ni mapeo PBR completo (D-09).
9. Rigging sin tooling: ni visualización de huesos ni adaptación XPMSSE (§IV.4).
10. Rendimiento sin serie de mediciones; suite completa no ejecutable en una llamada estándar (D-10).

## 2. Las diez mejoras a implementar primero

1. Resolver D-01 y crear el gate de regresión completa única (incremento 0).
2. Verificación humana en juego del mod Tipo A (incremento 1) — convierte claims en hechos.
3. Colisión estática en el exportador (incremento 2).
4. MISC/ACTI en `StaticResourceBuilder` (incremento 3).
5. Consolidación git autorizada por ámbitos (resuelve D-02).
6. Endurecer toolchain: runner de tests, restore, `DOTNET_ROOT`, `dev-env.ps1` (D-04).
7. Perfil equipado: esqueleto referencia + pesos + particiones + ARMO/ARMA (incremento 4, subdividido).
8. Materiales avanzados BC5/BC7 + normalización de rutas (incremento 5).
9. Certificación FaceGen nativo (incremento 6) — el desbloqueo estratégico.
10. Telemetría de rendimiento archivada por build (arranque, NIF, indexación, viewport).

## 3. Componentes existentes a reutilizar (no duplicar)

`ForgeBlenderBridge` (proceso Blender con timeout/cancelación) · `engines/gltf` (validación) · `sns::nif` (nifly readback) · `sns::textures` (DirectXTex) · `ModPackager` + `StaticResourceBuilder` + `NativeNpcBuilder` + `OutfitPatchBuilder` (worker) · `bodyslide_build`/`bodyslide_morphs` (TRI) · `outfit_fit_doctor` (familias de cuerpo) · `AssetResolver`/`engines/mo2` (VFS, BSA, ganadores) · `CatalogStore` (SQLite incremental) · `WorkerClient`/IPC (cancelación, progreso) · arnés `--forge-preview` y `make_test_models.py` (verificación en ventana real y fixtures sintéticos) · `ForgeProjectStore` (.sncf).

## 4. Propuesta técnica de integración Blender y REA

**Blender (ya en marcha, consolidar):** mantener el contrato `sns.forge.skyrim/1` (job.json → script versionado → report.json), ampliarlo con operaciones `inspectRig`/`exportEquipped` para el incremento 4; añadir un add-on propio mínimo («Character Forge Tools») que lea el `report.json` y muestre los hallazgos dentro de Blender (comunicación de vuelta sin MCP ni sockets).
**REA:** no integrar ahora (§VI). Las cinco herramientas prioritarias son OSS o ya están integradas; REA queda reservado para formatos cerrados sin spec si aparecen (p. ej. HKX complejo). Nunca sobre ejecutables propietarios protegidos.

## 5. Primer incremento pequeño, concreto y verificable

**Incremento 0 — «regresión verde y gate»** (1–2 sesiones):
1. Reescribir `AppearanceTests.UnresolvedAndTemplatedAppearancesFailExplicitly` para la semántica actual: template resoluble OK, irresoluble lanza, ciclo de templates corta (cubre `visited` sin test actual).
2. Script `scripts/regresion-completa.ps1`: build + `sns_engine_tests` + binarios app + ambas suites .NET (con `DOTNET_ROOT` y exes directos) + informe con exit codes en `out/`.
3. Aceptación: una sola orden deja constancia verde de todo el árbol; `BethesdaWorker.Tests` vuelve a 100 %.

## 6. Archivos exactos a modificar en el incremento 0 y pruebas

- `tests/unit/BethesdaWorker.Tests/AppearanceTests.cs` — reescribir el caso templated (único archivo de test tocado).
- `tests/fixtures/SkyrimNpcStudio.TestFixtures/SyntheticLoadOrder.cs` (o el fixture equivalente) — añadir NPC con template irresoluble y ciclo si no existen.
- `scripts/regresion-completa.ps1` — nuevo; respeta claims (scripts nuevos están permitidos como altas aditivas; no tocar los existentes).
- **Pruebas:** `dotnet build` + exe de `BethesdaWorker.Tests` (objetivo: 92/92), ejecución completa del script con exit 0, registro en `out/`.
- **No tocar:** `AppearanceQueries.cs` (comportamiento nuevo es el deseado según el commit HEAD; si el propietario decide lo contrario, el incremento cambia a revertir f5b87b0 parcialmente — decisión humana requerida).
