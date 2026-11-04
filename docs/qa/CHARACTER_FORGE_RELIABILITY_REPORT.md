# Character Forge — informe de fiabilidad QA

Fecha de ejecución: 2026-11-03 (zona America/Santiago)  
Rama: `main`  
Checkout: `D:\.Proyectos Games\SkyrimNpcStudio-ui2`  
Build probado: `build/claude-forge`

## Alcance

Este incremento añade un runner de auditoría independiente. No modifica `forge_skyrim.py`, `skyrim_conversion.cpp`, `CharacterForgeViewModel`, `tests/CMakeLists.txt` ni el contrato skinned que está desarrollando Claude.

Archivos añadidos:

- `scripts/qa/Invoke-CharacterForgeReliability.ps1`: ejecuta filtros directos y CTest, guarda logs, compara la fixture original y verifica que la salida no esté bajo `Skyrim/Data` ni MO2.
- `tests/qa/character_forge_coverage.json`: matriz reproducible de cobertura y brechas.
- `out/local-implementation/forge-skyrim-qa/`: logs y resumen de la ejecución local.

## Resultados

| Ejecución | Resultado |
|---|---:|
| Conversión directa `[forge][skyrim]` | PASS — 9 casos, 161 aserciones |
| Paquete Forge `[forge][skyrim]` | PASS — 8 casos; 5 casos Blender reales SKIP por fixtures no configuradas |
| CTest conversión Skyrim | PASS — 6/6 |
| CTest aplicación Forge | PASS — 9/10; 1 SKIP por Blender/PyNifly no configurados |
| Filtros directos NPC Unicode | PASS — 2 casos, 52 aserciones |
| Filtros directos Serana Unicode | PASS — 7 casos, 205 aserciones |
| CTest Unicode | REGRESIÓN — 9/9 fallan antes de entrar al caso |

La fixture `tests/fixtures/models/skyrim/goblet_report.json` mantuvo SHA-256:

`A3BFE94AB6BDB90CA1B795BB9E7F8F01032C7FEC9ADFAA655A98AF3B2BED261C`

## Defecto CTest/Unicode

CTest registra correctamente nombres como `NPC-08 — Generalización...`, pero al invocar Catch2 entrega el filtro como texto corrupto (`�`, `Ã³`, etc.). Catch2 responde `No test cases matched` y no ejecuta el cuerpo de la prueba.

La solución mínima aplicada en este ámbito es operacional: el runner usa filtros ASCII directos (`NPC-0*`, `SERANA-0*`) como evidencia de ejecución real y conserva el CTest Unicode como regresión explícita. No se renombraron pruebas ni se modificó infraestructura compartida.

## Brechas pendientes

- Geometría degenerada: sin regresión dedicada de Character Forge.
- Timeout/Blender colgado: documentado, pero sin prueba acotada específica.
- ZIP truncado/incompleto: falta una prueba aislada de rechazo.
- Modelo grande: existe el límite de vértices, pero falta fixture de presupuesto de recursos.
- Skinned: existen pruebas de pesos no válidos y huesos no reconocidos, pero están condicionadas a `SNS_LOCAL_SKINNED_FIXTURES`; el contrato completo permanece bajo responsabilidad de Claude.
- Validación visual real en ventana y aceptación dentro de Skyrim: no demostradas por este runner.

## Comando reproducible

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\qa\Invoke-CharacterForgeReliability.ps1
```

Resumen generado: `out/local-implementation/forge-skyrim-qa/summary.json`.  
Logs completos: `out/local-implementation/forge-skyrim-qa/*.log`.

## Estado y rollback

Estado: `INTEGRACION_WINDOWS_OK` para las pruebas ejecutadas; `PENDIENTE_SKYRIM` para compatibilidad dentro del juego.

No se hizo commit ni push. Para retirar este incremento local, eliminar únicamente los archivos nuevos listados arriba; no ejecutar reset, checkout ni borrar builds compartidos.
