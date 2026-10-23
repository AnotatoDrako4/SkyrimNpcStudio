# Character Forge — comportamiento de seguidor (AG04)

Qué hace y cómo se comprueba. Informe de la misión: `out/ag04-onepass/ag04-20261021-2344/FINAL_REPORT.md`.

## Flujo

`StatsEditor.qml` (pestaña **Datos**) → `CharacterForgeViewModel` → `WorkerClient` (JSON-RPC) → `BethesdaWorker` → `Behavior/BehaviorRpc.cs` → motores del lab sobre la **sesión indexada** del perfil MO2 activo (`Catalog.BehaviorContext`).

| Control | Llamada | Resultado |
|---|---|---|
| (aparece la pestaña) | `creation.getBehaviorCatalog` | arquetipos, clases, estilos y voces del perfil, con la huella del perfil |
| Vista previa | `creation.applyArchetype` con `dryRun` | lista de cambios; **no toca el borrador** |
| Aplicar | de nuevo `applyArchetype` y, si el borrador y el perfil siguen siendo los mismos y no hay campos protegidos distintos, edita el borrador | **un solo paso** deshacible |
| Deshacer / Rehacer | pila del proyecto (`ForgeDraft`) | borrador anterior / aplicado exactos |
| voz, sexo o facciones cambian | `creation.analyzeVoice` | `forge.voiceAnalysis` (evidencia estática de las FLST del perfil) |
| Validar NPC | `creation.validateNpc` + `creation.validateBehavior` | diagnóstico combinado; la exportación jugable sigue bloqueada |

Cada botón deshabilitado dice por qué (sin proyecto, perfil sin indexar, worker antiguo, arquetipo con registros que faltan, cálculo en curso).

## Qué puede cambiar un arquetipo

Clase, estilo de combate, estadísticas, habilidades, equipo, hechizos, perks, facciones y paquetes. **Nunca**: nombre, EditorID, descripción, raza, sexo, voz, altura/peso, cara, partes de cabeza, color de pelo, textura de cabeza, nivel y escalado, ubicación. El worker no los modifica y el estudio lo vuelve a comprobar antes de editar el borrador.

## Datos reales, no sintéticos

- Las plantillas usan FormKeys de los masters oficiales (comprobado por EditorID en el Skyrim instalado). `RealGameBehaviorTests` lo vigila leyendo los masters en solo lectura.
- Skyrim no tiene FLST `VoicesFollower`/`VoicesMarriage`: tiene `VoicesFollowerAll`, `…Neutral`, `…Rough`, `…Orc` y `VoicesMarriageAll`, `…Neutral`, `…Rough`, `…Arrogant`, `…Nord`, `…Orc`, `…Argonian` (las `…Adoption*` son de adopción). `VoiceDialogueAnalyzer` busca la familia.
- La evidencia es **estática** (`STATIC_EVIDENCE`, `MOD_DEPENDENT`, `UNKNOWN`, …): no garantiza reclutamiento ni matrimonio en el juego (`REQUIRES_IN_GAME_TEST`).

## Cómo probar

```powershell
dotnet test --project tests\unit\BethesdaWorker.Tests            # incluye RealGameBehaviorTests (masters reales, solo lectura)
ctest --test-dir build\<carpeta-de-build> -R refint:            # incluye los casos de json_bridge
scripts\smoke-test.ps1 -Binary <exe> -Instance <ModOrganizer.ini> -MO2Profile <perfil> -Npc 0A2C8E:Skyrim.esm `
  -WorkspaceIndex 0 -Size 1366x768 -StressAppearance forge-behavior -StressWork <carpeta> -ShowWindow
```

Escenarios del arnés (no están en `all`): `forge-behavior` (clics reales sobre el perfil indexado), `forge-behavior-edge` (perfil que cambia en vuelo, cancelación, arquetipo inexistente, lista sobredimensionada, ida y vuelta `.sncf`), `forge-behavior-perf` (mediana y p95).
