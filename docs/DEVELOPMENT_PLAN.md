# Plan de desarrollo

Estado a 2026-10-14. Una fase solo se marca completa con implementación, compilación sin errores, pruebas, manejo de errores, documentación y limitaciones conocidas.

## Protección facial (Serana / High Poly Head) — P0–P4

Rama `fix/serana-hair-color-safe-mode`. Estados: PENDIENTE, EN DESARROLLO, IMPLEMENTADO SIN
VERIFICACIÓN, VERIFICADO EN CLOUD, VERIFICADO EN WINDOWS, VERIFICADO EN SKYRIM, BLOQUEADO.

| Etapa | Estado | Evidencia |
|---|---|---|
| P0 Diagnóstico con el replacer real | VERIFICADO EN WINDOWS | `[serana]` local: análisis, ruta antigua (`PreserveExisting` y `Regenerate`) frente al FaceGen ganador; causas confirmadas y pendientes en [HIGH_POLY_HEAD_COMPATIBILITY.md](HIGH_POLY_HEAD_COMPATIBILITY.md) |
| P1 Hair Color Safe Mode (parche byte a byte del FaceGen ganador, sin fallback) | VERIFICADO EN WINDOWS | unitarias `[face]`; app real sobre Serana (`-StressAppearance hair-safe`) |
| P2 FaceGen Geometry Integrity Guard (pieza, propiedad, antes, después) | VERIFICADO EN WINDOWS | unitarias: posiciones, textura, índices, UV, shape eliminada, archivo ilegible |
| P3 Exportación mínima (`OnlyFields`, releída con Mutagen) | VERIFICADO EN WINDOWS | .NET `FacePatchExportTests`; manifiesto de la exportación real de Serana: `changedFields = [HCLF]` |
| Diagnóstico de compatibilidad facial y tarjeta «Protección facial» | IMPLEMENTADO SIN VERIFICACIÓN visual por una persona | unitarias `[face][compat]`; ejecución en la app con Serana |
| Estabilidad del árbol de compilación (dependencias de cabeceras, caché MSVC) | VERIFICADO EN WINDOWS | 2026-10-15: `build:header` pasa; configuración falla si la caché pierde `/EHsc` o `/debug`; ver [CRASH_DIAGNOSTICS.md](CRASH_DIAGNOSTICS.md) |
| P4 Regresiones (Lydia, suites completas, `local-review`) | EN DESARROLLO | 2026-10-15, Windows, Debug, perfil MO2 `Default`: `ctest` 128/128 (2 omitidos: Blender), `dotnet test` 67/67, arnés Serana de 7 escenarios `PASSED` dos veces (410 comprobaciones), arnés Lydia de 19 escenarios `PASSED` con ventana visible (776 comprobaciones; los escenarios de clics necesitan `-ShowWindow`). ASan (`build\asan`, arnés Serana, 410 comprobaciones): sin informes. Pendiente: `local-review.ps1` |
| Comportamiento dentro de Skyrim | PENDIENTE | prueba manual |
| P5–P6 Creador de mods — Modo fácil | PENDIENTE | diseño en [EASY_MOD_CREATOR.md](EASY_MOD_CREATOR.md) |

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

## Fase 4 — Character Creator y FaceGen (núcleo verificado, límites de topología)

- [x] Motor facial (`engines/face/`): sliders y morphs estándar de Skyrim (nariz, mandíbula, mejillas, ojos, cejas, labios, barbilla), HeadParts y validaciones.
- [x] Motor de presets (`engines/presets/`): parser RaceMenu `.jslot` en VFS de MO2, compatibilidad por raza/sexo/mallas, informes de discrepancias.
- [x] Borrador de apariencia unificada (`engines/appearance/`): `AppearanceDraft` con stack de comandos (deshacer/rehacer), paleta de colores, tono de piel, peso y altura.
- [x] Formato de proyecto `.snps` v3: rostro y atuendo completos con migración de proyectos v2.
- [x] Pipeline BethesdaWorker: `face.getRecipe`, `catalog.searchHeadParts`, `export.faceOverhaul`, `export.completeOverhaul`.
- [x] FaceGen NIF construido desde la receta de raza, Head Parts y TRI, con modo de conservación para una cabeza existente compatible; FaceTint DDS compuesto y validado. El visor usa los mismos bytes que exporta el estudio.
- [x] Cambios aislados de color de cabello sin reconstruir toda la geometría; la exportación escribe HCLF/CLFM y el NIF correspondiente.
- [x] Un replacer cuya topología no coincide con sus TRI bloquea los morphs que dependen de ellos; un cambio de color no queda bloqueado por ese problema.
- [x] Exportador de parches de rostro (`FacePatchBuilder` & `ModPackager`): Modos B (Face Overhaul) y C (Complete Overhaul con Atuendo), generación de ESP/ESL, `manifest.json`, `meta.ini` y ZIP para MO2.
- [x] UI de Character Creator 2.0 (`apps/desktop/qml/charactercreator/`):
  - Nuevo workspace "PERSONALIZAR PERSONAJE" con conmutador en barra superior.
  - Toolbar 3D con presets de cámara (Rostro, Cuerpo, Frontal, Lateral, Posterior, Reset).
  - Comparador antes/después en tiempo real con alternancia visual fluida.
  - Pestañas: Presets (.jslot y catálogo), Rostro (sliders anatómicos), Cabello y Ojos (paleta y HeadParts), Cuerpo y Piel (peso, altura, tono), y Vestuario.
  - Selector de Modo Fácil vs Modo Avanzado.
  - Asistente de exportación (`ExportWizardDialog`) con Modos A, B y C.

## Fases 5–7 ⏳

Completar y verificar variantes de materiales que generen recursos DDS/TXST/ARMA/ARMO válidos,
adaptadores de BodySlide y cuerpos personalizados, informe central de compatibilidad y el caso de
Lydia dentro de Skyrim. Los controles que no se puedan materializar en el juego deben mostrar un
bloqueo antes de exportar. La animación dinámica del visor queda fuera del foco actual.

Estado de variantes de material: el borrador y los campos del panel existen, pero aún no se
representan en el visor. El antiguo TXST/ARMA
sin DDS propio ni mapeo NIF fiable se retiró. Los modos A y C rechazan esas variantes con
`MaterialVariantPreviewOnly` ([detalle](MATERIAL_VARIANTS.md)).

## Character Forge — Integración 3D y Blender

Línea de trabajo propia del tercer workspace (creación de NPC nuevos a partir de modelos externos).
Sus etapas **no** son las Fases 0–7 del editor de NPC de arriba; se numeran CF-0…CF-8. Estados
según [CHARACTER_FORGE_IMPLEMENTATION_LOG.md](CHARACTER_FORGE_IMPLEMENTATION_LOG.md), fuentes en
[CHARACTER_FORGE_TECHNICAL_SOURCES.md](CHARACTER_FORGE_TECHNICAL_SOURCES.md).

| Etapa | Contenido | Estado |
|---|---|---|
| CF-0 | Estabilización de la base 298990d: build, `dotnet test`, `ctest`, smoke y smoke del workspace 2 | VERIFICADO EN WINDOWS |
| CF-1 | Visor 3D real: validación `engines/gltf`, `ForgeSceneViewModel`, `RuntimeLoader`, encuadre, vistas, malla, errores visibles, recarga segura, paquete `dist` | VERIFICADO EN WINDOWS (Debug y dist) |
| CF-2 | Inspección de geometría y multi-parte: declarado/validado/renderizado, selección de partes por clic con caja 3D, ocultación/visibilidad con jerarquía | VERIFICADO EN WINDOWS |
| CF-3 | Biblioteca de assets dentro de `.sncf` (copia/enlace, SHA-256, duplicados, rutas relativas, compatibilidad con referencias anteriores) | VERIFICADO EN WINDOWS (pruebas automáticas; interacción manual pendiente) |
| CF-4 | Blender Bridge determinista (`QProcess`, versión, FBX/OBJ/BLEND → GLB, informe JSON, timeout, cancelación) | VERIFICADO EN WINDOWS (conversión real y pruebas de proceso; flujo manual UI pendiente) |
| CF-5 | Personalización de modelos y vestuario reutilizando Outfit Studio | PENDIENTE |
| CF-6 | NPC nuevos + FaceGen sobre el NativeNpcBuilder existente; exportación final bloqueada sin FaceGen verificable | PENDIENTE |
| CF-7 | Regresiones (Character Creator, Outfit Studio, `.snps`, `.sncf`, exportaciones A/B/C, GPU, cancelación, 115 tests C++, 65 .NET, 4 MO2 local) | VERIFICADO EN WINDOWS |
| CF-8 | Interfaz: viewport protagonista, controles de órbita/paneo/zoom (clic izq: parte, clic der: órbita, central: paneo), inspector avanzado | VERIFICADO EN WINDOWS |

Los estados A (modelo visible), B (preparado para Skyrim) y C (NPC listo para exportar) no se
confunden: hoy solo existe el estado A para GLB/glTF.

## Siguiente paso recomendado

1. Probar en el juego Skyrim SE/AE los mods generados con Modo B (Face Overhaul) y Modo C (Overhaul Completo).
2. Avanzar las variantes de prendas y BodySlide con pruebas de recursos y exportación, sin afirmar
   compatibilidad universal de mallas o sculpt data.
