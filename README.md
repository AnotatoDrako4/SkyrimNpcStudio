# Skyrim NPC Studio Ultimate

Estudio de escritorio para Windows que lee tu instancia de **Mod Organizer 2**, indexa los NPC y prendas de tu perfil de **Skyrim SE/AE** y exporta los cambios como **mods reales e instalables en MO2**.

> Estado: explorador MO2, visor 3D, editor de atuendos, edición facial y exportación A/B/C disponibles con validaciones. La fidelidad de algunas mallas de replacers limita los morphs, y la comprobación final dentro de Skyrim sigue siendo manual ([plan](docs/DEVELOPMENT_PLAN.md), [diagnóstico de cierres](docs/CRASH_DIAGNOSTICS.md)).

## Qué hace hoy

- Detecta instancias de MO2 (globales y portables), perfiles, mods, prioridades, load order efectivo y conflictos de archivos — **en solo lectura**, con MO2 cerrado.
- Indexa NPC, armaduras y atuendos con Mutagen (caché SQLite incremental: 415 plugins en ~8 s, re-indexado en ~1,5 s).
- Ficha del NPC: plugin de origen y ganador, **ubicaciones en el mundo (referencias ACHR por celda)**, historial de overrides con los campos que cambia cada mod, **ediciones perdidas**, cabeza, facciones, inventario, FaceGen (y qué mod la aporta).
- Editor de atuendos con deshacer/rehacer, catálogo filtrable por slot, validación en vivo (slots, raza/ARMA, plantillas, mallas).
- **GUARDAR MOD (Modo A)**: OTFT nuevo (y atuendo de dormir opcional) + override mínimo copiado del registro ganador → ESP (opcionalmente ESL) verificado releyéndolo, carpeta lista para MO2 + ZIP, README y `manifest.json`. Atómico y sin sobrescribir nada.
- **Instalación asistida en MO2**: Opción directa desde el diálogo de exportación para copiar el mod a `mods/` tras validar que `ModOrganizer.exe` está cerrado y previa confirmación de seguridad.
- Proyectos editables `.snps` con historial de versiones, rostro y vestuario compartiendo el borrador.
- Cambios de color de cabello con vista previa inmediata y actualización del FaceGen exportable; los morphs incompatibles con la topología de un replacer se bloquean con diagnóstico.
- Las configuraciones de material de prendas se conservan en el proyecto, pero aún no afectan al visor ni al juego. La exportación las bloquea hasta que puedan generar DDS y referencias NIF verificadas; ver [estado de materiales](docs/MATERIAL_VARIANTS.md).
- Visor Qt Quick 3D: NIF mediante nifly, recursos sueltos/BSA 105, DDS mediante DirectXTex, mezcla de peso y skinning CPU. Transparencias traducidas de la `NiAlphaProperty` y del alfa real de cada textura, FaceGen con tinte compuesto, formas de física SMP ocultas como en el juego. Cámara orbital, paneo, zoom, selección, ocultación, UV, huesos, comparación del atuendo original/borrador, inspector por geometría (origen MO2, shader, texturas, alfa, huesos, avisos) y vistas de diagnóstico (canal alfa, normales, tangentes, alambre, solo difusa, sin luz).
- Recursos incompatibles o ausentes generan diagnósticos. No reproduce shaders, animaciones ni física de Skyrim; el visor no certifica exportaciones faciales.

## Ejecutar Sin Compilar

Abre `SkyrimNpcStudio.bat`. La copia preparada en `dist/SkyrimNpcStudio` contiene Qt y el worker .NET autocontenido. El lanzador actualiza esa copia si falta o si corresponde a un commit anterior; `SkyrimNpcStudio.bat --rebuild` fuerza la recompilación.

En el selector de instancia puedes elegir **Carpeta de Skyrim** si el juego fue movido. La elección se guarda en las preferencias del estudio, nunca en `ModOrganizer.ini`.

## Arquitectura en una línea

`SkyrimNpcStudio.exe` (C++20, Qt 6.11 Quick) ⇄ JSON-RPC sobre named pipe ⇄ `SkyrimNpcStudio.BethesdaWorker.exe` (.NET 10, Mutagen). Detalle en [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

```
apps/desktop/        aplicación Qt (QML + view models)
engines/             C++20 sin Qt: core, ipc, mo2, projects, outfit (+ módulos de fases futuras)
services/BethesdaWorker/  worker .NET: Contracts, Rpc, PluginReader, NpcCatalog, Validation, PatchBuilder
tests/               unit (Catch2 + xUnit), integration (proceso real + pipe real), export, fixtures sintéticos
tools/FixtureGen/    genera el load order sintético para los tests C++
scripts/             bootstrap, dev-env, build, smoke-test, fetch_qt
docs/                arquitectura, MO2, formato de exportación, NIF, compatibilidad, ADRs
```

## Compilar

Requisitos: Windows 10/11 x64, Visual Studio 2022 o Build Tools con C++ (MSVC 14.4x), Python 3.10+, CMake ≥ 3.25 y Ninja.

```powershell
.\scripts\bootstrap.ps1            # .NET SDK 10.0.401 + Qt 6.11.2 en D:\DevTools (sin admin)
.\scripts\build.ps1                # configura, compila app + worker, ejecuta todos los tests
.\scripts\build.ps1 -Preset engines   # sin Qt: solo motores + worker + tests
```

Ejecutar desde la consola de desarrollo (`. .\scripts\dev-env.ps1` pone Qt en el PATH):

```powershell
.\build\debug\bin\SkyrimNpcStudio.exe
.\build\debug\bin\SkyrimNpcStudio.exe --instance D:\Apps\ModOrganizer.ini --profile Default --npc 0A2C8E:Skyrim.esm
cmake --build --preset debug --target deploy   # copia el runtime de Qt junto al .exe
```

## Pruebas

| Suite | Cómo | Qué cubre |
|---|---|---|
| .NET (49) | `dotnet test --solution SkyrimNpcStudio.slnx` | índice, receta facial, apariencia efectiva, overrides/conflictos, caché, validador, protocolo, exportación |
| C++ (96, con MO2 local configurado) | `ctest --preset debug` | IPC, MO2, proyectos, exportación, FaceGen, BSA/LZ4, DDS, NIF, escena 3D, informes de fallos y dependencias de compilación |
| Locales `[local]` | variables `SNS_LOCAL_*` (ver [tests/README.md](tests/README.md)) | tu instancia real de MO2; se omiten si no están definidas |
| UI | `.\scripts\smoke-test.ps1` | verificación de carga QML completa, renderizado nativo y captura gráfica de la ventana |

La regresión de Character Creator se ejecuta en una ventana real con `--stress-appearance all`; incluye 18 escenarios de Lydia, cambios rápidos de cabello, cambio de NPC, rechazo de material no exportable, vestuario, guardado/reapertura y exportación. La guía y los resultados están en [docs/CRASH_DIAGNOSTICS.md](docs/CRASH_DIAGNOSTICS.md). Ninguna prueba automatizada afirma que un mod ya se comprobó dentro de Skyrim.

## Primer caso de uso: el atuendo de Lydia

Pasos 1–16 automatizados y verificados con datos reales (perfil "Default", 415 plugins): Lydia es `0A2C8E:Skyrim.esm` (FormID `000A2C8E`; `000A2C94` es su *referencia* en el mundo, no su NPC). Gana `Helen_Lydia_replacer.esp`; el parche exportado exige ese plugin como master porque conserva la cara y el pelo del ganador.

**Pendiente (lo haces tú):** 17) instalar el ZIP con *Instalar mod desde archivo* en MO2 y 18) comprobar el atuendo en el juego. El estudio nunca marca un cambio como verificado en el juego (`manifest.json → inGameVerified: false`).

## Garantías

- Nunca escribe en `modlist.txt`, `plugins.txt`, `overwrite/`, `mods/` ni en `Data/` del juego.
- Nunca modifica plugins de otros autores ni copia sus assets (modo dependiente).
- Exportación en *staging* + verificación + `rename` atómico; nunca sobrescribe.
- Sin datos del juego en el repositorio: los tests generan plugins sintéticos.

## Licencia

GPL-3.0-only (Qt Quick 3D y Mutagen lo exigen; ver [ADR 0004](docs/ADR/0004-licensing.md) y [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
