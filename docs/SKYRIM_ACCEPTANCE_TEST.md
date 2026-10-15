# Prueba de aceptación dentro de Skyrim (Serana, Lydia, Shady Sam)

Estado: **PENDIENTE**. Nadie ha cargado todavía estos mods en Skyrim. Todo lo que figura abajo como
comprobado se hizo en Windows (arnés de la app sobre el perfil MO2 real y relectura con Mutagen), no
dentro del juego. Esta página no cambia de estado sin capturas y notas de una partida real.

## Los paquetes

Se generan con el arnés de la app sobre el perfil MO2 `Default` (no se escribe nada en Skyrim/Data ni
en el perfil):

```
scripts\smoke-test.ps1 -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc 002B6C:Dawnguard.esm -StressAppearance hair-safe -StressWork <carpeta>
scripts\smoke-test.ps1 -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc 0A2C8E:Skyrim.esm -StressAppearance easy-mod-outfit -StressWork <carpeta>
scripts\smoke-test.ps1 -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default -Npc "0370FB:alternate start - live another life.esp" -StressAppearance easy-mod-outfit -StressWork <carpeta>
```

Copias del 2026-10-15 en `out\skyrim-acceptance\` (no versionadas):

| ZIP | Plugin | Cambios en el `NPC_` (diff semántico del manifiesto) | FaceGen | Masters |
| --- | --- | --- | --- | --- |
| `1_Serana_HairColorOnly.zip` | `SNS_HairSafe.esp` | `HairColor` (HCLF) | NIF ganador del replacer con solo el color HairTint cambiado (3.726.729 bytes, mismo tamaño) | Skyrim, Dawnguard, `Priscilla.Serana.Replacer.esp` |
| `2a_Lydia_ModeA_Outfit.zip` | `SNS_EasyOutfit.esp` | `DefaultOutfit` (DOFT) | ninguno | Skyrim, `[Amsedillir] Runes of Roots.esp`, `Helen_Lydia_replacer.esp` |
| `2c_Lydia_ModeC_HairOutfit.zip` | `SNS_EasyComplete.esp` | `HairColor`, `DefaultOutfit` | NIF ganador de Helen con solo el HairTint cambiado | igual que 2a |
| `3a_ShadySam_ModeA_Outfit.zip` | `SNS_EasyOutfit.esp` | `DefaultOutfit` | ninguno | Skyrim, `alternate start - live another life.esp` |
| `3c_ShadySam_ModeC_HairOutfit.zip` | `SNS_EasyComplete.esp` | `HairColor`, `DefaultOutfit` | NIF ganador (con los 31.260 bytes sobrantes conservados) con solo el HairTint cambiado | igual que 3a |

SHA-256 de los ZIP: Serana `20693bfe…b841f`, Lydia A `0da2b906…3aab5c`, Lydia C `547dfe76…3e23dd`,
Shady Sam A `01d015f6…c97c935`, Shady Sam C `a6da11f8…c4756` (valores completos en
`out\skyrim-acceptance\SHA256SUMS.txt`).

Ya comprobado en Windows por el arnés y el worker, en cada paquete: el plugin se relee con Mutagen
(masters presentes y en orden, referencias resueltas); cada `NPC_` se compara campo a campo con el
registro ganador y solo difiere lo pedido; el ZIP se abre y contiene exactamente los archivos del
manifiesto. FaceGen frente al ganador real, mismo tamaño y solo cambian los colores HairTint:
Serana, comprobación `hair-tint-only` del arnés (4 formas); Lydia C, comparación byte a byte con el NIF
de `Farodadestin` (ganador por prioridad sobre NITHI y D'Ibella): 44 bytes en 4 bloques; Shady Sam C,
comparación con el NIF extraído del BSA de Alternate Start (`sns-probe bsa-extract`): 55 bytes en 5
bloques, cola sobrante idéntica.
Avisos esperados en los manifiestos: `SaveGamePersistence` (todo cambio de atuendo) y, en Lydia,
`SlotConflictInherited` ×2 (el atuendo de Helen ya trae Torso V1 y V2 en la misma ranura; no lo
introduce el estudio).

## Instalación (perfil MO2 de prueba)

1. En MO2, crea un perfil nuevo copiando `Default` (Perfiles → Copiar). No pruebes en `Default`.
2. Instala cada ZIP con «Instalar un mod desde archivo». Los ZIP 2a y 3a usan el mismo nombre de
   plugin, y también 2c y 3c, así que se prueban en dos rondas:
   - **Ronda 1:** `1_Serana_HairColorOnly`, `2c_Lydia_ModeC_HairOutfit`, `3a_ShadySam_ModeA_Outfit`.
   - **Ronda 2:** desactiva los de la ronda 1 y activa `2a_Lydia_ModeA_Outfit` y `3c_ShadySam_ModeC_HairOutfit`.
3. Prioridad: cada mod del estudio **por encima** del replacer del mismo NPC (Priscilla, Helen) en el
   panel izquierdo, para que su FaceGen gane. En la pestaña «Conflictos» del mod debe aparecer
   ganando el `.nif` de la ruta `facegeom` del NPC; si otro mod lo sobrescribe, la prueba no vale.
4. El plugin va después de sus masters (MO2/LOOT lo hacen solos); comprueba que no aparece el aviso de
   master ausente.

## Cómo ver al NPC con el mod aplicado

Un NPC que ya estaba cargado en una partida puede conservar su equipo anterior (`SaveGamePersistence`).
Para la prueba usa una partida en la que el NPC no se haya visto todavía, o crea una copia nueva con la
consola (y no guardes después):

- Serana: `player.placeatme 02002B6C` (si Dawnguard no está en el índice 02, búscalo con `help Serana 4`).
- Lydia: `player.placeatme 000A2C8E`.
- Shady Sam: `help "Shady Sam" 4` para obtener el ID base con el índice de carga, luego `player.placeatme <ID>`.

Para comparar, repite con el mod del estudio desactivado y la misma partida.

## Lista de comprobaciones

Para cada NPC y ronda, anota sí/no y guarda una captura (cara de frente y de perfil, cuerpo entero):

- [ ] La cara tiene la misma forma que sin el mod (sin deformaciones, ojos y boca en su sitio).
- [ ] No hay «cara oscura» (cabeza de otro color que el cuello) ni costura en el cuello.
- [ ] El color del pelo, las cejas y la línea del pelo coincide con la vista previa del estudio (solo
      Serana, Lydia C y Shady Sam C; en 2a y 3a el pelo debe quedar como sin el mod).
- [ ] La textura de la cara es la del replacer (Priscilla, Helen), no una genérica.
- [ ] El atuendo nuevo está equipado (Lydia: piezas de Runes of Roots; Shady Sam: Black Robes y Shrouded
      Hood). En Serana el atuendo no cambia.
- [ ] Lydia: con Torso V1 y V2 en la misma ranura solo se ve una pieza; anota cuál (conflicto heredado).
- [ ] Ningún otro aspecto del NPC cambió (voz, nombre, diálogo, IA, facción).
- [ ] El juego no se cierra al cargar, al acercarse al NPC ni al cambiar de celda.

Envía las capturas y las respuestas; con ellas la fila correspondiente del plan pasa a VERIFICADO EN
SKYRIM o se abre el fallo.
