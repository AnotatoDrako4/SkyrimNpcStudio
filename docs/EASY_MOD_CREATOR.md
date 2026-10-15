# Creador de mods — Modo fácil

Estado: **PENDIENTE** (diseño). Este documento describe lo que se va a construir; ninguna pantalla
del asistente existe todavía en la aplicación. Lo que sí existe y el asistente reutilizará se
indica en cada paso.

El asistente no es un cuarto editor: orquesta Character Creator (`AppearanceEditor`), Outfit Studio
(`OutfitEditor`) y Character Forge, y sus exportadores existentes.

## Pasos

| Paso | Qué hace | Backend existente | Estado |
|---|---|---|---|
| 1. Personaje | NPC del perfil, NPC de un mod o NPC nuevo de Character Forge; resumen: nombre, mod de origen, plugin ganador, raza, sexo, compatibilidad facial | catálogo del worker, `StudioController::selectNpc`, `faceCompatibility` | PENDIENTE (UI) |
| 2. Modificación | Color de cabello · Vestuario · Rostro · NPC nuevo · Overhaul | Hair Color Safe Mode; Outfit Studio (Modo A); `face_build_kind` + diagnóstico; Character Forge | PENDIENTE (UI) |
| 3. Vista previa | Original frente a modificado con los assets reales | visor de Character Creator (overlay = bytes a exportar) | PENDIENTE (UI) |
| 4. Comprobación | Plugin, FaceGen, FaceTint, dependencias, Head Parts, raza, vestuario, integridad, conflictos MO2, rutas | guard de integridad, validación del worker, `OutfitValidator`, diagnóstico facial | PENDIENTE (UI) |
| 5. Crear mod | Nombre, autor, descripción, ESP/ESL, carpeta, ZIP para MO2; staging atómico | `ModPackager` (no sobrescribe, no escribe en `Skyrim/Data`, no instala) | PENDIENTE (UI) |

Reglas que el asistente heredará del backend:

- Solo se ofrecen operaciones que el diagnóstico facial permite; las bloqueadas se muestran con
  su motivo, no como botones activos.
- Cambio mínimo: color → `HCLF` + FaceGen recoloreado; vestuario → OTFT, sin FaceGen; rostro →
  campos del rostro + FaceGen regenerado o como diferencia, solo si es exportable.
- Un NPC nuevo de Character Forge sigue con `readyForGame: false` e `inGameVerified: false` hasta
  que exista un FaceGen nativo verificable.
- Estados mostrados: operación segura · con limitaciones · bloqueada · pendiente de verificación en
  Skyrim (siempre, hasta una prueba manual en el juego).
