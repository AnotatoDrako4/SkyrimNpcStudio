# Formato de exportación

Implementado: **Modo A — Outfit Patch** (`export.outfitPatch`). Los modos B (Face Overhaul), C (Complete NPC Overhaul) y D (Multi-NPC Pack) reutilizarán el mismo empaquetado; el contrato ya admite varias asignaciones por parche.

## Qué se escribe en el plugin

Por cada NPC:

1. **OTFT nuevo** con las piezas elegidas (referencias `ARMO`, en el orden del editor). EditorID `SNS_<EditorID del NPC>Outfit` salvo que se indique otro; se hace único dentro del parche.
2. **Override mínimo del NPC_** copiado del registro **ganador** del perfil (no del original), cambiando solo `DefaultOutfit` (y `SleepingOutfit` si se pide). Así se conservan los cambios de otros mods (cara, pelo, estadísticas…).

Nunca se modifican registros de otros plugins ni se copian ARMO/ARMA (modo dependiente).

### Masters

Los calcula Mutagen recorriendo todas las referencias del parche y se ordenan según el load order activo (`MastersListOrderingByLoadOrder`). Incluyen los plugins de las piezas **y** los que referencia el registro ganador copiado. Ejemplo real: Lydia con el reemplazo de Helen exige `Helen_Lydia_replacer.esp` como master porque su pelo y cabeza viven ahí.

> El parche no necesita *master* de los plugins que también modifican al NPC, pero **debe cargarse después de ellos** para ganar. El README generado lo indica.

### ESL

Con `eslFlag`, la cabecera lleva el flag `Small` (ESP-FE) y todos los registros nuevos deben tener ID local en `0x800–0xFFF`; si no, la exportación se rechaza. Se vuelve a comprobar tras escribir.

## Validación (bloqueante)

Antes de escribir, por cada asignación (`OutfitValidator`):

| Código | Severidad | Qué significa |
|---|---|---|
| `NpcNotFound`, `ArmorNotFound` | error | El FormKey no está en el load order activo |
| `SlotConflict` | error | Dos piezas comparten un slot biped (el juego equiparía solo una) |
| `NoAddonForRace` | error | Ningún ARMA cubre la raza del NPC ni su *armor race*: la pieza sería invisible |
| `OutfitFromTemplate` | error | El NPC hereda el inventario de su plantilla (flag *Inventory*): el atuendo se ignoraría |
| `DuplicateNpc` | error | Dos atuendos para el mismo NPC en un parche |
| `NoModelForSex`, `NoBipedSlots`, `AddonNotFound`, `DuplicatePiece`, `EmptyOutfit` | warning | Exportable, pero probablemente no deseado |
| `NonPlayable`, `SaveGamePersistence` | info | Informativo |

El estudio añade `MeshNotLoose` (warning) si una malla no existe como archivo suelto en el VFS (puede estar en un BSA). Con cualquier `error`, no se escribe nada.

## Verificación tras escribir

El ESP se **relee desde disco** (independiente del modelo en memoria) y se comprueba: masters presentes y en orden de carga, flag ESL, número de NPC/OTFT, que cada override apunta al OTFT nuevo, que los ítems coinciden con la petición, rango ESL, y que toda referencia externa se resuelve (`UnresolvedReference` como warning si el registro ganador ya traía referencias rotas).

## Estructura en disco

```
<carpeta elegida>/
├── SkyrimNpcStudio_Lydia/          ← copiable tal cual a mods/ de MO2
│   ├── SkyrimNpcStudio_Lydia.esp
│   ├── meta.ini                    ← gameName=SkyrimSE, installationFile=…zip
│   ├── README.md                   ← cambios, masters y su mod de MO2, instalación, limitaciones
│   └── manifest.json
└── SkyrimNpcStudio_Lydia.zip       ← ESP + README + manifest (sin meta.ini: MO2 crea el suyo)
```

FaceGen: los archivos de cara siempre se nombran según el plugin **de origen** del NPC y su FormID local (`meshes\actors\character\facegendata\facegeom\Skyrim.esm\000A2C8E.nif`), nunca según el parche. El Modo A no toca la cara, así que no genera FaceGen.

## Atomicidad

1. Comprobar que ni `<carpeta>/<Mod>` ni `<Mod>.zip` existen (nunca se sobrescribe: `1008 OutputExists`).
2. Construir todo en `<carpeta>/.sns-staging-<guid>/`.
3. Escribir el ESP, verificarlo, escribir README/manifest/meta.ini y el ZIP.
4. Confirmar con dos `rename` en el mismo volumen (atómicos). Si el segundo falla, se deshace el primero.
5. Ante cualquier error, se borra el *staging*.

Se rechazan destinos que contengan plugins activos (la `Data/` del juego o un mod instalado) y, desde el estudio, `overwrite/` y la carpeta del juego.

## `manifest.json` (formatVersion 1)

```json
{
  "format": "skyrim-npc-studio/export",
  "formatVersion": 1,
  "tool": { "name": "Skyrim NPC Studio", "workerVersion": "0.1.0", "protocol": "1.0" },
  "createdUtc": "2026-10-11T20:05:00.0000000Z",
  "mode": "outfit-patch",
  "plugin": { "fileName": "SkyrimNpcStudio_Lydia.esp", "sha256": "…", "eslFlagged": true },
  "masters": [ { "plugin": "Skyrim.esm", "providedBy": "<Game Data>", "loadIndex": 0 } ],
  "records": [ { "formKey": "000800:SkyrimNpcStudio_Lydia.esp", "recordType": "OTFT", "editorId": "SNS_HousecarlWhiterunOutfit", "kind": "new" } ],
  "assignments": [ { "npc": "0A2C8E:Skyrim.esm", "outfit": "000800:SkyrimNpcStudio_Lydia.esp", "armors": [ … ] } ],
  "dependencyMode": "dependent",
  "bundledAssets": [],
  "issues": [ … ],
  "inGameVerified": false
}
```

`inGameVerified` es siempre `false` al exportar: la comprobación dentro del juego la hace el usuario.

## Partidas guardadas

Un NPC ya cargado en una partida puede conservar su equipamiento hasta que el juego lo reinicialice. No se garantiza la aplicación inmediata en todas las partidas; se recomienda probar en una partida nueva o tras reinicializar al NPC por un medio soportado.
