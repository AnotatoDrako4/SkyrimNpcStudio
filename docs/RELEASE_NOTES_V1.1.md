# Skyrim NPC Studio Ultimate 1.1.0

Rama `release/v1.1`. Integra en una sola versión la cadena de PR apilados #8 → #19 (la punta, #19,
contiene a todos los anteriores) y la corrección de dependencias #20. Versión anterior: 0.1.0 (beta
candidata 1, PR #14).

**Estado:** integración local compilada, probada en Windows y empaquetada. Los PR que incluye siguen
**pendientes de revisión** por el dueño y nada se ha comprobado **dentro de Skyrim**; los NPC nuevos y
los parches exportados mantienen `readyForGame: false` / `inGameVerified: false`. No es una release
publicada en GitHub.

## Qué incluye

| PR | Contenido |
|---|---|
| #8 | Safe Patch Engine: escribe solo los campos pedidos; lectura conservadora de máscaras |
| #9 | Paquete de prueba en Skyrim (Serana, Lydia, Shady Sam) |
| #10 | Easy Mod Creator de 1100 a 1920 px (maquetación medida) y aviso de dependencias |
| #11 | Estabilidad: deshacer/rehacer exacto, cambio rápido de NPC, ciclo de proyecto (ASan limpio) |
| #12 | Character Forge: integra los PR #5 y #6 verificados |
| #13 | Release reproducible desde `dist`, ZIP verificado |
| #14 | Documentación sincronizada, beta candidata 1 (0.1.0) |
| #15 | Protección facial (donante, presets, proyectos), prioridad del FaceGen en MO2 (`installAbove`), filtro por mod en prendas |
| #16 | Visor: piel del cuerpo coherente con la cara (SkinTint) |
| #17 | Visor: normal maps `_msn`, normales geométricas, tinte de pelo por color de vértice |
| #18 | Mapa de control de la apariencia (pestaña Origen) y análisis de todo el perfil |
| #19 | «Usar el rostro de X»: parche asistido desde el mapa de control |
| #20 | Dependencias: MessagePack 2.5.305 (NU1902, GHSA-qhrr-8q5h-9q3h) |

## Sistemas de seguridad (sin cambios)

Hair Color Safe Mode, FaceGen Integrity Guard, NPC Safe Patch Engine, bloqueos de cabezas High Poly,
`installAbove`. El estudio no modifica Skyrim/Data ni los perfiles de MO2.

## Cómo se comprobó

Ver la sección «Verificación 1.1.0» de `docs/plans/next-generation-development.md` (compilación limpia,
ctest, pruebas .NET, arnés sobre el perfil MO2 real en solo lectura con hash del perfil antes y después,
ZIP generado dos veces).
