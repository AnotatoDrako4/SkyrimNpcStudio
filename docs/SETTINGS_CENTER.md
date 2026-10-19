# Centro de Configuración (Settings Center)

Estado (2026-10-19): `INTEGRACION_WINDOWS_OK` para los ajustes listados abajo (ventana real, perfil MO2 `Default`
en solo lectura, Debug y Release). Nada de esto se ha observado dentro de Skyrim ni afecta a lo que el juego carga.
Evidencias: `out/local-settings-implementation/20261019-c1/` (`changes.md`).

## Acceso

- `Archivo › Configuración…` y **Ctrl+,** (sin conflicto con otro atajo del shell), botón **⚙ Configuración** de la
  barra superior, `Vista › Tema y colores…`, y «Todos los ajustes…» en `Vista › Preferencias de apariencia…`
  (`ScalePreferencesDialog` se conserva como acceso rápido).
- Al abrir, el foco está en el buscador. Escape cierra. Cerrar no toca proyectos, NPC ni la escena.

## Arquitectura

| Pieza | Archivo | Papel |
|---|---|---|
| `SettingsService` (singleton QML `AppSettings`) | `apps/desktop/viewmodels/settings_service.{hpp,cpp}` | Catálogo de ajustes con efecto real, validación estricta por tipo/rango/opción, persistencia, búsqueda, vista previa/revertir, import/export, paletas |
| `WorkbenchState` (singleton `Workbench`, existente) | `viewmodels/workbench_state.*` | **Única** fuente de escala, texto, densidad, fuente y distribuciones. `AppSettings` solo las describe y las alcanza por `AppSettings.workbench` |
| Centro | `qml/settings/SettingsCenter.qml` | Categorías, búsqueda con ruta «Categoría › Ajuste», páginas, paneles reales (distribuciones, caché, MO2, sistema, import/export, diferencias) |
| Control de un ajuste | `qml/settings/SettingControl.qml` | Pinta el tipo del descriptor; valor actual, predeterminado, alcance, política de aplicación, «cambiado» y ↺ |
| Paleta | `qml/Theme.qml` | Mismos nombres `Theme.*` para todos los consumidores; ahora vienen de `AppSettings.palette` |
| Paleta de controles | `qml/Main.qml` (`palette.*`) | Fusion (menús, campos, listas, diálogos) sigue el tema; «Grafito» devuelve exactamente `dark_palette()` de `main.cpp` |
| Banner de vista previa | `qml/Main.qml` (`themePreviewBanner`) | «¿Mantener esta apariencia? Se revierte en N s» — Mantener / Revertir; revierte solo a los 15 s |

### Persistencia

- `QSettings`, grupo **`appSettings/v1`**, clave `schemaVersion = 1`. Solo se escriben valores distintos del
  predeterminado (volver al predeterminado borra la clave); arrancar no escribe nada.
- `workbench/*` no se migra ni se duplica.
- Un valor guardado inválido (tipo, rango, opción, `NaN`) se ignora con su motivo en `loadIssues` (visible en
  General). Claves desconocidas: se ignoran y se informan. Un esquema más nuevo: sesión de solo lectura, el archivo
  no se sobrescribe.
- `SNS_SETTINGS_DIR` sigue aislando las pruebas (INI propio).

### Import / export

JSON `{format: "skyrim-npc-studio-settings", schemaVersion, settings: {id: valor}, layouts: [...]}` sin rutas ni
secretos. La importación valida **todo** el documento antes de aplicar nada (formato, versión, cada valor; claves
que parezcan credenciales —`password`, `token`, `apikey`, `secret`…— rechazan el documento entero), muestra la lista
de cambios y, si un paso fallara, revierte a la instantánea previa.

## Catálogo implementado (cada ajuste con su efecto)

| Categoría | Ajuste (`id`) | Efecto real (handler) | Aplicación |
|---|---|---|---|
| General | `general.startWorkspace` | `Main.qml` `workspaceTabs` al iniciar (último / fijo) | al reiniciar |
| | `general.confirmUnsavedClose` | `Main.qml onClosing`: `outfit.dirty`, `appearance.dirty`, `forge.dirty` → diálogo Volver / Salir sin guardar | inmediata |
| | `general.advancedMode` (panel) | `Theme.advancedMode` (persistido como antes en `window`) | inmediata |
| | `general.settingsTransfer`, `general.settingsDiff` (paneles) | export/import con vista previa; diferencias con ↺ por clave | — |
| Apariencia | `appearance.theme`: Grafito oscuro (original), Oscuro profundo, Azul acero, Alto contraste | `Theme.*` + paleta Fusion | vista previa |
| | `appearance.accent`: 6 acentos | `accent*`, `selection`, `borderFocus`, `onAccent`; error/aviso/correcto no cambian | vista previa |
| | `appearance.uiScale`, `appearance.fontScale` | `WorkbenchState` | vista previa |
| | `appearance.density`, `appearance.fontFamily` | `WorkbenchState` (la lista de fuentes se carga al abrir el combo) | inmediata |
| Espacios de trabajo | paneles por espacio (visibles, anchos, alto de consola, visor enfocado) | `WorkspaceLayout` → `WorkbenchSplit` | inmediata |
| | distribuciones predefinidas / guardadas | `applyBuiltinLayout`, `saveLayout`, `applyLayout`, `deleteLayout` | inmediata |
| Visor 3D | `viewport.background` | `SceneEnvironment.clearColor` | inmediata |
| | `viewport.antiAliasing` (off/medio/alto/muy alto) | `antialiasingMode/Quality` (MSAA de Qt Quick 3D) | inmediata |
| | `viewport.cameraFov` 25–75° | `PerspectiveCamera.fieldOfView` | inmediata |
| | `viewport.orbitSensitivity`, `invertOrbitX/Y` | `OrbitCameraController.xSpeed/ySpeed/xInvert/yInvert` | inmediata |
| | `viewport.autoFitOnNpc`, `viewport.defaultCamera` | `CharacterViewport.onReady` / `reset()` | inmediata |
| | `viewport.lightIntensity` 50–160 % | brillo de las tres `DirectionalLight` | inmediata |
| NPC y retratos | `npc.browserDefaultView` (recordada al cambiarla) | `NpcBrowser.viewMode` | inmediata |
| | `npc.galleryCardSize` | columnas del `GridView` (galería y donantes) | inmediata |
| | `npc.donorView` | Presets › Transferencia: lista o **galería de rostros** | inmediata |
| | `npc.searchDebounceMs` 150–500 | `debounce.interval` | inmediata |
| | `npc.portraitInstantCache` | `PortraitRenderer.instantCache` (retrato guardado mostrado como provisional) | inmediata |
| | `npc.portraitCache` (panel) | `cacheUsage()` / `clearRenders()` con confirmación; conserva capturas de Skyrim | — |
| Rendimiento | `performance.portraitPrefetchRows` 0–6 | `NpcBrowser` → `PortraitRenderer.prefetch()` | inmediata |
| | `performance.portraitStats` (panel) | `PortraitRenderer.stats` medido | — |
| MO2 y recursos | `mo2.status` (panel, solo lectura) | `StudioController.summary`, instancia, perfil | — |
| Sistema | `system.info` (panel) | `systemInfo()` + `GraphicsInfo.api`; copiar información técnica | — |

### Omitido a propósito (sin interruptores inertes)

- **Tamaño de letra por zona** (`Workbench.zoneFonts`): se guarda, pero ningún panel QML lo consume todavía; no se
  muestra hasta que explorador, visor, inspector y consola lo usen.
- **Tema claro**: hay ~50 colores fijos en `BodyCustomizer`, `HairEyeCustomizer`, `OutfitPanel`, `CharacterToolbar`
  y el visor sin verificar sobre fondo claro; solo se ofrecen temas oscuros cubiertos de punta a punta.
- **Física SMP/FSMP, atajos remapeables, paleta de comandos, idioma, autoguardado, presupuestos de textura/mallas,
  rutas de herramientas externas e IA**: sin backend verificado; no aparecen (buscar «SMP» o «física» no devuelve
  nada, comprobado por test).
- **Nunca existirán**: `disableSafePatch`, `ignoreMasters`, `forceFaceGen`, `autoApproveAmbiguous`, `skipReadback`
  y equivalentes (test).

## Retratos: caché instantánea, verificación en segundo plano y precarga

Ver `NPC_PORTRAIT_BROWSER.md` § «Carga instantánea». Resumen: al indexar el perfil se lee el índice de la caché
(retratos de sesiones anteriores, hash verificado); una tarjeta los muestra al instante con la marca
«verificando…» (o «rehaciendo…» si quedaron desactualizados), las verificaciones van en lotes de 24 en el pool de
hilos (antes 8 por pasada en el hilo GUI) y, en reposo, se verifican/generan las filas siguientes.

## Contraste (test «Every theme and accent keeps text readable»)

Texto principal y secundario ≥ 4,5:1 en todas las superficies (≥ 7:1 en Alto contraste); acento ≥ 3:1 contra el
panel; texto sobre acento ≥ 4,5:1 eligiendo blanco u oscuro según medida, excepto el par original Grafito + Azul
acero (blanco sobre `#4382c4` = 4,0:1), conservado como identidad del estudio. `textMuted` (pistas decorativas) no
llega a 4,5:1 en Grafito (2,7:1, valor original) y no se usa para contenido esencial nuevo.

## Medidas (2026-10-19, una máquina)

| Medida | Debug | Release |
|---|---|---|
| Construir una página de categoría | 0–643 ms | 26–112 ms |
| Cambiar de tema en caliente (repinta toda la ventana) | ~6 s | ~1,0 s |

Límite conocido: el cambio de tema propaga ~40 colores y la paleta Fusion por todo el árbol de elementos; ~1 s en
Release (aceptable para un cambio raro y con vista previa), ~6 s en Debug.

## Pruebas

- `tests/app/test_settings_service.cpp` (`sns_settings_tests`, 9 casos): predeterminados sin escritura, validación
  y persistencia, corrupción/claves desconocidas/esquema futuro, búsqueda con y sin acentos, catálogo (handler,
  alcance, política; ajustes prohibidos ausentes), acceso a `WorkbenchState` sin duplicar, vista previa/revertir,
  import/export con rechazos sin efectos, contraste de 4 temas × 6 acentos.
- `tests/app/test_portrait_cache.cpp`: índice de renders (solo intactos, nunca capturas) y vaciado que conserva
  capturas y no sale de su carpeta.
- Ventana real: `scripts/settings-check.ps1` (`--settings-check` / `--settings-expect`): Ctrl+, con foco en la
  búsqueda, búsqueda, 8 páginas medidas, tema por su combo con clic real en «Mantener», texto 130 % con clic real en
  «Revertir», fondo y FOV leídos del `View3D`, ancho de panel, Escape; reinicio que encuentra todo.
- Escenarios del arnés: `npc-portraits-instant` (fría / caliente / perfil cambiado) y `donor-gallery`.

## Matriz SET

| ID | Resultado | Evidencia |
|---|---|---|
| SET-01 | PASS | `s2-…`, `s3-settings-release/run1.log` |
| SET-02 | PASS (escala, tema, retrato, MO2, cámara; «SMP» → 0) | ídem |
| SET-03 | PARCIAL: escala por test unitario y el `workbench-check` existente; no repetido aquí | — |
| SET-04 | PASS (píxeles de paneles `#22252a → #1b2430`) | `10-theme-steel-preview.png` |
| SET-05 | PASS (Revertir por clic real) | run1.log |
| SET-06 | PASS (texto) / fuente: test unitario | run1.log, unit |
| SET-07 | PASS (ancho de panel, reinicio) | run1 + restart |
| SET-08 | NO APLICA (zonas no consumidas: omitido) | — |
| SET-09, SET-10 | PASS (unit) | `tests-c1-unit.log` |
| SET-11 | PENDIENTE (multi-monitor) | — |
| SET-12 | PASS (unit: restablecer interfaz conserva distribuciones) | unit |
| SET-13 | Código existente (`set_profile` cancela y vacía); no repetido | — |
| SET-14 | PASS (fondo/FOV reales, sin tocar NIF/DDS) | run1.log |
| SET-15 | PASS (pausa máx. del bucle 72–111 ms al abrir la galería) | `q*-*.log` |
| SET-16 a SET-18 | NO IMPLEMENTADO (SMP, atajos remapeables) | — |
| SET-19, SET-20 | No afectados: Settings no toca el worker ni los gates de exportación | — |
