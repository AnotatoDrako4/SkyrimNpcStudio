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

### NPC Safe Patch Engine (modos A, B y C)

Cada override de NPC releído se compara con su registro ganador **en todos sus campos** (raza,
clase, voz, estadísticas, IA, paquetes, inventario, facciones, hechizos, perks, plantilla, nombre…),
con la máscara de igualdad que Mutagen genera para `NPC_`, no con una lista escrita a mano
(`services/BethesdaWorker/PatchBuilder/NpcSafePatch.cs`). Solo se ignoran sellos de versión y flags
del registro (`VersionControl`, `FormVersion`, `Version2`, flags/compresión). Campos permitidos por
operación:

| Operación | Campos que puede cambiar |
|---|---|
| Atuendo (A, o el atuendo de C) | `DefaultOutfit` (DOFT), `SleepingOutfit` (SOFT) si se pidió |
| Rostro completo (B/C) | Solo los campos que el usuario cambió (`requestedFields`, calculados por el estudio comparando el borrador con el registro), dentro de `HairColor`, `FaceMorph`, `FaceParts`, `HeadParts`, `HeadTexture`, `Weight`, `Height`, `TextureLighting`, `TintLayers`; el worker escribe solo esos (sin `requestedFields`, todo el rostro) |
| Hair Color Safe Mode (`onlyFields`) | solo `HairColor` (y `Height` si se pidió `NAM6`) |

Cualquier otra diferencia → `UnauthorizedNpcChange` (error) y no se entrega nada. Un miembro de la máscara que no se puede interpretar cuenta como diferencia; uno que la máscara no informa se
vuelve a comparar con el valor completo de la propiedad (nunca se asume igualdad). Los textos traducibles (p. ej. `Name`) se comparan por su valor resuelto en el idioma del juego:
un master localizado (`Skyrim.esm`) guarda una referencia a STRINGS y el parche, no localizado, el texto; es el
mismo nombre en el juego (como cualquier parche no localizado, queda fijado en ese idioma). Las diferencias
permitidas forman el **diff semántico** (`semanticDiff`): campo, subregistro, valor original, valor
exportado, motivo y dependencias (FormKeys a los que apunta el valor nuevo; «(nuevo en este mod)»
si el registro lo crea el parche). Va en la respuesta RPC, en `manifest.json` y en el README.
Pruebas: `tests/export/SkyrimNpcStudio.ExportTests/NpcSafePatchTests.cs` (14 tipos de cambio ajeno
bloqueados, diff exacto en color de cabello y en atuendo).

### ZIP

El ZIP se reabre antes de entregarlo (`ModPackager.VerifyZip`): sin rutas absolutas, `..`, `\` ni
unidades; exactamente los archivos de la carpeta del mod (sin `meta.ini`), sin duplicados y con el
mismo SHA-256 que cada archivo. Si falla, no se mueve nada al destino.

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

## `manifest.json` (formatVersion 2)

```json
{
  "format": "skyrim-npc-studio/export",
  "formatVersion": 2,
  "tool": { "name": "Skyrim NPC Studio", "workerVersion": "0.1.0", "protocol": "1.0" },
  "createdUtc": "2026-10-11T20:05:00.0000000Z",
  "mode": "outfit-patch",
  "plugin": { "fileName": "SkyrimNpcStudio_Lydia.esp", "sha256": "…", "eslFlagged": true },
  "masters": [ { "plugin": "Skyrim.esm", "providedBy": "<Game Data>", "loadIndex": 0 } ],
  "records": [ { "formKey": "000800:SkyrimNpcStudio_Lydia.esp", "recordType": "OTFT", "editorId": "SNS_HousecarlWhiterunOutfit", "kind": "new" } ],
  "assignments": [ { "npc": "0A2C8E:Skyrim.esm", "outfit": "000800:SkyrimNpcStudio_Lydia.esp", "armors": [ … ] } ],
  "dependencyMode": "dependent",
  "bundledAssets": [],
  "semanticDiff": [ { "npc": "0A2C8E:Skyrim.esm", "npcEditorId": "HousecarlWhiterun",
      "changes": [ { "field": "DefaultOutfit", "subrecord": "DOFT", "original": "0A0010:Skyrim.esm",
                     "exported": "000800:SkyrimNpcStudio_Lydia.esp", "reason": "Atuendo por defecto asignado",
                     "dependencies": [ "000800:SkyrimNpcStudio_Lydia.esp (nuevo en este mod)" ] } ] } ],
  "files": [ { "path": "SkyrimNpcStudio_Lydia.esp", "sha256": "…", "bytes": 1234 } ],
  "verification": [ "Plugin releído con Mutagen tras escribirlo: …", "Cada NPC_ releído se comparó campo a campo …" ],
  "pendingVerification": [ "Probar el mod dentro de Skyrim, en un perfil de MO2 de prueba: …" ],
  "issues": [ … ],
  "inGameVerified": false
}
```

`inGameVerified` es siempre `false` al exportar: la comprobación dentro del juego la hace el usuario.
`files` no incluye `manifest.json`, `README.md` ni `meta.ini`. `verification` lista lo comprobado
antes de escribir el manifiesto; la respuesta RPC añade la verificación del ZIP (que el manifiesto,
al estar dentro del ZIP, no puede mencionar). El diálogo de exportación de Outfit Studio muestra
todo esto plegado en «Qué contiene este mod».

## Partidas guardadas

Un NPC ya cargado en una partida puede conservar su equipamiento hasta que el juego lo reinicialice. No se garantiza la aplicación inmediata en todas las partidas; se recomienda probar en una partida nueva o tras reinicializar al NPC por un medio soportado.
