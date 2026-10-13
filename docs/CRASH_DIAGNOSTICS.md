# Estabilidad del Character Creator y diagnóstico de cierres

Este documento recoge la investigación del cierre reportado al cambiar el color de cabello de
Lydia (`0A2C8E:Skyrim.esm`), lo que se corrigió, cómo se diagnostica un cierre a partir de ahora y
cómo se ejecutan las pruebas de regresión.

## 1. Investigación del cierre reportado

### Evidencia reunida

| Fuente | Resultado |
|---|---|
| Registro de eventos de Windows (Application Error 1000 / WER 1001) | 9 fallos de `SkyrimNpcStudio.exe` el 2026-10-12 entre 00:20 y 00:26 (anteriores al Character Creator: `0xC0000374` en `ntdll`, `0xC0000005` en `Qt6Qml.dll` y en código JIT). **Ningún** fallo de `SkyrimNpcStudio.exe` desde entonces, aunque WER funcionaba (registró esos 9). |
| `scripts/package.ps1` (lanzado por `SkyrimNpcStudio.bat` cuando HEAD ≠ commit empaquetado) | Terminaba con `Stop-Process -Force` cualquier `SkyrimNpcStudio.exe` que se ejecutara desde `dist\`. Un proceso terminado así desaparece **sin** evento WER. `dist\VERSION.txt` muestra un reempaquetado a las 18:11:25 (commit 3ca70f0). |
| Dependencias de cabeceras del paquete anterior (`ninja -t deps`) | 47 de 114 objetos no tenían dependencias de cabeceras registradas. Los objetos de `appearance_editor.cpp`, `scene_viewmodel.cpp`, `outfit_editor.cpp` y `viewport_verifier.cpp` databan de las 14:33, mientras que `main.cpp` y `studio_controller.cpp` se recompilaron a las 17:15 después de cambiar cabeceras compartidas. Se enlazó código compilado con disposiciones distintas de clases C++. |
| Reproducción controlada de `cl.exe /showIncludes` | Con la consola CP850, Ninja registra `#deps 0`; con UTF-8, el mismo objeto registra sus 2 cabeceras. El prefijo español de `/showIncludes` contiene `ó`: CMake esperaba UTF-8 (`C3 B3`) y `cl.exe` emitía CP850 (`A2`). Una reconstrucción limpia elimina esa incompatibilidad de objetos. |
| Reproducción automatizada en la ventana real (17 escenarios, ver §4) | Sin cierre en Debug, RelWithDebInfo, Release con los flags exactos de `dist` (preset `package`) ni AddressSanitizer. ASan no detectó ningún acceso inválido. |
| Ruta de edición del cabello | Funcionaba, pero cada cambio de color reconstruía todas las piezas de la cabeza **y** toda la escena 3D (≈1,5 s en Release), dejaba el borrador de Lydia **no exportable** (`TriVertexMismatch` en una cabeza de replacer) y el worker rechazaba la exportación (`FaceTintRequired` falso). |

### Conclusión

El paquete anterior mezclaba objetos compilados antes y después de cambios en cabeceras C++: una
causa comprobada de corrupción de memoria que explica cierres del proceso principal. La causa de
esa mezcla fue el desajuste de codificación en `/showIncludes`, reproducido de forma controlada.
No hay un minidump del cierre concreto al cambiar el cabello, por lo que no se puede atribuir ese
evento individual con certeza. Además, el empaquetador podía cerrar el estudio con
`Stop-Process -Force`; ese riesgo independiente también queda corregido. La compilación limpia,
el control de dependencias de cabeceras y las pruebas de estrés verifican el estado nuevo.

Defectos reales encontrados en la ruta auditada y corregidos:

1. **Objetos C++ obsoletos en el paquete**: la consola de compilación se pone en UTF-8, el primer
   paquete posterior a la corrección se reconstruye desde cero y una prueba de CMake falla si un
   objeto propio carece de dependencias de cabeceras.
2. **El lanzador cerraba el estudio en uso** (`package.ps1`): ahora se niega a empaquetar mientras
   el estudio está abierto desde `dist\` (código 3, mensaje claro); solo `-CloseRunning` lo cierra.
   `SkyrimNpcStudio.bat` informa y no lanza una segunda instancia.
3. **Edición de color sin ruta específica**: cada color regeneraba la cabeza y reconstruía la
   escena. Ahora el color se aplica al instante a los materiales HairTint (propiedad
   `SceneViewModel.hairTint`) y el FaceGen se actualiza con `face::patch_hair_tint` (solo el color
   del shader; 20–90 ms), con bytes idénticos a una regeneración completa (prueba unitaria).
4. **`TriVertexMismatch` bloqueaba cambios que no usan morphs**: una cabeza de replacer
   (`AAA_GirlHeads`, 3832 vértices) que nombra los TRI vanilla (996) quedaba no exportable incluso
   para un cambio de color. Ahora es error solo si la edición necesita morphs de ese TRI; si no, es
   un aviso informativo. Los sliders sobre esa cabeza siguen bloqueados, con el motivo visible.
5. **Falso «QNAM cambiado» en el worker**: `ApplyFace` reescribía QNAM con alfa 255 y
   `ChangedFields` comparaba ARGB; QNAM solo guarda RGB. Toda exportación facial exigía un FaceTint.
   Ahora se compara RGB y se conserva el valor del registro (prueba de exportación nueva).
6. **Visor de prendas oculto reconstruyendo con cada edición facial** y **vista previa facial de
   otro NPC** aplicada al maniquí del visor de prendas: el visor oculto queda inactivo hasta que se
   muestra, y la vista previa solo se aplica al NPC al que pertenece.
7. **Cola sin límite de reconstrucciones** (`QThreadPool` de un hilo): con ediciones rápidas se
   encolaba una reconstrucción completa por cada pausa de 60 ms. Ahora hay un trabajo en curso y
   como mucho uno pendiente (§2).
8. **Hash de texturas en el hilo de GUI**: `install()` calculaba un hash sobre cientos de MB de
   texturas en cada recarga. Ahora las claves se calculan en el hilo de construcción (geometrías) o
   por identidad del `shared_ptr` con *keepalive* (texturas).

## 2. Modelo asíncrono

`AppearanceEditor` (FaceGen) y `SceneViewModel` (escena 3D) siguen estas reglas:

- **Identificación**: cada trabajo lleva un id de tarea (`diag::next_task_id`) y el FormKey del NPC;
  el resultado guarda el `FaceState` exacto con el que se construyó.
- **Uno en curso, uno pendiente**: si llega una edición mientras se construye, se pide la
  cancelación del trabajo en curso y se marca una reconstrucción pendiente; al volver, se construye
  el borrador más reciente. La cola nunca crece con cada edición.
- **Cancelación cooperativa**: `FaceGenBuildOptions::cancelled` se comprueba entre piezas de cabeza;
  la escena usa `SceneOptions::cancelled`.
- **Resultados obsoletos**: un resultado se instala solo si es del NPC seleccionado, no fue
  cancelado y su estado es igual al borrador actual. Nunca se instala la geometría de una versión
  anterior sobre una posterior.
- **Cambio de NPC**: `set_target` cancela trabajos y análisis; lo que aún llegue se descarta por
  FormKey.
- **Cierre de la aplicación**: los destructores cancelan y esperan a sus pools; los resultados se
  publican con `QPointer` y se descartan si el objeto ya no existe. Ningún hilo toca objetos QML.
- **Hilos**: los objetos Quick 3D (`QQuick3DGeometry`, `QQuick3DTextureData`) se crean y destruyen
  solo en el hilo de GUI; los que dejan de usarse se liberan con `deleteLater` después de que QML
  cambió a los nuevos.
- **Debounce configurable** (QSettings, `HKCU\Software\SkyrimNpcStudio\SkyrimNpcStudio`):
  `preview/faceBuildDebounceMs` (60 por defecto) y `preview/sceneReloadDebounceMs` (200).
- **Memoria**: presupuestos existentes (caché de escena 768 MB, 512 MB de texturas por escena,
  caché FaceGen 256 MB); el visor oculto no construye escenas y las texturas GPU se reutilizan.

## 3. Diagnóstico de cierres

Carpeta: `%LOCALAPPDATA%\SkyrimNpcStudio\SkyrimNpcStudio\diagnostics` (o `SNS_DIAGNOSTICS_DIR`).

- `sessions\session-<fecha>-<pid>.log`: una línea por evento, escrita y vaciada al momento (por eso
  sobrevive incluso a fallos que ningún manejador ve). Incluye NPC seleccionado, acción, color
  anterior y nuevo, inicio/etapa/fin de cada trabajo con su id e hilo (`*` = hilo de GUI), trabajos
  descartados y por qué, exportaciones, y los avisos de Qt/QML (repeticiones agrupadas).
- `crashes\crash-<fecha>-<pid>.txt` + `.dmp`: ante una excepción nativa no controlada (acceso
  inválido…), `std::terminate`, `abort()`, llamada virtual pura o parámetro CRT inválido se escribe un
  informe (excepción, módulo+offset, hilo, contexto NPC/plugin ganador, operaciones en curso con su
  etapa, últimas 256 migas, pila simbolizada) y un minidump. Después el proceso termina: nunca se
  continúa con memoria posiblemente corrupta.
- Al arrancar, si la sesión anterior no terminó limpiamente, la consola lo indica con la ruta.
- Fallos que esquivan cualquier manejador en proceso (corrupción de heap `0xC0000374`, *fail-fast*):
  `scripts\enable-crash-dumps.ps1` (PowerShell como administrador) registra WER LocalDumps para
  `SkyrimNpcStudio.exe` en `diagnostics\wer-dumps`. `-Disable` lo quita.

Para analizar un `.dmp`: abrirlo en Visual Studio o WinDbg junto con el `.pdb` del mismo build.

## 4. Pruebas de regresión

### Escenarios en la ventana real

```
SkyrimNpcStudio.exe --instance <ModOrganizer.ini> --profile <perfil> --npc 0A2C8E:Skyrim.esm --stress-appearance all
```

Código de salida 0 = todos pasan; las exportaciones van a `out\stress-work\<fecha>` (nunca a MO2).

| Escenario | Qué hace | Requisito |
|---|---|---|
| `hair-single` | Un cambio de color; comprueba editor = material del visor = tinte del FaceGen (2× color) y exportabilidad | 1 |
| `hair-sequence` | Diez cambios con espera | 2 |
| `hair-rapid` | 40 cambios cada 15 ms | 3 |
| `ui-hair-clicks` | Pestaña «Pelo/Ojos» y paleta con eventos de ratón reales (10 + 30 clics) | 1–3 |
| `color-dialog` | «Selector libre…» con el diálogo de color real | 1 |
| `viewport-during-build` | Arrastres de órbita y clics de selección sobre el visor mientras se reconstruye | — |
| `early-edit` | Ediciones mientras el NPC aún se carga | 5 |
| `sliders` | 50 cambios de sliders; exportable o rechazo con motivo | 4 |
| `undo-redo` | Deshacer/rehacer rápidos | 6 |
| `reset` | Restablecer durante una reconstrucción | 7 |
| `workspace-during-build` | Cambiar al Estudio de Atuendos durante un trabajo y volver | 8 |
| `original-toggle` | Alternar original/modificado durante la recarga | 9 |
| `replacer-facegen` | Lydia con FaceGen de replacer: modo conservar y exportable | 10 |
| `outfit-during-build` | Quitar y restaurar una prenda mientras se reconstruye la cabeza | 11 |
| `material-preview-block` | Rechazar una variante sin DDS/NIF verificados y permitir exportar al restablecerla | Exportación de vestuario |
| `export-reopen` | Exportar, verificar bytes, guardar proyecto, restablecer y reabrir | 12 |
| `switch-npc` | Cambiar de NPC durante la generación (dos veces) | 5 |
| `quit-during-build` | Cerrar la aplicación con un trabajo activo | 8 |

### Otras pruebas

- `ctest --preset debug`: pruebas unitarias del motor (parche de tinte = regeneración completa,
  shader sin HairTint, regla de TRI, cancelación) y `app:crash report and minidump (…)`, que provoca
  un acceso inválido, `std::terminate` y `abort()` y valida informe y minidump.
- `dotnet test --solution SkyrimNpcStudio.slnx`: incluye
  `ExportFaceOverhaul_HairColorOnlyChangesHclfAndNeedsOnlyTheFaceGenNif`, además de pruebas de
  receta facial con Head Parts, partes extra, morph race vampírica y tintes.
- AddressSanitizer: `cmake -S . -B build\asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc /fsanitize=address" "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /fsanitize=address" -DSNS_BUILD_WORKER=OFF`,
  copiar `bin\worker` de otro build y ejecutar los escenarios con
  `ASAN_OPTIONS=halt_on_error=1,detect_stack_use_after_return=0,windows_hook_rtl_allocators=true`.
  Qt no está instrumentado: ASan vigila el código del estudio, no el interior de Qt.

Verificación del 2026-10-12: reconstrucción limpia Debug y paquete Release correctos; 96/96
pruebas CTest (con el perfil MO2 real configurado), 48/48 pruebas .NET, 17/17 escenarios en
Release y 17/17 con ASan limpio, sin hallazgos de memoria. El paquete autónomo supera además
`hair-single` con Lydia (código de salida 0). Ninja registró dependencias para los 116 objetos
del paquete.

## 5. Límites

- No se ha comprobado nada dentro del juego; las exportaciones quedan «pendiente de comprobar
  dentro del juego».
- No hay minidump del cierre concreto del usuario; el defecto del paquete y el problema del
  empaquetador sí están demostrados (ver §1).
- Los fallos que no pasan por un manejador en proceso solo dejan minidump si se activa WER
  LocalDumps (requiere administrador); el registro de sesión sí los cubre hasta el último evento.
