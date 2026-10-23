# UX06 — Workbench profesional: legibilidad, paneles, catálogos, Character Forge y paleta

Misión `SNS-UX06-PRO-WORKBENCH-REAL-REDESIGN` (2026-10-22). Evidencia, capturas y matriz de aceptación:
`out/uiux-ux06/ux06-20261022-1630/` (`FINAL_REPORT.md`). Sin Git; ningún cambio en Skyrim, MO2, saves ni en el
motor (NIF, FaceGen, render 3D, worker, IPC, formatos `.sncf`/`.snps`).

## 1. Legibilidad (UX06-01)

- **Un solo cálculo de tamaño de texto**: `WorkbenchState::legibleFontPixels(base, ui, font)` (C++), usado por
  `Theme.fs()` y por `workbench_check`. Escala la base por interfaz × texto, pero nunca baja del mínimo de su rol:
  títulos 12 px, texto de cuerpo 11 px, secundario 10 px, leyendas 9 px. La preferencia guardada no cambia;
  `Workbench.legibilityFloorActive` lo dice y la barra de estado muestra **TEXTO AL MÍNIMO LEGIBLE** (clic: preferencias).
- **Base de texto 13 px** a 100 % (antes 12). Títulos 17–20 px.
- **Objetivos de clic**: `Theme.controlHeight` ≥ 32 px (28 px en densidad compacta), cualquiera sea la escala.
- **Presets de presentación** (`Workbench.readabilityPresets`, `applyReadabilityPreset`): Cómodo (110/110, cómoda),
  Equilibrado (100/100, normal), Compacto (90/100, compacta — ahorra espacio sin microtipografía). Se aplican solo por
  acción del usuario, como **vista previa** (`AppSettings.beginPreview`, aviso «¿Mantener?», revierte solo).
  Están en Vista › Preferencias de apariencia, en Configuración › Apariencia y en la paleta.

## 2. Paneles (UX06-02) — `WorkbenchSplit` evolucionado, no reemplazado

- Divisores de 8 px con grip visible, cursor de redimensión y tooltip. **Doble clic** restablece el ancho
  predeterminado del panel junto al divisor (`WorkspaceLayout.defaultLeftWidth/defaultRightWidth`, `resetSizes()`).
  Nota técnica: `SplitView` toma el grab exclusivo del press, por eso el doble clic se detecta con `SplitHandle.pressed`
  (dos liberaciones quietas dentro de `styleHints.mouseDoubleClickInterval`) y se aplica diferido (`Qt.callLater`)
  para que el fin de «resizing» no lo lea como un arrastre. `SplitHandle.previousItem` no existe en Qt 6.11: el lado se
  decide por la posición del divisor.
- **Auto-ocultación por falta de espacio**: si ni siquiera el 80 % de los mínimos cabe, el inspector se oculta en
  pantalla primero, luego el explorador. Es transitorio (`leftAutoHidden/rightAutoHidden`, nunca persistido), no
  toca `leftVisible/rightVisible` ni los anchos guardados, y el panel vuelve solo al haber espacio. Función pura del
  ancho, la escala y la elección del usuario: no oscila.
- **Conmutadores en la cabecera** (izquierdo, derecho, consola) con estado visible; en ámbar si el panel está
  auto-oculto (clic: se muestra y el otro panel lateral cede, como acción del usuario).
- **Distribuciones por tarea** (`Workbench.builtinLayouts`, con campo `workspace`): Exploración de NPC, Edición de
  rostro, Comparación A/B, Vestuario, Outfit / Armadura, Character Forge, Revisión de mod, Enfoque visor, Diseño
  compacto, Predeterminado del workspace. El menú muestra solo las del workspace actual + las generales. Las claves
  antiguas se conservan.
- «Restablecer paneles de este workspace» en Vista › Distribuciones, en el menú Distribución y en la paleta.

## 3. Cabecera y barra de estado (UX06-03)

- Pestañas de workspace con **iconos vectoriales** (Segoe Fluent Icons / Segoe MDL2 Assets del sistema; nunca se
  distribuyen fuentes; `Icon.qml` cae a texto si faltan) — sin emoji.
- Contexto de trabajo: NPC seleccionado (Personaje/Atuendos) o proyecto (Forge) + insignia **MODIFICADO / SIN GUARDAR**.
- Las etiquetas ceden antes que las acciones: `headerRow.designWidth` (ancho en píxeles de diseño) → `compact`
  (solo icono + tooltip) < 1500 → `ultraCompact` < 900 (Abrir/Guardar/paneles/distribución pasan al menú «Más»).
  `workbench_check` comprueba que toda acción de la cabecera queda dentro de la ventana.
- Exportar: deshabilitado en Character Forge con el motivo (exportación de NPC nuevos bloqueada sin FaceGen nativo).
- Abrir/Guardar viven una vez (cabecera, Proyecto, Ctrl+S, paleta) y llaman a `window.saveCurrentProject()` /
  `openCurrentProject()`. La barra de Forge ya no los repite.

## 4. Catálogos (UX06-04)

- `NpcBrowser`: búsqueda fija con icono y borrar; segmentado Lista/Galería; filtros plegables con contador y
  «Limpiar filtros»; «Mostrando N de M»; filas y tarjetas con nombre/EditorID/FormKey/plugin completos en tooltip;
  teclado (flechas, Espacio selecciona, Enter abre, **Ctrl+C copia el FormKey** vía `StudioController.copyToClipboard`).
  Tarjetas de galería con texto legible y altura de celda calculada desde el tamaño real del texto.
- `PortraitThumb`: sin retrato → código corto (`NO_HEAD`, `TEMPLATE`, `OFFSCREEN`, `ERROR`) + una línea; el motivo
  original del renderizador, sin editar, en el tooltip. Nunca un rostro sustituto.
- `ArmorCatalog`: búsqueda fija; mod; tipo (segmentado); Solo SMP; Slot (plegable); limpiar filtros con contador;
  vista lista/cuadrícula; fila de tres líneas (nombre · slots/tipo/FormID · mod). Slot como número (no emoji) con
  sus nombres en el tooltip; Espacio = 3D, Enter = equipar, Ctrl+C = FormKey, tecla Menú = menú contextual.
- Siguen virtualizados (`ListView`/`GridView` sobre los modelos del controlador); ningún filtro nuevo en JS por tecla.

## 5. Visor (UX06-05)

- `CharacterViewport`: solo la capa de controles. Botones con iconos y nombre accesible; colores desde tokens.
  Estado vacío (`EmptyState`) con acciones reales: «Buscar NPC», «Ver prendas» / «Ver catálogo de prendas» (muestran
  el explorador y enfocan su búsqueda). La escena 3D, cámara, picking y materiales no se tocaron.

## 6. Character Forge (UX06-06)

- `ForgeNavigation` agrupada: Proyecto · Personaje (NPC nativo, Atuendos e inventario) · Recursos y 3D (Importar,
  Biblioteca, IA, Blender, Esqueleto, Materiales) · Verificación y salida (Validación, Exportar). Grupos plegables,
  candado + tooltip en herramientas que requieren proyecto, flechas para recorrer.
- Visor sin modelo: tarjeta «Empezar» con Nuevo proyecto / Abrir .sncf / Biblioteca (o Importar / NPC nativo con
  proyecto). Ninguna acción sin backend.
- Inspector con título y propósito de la herramienta; panel Proyecto con estado claro (guardado, perfil, validación
  requerida, FaceGen pendiente) y la advertencia «validado en Windows ≠ jugable en Skyrim».
- AG04 (`StatsEditor`, arquetipos, voz, hechizos/perks) y `CharacterForgeViewModel` intactos.

## 7. Paleta de comandos y atajos (UX06-07)

- **Ctrl+Shift+P** (libre; `Qt.ApplicationShortcut`): filtro por palabras sin acentos, flechas/Enter/Esc; devuelve el
  foco al control anterior. `window.commands` lista ~35 comandos que llaman a los mismos handlers que menús y botones
  (y se habilitan igual). Ayuda › Atajos de teclado lista solo atajos realmente enlazados.

## 8. Personalización (UX06-08)

- Configuración › Apariencia: tarjeta «Presentación» con los presets y el aviso de legibilidad (vista previa con
  revertir automático). Sin nuevo almacén: todo pasa por `WorkbenchState`/`AppSettings`.

## Componentes nuevos (`apps/desktop/qml/components/`)

`Icon`, `IconToolButton`, `PanelHeader`, `SegmentedControl`, `EmptyState`, `CommandPalette` — registrados de forma
aditiva en `apps/desktop/CMakeLists.txt`.

## Verificación

```powershell
.\scripts\workbench-check.ps1 -Size 1366x768 -Preset ux06 -Label ux06-workbench-1366x768   # + -DisplayScale 1.25 / 1.5
build\ux06\bin\sns_workbench_tests.exe
ctest --test-dir build/ux06
```

`workbench_check` (ventana real, mouse y teclado reales) cubre: arrastre y persistencia, Ctrl+B/Ctrl+Alt+B/Ctrl+J/F11,
escalas 85–150 %, suelo de legibilidad a 85/80, doble clic en ambos divisores, auto-ocultación a 1100 px y 150 %,
cabecera dentro de la ventana, paleta por teclado (abrir, escribir, Enter, Esc, foco) y 14 comandos, lista de atajos,
P01/P02/P06 (cambios de workspace, paneles con cámara intacta, deriva de distribuciones) y reinicio.

**Estado**: todo esto es comportamiento de la aplicación en Windows. Nada de UX06 cambia lo que llega a Skyrim;
la exportación jugable de NPC nuevos sigue bloqueada (`NativeFaceGenUnavailable`) y el estado de juego es `PENDIENTE_SKYRIM`.
