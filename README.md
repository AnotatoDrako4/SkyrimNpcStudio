# Skyrim NPC Studio Ultimate

Estudio de escritorio para Windows que lee tu instancia de **Mod Organizer 2**, indexa los NPC y prendas de tu perfil de **Skyrim SE/AE** y exporta los cambios como **mods reales e instalables en MO2**.

> Estado: explorador MO2, visor 3D, editor de atuendos, edición facial y exportación A/B/C disponibles con validaciones. La fidelidad de algunas mallas de replacers limita los morphs, y la comprobación final dentro de Skyrim sigue siendo manual ([beta candidata 1](docs/BETA_CANDIDATE.md), [plan](docs/DEVELOPMENT_PLAN.md), [diagnóstico de cierres](docs/CRASH_DIAGNOSTICS.md)).

## Qué hace hoy

- Detecta instancias de MO2 (globales y portables), perfiles, mods, prioridades, load order efectivo y conflictos de archivos — **en solo lectura**, con MO2 cerrado.
- Indexa NPC, armaduras y atuendos con Mutagen (caché SQLite incremental: 415 plugins en ~8 s, re-indexado en ~1,5 s).
- Ficha del NPC: plugin de origen y ganador, **ubicaciones en el mundo (referencias ACHR por celda)**, historial de overrides con los campos que cambia cada mod, **ediciones perdidas**, cabeza, facciones, inventario, FaceGen (y qué mod la aporta).
- Editor de atuendos con deshacer/rehacer, catálogo filtrable por slot, validación en vivo (slots, raza/ARMA, plantillas, mallas).
- **GUARDAR MOD (Modo A)**: OTFT nuevo (y atuendo de dormir opcional) + override mínimo copiado del registro ganador → ESP (opcionalmente ESL) verificado releyéndolo, carpeta lista para MO2 + ZIP, README y `manifest.json`. Atómico y sin sobrescribir nada.
- **Rostro (Modo B) y NPC completo (Modo C)**: solo los campos de rostro que cambiaste, con su FaceGen; el modo C junta rostro y atuendo del mismo NPC en un plugin. El **NPC Safe Patch Engine** relee cada override y lo compara campo a campo con el registro ganador: cualquier cambio no pedido bloquea la exportación, y lo permitido queda en el diff semántico del manifiesto y del README ([formato](docs/MOD_EXPORT_FORMAT.md)).
- **Easy Mod Creator**: asistente de 5 pasos (personaje, modificación, vista previa, comprobación, crear mod) que elige el modo con el cambio mínimo, avisa de las dependencias y de lo bloqueado para ese NPC, y crea el ZIP para MO2 ([guía](docs/EASY_MOD_CREATOR.md)).
- **Instalación asistida en MO2**: Opción directa desde el diálogo de exportación para copiar el mod a `mods/` tras validar que `ModOrganizer.exe` está cerrado y previa confirmación de seguridad.
- Proyectos editables `.snps` con historial de versiones, rostro y vestuario compartiendo el borrador.
- Cambios de color de cabello con **Hair Color Safe Mode**: desde el primer cambio se copia el FaceGen ganador de MO2 y solo se reescribe el color de las shapes HairTint; un guard bloquea cualquier otra diferencia y el plugin exportado cambia únicamente `HCLF`. Un diagnóstico facial (FaceGen, mallas y TRI, sin fiarse de nombres) indica qué ediciones son seguras y bloquea las demás con su motivo, p. ej. sliders en cabezas cuyos TRI no coinciden con la malla ([compatibilidad facial](docs/HIGH_POLY_HEAD_COMPATIBILITY.md)). Probado con el replacer de Serana fuera del juego; verificación en Skyrim pendiente.
- Las configuraciones de material de prendas se conservan en el proyecto, pero aún no afectan al visor ni al juego. La exportación las bloquea hasta que puedan generar DDS y referencias NIF verificadas; ver [estado de materiales](docs/MATERIAL_VARIANTS.md).
- Visor Qt Quick 3D: NIF mediante nifly, recursos sueltos/BSA 105, DDS mediante DirectXTex, mezcla de peso y skinning CPU. Transparencias traducidas de la `NiAlphaProperty` y del alfa real de cada textura, FaceGen con tinte compuesto, formas de física SMP ocultas como en el juego. Cámara orbital, paneo, zoom, selección, ocultación, UV, huesos, comparación del atuendo original/borrador, inspector por geometría (origen MO2, shader, texturas, alfa, huesos, avisos) y vistas de diagnóstico (canal alfa, normales, tangentes, alambre, solo difusa, sin luz).
- Recursos incompatibles o ausentes generan diagnósticos. No reproduce shaders, animaciones ni física de Skyrim; el visor no certifica exportaciones faciales.
- **Character Forge** (tercer workspace, también sin perfil MO2): proyectos `.sncf` de NPC nuevos y vista previa 3D de modelos **GLB/glTF 2.0** validados antes de cargarlos (estructura, referencias, índices, rutas confinadas, límites), con encuadre, vistas de cámara, inspección multi-parte, selección por clic con caja delimitadora 3D y conmutación de visibilidad jerárquica. La biblioteca permite conservar enlaces externos o copiar modelos GLB/glTF y sus recursos validados dentro de `.sncf`, con rutas relativas, SHA-256 y deduplicación. Con Blender 4/5 instalado, el bridge convierte FBX/OBJ/BLEND a GLB mediante un script versionado, valida el resultado y lo incorpora a la biblioteca; admite cancelación y timeout. El visor se verificó tanto en compilación de desarrollo como en el paquete distribuible `dist/SkyrimNpcStudio`; la biblioteca y el bridge se verificaron con pruebas automáticas en Windows. Un modelo visible aún no está adaptado a Skyrim: conversión a NIF y FaceGen de NPC nuevos siguen pendientes ([registro](docs/CHARACTER_FORGE_IMPLEMENTATION_LOG.md)).

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

## Release

```powershell
.\scripts\release.ps1      # desde un commit sin cambios: paquete Release, ZIP y verificación
```

- Rechaza cambios sin commit en archivos versionados (`-AllowDirty` solo para pruebas, marca `-dirty`).
- `dist\SkyrimNpcStudio-<versión>-<commit>-win64.zip` + `.sha256`. Dentro: `BUILDINFO.json` (commit,
  MSVC, Qt, .NET SDK) y `release-manifest.json` (tamaño y SHA-256 de cada archivo). Los símbolos
  (`.pdb`) quedan fuera, en `dist\symbols\<commit>`.
- Reproducible: el paquete se compila con `/Brepro` y los archivos llevan la fecha del commit; dos
  reconstrucciones desde cero del mismo commit dieron el mismo ZIP byte a byte (2026-10-15).
- `scripts\verify-release.ps1 -Zip <zip>` (también lo llama `release.ps1`) comprueba archivos
  obligatorios (incluida `Qt6Test.dll`), que no haya restos de compilación, cada hash, y arranca la
  copia extraída con un entorno nuevo (sin Qt, MSVC ni SDK de .NET). Con `-Instance`, `-MO2Profile`,
  `-Npc` y `-Scenarios` además carga un perfil real en solo lectura.

### CI en GitHub Actions

`.github/workflows/windows.yml` no llega a ejecutarse: todas las ejecuciones (84 hasta el
2026-10-15) terminan en `startup_failure` sin que empiece ningún trabajo, también con un workflow
mínimo. Es una restricción de la cuenta, no del YAML. **Acción del propietario de la cuenta:**
revisar https://github.com/settings/billing (minutos de Actions, límite de gasto, pagos pendientes)
y *Settings → Actions → General* del repositorio; después, *Re-run* de la última ejecución. Hasta
entonces la verificación es local: `ctest`, `dotnet test`, el arnés y `release.ps1`.

## Pruebas

| Suite | Cómo | Qué cubre |
|---|---|---|
| .NET (105) | `dotnet test --solution SkyrimNpcStudio.slnx` | índice, receta facial, apariencia efectiva, overrides/conflictos, caché, validador, protocolo, exportación (unit + export) |
| C++ (138) | `ctest --preset debug` | IPC, MO2, proyectos, exportación, FaceGen, BSA/LZ4, DDS, NIF, escena 3D, validador glTF, informes de fallos y dependencias de compilación |
| Locales `[local]` (4) | variables `SNS_LOCAL_*` (ver [tests/README.md](tests/README.md)) | tu instancia real de MO2 (8.686 NPCs, 8.297 armaduras, 414 plugins, Lydia); se omiten si no están definidas |
| UI & Forge 3D | `.\scripts\smoke-test.ps1` / `.\scripts\local-review.ps1` | verificación de carga QML completa, renderizado nativo, captura gráfica y suite `-ForgePreview` con 6 modelos GLB/glTF |

La regresión de Character Creator se ejecuta en una ventana real con `--stress-appearance all` (32 escenarios): cambios rápidos de cabello, Hair Color Safe Mode, Easy Mod Creator (modos A/B/C y tamaños de ventana), Head Parts, sliders, tono de piel, donante, altura y peso, deshacer/rehacer exacto, cambio rápido de NPC, ciclo de proyecto, rechazo de material no exportable y exportación con reapertura. La guía y los resultados están en [docs/CRASH_DIAGNOSTICS.md](docs/CRASH_DIAGNOSTICS.md). Ninguna prueba automatizada afirma que un mod ya se comprobó dentro de Skyrim.

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
