# Verificación en Skyrim — «Usar el rostro de X»

Lo que el estudio **no** puede comprobar: si en el juego la cara de Bolli y Brand-Shei deja de verse oscura
o distinta con el parche. Todo lo demás está verificado en archivos (ver
`docs/plans/conflict-intelligence-hardening.md`). Este procedimiento es para una persona; el resultado
lo decide quien lo ejecuta. Complementa `docs/SKYRIM_ACCEPTANCE_TEST.md`.

## Kit

`out\skyrim-verification-kit\` (generado por el arnés `face-resolution` con el binario de la rama
`feature/conflict-intelligence-hardening`):

| Archivo | SHA-256 |
|---|---|
| `SNS_FaceFix_Bolli.zip` | `AEC389CFD2561BF509F9C8B7094AC957007CBF2A4E46321B0DAE0082891EA01A` |
| `SNS_FaceFix_BrandShei.zip` | `2835B0C01955C717D062C8AB479023914E3012D74A8C6A236F9D7260675B3657` |

Cada ZIP trae solo un plugin ESL (`SNS_FaceFix_<NPC>.esp`), `manifest.json` y `README.md`; ningún FaceGen.
Masters: `Skyrim.esm`, `NITHI NPCs - The Rift - Men.esp`, `AI Overhaul.esp`.

## Procedimiento (no tocar el perfil ni las partidas habituales)

1. Comprueba los hashes (`Get-FileHash <zip> -Algorithm SHA256`).
2. En MO2, **crea un perfil nuevo** copiando el tuyo (p. ej. `SNS-Verify`); no instales nada en el
   perfil habitual. Instala los dos ZIP como mods y colócalos **al final** del orden de mods y de plugins
   (deben cargar después de NITHI y de AI Overhaul).
3. Empieza una **partida nueva de prueba** (o una guardada exclusiva para pruebas), nunca sobre partidas
   personales. Ve a Riften (`coc RiftenOrigin` desde el menú principal).
4. Para cada NPC (Bolli, puesto de pescado; Brand-Shei, mercado):
   - con el parche **desactivado**: captura de frente con luz normal (cara y cuello);
   - con el parche **activado** (misma partida recargada): la misma captura.
5. Anota en la tabla. No hace falta interpretar: describe lo que se ve.

| NPC | Sin parche: ¿cara oscura / cuello de otro tono / rostro distinto? | Con parche: ¿igual? | Capturas |
|---|---|---|---|
| Bolli | | | |
| Brand-Shei | | | |

## Cómo leer el resultado

- **Mejora** (sin parche se ve mal, con parche bien): la resolución funciona para este caso.
- **Sin cambios** (bien en ambos): el desajuste del registro no tenía efecto visible; el parche es inocuo.
- **Peor o igual de mal**: no es válido; envía las capturas y el `manifest.json` del mod.

Hasta que alguien complete esta tabla, los parches siguen con `readyForGame: false` e
`inGameVerified: false`.
