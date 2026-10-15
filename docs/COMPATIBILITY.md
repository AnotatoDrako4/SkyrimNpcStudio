# Compatibilidad

| Componente | Versión verificada | Notas |
|---|---|---|
| Windows | 11 Pro 24H2 (10.0.26100) | Solo x64 |
| Skyrim | SkyrimSE.exe 1.6.1170 (runtime AE) | 1.5.x se detecta como SE; el estudio no depende de la versión del runtime |
| Mod Organizer 2 | 2.5.2 portable | Instancias globales: detección implementada, probada con fixtures |
| MSVC | 14.44.35207 (VS 2022 Build Tools 17.14) | C++20 |
| Qt | 6.11.2 msvc2022_64 | Quick, QuickControls2, Quick3D; prueba nativa D3D11 |
| .NET SDK | 10.0.401 | Runtime 10.0.x |
| Mutagen.Bethesda.Skyrim | 0.54.4 | `net10.0` |
| nifly | cca0a770094bb962fb28ea1fec5ea903e68fda8e | Lectura NIF SE, sin exportación geométrica |
| LZ4 | 1.10.0 | BSA 105 |
| DirectXTex | May 2026 | DDS a RGBA8; límites en NIF_PIPELINE.md |

## Cabezas personalizadas y High Poly

El color de cabello se edita sin regenerar geometría cuando existe un FaceGen ganador legible
(SSE) y al menos una shape usa el shader HairTint: solo cambian los 12 bytes del color de cada una,
y un guard bloquea la exportación si cambia cualquier otro dato. Sliders, presets y Head Parts
dependen de TRI cuyo número de vértices coincida con la malla; el diagnóstico facial lo comprueba
pieza a pieza y nunca deduce una cabeza «High Poly» por su nombre. Probado con el replacer de
Serana `Priscilla.Serana.Replacer.esp` (cabeza de 3832 vértices con TRI vanilla de 996): color
permitido, sliders y regeneración bloqueados. Comportamiento en el juego sin verificar. Consulta
[HIGH_POLY_HEAD_COMPATIBILITY.md](HIGH_POLY_HEAD_COMPATIBILITY.md).

## Plugins

- `.esm`, `.esp`, `.esl`; flags ESM y ESL (ESP-FE); masters ligeros resueltos con `MasterFlagsLookup`.
- Plugins localizados (`.STRINGS`) en el idioma de `sLanguage`.
- FormIDs mostrados como en xEdit: `00`–`FD` completos, `FExxx` ligeros.
- Límites: 254 plugins completos, 4096 ligeros (diagnosticados).

## Sistemas corporales (CBBE, 3BA, UNP, HIMBO…)

Aún **no** se detectan ni se adaptan mallas entre sistemas. La validación actual comprueba que el ARMA cubre la raza y que existe modelo para el sexo del NPC; no comprueba que la malla esté hecha para el cuerpo instalado. Planificado para la Fase 5 mediante adaptadores; nunca se convertirá automáticamente entre sistemas incompatibles.

## Idiomas

Interfaz en español. Nombres de registros en el idioma del juego. Los mensajes técnicos de validación del worker están en inglés (códigos estables para documentación y soporte).
