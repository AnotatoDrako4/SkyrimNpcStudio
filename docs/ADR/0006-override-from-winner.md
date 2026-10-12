# ADR 0006 — Los overrides se copian del registro ganador

**Estado:** aceptada (2026-10-11)

## Contexto

En el perfil real de prueba, Lydia (`0A2C8E:Skyrim.esm`) tiene cuatro versiones: Skyrim.esm → USSEP → NITHI NPCs → `Helen_Lydia_replacer.esp` (ganador, que aporta cara y pelo).

## Decisión

El override del parche se crea con `GetOrAddAsOverride(<registro ganador>)` y solo se cambia el campo editado. Nunca se parte del registro original.

## Consecuencias

- Se conservan las ediciones del ganador (cara, pelo, WNAM…). Partir del original revertiría la cara de Helen y produciría el error de cara oscura (la FaceGen instalada ya no coincidiría con el registro).
- El parche hereda los masters que el ganador referencia (aquí `Helen_Lydia_replacer.esp`).
- El parche debe cargarse **después** de los mods que modifican al NPC. Si el usuario cambia esos mods, debe volver a exportar (lo indica el README del mod).
- Las "ediciones perdidas" que el ganador ya arrastraba (p. ej. arreglos de USSEP que Helen no incluye) se muestran como conflictos en la ficha; resolverlas es trabajo de un parche de conflictos, fuera del Modo A.
