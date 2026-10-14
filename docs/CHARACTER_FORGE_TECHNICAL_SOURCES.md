# Character Forge — registro de fuentes técnicas

Cada decisión técnica de Character Forge con la API investigada, dónde se comprobó y cómo se
verificó. Solo figuran fuentes consultadas de verdad; una URL no se cita como evidencia de algo que
esa página no documenta. Niveles: (1) repositorio, (2) dependencia instalada, (3) documentación
oficial de la versión, (4) código fuente oficial, (5) prueba reproducible.

## Qt Quick 3D — `RuntimeLoader` (vista previa GLB/glTF)

| Campo | Valor |
|---|---|
| Componente | `apps/desktop/qml/characterforge/ForgeViewport.qml` |
| API | `QtQuick3D.AssetUtils/RuntimeLoader`: `source`, `status` (Empty/Success/Error), `errorString`, `bounds`, `instancing`, `supportedExtensions`, `supportedMimeTypes` |
| Biblioteca / versión | Qt 6.11.2 (`D:\DevTools\Qt\6.11.2\msvc2022_64`) |
| Fuentes | (2) `qml/QtQuick3D/AssetUtils/plugins.qmltypes` e `include/QtQuick3DAssetUtils/6.11.2/.../qquick3druntimeloader_p.h`; (3) https://doc.qt.io/qt-6.11/qml-qtquick3d-assetutils-runtimeloader.html; (3) https://doc.qt.io/qt-6/qml-qtquick3d-assetutils-runtimeloader.html (página 6.12.0); (3) https://doc.qt.io/qt-6.11/qtquick3d-runtimeloader-example.html; (4) https://raw.githubusercontent.com/qt/qtquick3d/v6.11.2/src/assetutils/qquick3druntimeloader.cpp |
| Consulta | 2026-10-13 |
| Formatos | Documentación: ".obj and glTF version 2.0 files in both text (.gltf) and binary (.glb)". Código: `supportedExtensions()` filtra `obj`, `gltf`, `glb` según los plugins importadores instalados (`plugins/assetimporters/assimp.dll`). |
| `status` / `errorString` | `Empty` sin URL; `Success` ("Success!"); `Error` con "IO Error: …" o "Unsupported: …" (código 6.11.2). |
| `bounds` | Documentado: "may not be available before the first render". Código: `bounds()` calcula de forma perezosa y marca los límites como limpios aunque salgan vacíos; `updateSpatialNode` solo emite `boundsChanged` mientras están sucios. **Leer `bounds` antes de `boundsChanged` suprime la señal** (reproducido en la app; ver registro de implementación). El visor solo los lee tras la señal. |
| Carga | Código: `setSource` → `loadSource()` → `QSSGAssetImportManager::importFile` **síncrono en el hilo de GUI**. No hay API asíncrona en 6.11. |
| Seguridad | Documentación: "RuntimeLoader does not sandbox or validate asset contents. Loading malformed or untrusted assets may have security implications." → validación previa propia (`engines/gltf`) y copia aislada de los bytes validados. |
| No disponible en 6.11.2 | `query()` y `queryAll()` (documentados "since 6.12" en la página 6.12.0). La página 6.11.2 menciona el enum `QueryFilter` ("See also queryAll()"), pero el módulo instalado no lo exporta (`plugins.qmltypes` sin `QueryFilter` ni métodos). **No se usan.** La inspección de nodos, mallas, huesos y materiales sale del análisis propio del glTF. |
| Limitaciones | Sin aislamiento; importación síncrona (por eso se limitan tamaño y vértices); sin acceso a articulaciones importadas. |
| Verificación | (5) Sonda `qml.exe` con `RuntimeLoader` y el GLB de prueba: estado Success, `bounds` (−1.37, −0.98, −0.85)…(1.37, 0.98, 0.85), render visible. (5) App: `--forge-preview` en Windows, ver CHARACTER_FORGE_IMPLEMENTATION_LOG.md. **VERIFICADO EN WINDOWS** (Debug, D3D11). |

## Qt Quick 3D — `PerspectiveCamera` y `OrbitCameraController`

| Campo | Valor |
|---|---|
| Componente | `ForgeViewport.qml` |
| API | `PerspectiveCamera.clipNear/clipFar/fieldOfView`; `QtQuick3D.Helpers/OrbitCameraController` (`origin`, `camera`, `panEnabled`, `automaticClipping`) |
| Fuente | (2) `qml/QtQuick3D/Helpers/OrbitCameraController.qml` de la instalación 6.11.2 |
| Comprobado | Arrastre izquierdo = órbita; `ctrlDragHandler` con `Qt.ControlModifier` = desplazamiento; `WheelHandler` modifica `camera.z` (zoom); `automaticClipping` ajusta los planos según `camera.z`. |
| Hallazgo | `clipNear` vale 10 por defecto: un modelo glTF en metros a 5–6 unidades queda recortado entero (sonda: escena vacía; con `clipNear: 0.01`, visible). El visor fija y adapta los planos al encuadrar. |
| Estado | VERIFICADO EN WINDOWS (sonda y app). |

## glTF 2.0 / GLB (validación propia)

| Campo | Valor |
|---|---|
| Componente | `engines/gltf` (`sns::gltf::inspect`, `load_and_inspect`, `safe_relative_uri`) |
| Especificación | Khronos glTF 2.0 (contenedor GLB: cabecera de 12 bytes, chunk JSON `0x4E4F534A` primero, BIN `0x004E4942` opcional; tipos de componentes 5120–5126; `byteStride` 4–252 múltiplo de 4; un nodo con a lo sumo un padre; el frente del asset mira a +Z). Aplicada desde el conocimiento de la especificación; no se consultó la URL de Khronos en esta sesión. |
| Contrastado con | (5) Archivos reales exportados por Blender 5.2.2 (`tests/fixtures/models`); pruebas Catch2 `[gltf]` (8 casos, 112 aserciones). |
| Limitaciones | No decodifica Draco/meshopt/Basis (se rechazan si son obligatorios); comprueba índices y rangos, no la topología ni las normales; los `bounds` declarados son de espacio de malla (sin transformaciones de nodo). |
| Estado | VERIFICADO EN WINDOWS (Catch2 + carga real en la app). |

## Blender

| Campo | Valor |
|---|---|
| Componente | `tools/blender/make_test_models.py` (fixtures); Blender Bridge pendiente |
| Versión | Blender 5.2.2 LTS (`D:\Program Files\blender.exe`, build 2026-09-15) |
| Fuente | (2) Introspección en la versión instalada (`blender --background --factory-startup --python`): existen `bpy.ops.export_scene.gltf` (propiedades `filepath`, `export_format`, `use_selection`, `export_apply`, `export_skins`, `export_morph`, `export_animations`, `export_yup`), `bpy.ops.import_scene.gltf`, `bpy.ops.import_scene.fbx`, `bpy.ops.wm.obj_import`, `bpy.ops.mesh.primitive_monkey_add`. `export_format` es un enum dinámico (sin elementos estáticos en RNA); los valores `GLB` y `GLTF_SEPARATE` se comprobaron exportando de verdad. |
| Referencias a consultar en la fase del bridge | https://docs.blender.org/api/current/bpy.ops.export_scene.html y https://docs.blender.org/api/current/bpy.ops.import_scene.html (no consultadas aún en esta sesión: la evidencia actual es la introspección de la versión instalada). |
| Estado | Generación de fixtures VERIFICADA EN WINDOWS; bridge PENDIENTE. |

## Windows / despliegue

| Campo | Valor |
|---|---|
| API | `windeployqt` 6.11.2 + `scripts/package.ps1` |
| Comprobado | `apps/desktop/CMakeLists.txt` enlaza `Qt6::Quick3DAssetUtils`; `scripts/package.ps1` despliega `Qt6Quick3DAssetUtils.dll`, `Qt6Quick3DAssetImport.dll`, `assetimporters/assimp.dll` y `qml/QtQuick3D/AssetUtils`. |
| Verificación | Paquete `dist/SkyrimNpcStudio` probado de forma autónoma con `smoke-test.ps1 -Binary dist/SkyrimNpcStudio/SkyrimNpcStudio.exe -ForgePreview`: 6/6 modelos procesados (carga, selección por clic, caja 3D, visibilidad y rechazo de truncado) con salida PASSED y código 0. |
| Estado | VERIFICADO EN WINDOWS. |
