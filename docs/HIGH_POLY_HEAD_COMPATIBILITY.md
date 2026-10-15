# Cabezas personalizadas, High Poly Head y Hair Color Safe Mode

Estado a 2026-10-14. Rama `fix/serana-hair-color-safe-mode`. Nada de este documento está
verificado dentro de Skyrim: las pruebas comparan archivos, registros y la vista previa del estudio.

## 1. Defecto informado

Al cambiar solo el color de cabello de Serana con un replacer de cabeza de alta resolución
(`Priscilla.Serana.Replacer.esp`, mod MO2 «ESP-FE Servant Priscilla high poly follower healer - …
replacer - Vampire form»), la cabeza podía quedar deformada tras exportar e instalar el mod.

## 2. Caso real medido (perfil `Default` del usuario, solo lectura)

Datos obtenidos con `sns_integration_tests "[serana]"` (sección 7) y el registro de la app:

| Dato | Valor |
|---|---|
| NPC | `DLC1Serana`, `002B6C:Dawnguard.esm` |
| Cadena de overrides | Dawnguard.esm → unofficial skyrim special edition patch.esp → **Priscilla.Serana.Replacer.esp** (ganador, índice 412) |
| FaceGen ganador | suelto, del mismo mod, 3.726.729 B, `meshes\actors\character\facegendata\facegeom\Dawnguard.esm\00002B6C.nif` |
| FaceTint ganador | suelto, del mismo mod, 1.048.704 B |
| Shapes del FaceGen | AAA_GirlHeads 3832 v · AAA_Hair 23320 · AAA_Hairline 23320 · AAA_Scalp 897 · FemaleEyesHumanHazelBrown 176 · AAA_FemaleBrows 371 · AAA_Horn 1162 · FemaleMouthHumanoidDefault 141 |
| Cabeza `AAA_GirlHeads` | malla `meshes\Priscilla body\Body\FemaleHead.nif` (3832 v); TRI de raza `FemaleHeadRaces.tri` y de chargen `FemaleHeadCharGen.tri` **vanilla, 996 v**; expresión `meshes\Priscilla body\Body\femalehead.tri` |
| Shapes HairTint | AAA_Hair, AAA_Hairline, AAA_Scalp, AAA_FemaleBrows |
| Regeneración desde el registro | desviación máx. 2,797 u en AAA_GirlHeads (media 0,44), boca 1,03, ojos 1,41, cejas 1,12; **no exportable** (`TriVertexMismatch` 996 ≠ 3832) |

Conclusión del análisis: geometría personalizada con morphs de raza y chargen incompatibles con
su malla. El nombre del archivo no interviene en esa conclusión.

## 3. Causa raíz

### Confirmado con los datos de Serana y el código

1. **Modo de geometría provisional.** `FaceState::geometry_mode` valía `"regenerate"` por defecto y
   pasaba a `"preserve"` solo cuando terminaba `analyse_existing_facegen()`, que es asíncrono y
   regenera la cabeza completa para comparar. `start_build()` capturaba el modo al editar, y el
   historial de deshacer/rehacer guardaba ese modo. Un cambio de color hecho antes de acabar el
   análisis, o restaurado después por deshacer/rehacer o al reabrir un proyecto guardado con ese
   modo, se construía con `Regenerate`: para Serana eso es la cabeza sin esculpido (hasta 2,8 u de
   diferencia, boca y ojos desplazados ~1 u). La vista previa mostraba esa cabeza; la exportación
   quedaba bloqueada por `TriVertexMismatch`.
2. **El primer cambio reconstruía el NIF.** Sin un `build_` previo, la ruta rápida de color no se
   usaba y se llamaba a `build_facegen()` en `PreserveExisting`. Las posiciones de vértices se
   conservaban (diferencia 0 en las 8 shapes), pero el archivo era otro: raíz y estructura
   recreadas (desaparece un nodo sin nombre del original) y la ranura 6 del shader de
   `AAA_GirlHeads` pasaba de `textures\priscilla body\body\01_priscilla.dds` (la del mod) a la ruta
   FaceTint genérica. Son cambios no pedidos sobre recursos del replacer.
3. **`only_hair_color_differs()` comparaba también `source` y `geometry_mode`**, de modo que una
   diferencia de procedencia o de modo sacaba un cambio de color de la ruta de color.
4. **La exportación reescribía todos los campos del rostro** (NAM9, NAMA, peso, QNAM, tintes,
   PNAM, FTST) desde los valores leídos por el estudio, aunque solo hubiera cambiado `HCLF`.

### Pendiente (hipótesis no verificada)

Con este perfil no se reproduce una deformación **geométrica** en el NIF exportado por la ruta
antigua: cuando la exportación era posible, las posiciones coincidían. La deformación vista en el
juego puede venir de una versión anterior del estudio que no bloqueaba `TriVertexMismatch`, de
otro orden de carga o de recursos de otro perfil. Queda pendiente reproducirla dentro de Skyrim; no
se declara resuelta hasta comprobarlo allí.

## 4. Corrección

### Hair Color Safe Mode (`engines/face`, `AppearanceEditor`)

- La decisión de qué construir es una función pura, `sns::appearance::face_build_kind()`:
  `Unchanged`, `WaitForAnalysis`, `HairColorSafe` (solo cambia HCLF en los datos del registro y el
  modo es el mismo; `source` no cuenta) o `Full`.
- El modo de geometría es `"pending"` hasta que termina el análisis. Mientras tanto
  `AppearanceEditor::edit()` rechaza ediciones y no se construye nada (propiedad QML `analysing`).
  Al reabrir un proyecto, el modo vuelve a decidirlo el análisis; si el proyecto pedía
  `"regenerate"`, se avisa para que el usuario lo elija de nuevo.
- `face::patch_hair_tint()` ya no reserializa el NIF: copia el FaceGen ganador byte a byte y
  reescribe solo los 12 bytes del color de cada shader HairTint. La posición sale de los tamaños de
  bloque del encabezado contados desde el pie del archivo (`hairTintColor` es el último campo de
  `BSLightingShaderProperty` tipo 6 en SSE, según `nifly` `Shaders.cpp`), y se comprueba contra el
  valor que nifly lee; si no coincide → `HAIR_TINT_LAYOUT_UNVERIFIED` y no se cambia nada.
- Siempre parte del FaceGen ganador de MO2 (suelto o BSA), también en el primer cambio y en cada
  uno de los siguientes. No hay *fallback* a `build_facegen()`: sin FaceGen →
  `FACEGEN_ORIGINAL_REQUIRED`; ilegible → `FaceGenUnreadable`.
- Sin shapes HairTint → `HAIR_TINT_NOT_APPLIED` (error) y `HairTintUnsupported` por pieza; no se
  convierte el shader ni se declara un cambio de color. La alternativa es editar la textura o el
  material del cabello aparte.

### FaceGen Geometry Integrity Guard (`face::verify_hair_tint_integrity`)

Tres niveles: (1) bytes: mismo tamaño y todo byte distinto dentro de un color HairTint del original;
(2) semántica por shape, emparejada por nombre: tipo de bloque, flags, transformación, vértices,
posiciones, triángulos e índices, normales, UV, tangentes, colores de vértice, huesos, pesos,
transformaciones de skin, particiones, tipo de shader, texturas 0–8, alfa, y transformaciones de
nodos; (3) si los bytes difieren sin diferencia semántica, reserialización canónica de ambos con
los tintes igualados. Cada diferencia informa pieza, propiedad, valor anterior y nuevo; el mensaje
`FACEGEN_GEOMETRY_CHANGED` nombra la operación (Hair Color Safe Mode o exportación). El guard se
aplica al construir y otra vez sobre los bytes que se van a exportar.

### Exportación mínima (worker)

`FaceAssignment.OnlyFields`: el estudio pide `["HCLF"]` (y `"NAM6"` si cambió la altura). El worker
copia el registro ganador, escribe solo esos campos (CLFM reutilizado o nuevo) y, tras releer el
plugin con Mutagen, exige que los campos faciales distintos del ganador sean exactamente esos
(`MinimalChangeViolated`, `OnlyFieldsUnsupported`). El manifiesto lista `changedFields`.

### Diagnóstico de compatibilidad (`face::diagnose_face_compatibility`)

Clasifica con evidencia: FaceGen ganador (legible, shapes, HairTint), vértices de cada malla frente
a cada TRI, mallas o TRI ausentes, auditoría FaceGen/registro y desviación de una regeneración.

| Clasificación | Permitido | Bloqueado |
|---|---|---|
| Geometría compatible con el registro | color, vestuario, sliders, Head Parts, regenerar | — |
| Geometría personalizada: edición limitada | color, vestuario, sliders (como diferencia), Head Parts | regenerar |
| Geometría personalizada con morphs incompatibles (Serana) | color, vestuario | sliders/presets, Head Parts, regenerar |
| FaceGen ausente | vestuario; sliders/Head Parts/regenerar si la cabeza se construye | color |
| FaceGen inconsistente | vestuario; color si es legible y tiene HairTint | sliders, Head Parts, regenerar |
| Dependencias faltantes | color, vestuario | sliders, Head Parts, regenerar |

La UI (tarjeta «Protección facial» en Character Creator) muestra la clasificación, cada operación
con ✓/✕ y su motivo, la procedencia MO2 del ESP ganador, del FaceGen y del FaceTint, un aviso si el
ESP ganador y el FaceGen vienen de mods distintos (no se reordena nada) y «Pendiente de verificación
en Skyrim». Los sliders, el color o los Head Parts bloqueados se desactivan y `AppearanceEditor`
rechaza la edición con el motivo.

## 5. Archivos

`engines/appearance/{include/sns/appearance/appearance_draft.hpp, src/appearance_draft.cpp}`,
`engines/face/{include/sns/face/facegen.hpp, src/facegen.cpp}`,
`apps/desktop/viewmodels/appearance_editor.{hpp,cpp}`,
`apps/desktop/qml/charactercreator/{FaceProtectionCard, HairEyeCustomizer, FaceCustomizer}.qml`,
`apps/desktop/components/appearance_stress.cpp`,
`services/BethesdaWorker/{Contracts/OutfitDtos.cs, PatchBuilder/FacePatchBuilder.cs}`,
pruebas en `tests/unit/engines/test_facegen.cpp`, `tests/unit/engines/test_appearance_draft.cpp`,
`tests/export/SkyrimNpcStudio.ExportTests/FacePatchExportTests.cs`,
`tests/integration/test_local_facegen.cpp`.

## 6. Pruebas

Sintéticas (sin recursos de terceros): ver `docs/DEVELOPMENT_PLAN.md`, sección «Protección facial»,
para la lista y los resultados de la última ejecución. Locales con el perfil real (no versionan
bytes del mod; `SNS_LOCAL_DUMP_DIR` solo guarda metadatos):

```powershell
. .\scripts\dev-env.ps1   # imprescindible: el worker necesita el .NET 10 de dev-env
$env:SNS_LOCAL_MO2_INI = 'D:\Apps\ModOrganizer.ini'; $env:SNS_LOCAL_PROFILE = 'Default'
.\build\debug\bin\sns_integration_tests.exe "[serana]"
.\scripts\smoke-test.ps1 -Instance 'D:\Apps\ModOrganizer.ini' -MO2Profile Default -Npc '002B6C:Dawnguard.esm' `
  -StressAppearance 'hair-single,replacer-facegen,hair-safe,early-edit,sliders,undo-redo,export-reopen' -ShowWindow
```

Las exportaciones de prueba van a `out\stress-work` (ignorado por git), nunca a la instancia MO2 ni
a `Skyrim/Data`.

## 7. Limitaciones y problemas abiertos

- **Corrupción del montón al cerrar (no reproducida, sin ASan):** una ejecución del arnés sobre
  Serana acabó, en un árbol Debug con 133 objetos sin dependencias de cabeceras y sin los
  indicadores por defecto de MSVC, con «HEAP CORRUPTION DETECTED … wrote to memory after end of
  heap buffer». Tras reconstruir el árbol correctamente (ver `docs/CRASH_DIAGNOSTICS.md`,
  actualización 2026-10-15) el mismo arnés terminó dos veces en `summary: PASSED` (410
  comprobaciones, 0 fallos) y cerró sin aviso. Causa más probable: objetos compilados contra otra
  disposición de `FaceGenBuild`/`FaceState`; no está demostrada. Pendiente: una ejecución con el
  preset `asan` como verificación definitiva.
- **Guardar y reabrir (resuelto en el árbol corregido):** el cambio de color de Serana tras reabrir el
  proyecto se construía como `full`; en el árbol reconstruido el escenario `export-reopen` da
  `build kind hair-safe`, integridad `hair-tint-only` y FaceGen exportado = ganador salvo HairTint
  (3.726.729 B). La prueba unitaria con datos tipo Lydia ya daba `HairColorSafe`.
- Verificación dentro de Skyrim: pendiente (manual).
- Solo NIF SSE (`BSLightingShaderProperty` tipo 6); otras disposiciones se rechazan sin cambios.
- El FaceGen exportado deriva del FaceGen del replacer: es para uso personal salvo permiso del
  autor; el manifiesto lo registra en `derivedFrom`.
- Un cambio de Head Parts en cabezas con morphs incompatibles se bloquea por no estar verificado.

## 8. Recuperación

Si un mod exportado con una versión anterior deforma la cabeza: desactivar ese mod en MO2 (el
replacer original no se modificó nunca: el estudio no escribe en su carpeta ni en `Skyrim/Data`),
y volver a exportar el color con esta versión. El nuevo NIF es el del replacer con otro color.
