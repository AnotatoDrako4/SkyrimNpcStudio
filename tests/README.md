# Pruebas

| Carpeta | Tipo | Ejecuta |
|---|---|---|
| `unit/engines` | Catch2, sin procesos externos | `ctest --preset debug -R unit:` |
| `unit/BethesdaWorker.Tests` | xUnit v3 (Microsoft.Testing.Platform) | `dotnet test --solution SkyrimNpcStudio.slnx` |
| `export/SkyrimNpcStudio.ExportTests` | xUnit v3: ESP generado releído con Mutagen, ZIP, atomicidad | idem |
| `integration` | Catch2: worker real lanzado con Job Object, named pipe real, verificación del dueño del pipe, caso de uso completo | `ctest --preset debug -R integration:` |
| `fixtures` | Biblioteca .NET que genera con Mutagen un load order sintético (Skyrim.esm sintético, dos overrides de Lydia —uno pierde la edición del otro— y un pack de armaduras ESL) | usada por todo lo anterior y por `tools/FixtureGen` |

## Pruebas locales con tu instalación real

Se registran en CTest y se marcan *Skipped* si no hay configuración. Nunca modifican MO2 ni Skyrim.

```powershell
$env:SNS_LOCAL_MO2_INI    = 'D:\Apps\ModOrganizer.ini'      # obligatorio
$env:SNS_LOCAL_PROFILE    = 'Default'                        # opcional (por defecto, el perfil seleccionado)
$env:SNS_LOCAL_GAME_DIR   = 'D:\SteamLibrary\steamapps\common\Skyrim Special Edition' # opcional si el juego fue movido
$env:SNS_LOCAL_CACHE_DIR  = "$PWD\out\cache"                 # opcional: reutiliza el índice entre ejecuciones
$env:SNS_LOCAL_EXPORT_DIR = "$PWD\out\local-exports"        # opcional: exporta un parche de Lydia ahí
$env:SNS_LOCAL_OUTFIT     = 'ArmorSteelCuirassA,ArmorSteelBootsA,ArmorSteelGauntletsA'  # EditorIDs
.\build\debug\bin\sns_integration_tests.exe "[local]"
```

Resultado de referencia (2026-10-11, perfil real "Default"): 415 plugins activos, 84 619 archivos en el VFS, 8 686 NPC, 8 297 ARMO, 1 033 OTFT, 3 *issues* (dos registros malformados en `Follower_Isis.esp` y nombres ilegibles por un BSA del CC dañado).

## Datos

El test local también construye la escena real de Lydia y comprueba FaceGen de origen, geometrías, texturas y esqueleto. `scripts/smoke-test.ps1 -VerifyViewport` comprueba imagen y controles nativos; ver `docs/NIF_PIPELINE.md`.

No se incluye ningún asset propietario. Los IDs de algunos registros sintéticos imitan a los vanilla (Lydia `0A2C8E`, DefaultRace `000019`) para que los escenarios se lean como reales; el `Skyrim.esm` sintético se escribe desactivando la comprobación de rango bajo de FormID solo para él.
