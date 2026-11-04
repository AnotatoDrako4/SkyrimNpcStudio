# Character Forge 2.0 — Blender → Skyrim pipeline

Fecha: 2026-11-03 · Carpeta: `D:\.Proyectos Games\SkyrimNpcStudio-ui2` (worktree de `D:\.Proyectos Games\SkyrimNpcStudio`)
· Rama `main` · SHA `f5b87b0599aa4e3c6fa0397cb40e884d586efd7c` · Fase AGENTS.md: F0 activa (este trabajo es un
incremento aislado de Character Forge; no toca D1/D2/D3/D22/D35/D36 ni publica nada).

**Estado del incremento 2 (recurso estático Tipo A): `INTEGRACION_WINDOWS_OK` · carga en el juego `PENDIENTE_SKYRIM`.**
Incremento 3 (recurso equipado) e incremento 4 (NPC completo): `NO_INICIADO`.

## 1. Estado real del código (auditoría previa)

Registro antes de editar: 59 cambios locales de otros agentes (Antigravity/Codex: Outfit Fit Doctor, BodySlide, visual-qa…);
12 worktrees (`git worktree list`). No se hizo commit, reset, stash, merge ni push. Archivos con edición ajena
(`apps/desktop/main.cpp`, `tools/sns-probe/*`, `engines/rendering/*`, `services/BethesdaWorker/*`…) no se tocaron;
`apps/desktop/CMakeLists.txt` y `tests/CMakeLists.txt` (ya modificados por otros) recibieron solo líneas añadidas.
Copias previas con SHA-256: `out/local-implementation/forge-skyrim-20261103/backups/`.

| Pieza | Estado encontrado (confirmado leyendo el código y ejecutando) |
|---|---|
| `ForgeBlenderBridge` | Funcional: FBX/OBJ/BLEND → GLB con `QProcess`, `--factory-startup --disable-autoexec`, versión 4/5 comprobada, timeout 5 min, cancelación, informe JSON validado. 7/7 pruebas (3 con Blender real). |
| `tools/blender/forge_bridge.py` | Script versionado incrustado como recurso Qt. No exporta NIF. |
| `gltf_inspect` / `ForgeSceneViewModel` | Inspección y vista previa GLB reales. **Una vista previa GLB no es un recurso de Skyrim.** |
| `forge_bridge_contracts.hpp` | Interfaz futura sin implementación (no se usaba; se deja). |
| `sns::nif` (nifly privado) | Lectura real de NIF (`load_mesh`, `prevalidate_nif`). Ningún exportador NIF propio. |
| `sns::textures` | `encode_dds` (DirectXTex may2026, MIT) RGBA8/BC1/BC3/BC7 con mips. |
| Exportación técnica NPC | BethesdaWorker/Mutagen; FaceGen nativo bloqueado (sin cambios aquí). |
| UI «Blender y MCP» | MCP y «Abrir escena/Sincronizar» deshabilitados con motivo (correcto, se mantiene). |

Defectos reales encontrados:

1. **`.blend` comprimidos rechazados** por `CharacterForgeViewModel::selectModelFile` («Invalid Blender file header»): solo
   aceptaba la cabecera `BLENDER`; Blender también guarda zstd (`28 B5 2F FD`) y gzip (`1F 8B`). Reproducido en la ventana real
   (run1-ui), corregido y verificado (run2/run3).
2. **PyNifly escribe rutas absolutas** cuando la imagen no está bajo una carpeta `textures` (`shader_io.py`: ruta del disco con
   extensión `.dds`). Resuelto preparando las texturas en la copia de trabajo (§2).
3. **PyNifly 29.1 devuelve un conjunto vacío al exportar con éxito** (no `{'FINISHED'}`) y `{'CANCELLED'}` al fallar
   (`export_nif.py`, final de `execute`). Un comprobador que exija `FINISHED` declararía fallida una exportación correcta.
4. Arnés `--forge-preview`: `findChildren` no ve delegados de Repeater ni contenido de Loader (solo árbol visual). Corregido en la
   fase nueva con búsqueda por `childItems`.

## 2. Arquitectura de integración

```
QML SkyrimConversionWizard ──► CharacterForgeViewModel.analyzeSkyrimCompatibility() / exportSkyrimNif()
                                   │
                                   ▼
                    ForgeBlenderBridge.runSkyrimJob(operation, source, prefix, nifName)
                    (QTemporaryDir aislado · job.json · QProcess sin shell · timeout 10 min · cancelación ·
                     progreso SNS_FORGE_PROGRESS)
                                   │  blender --background --factory-startup --disable-autoexec
                                   │          --python-exit-code 17 --python forge_skyrim.py -- job.json
                                   ▼
                    tools/blender/forge_skyrim.py (contrato sns.forge.skyrim/1)
                     load → save_as work.blend (copia; relative_remap) → inspect (hechos)
                     [exportNif] safe transforms → stage textures → export_scene.pynifly(SKYRIMSE) → readback PyNifly
                                   │  report.json
                                   ▼
          engines/creation skyrim_conversion (Qt-free): classify(report) · verify_nif(bytes, report) con sns::nif
                                   │
                                   ▼
          forge_skyrim::package_export → <proyecto>.sncf/exports/skyrim/<nombre>-<UTC>/ (atómico .partial → rename)
            Data/meshes/…nif · Data/textures/…dds (BC1/BC3 + mips) · report.json · verification.json · manifest.json
```

Principios: Blender solo **recoge hechos**; los veredictos (severidades, verificación) se calculan en C++ de forma determinista y
con pruebas. Blender recibe únicamente la ruta de un `job.json` escrito por la aplicación (nunca texto Python ni comandos de shell
concatenados); prefijo y nombre se validan con expresiones regulares en C++ y otra vez en Python. El original nunca se guarda (se
compara su SHA-256 antes y después). Nada se escribe en `Skyrim/Data`, `MO2/mods`, `MO2/overwrite` ni perfiles.

Severidades: `blocking`, `autoFixable`, `warning`, `manualReview`, `compatible`. Cada hallazgo lleva problema, consecuencia,
solución, `nonDestructive` y `fixAvailable`; `fixAvailable=true` **solo** para lo que el job hace de verdad: aplicar rotación/escala
en mallas sin shape keys, armature, padre ni datos compartidos; preparar texturas; omitir una imagen ausente.

`verify_nif`: `formatSkyrimSE` (stream BS 100), `readable` (nifly del estudio), `geometry` (UV y normal por vértice), `triangles`
(= Blender preparado), `bounds` (escala/orientación = Blender, tolerancia 1 %), `texturePathsRelative`, `texturesPackaged`,
`shaderBlocks`, `noSkin` (perfil estático) y `pyniflyAgrees` (dos builds de nifly coinciden).

UI: nueva herramienta «Convertir a Skyrim» (grupo Recursos y 3D) con el recorrido Importar → Diagnosticar → Preparar → Adaptar →
Exportar → Verificar, botón **ANALIZAR COMPATIBILIDAD CON SKYRIM**, lista de hallazgos por severidad, progreso por paso de Blender,
cancelación, **EXPORTAR NIF VALIDADO** (deshabilitado sin análisis o con bloqueantes), comprobaciones de la relectura, carpeta y
hashes del paquete y aviso explícito de `PENDIENTE_SKYRIM` / sin plugin. Perfiles Tipo B y C se muestran como no disponibles con
su motivo.

## 3. Componentes reutilizados

`ForgeBlenderBridge` (mismo proceso, timeout, cancelación y validación de versión; ahora con modo job), recurso Qt `/forge/`,
`forge_assets::resolve_model`, `ForgeProjectStore` (.sncf), `sns::nif::load_mesh` (nifly), `sns::textures::encode_dds` /
`measure_alpha` (DirectXTex), arnés `--forge-preview`, `make_test_models.py` (fixtures sin assets de terceros). No se creó
exportador NIF propio ni se sustituyó el BethesdaWorker.

## 4. Dependencias verificadas

| Dependencia | Versión / ubicación | Verificación |
|---|---|---|
| Blender | 5.2.3 LTS, `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe` | `--version` |
| PyNifly (`io_scene_nifly`) | 29.1.0, add-on de usuario de Blender 5.2; `NiflyDLL.dll` SHA-256 `B66A0EAADA3B3C1DC38A5BEE8643E3CB2C0502C901074DE1886A8445F1475F67` | `addon_utils.enable` bajo `--factory-startup`; operador `export_scene.pynifly`; propiedades reales listadas (`target_game` con `SKYRIMSE`, `intuit_defaults`, `blender_xf`, `write_tris`, `export_modifiers`…) |
| nifly del estudio | `build/debug/_deps/nifly-src` (FetchContent existente) | relectura en pruebas |
| DirectXTex | may2026 (MIT, ya en `cmake/Dependencies.cmake`) | DDS BC1/BC3 releídos con `parse_dds` |
| Qt / MSVC | 6.11.2 / 14.44 | build `build/claude-forge` |

No se instaló ninguna dependencia nueva. La carpeta instalada de PyNifly no trae archivo de licencia; su licencia no se verificó
localmente y PyNifly no se redistribuye (el estudio invoca la instalación del usuario). La versión probada queda fijada en
`forge_skyrim.py` (`PYNIFLY_TESTED_VERSIONS`); otra versión produce `pyniflyUntested` y la relectura decide.

Blender MCP (`mcp-for-blender`) no se integró: el pipeline determinista no lo necesita y expone un socket de ejecución Python.
Queda como opción futura, local y restringida (incremento 5).

## 5. Riesgos técnicos

- **Escala**: PyNifly exporta 1:1 (`blender_xf=False`). Un modelo hecho en metros mide 1,8 unidades (≈ 2,6 cm). Se avisa
  (`suspiciousScale`) pero **no** se reescala automáticamente.
- **Texturas**: PNG → DDS BC1/BC3; sin normal maps especializados (BC5/BC7) ni conversión PBR; roughness/metallic quedan como
  metadato (`pbrChannelDropped`).
- **Skinning / TRI / particiones**: fuera del perfil estático; una malla con Armature se bloquea (`skinnedStatic`).
- **Colisión**: el NIF no lleva `bhkCollisionObject`.
- **Sin ESP**: el paquete Tipo A no contiene registros.
- Mayúsculas: rutas de malla `meshes\CharacterForge\…`, de textura en minúsculas (PyNifly). Windows/Skyrim no distinguen, pero un
  BSA debería normalizarlas.

## 6. Incrementos ejecutables

| # | Meta | Estado |
|---|---|---|
| 1 | Auditoría, pruebas existentes, BLEND→GLB real, .sncf, detección PyNifly | `TESTS_UNITARIOS_OK` (+ defecto `.blend` comprimido corregido) |
| 2 | Primer NIF real: PyNifly → relectura nifly → paquete → integrado en Character Forge | `INTEGRACION_WINDOWS_OK` · `PENDIENTE_SKYRIM` |
| 2b | Registro STAT del recurso vía BethesdaWorker + ZIP MO2 con el `ModPackager` existente | `TESTS_UNITARIOS_OK` (worker + C++); ventana real solo camino de error; `PENDIENTE_SKYRIM` |
| 3 | Recurso equipado: esqueleto de referencia (skeleton.nif vanilla/XPMSSE), pesos, particiones, slots, ARMO/ARMA | `NO_INICIADO` |
| 4 | NPC completo con FaceGen/HeadParts validados | `BLOQUEADO` por FaceGen nativo (bloqueo existente del estudio) |
| 5 | Panel Blender «Character Forge — Skyrim Tools», sincronización bidireccional, perfiles CBBE/3BA/UBE, MCP opcional | `NO_INICIADO` |

Siguiente incremento viable (2b): pedir al BethesdaWorker un plugin ESL con un `STAT` que apunte a
`meshes\CharacterForge\<nombre>\<nombre>.nif`, readback del plugin, ZIP con el `ModPackager` existente y kit de prueba en un perfil
MO2 aislado **con autorización explícita** (nunca el perfil habitual).

## 7. Archivos modificados

Nuevos: `tools/blender/forge_skyrim.py` · `engines/creation/include/sns/creation/skyrim_conversion.hpp` ·
`engines/creation/src/skyrim_conversion.cpp` · `apps/desktop/viewmodels/forge_skyrim_package.{hpp,cpp}` ·
`apps/desktop/qml/characterforge/SkyrimConversionWizard.qml` · `tests/unit/engines/test_skyrim_conversion.cpp` ·
`tests/app/test_forge_skyrim.cpp` · `tests/fixtures/models/skyrim/goblet.nif` (2 879 B, SHA-256
`9B65C070EE672EC2A1790E7D5886B2FA4F82F1884124528AAA7237D74755926D`, sintético) ·
`tests/fixtures/models/skyrim/goblet_report.json` (SHA-256 `A3BFE94AB6BDB90CA1B795BB9E7F8F01032C7FEC9ADFAA655A98AF3B2BED261C`,
sin rutas personales).

Modificados: `ForgeBlenderBridge` (`runSkyrimJob`, `start`, `read_progress`, señales `skyrimCompleted`/`progressed`, detalle del
error desde el informe) · `CharacterForgeViewModel` (`skyrimConversion`, `analyzeSkyrimCompatibility`, `exportSkyrimNif`,
`start_skyrim_job`, cabecera `.blend` zstd/gzip, reinicio al abrir/crear proyecto, capacidades `skyrimStaticNif/Armor/Npc`) ·
`forge_preview_check.{hpp,cpp}` (fase «Convertir a Skyrim» con clics reales; búsqueda por árbol visual) · `CharacterForge.qml`,
`ForgeNavigation.qml`, `BlenderConnectionPanel.qml` · `make_test_models.py` (`skyrim_prop`) · `engines/creation/CMakeLists.txt`,
`apps/desktop/CMakeLists.txt`, `tests/CMakeLists.txt` (solo altas).

## 8. Comandos y resultados

Build en directorio propio (no interfiere con `build/debug` ni con los de otros agentes), consola UTF-8 vía `scripts/dev-env.ps1`:

```powershell
. .\scripts\dev-env.ps1
cmake --preset debug -B build\claude-forge "-DFETCHCONTENT_SOURCE_DIR_NIFLY=build\debug\_deps\nifly-src" ...  # resto de deps igual
cmake --build build\claude-forge     # BUILD_EXIT=0 (app + todas las pruebas)
```

Script usado: `out/local-implementation/forge-skyrim-20261103/logs/build-forge.ps1`. Pruebas locales con
`SNS_LOCAL_BLENDER`, `SNS_LOCAL_BLENDER_FIXTURES` (make_test_models.py `bridge_sources`) y `SNS_LOCAL_SKYRIM_FIXTURES`
(make_test_models.py `skyrim_prop`).

| Suite | Resultado |
|---|---|
| Línea base antes de editar: `ctest -R "app:.*(Blender\|forge\|Forge)"` | 7/7 PASS (incluye BLEND/FBX/OBJ → GLB real) |
| Línea base `ctest -R "unit:.*([Ff]orge\|gltf\|nif)"` | 9/9 PASS |
| `sns_engine_tests "[skyrim]"` (nuevo) | 6 casos, 77 aserciones PASS |
| `ctest -R "app:.*(Skyrim\|Blender\|Forge\|NIF)"` | 16/16 PASS (3 con Blender + PyNifly reales: extremo a extremo, textura ausente, cancelación) |
| Ventana real `SkyrimNpcStudio.exe --smoke-test --forge-preview parts_set.glb` + fase Skyrim | run1-ui FAIL (defectos 1 y 4) → run3-ui **34 PASS / 0 FAIL**, exit 0, sin advertencias QML |
| Regresión completa `ctest -j4` (372) | 363 PASS / 9 FAIL; los 9 (NPC-08/09, SERANA-01..07 de Outfit Fit Doctor, trabajo local de otro agente) fallan con «No test cases matched» porque ctest entrega mal codificados sus nombres con «—» y tildes. Ejecutados directamente (`sns_engine_tests "SERANA-0*"`, `"NPC-0*"`): 9/9 PASS, 257 aserciones. No es una regresión de este trabajo. Log: `logs/ctest-full.log`. |
| .NET (BethesdaWorker) | SKIP: sin cambios en .NET; `dotnet test` escribe en `obj/` compartido con otros agentes |

Casos cubiertos: Blender inexistente o ejecutable que no es Blender, PyNifly ausente, versión no probada, NIF corrupto/truncado/LE,
rutas maliciosas (job y paquete), proyecto no `.sncf`, sin UV, sin mallas, >65 535 vértices, malla con armature,
transformaciones con shape keys, textura ausente, PBR, alfa (BC3), escala en metros, topología distinta de Blender (lo que invalida
un TRI), escala/orientación distinta, textura no preparada, desacuerdo PyNifly/nifly, cancelación real, exportación interrumpida
(sin `.partial`), original sin cambios, regresión de Character Creator/visor/Outfit Studio dentro de la suite completa.
No cubiertos aún: Blender colgado (cubierto por el timeout del puente, sin prueba dedicada de 10 min), falta de masters (no hay ESP
en Tipo A), apertura/cierre repetido del proyecto con la fase Skyrim.

## 9. Evidencia del primer modelo exportado

Fuente sintética `skyrim_prop.blend` (make_test_models.py `skyrim_prop`: copa de 24 lados, PNG externo 64×64, escala Z 1,4 sin
aplicar), SHA-256 `812167D0D9D5874FCDB3FF2E6F06DD2012BF66050CFF3E8873C0323C3A8F51F7`, intacta tras la exportación. Copia en
`out/local-implementation/forge-skyrim-20261103/fixtures/`.

Paquete (run3-ui): `out/local-implementation/forge-skyrim-20261103/run3-ui/SkyrimCheck.sncf/exports/skyrim/skyrim_prop-20261103-200924/`

| Archivo | Bytes | SHA-256 |
|---|---:|---|
| `Data/meshes/CharacterForge/skyrim_prop/skyrim_prop.nif` | 2 884 | `DBD9646CA3C9C0499944F62FC9E35B30DB3B88902D441ED73F8E2944D194270F` |
| `Data/textures/characterforge/skyrim_prop/goblet_diffuse.dds` | 2 872 | `A00C76078691859F4E27A3BB2CA4E55E092DF03E696A28D37DF186EF860555D1` |
| `report.json` | 6 652 | `E6CF5E86B1B4713607EFCCDD88C38A4B951649EA901B3DD6F99F0E341EF45B2F` |
| `verification.json` | 2 934 | `306D267F00F0D3471AFDA9F3E193F8F215D7295CBC86D33A1BE81A3918FC101B` |
| `manifest.json` | — | `5D8E3F13E28269172DEFFDBD97D13F965B8E3E95446A168CA9A0D62D28A91C0C` |

Relectura: SKYRIMSE (stream 100), 1 forma, 61 vértices con UV y normal, 92 triángulos (= Blender), límites
[-4, -4, -2,8]–[4, 4, 16,8] iguales a Blender tras aplicar la escala, `BSLightingShaderProperty`, difusa
`textures\characterforge\skyrim_prop\goblet_diffuse.dds` (DDS BC1, 7 mips), sin huesos; PyNifly y el nifly del estudio coinciden.
**Reproducible**: run2-ui y run3-ui produjeron NIF y DDS idénticos byte a byte. Capturas:
`run3-ui/skyrim-{1-open,2-analyzed,3-exported}.png`. Log: `run3-ui/app.log`.

## 10. Limitaciones que impiden cargarlo en Skyrim

1. **No hay plugin**: sin un registro STAT/MISC/ACTI que referencie el NIF, el juego no lo usa (incremento 2b).
2. **Sin colisión** (`bhkCollisionObject`): colocado como STAT se atraviesa.
3. **No probado en el juego**: `PENDIENTE_SKYRIM`. `VERIFICADO_EN_SKYRIM` requiere observación humana con capturas y hashes del
   build y del perfil (AGENTS.md). Ninguna build, suite o paquete lo sustituye.
4. El paquete vive dentro del proyecto `.sncf`; no se generó ZIP MO2 para el Tipo A (el `ModPackager` existente trabaja sobre un
   plugin, que todavía no existe).

## 11. Incremento 2b — STAT + mod MO2 (2026-11-03)

Estado: `TESTS_UNITARIOS_OK` · ventana real: solo camino de error · `PENDIENTE_SKYRIM`. Sin commit.

Flujo: «EMPAQUETAR MOD MO2» (`CharacterForgeViewModel::packageSkyrimMod`) → `forge_skyrim::mod_request` (lee manifest.json y
verification.json del paquete verificado; OBND = caja nifly redondeada hacia fuera) → RPC `export.forgeStaticResource`
→ `ModPackager.ExportForgeStaticResource` (staging, política de destino, sin sobrescritura, README/meta.ini, ZIP releído, commit
atómico) → `StaticResourceBuilder.Build`: valida rutas (solo `meshes/*.nif`, `textures/*.dds`, sin `..`, unidades, dispositivos,
enlaces), re-hashea cada archivo contra el manifiesto y copia esos mismos bytes; ESP nuevo con flag ESL, sin masters, un único STAT
(EDID `CF_<nombre>`, MODL, OBND) en rango 0x800–0xFFF; relectura con Mutagen. Salida: `<proyecto>.sncf/exports/skyrim/mods/`.

Archivos nuevos: `services/BethesdaWorker/Contracts/ForgeDtos.cs`, `services/BethesdaWorker/PatchBuilder/StaticResourceBuilder.cs`,
`tests/export/SkyrimNpcStudio.ExportTests/ForgeStaticResourceExportTests.cs`. Altas: `Protocol.cs` (método), `WorkerRpcTarget.cs`
(RPC), `ModPackager.cs` (envoltorio + textos README/pendiente para STAT), `engines/ipc/.../protocol.hpp`, `skyrim_conversion`
(`boundsMin/boundsMax` en `verify_nif`), `forge_skyrim_package` (`mod_request`), viewmodel, `SkyrimConversionWizard.qml`,
`forge_preview_check.cpp` (fase opcional `SNS_FORGE_SKYRIM_MOD=1`), `tests/app/test_forge_skyrim.cpp`. Copias previas:
`out/local-implementation/forge-skyrim-20261103/backups-2b/` (SHA256SUMS.txt). Protocolo sin cambio de versión menor (igual que
`export.convertedOutfit` de otro agente): un worker antiguo responde «método desconocido».

| Suite | Resultado |
|---|---|
| `dotnet test` ExportTests `--filter-class ForgeStaticResourceExportTests` (`--artifacts-path` propio, obj/ compartido intacto) | 21/21 PASS (log `logs/dotnet-forge-static.log`) |
| ExportTests completa | 175/175 PASS (`logs/dotnet-export-full.log`) |
| `ctest` Forge/Skyrim/NIF (31) | 31/31 PASS tras regenerar fixtures (2 fallaban con «código 17» usando las copias de `fixtures/` de la evidencia; con `make_test_models.py` recién generados pasan; causa de las copias no investigada) |
| Ventana real run4-mod (`--instance D:\Apps\ModOrganizer.ini --profile Default`) | FAIL del paso del mod: el modo `--forge-preview` no abre la instancia (`plugins=0`), el worker respondió NotIndexed y la UI lo mostró. Fases anteriores PASS. |

**Incidencia `ModOrganizer.ini` (investigada, causa: MO2 mismo, no el estudio)**: la escritura de las 19:11:56 (hora local)
coincide con el cierre de una sesión de MO2 2.5.2 abierta desde las 18:46 (`D:\Apps\logs\usvfs-2026-11-03_21-46-44.log`, horas en
UTC); MO2 guarda su ini al salir. Después hubo otra sesión de MO2 de 19:15:37 a 19:26:06 (`mo_interface.log`: `skse64_loader.exe`
lanzado dos veces, plugin «MO2 ApI Bridge» en 127.0.0.1:8930) que volvió a escribir el ini a las 19:26:06 y `nxmhandler.ini` a las
19:15:42; perfil seleccionado `SNS-Cuerpos-Test`. Ninguna ruta del estudio escribe ese archivo (`engines/mo2` solo lo lee; sus
`QSettings` van a `SNS_SETTINGS_DIR`). Prueba (run5-ini-check): con MO2 cerrado, `SkyrimNpcStudio.exe --smoke-test --instance
D:\Apps\ModOrganizer.ini --profile Default` indexó 415 plugins (exit 0) y `ModOrganizer.ini`, `nxmhandler.ini`, `plugins.txt` y
`modlist.txt` de Default quedaron idénticos en SHA-256 y fecha (`run5-ini-check/before.txt` = `after.txt`). Quién abrió MO2 (el
usuario u otro agente) no se puede saber desde los logs.

Falta para `PAQUETE_MO2_VERIFICADO` en la app: que el arnés abra la instancia antes de la fase Forge (o un perfil sintético), y
para `VERIFICADO_EN_SKYRIM`: instalar el ZIP en un perfil MO2 aislado, `help CF_<nombre> 4`, `player.placeatme`, capturas.

### 11.1 Ventana real del mod y prueba con un modelo del usuario (2026-11-03)

- **run6-mod** (`SNS_FORGE_SKYRIM_PROFILE=Default`, `--instance D:\Apps\ModOrganizer.ini`): el arnés carga el perfil (solo
  lectura, índice en 10 s), pulsa EMPAQUETAR MOD MO2 y comprueba el resultado: 39 PASS / 0 FAIL, exit 0. `CF_skyrim_prop.esp`
  SHA-256 `F3B842C1D191D4F415A5C0E86CBDB7D4ABF0386F1B17D81D2A9FE61DE4D1A6B4`, STAT `000800:CF_skyrim_prop.esp`, ESL, sin masters,
  ZIP releído (5 entradas). `ModOrganizer.ini`, `nxmhandler.ini`, `plugins.txt` y `modlist.txt` idénticos antes/después
  (`run6-mod/mo2-before.txt` = `mo2-after.txt`). Estado: `PAQUETE_MO2_VERIFICADO` · `PENDIENTE_SKYRIM`. (Las escrituras previas
  del ini fueron del propio MO2, abierto por el usuario.)
- Defecto corregido tras run6: el README y lo pendiente heredaban textos de NPC/FaceGen; el modo estático tiene README propio
  (prueba: el README no contiene «FaceGen» ni «override(s) de NPC").
- **`G:\Body_RETOPO_glutes_v1.blend`** (del usuario, 98 922 742 B, SHA-256 `F539FD99…6DF51`, intacto tras cada ejecución):
  - Defecto real encontrado: `forge_skyrim.py` `inside()` usaba `os.path.commonpath`, que lanza `ValueError` con rutas en unidades
    distintas → todo modelo fuera de la unidad del temporal (C:) fallaba con «código 17». Era también la causa de las copias de
    fixtures en `D:` que fallaban. Corregido (unidades distintas = «no está dentro»); 21/21 ctest Forge con fixtures en `D:`.
  - Ventana real (`user-body-retopo/run-ui`): análisis correcto, exit 0, `exportable = false` con 26 hallazgos: 5 bloqueantes
    (`shapeTooBig` 89 184/89 745 vértices; `skinnedStatic` en 3 mallas con `Goblin_Armature`), 3 corregibles (`textureStaging`),
    17 advertencias (4 texturas ausentes en `G:\textures\`, PBR roughness/metallic, no-manifold, caras degeneradas, una textura
    > 4096 px, escala en metros: 1,9 unidades ≈ 2,7 cm), 1 revisión manual (armature de 20 huesos no Skyrim).
  - Conclusión: es un cuerpo con rig, no un recurso estático; necesita el perfil equipado (incremento 3: esqueleto Skyrim,
    reescalado, división por debajo de 65 535 vértices). El botón EXPORTAR permanece deshabilitado, como debe.
