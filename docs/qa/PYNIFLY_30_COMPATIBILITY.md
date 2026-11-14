# PyNifly 30.x en Character Forge — informe M1

- **Fecha:** 2026-11-04
- **Checkout:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2`, rama `main` @ `66096f5` + cambios locales sin commit
  (carpeta de trabajo fijada por el dueño; sin commit, merge ni cambio de rama).
- **Fase:** M1 de [`docs/plans/2026-11-04-mejoras-investigadas.md`](../plans/2026-11-04-mejoras-investigadas.md).
- **Estado:** `INTEGRACION_WINDOWS_OK`. No hay verificación en Skyrim: los paquetes siguen en `PENDIENTE_SKYRIM`.

## Hechos comprobados en el código de PyNifly

Comparación de `io_scene_nifly` 29.1.0 (instalado) con la etiqueta `V30.3.0`:

| Punto | 29.1.0 | 30.3.0 |
|---|---|---|
| `blender_defs.get_bone_global_xf` | `(arma, bone_name, game, use_pose)`; lee el global `game_rotations` | `(…, use_pose, rotations=None)`; con `None` sigue leyendo el global |
| Elección de tabla al exportar (`export_nif.execute`) | sustituye `BD.game_rotations` según `settings.rotate_bones_pretty` | idéntico |
| Texto de la preferencia `rotate_bones_pretty` | «Display only; no effect…» | idéntico (no es nuevo en 30.x) |
| Propiedades del operador que usa el estudio | `target_game`, `intuit_defaults`, `blender_xf`, `export_modifiers`, `write_tris` | presentes |

Conclusión: el único cambio de API que afecta al estudio es `rotations=`, que además es compatible hacia atrás. Aun así,
`forge_skyrim.py` lo detecta leyendo el código (`inspect.signature`) y no por el número de versión. Así, si una versión
futura elimina el global, el estudio no queda roto en silencio.

## Cambios integrados

| Archivo | Cambio |
|---|---|
| `tools/blender/forge_skyrim.py` | `bone_rotation_api()` nuevo; `nif_bone_matrix()` pasa `rotations=` cuando existe y solo sustituye el global en 29.x; el informe gana `pynifly.boneRotationApi`, `pynifly.testedVersions` y `armatures[].bindRotationApi`; `PYNIFLY_TESTED_VERSIONS = {(29,1,0), (30,3,0)}` |
| `engines/creation/src/skyrim_conversion.cpp` | `pyniflyMissing`/`pyniflyUntested` nombran la versión instalada y las probadas (de `testedVersions`; los informes antiguos siguen indicando 29.1.0); una versión malformada sale como «desconocida» |
| `apps/desktop/viewmodels/forge_skyrim_package.cpp` | `manifest.json` del paquete registra `pyniflyTested` y `pyniflyBoneRotationApi` junto a `pyniflyVersion` |
| `apps/desktop/qml/characterforge/SkyrimConversionWizard.qml` | Una versión no probada muestra «(no probada; probadas: …)» |
| `tests/unit/engines/test_skyrim_conversion.cpp` | 2 secciones nuevas + 1 aserción (mensaje con versiones; versión malformada) |
| `tests/app/test_forge_skyrim.cpp` | El E2E skinned exige `boneRotationApi == "parameter"` en ≥30, que la API del armature coincida y que el manifiesto la registre |

La política para versiones no probadas no cambia: es un **aviso**, y la relectura del NIF (`bindMatchesBlender`,
`weightsMatchBlender`, `pyniflyAgrees`…) decide si el paquete pasa.

## Ejecución

Build: `cmake --build build/claude-forge` (todo el árbol, incluida `SkyrimNpcStudio.exe`): exit 0.

| Suite | Resultado | Exit |
|---|---|---|
| `sns_engine_tests` (completa) | 239 casos, 3174 aserciones, todo PASS | 0 |
| `sns_forge_asset_tests "~[local]"` | 16 casos, 230 aserciones, todo PASS | 0 |
| `sns_forge_asset_tests "[skyrim][blender][local]"` con PyNifly 29.1.0 | 5 PASS, 1 SKIP, 58 aserciones | 0 |
| ídem con PyNifly 30.3.0 (`BLENDER_USER_SCRIPTS` aislado) | 5 PASS, 1 SKIP, 59 aserciones | 0 |
| `qmllint SkyrimConversionWizard.qml` | 0 errores (avisos preexistentes, ninguno en las líneas editadas) | 0 |

El SKIP es «Skinned diagnosis of a user model», que necesita `SNS_FORGE_DIAGNOSE_MODEL`. No cuenta como PASS.

Datos observados por versión (de `pynifly-facts.json` y `skinned-package/manifest.json`):

| PyNifly | `tested` | `boneRotationApi` | manifiesto `pyniflyVersion` / `pyniflyTested` / `pyniflyBoneRotationApi` | `skyrimStatus` |
|---|---|---|---|---|
| 29.1.0 | true | `global` | 29.1.0 / true / `global` | `PENDIENTE_SKYRIM` |
| 30.3.0 | true | `parameter` | 30.3.0 / true / `parameter` | `PENDIENTE_SKYRIM` |

El `.blend` de prueba (`skyrim_skinned.blend`) tiene el mismo SHA-256 antes y después de cada ejecución.

## Procedencia de PyNifly 30.3.0

- Origen: release oficial `V30.3.0` de `github.com/BadDogSkyrim/PyNifly` (publicada el 2026-11-04T13:26:27Z), licencia GPL-3.0.
- `io_scene_nifly.zip`: 14 498 827 bytes, SHA-256 `e00da181ae1c15a785d6aa6f9a9a1bf4f4b7212eda3b8f1f9abe53a05122a359`, igual al digest
  que publica GitHub para el asset.
- Se usó desde una carpeta temporal de la sesión mediante `BLENDER_USER_SCRIPTS`. El PyNifly 29.1.0 del usuario y su
  configuración de Blender no se modificaron. Rollback: borrar esa carpeta.
- El estudio **no** incluye ni redistribuye PyNifly: el usuario lo instala en su Blender.

## Evidencia

`out/local-implementation/m1-pynifly30-20261104/`:
- `backup/`: copias previas de los archivos editados, con SHA-256.
- `run-pynifly-29.1.0/`, `run-pynifly-30.3.0/`: primera pasada.
- `integrated/`: pasada final tras la integración (logs, `pynifly-facts.json`, paquete skinned con su manifiesto).

## Riesgos y pendientes

- No probado: 30.0.x, 30.1.x y 30.2.x. Si un usuario los tiene, verá el aviso «no probada» y la relectura decidirá.
- Exportar HKX de forma nativa (30.0+) queda fuera de M1 y se anota como incremento posterior.
- Un build, una suite o un paquete no prueban el comportamiento en Skyrim. Los paquetes siguen en `PENDIENTE_SKYRIM`.
