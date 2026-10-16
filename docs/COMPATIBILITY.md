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
| Blender (opcional) | 5.2.2 LTS | Puente de Character Forge: OBJ/FBX/BLEND → GLB (`[blender][local]`, 2026-10-15) |
| Pantalla | 1100×700 (mínimo de la ventana), 1280×720, 1600×900, 1920×1080 | Easy Mod Creator medido por el arnés; 2560×1440 sin probar (monitor de 1080p) |

Estados usados en esta página y en el [plan](DEVELOPMENT_PLAN.md): **VERIFICADO EN WINDOWS** (pruebas y
arnés sobre el perfil real), **VERIFICADO EN SKYRIM** (nada todavía), **PENDIENTE**, **BLOQUEADO**.

## NPC probados (perfil MO2 real, solo lectura)

| NPC | Origen / ganador | Qué se verificó en Windows |
|---|---|---|
| Serana `002B6C:Dawnguard.esm` | `Priscilla.Serana.Replacer.esp` (cabeza High Poly) | Color de cabello (Hair Color Safe Mode), Easy Mod A/B/C, ASan |
| Lydia `0A2C8E:Skyrim.esm` | `Helen_Lydia_replacer.esp`, atuendo Runes of Roots | Color, atuendo (conflicto de slot heredado como aviso), modos A/C, proyecto |
| Shady Sam `0370FB:alternate start - live another life.esp` | Alternate Start (FaceGen en BSA, 31.260 bytes tras el pie) | Color, atuendo, A/C con master conservado |
| Ingrid `01363F:Skyrim.esm`, Aslfur `0135E7:Skyrim.esm` | vanilla | Sliders, tono de piel, Head Parts, ojos, donante, altura y peso |

Ninguno está comprobado dentro de Skyrim: [SKYRIM_ACCEPTANCE_TEST.md](SKYRIM_ACCEPTANCE_TEST.md).

## Cabezas personalizadas y High Poly

El color de cabello se edita sin regenerar geometría cuando existe un FaceGen ganador legible
(SSE) y al menos una shape usa el shader HairTint: solo cambian los 12 bytes del color de cada una,
y un guard bloquea la exportación si cambia cualquier otro dato. Sliders, presets y Head Parts
dependen de TRI cuyo número de vértices coincida con la malla; el diagnóstico facial lo comprueba
pieza a pieza y nunca deduce una cabeza «High Poly» por su nombre. Probado con el replacer de
Serana `Priscilla.Serana.Replacer.esp` (cabeza de 3832 vértices con TRI vanilla de 996): color
permitido, sliders y regeneración bloqueados. Lo mismo con Helen (Lydia). Si la cabeza usa su propia textura de cara en lugar del FaceTint del NPC, el tono de piel y los tintes se bloquean y la ranura 6 se conserva. Comportamiento en el juego sin verificar. Consulta
[HIGH_POLY_HEAD_COMPATIBILITY.md](HIGH_POLY_HEAD_COMPATIBILITY.md).

## Plugins

- `.esm`, `.esp`, `.esl`; flags ESM y ESL (ESP-FE); masters ligeros resueltos con `MasterFlagsLookup`.
- Plugins localizados (`.STRINGS`) en el idioma de `sLanguage`. Al comparar un override con un master localizado, los textos se comparan por su valor resuelto (el parche no localizado guarda el texto).
- FormIDs mostrados como en xEdit: `00`–`FD` completos, `FExxx` ligeros.
- Límites: 254 plugins completos, 4096 ligeros (diagnosticados).
- Presets `.jslot` de RaceMenu con Head Parts de plugins ligeros (`0xFE LLL XXX`): se resuelven al FormID local `& 0xFFF`.
- FaceGen con bytes sobrantes tras el pie (p. ej. Alternate Start): la cabecera NIF se lee hacia delante y la cola se conserva.

## Sistemas corporales (CBBE, 3BA, UNP, HIMBO…)

Aún **no** se detectan ni se adaptan mallas entre sistemas. La validación actual comprueba que el ARMA cubre la raza y que existe modelo para el sexo del NPC; no comprueba que la malla esté hecha para el cuerpo instalado. Planificado para la Fase 5 mediante adaptadores; nunca se convertirá automáticamente entre sistemas incompatibles.

## Idiomas

Interfaz en español. Nombres de registros en el idioma del juego. Los mensajes técnicos de validación del worker están en inglés (códigos estables para documentación y soporte).
