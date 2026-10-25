# SNS-WIN RC-A — Informe del incremento (2026-10-24)

**Carpeta:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2` · **Git:** no usado (orden del propietario: «guarda en local pero hace los cambios»)
**Build:** `build/sns-win` (Debug, MSVC 14.44, Qt 6.11.2, .NET 10.0.401) · **Perfiles MO2 leídos (solo lectura):** `BodyChangeNG-Test`, `Default`

| Artefacto | SHA-256 |
|---|---|
| `build/sns-win/bin/SkyrimNpcStudio.exe` | `acdec8cb5cc1b1927f03387940e8ac284613d5ec288992d325d745c2cdef56a2` |
| `build/sns-win/bin/worker/SkyrimNpcStudio.BethesdaWorker.dll` | `e2554d2cf8d814be1208aefd89f82a4b4b4af43776d5236c5500ec36aa39d124` |
| `run-final/.../SNS Body Test.zip` | `b67cb0eed772f8555114b13832e6a042f27938993796f57f32ed5adb312d2184` |
| `run-final/.../BodyChangeNGdistribution.json` | `0bb96750d6949e53a27c7d8ebbd522532692448129bbdcd73044e3e43786d473` |

## Estado por punto del plan

| Punto | Estado | Evidencia |
|---|---|---|
| RC-A 3 · Body Preset Director (BOD-01/02/03) | `INTEGRACION_WINDOWS_OK`, `PAQUETE_MO2_VERIFICADO`, `PENDIENTE_SKYRIM` | `run-final`, `run1`, `docs/BODY_PRESET_DIRECTOR.md` |
| BOD-04 (nombre repetido → `file`) | `TESTS_UNITARIOS_OK`; el perfil real no tiene nombres repetidos (SKIP honesto) | `test_body_analysis.cpp` |
| RC-A 2 · Galería de cabello/ojos/piezas (HAIR-01/02) | `INTEGRACION_WINDOWS_OK` (clics reales, aplicar y deshacer exactos) | `run18` |
| RC-A 1 · Serana sin pantalones (SER-01/02/03) | **NO REPRODUCIDO en la app** en ninguno de los dos perfiles; dentro del juego `PENDIENTE_SKYRIM` | `run3`, `run4-default`, `outfit-diagnosis.json` |
| RC-A 4 · Packs sin ESP | `INTEGRACION_WINDOWS_OK` (7,810 recursos indexados en MO2: 6,300 loose meshes, 1,510 BodySlide; RES-01/02/03 verificados) | `out/local-implementation/sns-win-20261024-loose/run1/20261024-193227/loose-resources-report.json`, `docs/LOOSE_RESOURCE_LIBRARY.md` |
| RC-A 5 · Asistente facial | `NO_INICIADO` (la base existente —resolución asistida, revisión D1, Easy Mod— no se tocó) | — |

## Serana: lo que se comprobó (sin parche específico)

- Atuendo ganador: `01_PriscilaSuit` `000814:Priscilla.Serana.Replacer.esp` (plugin del propietario, modificado 2026-01-15; masters Twilight
  Princess, Temio, Obi's Crimson…). Contiene 6 piezas: capa (46), botas (37), guantes (33), **Crimson Pants (49)**, corsé (45), camisa (32).
- No hay conflicto de slots (`piecesUnequipped = 0`), las mallas `Legs_0/_1.nif` existen y se dibujan: en `BodyChangeNG-Test` desde
  «BodyChangeNG - BodySlide Output» (con BODYTRI) y en `Default` desde «Obi's Crimson Outfit 2K 3BA BHUNP SMP» (sin BODYTRI).
- El INI de CBBEtoUBE solo **añade** ARMA UBE (`armorAddonsToAdd`) y el mod SkyPatcher no está activo en el perfil de prueba.
- Causas posibles dentro del juego, **no verificadas**: partida guardada con el inventario/atuendo anterior de Serana (cambiar el DOFT no
  re-equipa a un actor ya cargado), scripts de Dawnguard que le cambian el atuendo, o diferencias de raza ARMA en tiempo de ejecución.
  Siguiente paso comprobable: en un perfil aislado y partida nueva, `prid 2B6C` → `resetinventory` y observar; adjuntar captura.

## Pruebas

| Suite | Resultado |
|---|---|
| ctest `build/debug` | **314/314 PASS** (0 fallos, 10 SKIP: Blender ×3, symlink, integration) |
| Catch2 motores | 180 casos / 2010 aserciones PASS (`[loose]` 4 / 59, `[body]` 9 / 185) |
| .NET ExportTests / BethesdaWorker.Tests | 143/143 · 89/89 PASS |
| Escenarios reales `run-final` + `run1-loose` | `loose-resources`, `body-director`, `outfit-diagnosis`, `creator-layout`, `head-part`, `switch-npc`: **PASSED**, 0 avisos QML |
| `hair-gallery` (`run18`) | PASSED |

## Defectos encontrados y corregidos durante el ciclo

1. `body_analysis.hpp` declaraba operaciones sin implementación → implementadas y probadas.
2. C++ no tenía las constantes RPC 1.7 `body.inspectNpcs` / `export.bodyDistribution` → añadidas.
3. Mi primera versión del panel Cuerpo desbordaba a 420 px (etiquetas largas fijaban el ancho mínimo) → corregido, comprobado con `layout_problems`.
4. La galería de piezas podía quedarse vacía (carrera entre carga del NPC, análisis facial y protección) → búsqueda determinista por (NPC, categoría, texto), reintento ante error y estado `headPartSearch` visible (buscando / sin resultados / error).
5. La galería mostraba opciones de una cabeza **bloqueada** por la protección facial → ahora solo muestra el motivo.
6. `test_worker_pipe.cpp` exigía protocolo 1.6 literal; el worker actual es 1.7 → aserción `>= 6` y comprobación de las capacidades `body.*`.

## Archivos (respaldos con SHA-256 en `backups/`)

Nuevos: `engines/body/src/body_analysis.cpp`, `apps/desktop/viewmodels/body_director.{hpp,cpp}`,
`qml/charactercreator/{BodyPresetSection,HeadPartGallery}.qml`, `tests/unit/engines/test_body_analysis.cpp`, `docs/BODY_PRESET_DIRECTOR.md`.
Modificados: `engines/body/{CMakeLists.txt, include/sns/body/body_analysis.hpp}`, `engines/ipc/include/sns/ipc/protocol.hpp`,
`apps/desktop/viewmodels/{studio_controller,appearance_editor}.{hpp,cpp}`, `apps/desktop/components/appearance_stress.cpp`,
`qml/charactercreator/{BodyCustomizer,HairEyeCustomizer}.qml`, `tests/reference-integration/test_worker_pipe.cpp`; reservados con cambio
aditivo: `apps/desktop/CMakeLists.txt`, `tests/CMakeLists.txt`. Nota: `body_analysis.hpp`, `engines/body/CMakeLists.txt` y
`appearance_stress.cpp` se respaldaron después de su primera edición; su original es el del commit base `afe41e6`.

**Nada se escribió en MO2, `Skyrim/Data` ni partidas** (SHA-256 del `BodyChangeNGdistribution.json` del perfil igual antes y después).
