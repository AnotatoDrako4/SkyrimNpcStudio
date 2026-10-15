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
| 4. Comprobación | Lista de comprobaciones | `easyModPlan.checks` |
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
  - Lydia, `easy-mod-outfit`: su atuendo modificado no pasa la validación → el plan bloquea A y C
    (comportamiento correcto; no se exportó).

## Problema encontrado y evitado

Una página del `StackLayout` cuyo `ColumnLayout` contenía solo un `Repeater` con elementos provocaba
un acceso inválido dentro de Qt Quick Layouts 6.11 al mostrarse (reproducido en el arnés, aislado por
bisección). La lista de comprobaciones usa un `Column` (posicionador).

## Pendiente

- Revisión visual por una persona (los pasos se probaron con el arnés, ventana oculta).
- NPC nuevos de Character Forge: fuera del asistente hasta que exista FaceGen nativo verificado
  (`readyForGame: false`).
- Comprobar en Skyrim los mods creados.
