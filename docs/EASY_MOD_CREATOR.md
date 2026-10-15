# Creador de mods — Modo fácil (Easy Mod Creator)

Estado (2026-10-15): **IMPLEMENTADO, VERIFICADO EN WINDOWS** con el arnés automático; **sin revisión
visual por una persona** y **nada verificado dentro de Skyrim**.

El asistente no es un cuarto editor: es el diálogo «🚀 Exportar Mod…» (barra superior y Character
Creator), que ahora guía la exportación en cinco pasos sobre lo que ya editaste en Character Creator
y Outfit Studio, y escribe el mod con sus exportadores existentes.

## Cómo decide

`sns::appearance::plan_easy_mod()` (`engines/appearance/src/easy_mod.cpp`, función pura) recibe el
estado de los editores y devuelve el **modo**, los **cambios** que contendrá el mod y las
**comprobaciones**. `AppearanceEditor.easyModPlan` lo expone a QML y se recalcula con cada cambio del
rostro, del informe FaceGen, de la validación del atuendo o del estado de exportación.

| Lo editado | Modo | Exportador |
|---|---|---|
| Solo color de cabello | B, cambio mínimo (`onlyFields = [HCLF]`, Hair Color Safe Mode) | `AppearanceEditor::exportFaceOverhaul` |
| Rostro (sliders, presets, piezas, tintes) y/o altura | B | `AppearanceEditor::exportFaceOverhaul` |
| Solo atuendo (piezas distintas de las del NPC) | A | `OutfitEditor::exportPatch` (solo este NPC) |
| Rostro/cabello + atuendo | C | `AppearanceEditor::exportCompleteOverhaul` |

Un atuendo sin cambios **no** se exporta (antes el modo C reasignaba el atuendo actual del NPC como
un OTFT nuevo; corregido con `AppearanceDraft::is_outfit_dirty()`).

Comprobaciones (✓ correcto · ⚠ aviso · ◷ esperando · ✕ bloquea): NPC seleccionado; análisis del
FaceGen terminado; FaceGen generado y exportable; para el color, guard de integridad
(`hair-tint-only`); operación no bloqueada por el diagnóstico facial; atuendo validado (slots, raza,
sexo, plantilla; sin variantes de material solo previsualizables); conflicto de procedencia en MO2
(aviso); y siempre «Pendiente de verificación en Skyrim». «Crear mod» solo se activa si nada bloquea
ni espera.

## Pasos

| Paso | Qué muestra | Fuente |
|---|---|---|
| 1. Personaje | Nombre, FormKey, plugin de origen y ganador (overrides), raza, sexo, diagnóstico facial, conflicto MO2 | `StudioController.npc.summary`, `faceCompatibility` |
| 2. Modificación | Resumen del modo y cada cambio; operaciones no disponibles para ese NPC con su motivo | `easyModPlan.changes`, `faceCompatibility.blocked` |
| 3. Vista previa | «Comparar con el original» (el diálogo no es modal: el visor queda visible) y el resultado de Hair Color Safe Mode | `SceneViewModel.original`, `faceGen` |
| 4. Comprobación | Plugins que el mod necesitará como mínimo (plugin ganador del NPC con su mod de MO2 y, si cambia el atuendo, los plugins de las piezas) y la lista de comprobaciones | `faceCompatibility.provenance`, `outfit.pieces`, `easyModPlan.checks` |
| 5. Crear mod | Nombre del plugin, autor, descripción, carpeta, ESL, ZIP; resultado: plugin, ZIP, masters, pendientes en el juego, diff semántico, archivos y comprobaciones del worker | exportadores + respuesta del worker (`semanticDiff`, `files`, `verification`, `pendingVerification`) |

No instala en MO2 ni escribe en `Skyrim/Data`; la carpeta de salida se valida en el worker.

## Pruebas

- Unitarias: `tests/unit/engines/test_easy_mod.cpp` (8 casos, 54 aserciones: sin NPC, sin cambios,
  color con y sin guard, generando, operación bloqueada, atuendo válido/ inválido/ vacío/ con
  material, atuendo sin cambios, modo C, análisis en curso).
- Arnés en la app real (`-StressAppearance easy-mod,easy-mod-outfit`, perfil MO2 `Default`):
  - Lydia y Serana, `easy-mod`: sin cambios no se puede exportar; el asistente se abre, recorre sus
    cinco páginas y muestra el resultado; color → modo B; exportación con autor, descripción y ZIP;
    diff semántico = `HairColor`; ZIP verificado; pendientes listados.
  - Serana, `easy-mod-outfit`: quitar una pieza → modo A, diff = `DefaultOutfit`; más color → modo C,
    diff = `DefaultOutfit, HairColor`.
  - Lydia y Shady Sam, `easy-mod-outfit`: modos A y C exportados con el diff exacto (en Lydia los
    conflictos de ranura que ya trae su atuendo son avisos `SlotConflictInherited`, no errores).
  - `easy-mod-layout` (Serana y Lydia): cada página con un plan real y, en la última, un resultado
    real; comprueba que el asistente cabe en la ventana, que ningún texto se corta ni se sale por el
    lado y que los botones muestran su etiqueta completa. Guarda `wizard-<tamaño>-<página>.png`.

## Tamaños de ventana

2026-10-15, `-Size` con `easy-mod-layout` sobre Serana, 0 fallos en las 5 páginas: 1100×700 (mínimo
de la ventana), 1280×720, 1600×900 y 1920×1080 (Windows la deja en 1920×1061). 2560×1440 no se
pudo probar: el monitor es de 1080p y Windows recorta la ventana. Correcciones:

- A 1600×900 el asistente quedaba con altura negativa (aplastado sobre la lista de NPC): se
  dimensionaba con el overlay, que aún medía 0 tras el cambio de tamaño. Ahora usa el tamaño de la
  ventana y nunca baja de 320 px.
- Ancho entre 420 y 600 px (36 % de la ventana): a 1280 px deja visible parte del visor para el paso 3.
- En un asistente estrecho solo el paso actual lleva nombre; los demás muestran su número (nombre
  en la ayuda emergente). Antes «5. Crear mod», «Siguiente» y «Cerrar» se salían del panel.
- «Cerrar» va en la fila de Atrás/Siguiente (antes ocupaba una fila más).

Las capturas son de ventana oculta: el visor 3D solo aparece en la primera página capturada, así
que la vista previa del paso 3 sigue necesitando una revisión con la ventana visible.

## Problema encontrado y evitado

Una página del `StackLayout` cuyo `ColumnLayout` contenía solo un `Repeater` con elementos provocaba
un acceso inválido dentro de Qt Quick Layouts 6.11 al mostrarse (reproducido en el arnés, aislado por
bisección). La lista de comprobaciones usa un `Column` (posicionador).

## Pendiente

- Revisión visual por una persona con la ventana visible (el arnés mide el layout y guarda capturas con la ventana oculta).
- NPC nuevos de Character Forge: fuera del asistente hasta que exista FaceGen nativo verificado
  (`readyForGame: false`).
- Comprobar en Skyrim los mods creados.
