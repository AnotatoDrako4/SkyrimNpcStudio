# Plan de desarrollo

Estado a 2026-10-11. Una fase solo se marca completa con implementación, compilación sin errores, pruebas, manejo de errores, documentación y limitaciones conocidas.

## Fase 0 — Preparación ✅

- [x] Arquitectura de dos procesos y ADRs ([docs/ADR](ADR))
- [x] CMake + presets (`debug`, `release`, `engines` sin Qt), MSVC 14.44, C++20, `/W4 /WX`
- [x] Aplicación Qt 6.11.2 (Qt Quick, Fusion oscuro), view models con `QML_ELEMENT`
- [x] Worker .NET 10 (`SkyrimNpcStudio.BethesdaWorker`) publicado por CMake junto al ejecutable
- [x] IPC real: JSON-RPC 2.0 sobre named pipe, handshake versionado, cancelación, timeouts, verificación del dueño del pipe, Job Object
- [x] Dependencias fijadas por versión y hash, licencias revisadas ([ADR 0003](ADR/0003-dependencies.md), [ADR 0004](ADR/0004-licensing.md))
- [x] CI de Windows ([.github/workflows/windows.yml](../.github/workflows/windows.yml)) — escrito, **no ejecutado aún en GitHub**
- [x] No había proyecto C#/WPF previo en el directorio de trabajo: no se reutilizó código

## Fase 1 — MO2 y base de NPC ✅

- [x] Detección de instancias globales y selección manual de portables
- [x] Lectura de `ModOrganizer.ini` (dialecto QSettings), perfiles, `modlist`/`plugins`/`loadorder`, `settings.ini`
- [x] VFS de solo lectura con ganadores/perdedores por archivo y conflictos por mod
- [x] Load order efectivo (implícitos, `Skyrim.ccc`, partición de masters, diagnósticos)
- [x] Detección SE/AE por versión de `SkyrimSE.exe`; idioma (`sLanguage`)
- [x] Índice de NPC con Mutagen, caché SQLite incremental, búsqueda por nombre, EditorID, FormKey, FormID de xEdit, raza, sexo, plugin, facción y tipo
- [x] Ficha: origen, ganador, historial de overrides con campos cambiados, conflictos de "ediciones perdidas", cabeza, facciones, inventario, atuendos, FaceGen (rutas + mod proveedor)
- [x] Explorador de mods: prioridad, estado, archivos, plugins, recuentos NPC/ARMO/OTFT, dependencias, archivos ganados/perdidos
- [x] **Ubicación del NPC** (referencias ACHR por celda/mundo): tabla `npc_locations`, resolución de celdas persistentes y temporales
- [x] Recursos BSA en una capa separada del VFS suelto (Fase 2)

## Fase 2 — Visor 3D (núcleo funcional, pose de referencia)

Ver [NIF_PIPELINE.md](NIF_PIPELINE.md): arquitectura, archivos, compilación, pruebas y limitaciones.

- [x] nifly, LZ4 y DirectXTex integrados y fijados por hash
- [x] DTO de apariencia efectiva, cuerpo/ARMA, FaceGen y esqueleto reales
- [x] Geometría personalizada Qt Quick 3D, materiales aproximados, luces y sombras
- [x] Peso `_0`/`_1` validado por topología y skinning CPU estático
- [x] Cámaras, orbit/pan/zoom, picking, ocultación, UV, huesos y original/borrador
- [x] Diagnósticos, cancelación, liberación de recursos y límites de memoria
- [x] Fixtures y carga real de Lydia (29 geometrías, 41 texturas tras resolver el orden de equipamiento)
- [x] Auditoría del renderizador: transparencias explícitas, FaceTint compuesto, formas SMP no dibujadas, inspector y diagnósticos, 5 capturas antes/después
- [ ] Animación/skinning dinámico, normal maps model-space, TXST y shaders Bethesda
- [ ] Adaptadores de herencia por plantilla y cuerpos personalizados

## Fase 3 — Vestuario y primera exportación ✅ (núcleo e integraciones completas)

Adelantada respecto al orden del documento porque no depende del visor 3D.

- [x] Índice de ARMO/ARMA/OTFT, catálogo filtrable por slot y plugin
- [x] Borrador de atuendo con comandos (deshacer/rehacer), sustitución por slot
- [x] Validación: slots, raza y *armor race* en ARMA, modelo por sexo, plantillas con flag *Inventory*, mallas sueltas en el VFS
- [x] Proyectos `.snps` (guardar, abrir, versiones)
- [x] Exportación Modo A: OTFT + override mínimo desde el ganador, ESP/ESL, verificación releyendo, carpeta MO2 + ZIP, atómica, sin sobrescribir
- [x] Caso de uso 1–16 con datos reales (Lydia, perfil de 415 plugins) → `out/local-exports`
- [ ] **17–18: instalar en MO2 y comprobar en el juego** — lo hace el usuario; no se marca como validado en el juego
- [x] Importación asistida en MO2 (copia segura a `mods/` con verificación de proceso cerrado y confirmación modal)
- [x] Atuendo para dormir desde la UI (`alsoSetSleepingOutfit` en `ExportDialog` y vista de estado)
- [x] Multi-NPC en la UI (soporte de lote de NPCs en proyecto `.snps`, lista interactiva y exportación consolidada en un solo plugin)

## Fase 4 — Edición facial ⏳

Head parts, morphs, tintes, presets RaceMenu, pipeline FaceGen y validación de coherencia registro↔FaceGen (los datos para detectar el *dark face* ya están: el estudio avisa si la FaceGen ganadora viene de un mod distinto al plugin ganador; las HeadParts activas se muestran en la ficha del NPC).

## Fases 5–7 ⏳

Personalización de prendas (variantes, recoloreado, BodySlide), estudio completo (comparador, batch) y expansiones (SPID, Blender, poses). No se abordan hasta estabilizar el núcleo.

## Siguiente paso recomendado

1. Probar en el juego el parche generado (pasos 17–18) y registrar el resultado.
2. Fase 4: Integración del motor de edición facial (`engines/face/`), morphs/tints y validadores de coherencia FaceGen.
