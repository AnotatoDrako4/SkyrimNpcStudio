# Character Forge — registro de implementación

Registro vivo de la integración 3D de Character Forge. Estados: `PENDIENTE`, `EN DESARROLLO`,
`IMPLEMENTADO SIN VERIFICACIÓN`, `VERIFICADO EN CLOUD`, `VERIFICADO EN WINDOWS`,
`VERIFICADO EN SKYRIM`, `BLOQUEADO`. Ninguna función de esta línea está `VERIFICADO EN SKYRIM`.

## Entrega 1 — visor 3D real (CF-0, CF-1, inicio de CF-2)

| Campo | Valor |
|---|---|
| Fecha | 2026-10-13 |
| Rama / base | `feature/character-forge-runtime-3d` desde `origin/main` 298990d |
| Entorno | Windows 11 Pro 26100, MSVC 14.44.35207 (solo idioma español), Qt 6.11.2 msvc2022_64, .NET SDK 10.0.401, Blender 5.2.2 LTS, render D3D11 |
| Objetivo | Seleccionar un GLB/glTF real y verlo en el viewport de Character Forge con cámara funcional, errores visibles y cargas repetidas seguras |

### CF-0 — estabilización de la base — VERIFICADO EN WINDOWS

- `scripts/local-review.ps1` sobre 298990d sin cambios: build, `dotnet test`, `ctest`, smoke → `passed`
  (evidencia `out/local-review/20261013-205509-298990d/summary.json`, no versionada).
- `scripts/smoke-test.ps1 -WorkspaceIndex 2 -Size 1600x900 -ShowWindow` → `OK`, sin avisos QML.
- Hallazgo: sin perfil MO2, el diálogo modal de instancia (`WelcomeOverlay`, `NoAutoClose`) no se
  podía cerrar y bloqueaba Character Forge, que no necesita MO2 para crear proyectos ni para la
  vista previa. Corregido con el botón «Continuar en Character Forge sin perfil».

### Decisiones técnicas

1. **RuntimeLoader de Qt 6.11.2** para renderizar (no un segundo motor ni un parser de render
   propio). APIs comprobadas en la instalación y el código fuente: ver
   [CHARACTER_FORGE_TECHNICAL_SOURCES.md](CHARACTER_FORGE_TECHNICAL_SOURCES.md). `query()`/`queryAll()`
   (Qt 6.12) no se usan; no se migra a 6.12.
2. **Validación propia antes de renderizar** (`engines/gltf`, sin Qt): la documentación de Qt avisa
   que RuntimeLoader no valida ni aísla. Se comprueban contenedor GLB (cabecera, longitud, chunks),
   JSON, versión 2.x, extensiones obligatorias (Draco/meshopt/Basis se rechazan), cada referencia,
   rangos de buffers/vistas/accessors, **cada índice** contra el número de vértices, jerarquía de
   nodos (sin ciclos ni dos padres), skins, animaciones, imágenes (PNG/JPEG) y límites de tamaño.
3. **Recursos confinados**: URIs relativas sin `..`, sin unidad, sin esquema (`http:`, `file:`),
   sin barras invertidas ni rutas absolutas; enlaces simbólicos y rutas canónicas fuera de la
   carpeta del modelo se rechazan. Los `data:` base64 se aceptan con límite.
4. **Staging de los bytes validados**: el visor nunca lee los archivos originales; recibe una copia
   en `%TEMP%\sns-forge-preview-<pid>\<operación>\` escrita con los mismos bytes que se validaron
   (sin ventana entre validación y carga). Solo queda la copia del modelo mostrado; las carpetas
   de procesos terminados se limpian al iniciar.
5. **Límites de vista previa** (importación síncrona en el hilo de GUI): archivo 256 MiB,
   recursos 512 MiB, 5 M vértices y 5 M triángulos.
6. **Operaciones numeradas**: cada carga, cancelación o reinicio incrementa el número; resultados
   de validación e informes del visor de una operación anterior se descartan.
7. **Estado independiente**: `ForgeSceneViewModel` pertenece a `CharacterForgeViewModel`; no usa
   la escena de Character Creator ni la de Outfit Studio.
8. **Tres niveles de datos**: declarado (JSON), validado (datos comprobados) y renderizado
   (`bounds` informados por el visor). La UI no los mezcla.

### Archivos

Nuevos: `engines/gltf/` (validador), `apps/desktop/viewmodels/forge_scene_viewmodel.*`,
`apps/desktop/components/forge_preview_check.*` (verificación en la ventana real),
`tests/unit/engines/test_gltf_inspect.cpp`, `tools/blender/make_test_models.py`,
`tests/fixtures/models/` (generados con Blender 5.2.2 a partir de primitivas: `suzanne.glb`
24 740 B, `rigged_figure.glb` 117 848 B, `rigged_figure_split/` .gltf + .bin + .png),
`docs/CHARACTER_FORGE_TECHNICAL_SOURCES.md`, este registro.

Modificados: `ForgeViewport.qml` (View3D + RuntimeLoader + cámara), `ModelImporter.qml`
(declarado/validado/renderizado, «Recargar vista previa»), `ForgeAdvancedInspector.qml`,
`WelcomeOverlay.qml` y `Main.qml` (Character Forge sin perfil), `character_forge_viewmodel.*`
(propiedad `scene`, `modelPreview: true`, la vista previa sigue a la selección, deshacer/rehacer y
proyecto abierto), `main.cpp` (`--forge-preview`, `--forge-capture`), CMake, `smoke-test.ps1`
(`-ForgePreview`), `local-review.ps1` (paso `forge-preview`), `DEVELOPMENT_PLAN.md`.

### Errores encontrados y corregidos durante la entrega

| Problema | Evidencia | Corrección |
|---|---|---|
| Escena vacía con el modelo cargado | Sonda `qml.exe`: Success pero nada visible; con `clipNear: 0.01` visible | `clipNear`/`clipFar` explícitos y adaptados al radio al encuadrar |
| `bounds` nunca llegaba al ViewModel | Registro de diagnóstico: un único informe con (0,0,0); código fuente 6.11.2 de `bounds()`/`updateSpatialNode` | `bounds` se lee solo tras `boundsChanged`; encuadre provisional con los límites validados del glTF |
| Registro de tipo QML imposible | MSVC C3246: `QQmlElement<T>` no puede heredar de una clase `final` construible | `ForgeSceneViewModel` deja de ser `final` |
| Limpieza que podía apuntar al directorio actual | Revisión: `QDir("")` es el directorio de trabajo | `remove_below(root, folder)` solo borra carpetas bajo la raíz de staging propia |

### Pruebas ejecutadas (Windows)

| Comando | Resultado |
|---|---|
| `build\debug\bin\sns_engine_tests.exe "[gltf]"` | 8 casos, 112 aserciones, todas superadas |
| `ctest --preset debug -j 4` | 114/114 superadas (8 `[local]` omitidas sin `SNS_LOCAL_*`), ~5 s |
| `dotnet test --solution SkyrimNpcStudio.slnx` | 65/65 superadas |
| `SkyrimNpcStudio.exe --forge-preview <6 archivos> --forge-capture … --smoke-size 1600x900` (Debug) | PASSED en ~12 s: Suzanne (555 vért., 968 tri., 7,5 % de píxeles del View3D, bounds 2,73×1,97×1,70), figura con armature (1728 vért., 2012 tri., bounds 0,5×1,8×0,5, 8,4 %), glTF separado (8,5 %), GLB truncado → «GLB truncado o incompleto.», glTF con `../` → «…fuera de su carpeta: bloqueado.», recarga de Suzanne; una sola carpeta de staging al final |
| `scripts/local-review.ps1` | build, dotnet-test, ctest, smoke y forge-preview `passed` (`out/local-review/20261013-212609-298990d`) |
| `windeployqt --dry-run --release --qmldir apps/desktop/qml` | Incluye `Qt6Quick3DAssetUtils`, `Qt6Quick3DAssetImport`, `assetimporters/assimp.dll`, `qml/QtQuick3D/AssetUtils` |

Capturas: `out/local-review/forge-preview-debug4/forge-*.png` (no versionadas).

### Estado por función

| Función | Estado |
|---|---|
| Validación GLB/glTF (`engines/gltf`) | VERIFICADO EN WINDOWS |
| Visor: carga, geometría visible, encuadre, frontal/lateral/posterior, restablecer, malla | VERIFICADO EN WINDOWS (Debug; captura por píxeles) |
| Órbita, zoom y desplazamiento con ratón | IMPLEMENTADO SIN VERIFICACIÓN (controlador de Qt; la prueba automática no arrastra) |
| Errores visibles (truncado, escape de ruta) y reintento | VERIFICADO EN WINDOWS |
| Cargas repetidas y limpieza de staging | VERIFICADO EN WINDOWS |
| Paquete `dist` con AssetUtils/assimp | IMPLEMENTADO SIN VERIFICACIÓN |
| «Huesos» y «Original / adaptado» en el visor | BLOQUEADO (RuntimeLoader 6.11 no expone articulaciones; no hay modelo adaptado): deshabilitados con explicación |
| FBX/OBJ/BLEND | PENDIENTE (puente de Blender) |

### Limitaciones

- La importación de RuntimeLoader es síncrona: un modelo cerca de los límites congela la interfaz
  mientras se importa.
- Los `bounds` declarados no aplican transformaciones de nodo; solo sirven de encuadre provisional.
- Validar no equivale a ser apto para Skyrim: no hay conversión a NIF, esqueleto de Skyrim ni FaceGen.
- La selección de geometrías individuales en el visor queda para CF-2 (sin `query()` en 6.11).

### Próximos pasos

1. CF-2: selección e inspección por geometría compatible con 6.11 (picking del View3D + correspondencia
   con las mallas del análisis propio), y prueba de paquete `dist` con un GLB.
2. CF-3: biblioteca de assets dentro de `.sncf` con migración.
3. CF-4: Blender Bridge determinista (FBX → GLB validado → vista previa).
