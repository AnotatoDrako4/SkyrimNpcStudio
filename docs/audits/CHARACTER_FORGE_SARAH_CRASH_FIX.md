# Auditoría P0 — Reparación de Crash de SkyrimNpcStudio al Cargar Sarah (`0xC0000005` en `assimpd.dll`)

## 1. Causa raíz demostrada

Se identificó y demostró con evidencia binaria y minidump de Windows (`crash-20261103-231701-15668.dmp`) que el cierre inesperado de `SkyrimNpcStudio.exe` al cargar los modelos de Sarah (`Sarah_(TLOU)_00X.blend` de `577,7 MiB` y `Sarah-Classic/Sarah.blend` de `352,8 MiB`) era un **Access Violation (`0xC0000005`) por desreferencia de puntero nulo (`rax = 0x0`) en `assimpd.dll+0xcd0cd`** invocado por `Qt6Quick3DAssetUtilsd.dll` (`QQuick3DRuntimeLoader::setSource`) en el hilo principal GUI (`TID 36772`).

### Mecanismo exacto del fallo
1. En los rigs de Sarah (`SarahRig`), las mallas de los ojos (`SarahEye.L` / `SarahBody.003` y `SarahEye.R` / `SarahBody.011`) están emparentadas directamente a huesos mecánicos (`parent_type = 'BONE'`, `parent_bone = 'MCH-eye.L'` y `'MCH-eye.R'`) cuyos huesos tienen `use_deform = False`.
2. Al exportar desde Blender 5.2 con `export_def_bones=True`:
   - El exportador glTF de Blender incluyó `MCH-eye.L` (`node 220`) y `MCH-eye.R` (`node 233`) en el arreglo global `nodes` y dentro de `skins[3].joints` (en los índices `59` / `0x3b` y `72` / `0x48` de `164` / `0xa4` joints).
   - Sin embargo, debido a `export_def_bones=True` y `use_deform=False` en `MCH-eye.L`/`MCH-eye.R`, el exportador **omitió los nodos `220` y `233` del arreglo `children` de `Node 367` (`SarahRig`) y de `scenes[0].nodes`**, dejándolos como nodos huérfanos inalcanzables desde la raíz de la escena (`370` nodos totales, solo `368` alcanzables).
3. Cuando `QQuick3DRuntimeLoader` cargó el GLB a través de `Assimp::glTF2Importer` (`assimpd.dll`), Assimp instanció objetos `aiNode*` únicamente para los nodos alcanzables desde `scenes[0].nodes`. Al iterar sobre `skins[3].joints` (`0xa4` joints), en el índice `59` (`0x3b`, nodo `220` `MCH-eye.L`) el puntero `aiNode*` era `nullptr` (`0x0000000000000000`), y `assimpd.dll+0xcd0cd` ejecutó `mov rcx, qword ptr [rax+28h]` sin verificar nulidad.

### Problemas secundarios corregidos simultáneamente
- **Poda incompleta de objetos ocultos y jaulas de deformación en Blender (`forge_bridge.py`)**: `obj.hide_get()` no detectaba colecciones ocultas ni `hide_viewport`, exportando 34 mallas en `Sarah_(TLOU)_00X.blend` (incluyendo la jaula `Sarah_X.S_DEF` de 7.992 vértices que cubría el personaje) y 33 prendas superpuestas en `Sarah-Classic/Sarah.blend`.
- **Doble carga / cancelación en `CharacterForge.qml`**: `syncPreviewAndAutoConvert()` invocaba `forge.selectLibraryModel(extIdx)` (que ya disparaba `load_selected_library_model_into_scene()`) e inmediatamente llamaba a `forge.scene.load(cachedAfter.resolvedPath)`, cancelando `forge.validate #1` e iniciando `forge.validate #2`.
- **60+ advertencias QML por `Theme.tiny` indefinido**: `ForgeViewport.qml` y `SkyrimConversionWizard.qml` usaban `Theme.tiny` (`undefined`), generando decenas de errores `Unable to assign [undefined] to int`.

---

## 2. Stack trace y módulo responsable

- **Minidump analizado**: `C:\Users\juanc\AppData\Local\SkyrimNpcStudio\SkyrimNpcStudio\diagnostics\crashes\crash-20261103-231701-15668.dmp`
- **Log de sesión correlacionado**: `C:\Users\juanc\AppData\Local\SkyrimNpcStudio\SkyrimNpcStudio\diagnostics\logs\session-20261103-231430-15668.log`
- **Excepción**: `0xC0000005` (`EXCEPTION_ACCESS_VIOLATION` leyendo dirección `0x0000000000000028`)
- **Instrucción**: `assimpd!Assimp::glTF2Importer + 0xcd0cd` (`00007ffc5fa8d0cd 488b4828 mov rcx, qword ptr [rax+28h]`, con `rax = 0x0`, índice de joint `[rsp+80h] = 0x3b` (`59`), total joints `[rsp+84h] = 0xa4` (`164`)).

```text
00 00000046`9eeefea0 00007ffc`5fa95b70 assimpd!Assimp::GetglTF2ExporterEnabled+0xb99d7 [assimpd.dll+0xcd0cd]
01 00000046`9eeeffd0 00007ffc`5fa912b3 assimpd!Assimp::GetglTF2ExporterEnabled+0xc247a
02 00000046`9eef03c0 00007ffc`5f9ef63a assimpd!Assimp::GetglTF2ExporterEnabled+0x20801
03 00000046`9eef0410 00007ffc`5f9fb6b0 assimpd!Assimp::Importer::ReadFile+0x7c0
04 00000046`9eef08e0 00007ffc`82315a4b Qt6Quick3DAssetImportd!QSSGAssetImportManager::importFile+0x...
05 00000046`9eef0be0 00007ffc`7fa77c3c Qt6Quick3DAssetUtilsd!QQuick3DRuntimeLoader::setSource+0x...
...
27 00000046`9eef4aa0 00007ff7`071f8953 SkyrimNpcStudio!sns::ForgeSceneViewModel::sourceChanged+0x23 [forge_scene_viewmodel.cpp @ 419]
28 00000046`9eef4ad0 00007ff7`073ec7c1 SkyrimNpcStudio!sns::ForgeSceneViewModel::load::<lambda_1>::operator()+0x1511 [forge_scene_viewmodel.cpp @ 343]
```

---

## 3. Archivos modificados

1. [tools/blender/forge_bridge.py](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/tools/blender/forge_bridge.py)
2. [engines/gltf/src/gltf_inspect.cpp](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/engines/gltf/src/gltf_inspect.cpp)
3. [tests/unit/engines/test_gltf_inspect.cpp](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/tests/unit/engines/test_gltf_inspect.cpp)
4. [apps/desktop/viewmodels/forge_scene_viewmodel.cpp](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/apps/desktop/viewmodels/forge_scene_viewmodel.cpp)
5. [apps/desktop/viewmodels/character_forge_viewmodel.cpp](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/apps/desktop/viewmodels/character_forge_viewmodel.cpp)
6. [apps/desktop/qml/characterforge/CharacterForge.qml](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/apps/desktop/qml/characterforge/CharacterForge.qml)
7. [apps/desktop/qml/characterforge/ForgeViewport.qml](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/apps/desktop/qml/characterforge/ForgeViewport.qml)
8. [apps/desktop/qml/characterforge/SkyrimConversionWizard.qml](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/apps/desktop/qml/characterforge/SkyrimConversionWizard.qml)

---

## 4. Cambios implementados

- **Defensa en profundidad contra joints huérfanos (3 capas)**:
  1. **En Blender (`forge_bridge.py`)**: Antes de exportar con `export_def_bones=True`, se marcan como `use_deform = True` todos los huesos destino de `obj.parent_type == "BONE"` y toda la cadena de ancestros (`.parent`) de cada hueso deformable, evitando que Blender deje mallas emparentadas a huesos o joints sin padre en la escena. Además, se ejecuta `_sanitize_and_verify_glb(output)` con `os.fsync()` antes de salir.
  2. **En el cargador/staging C++ (`gltf_inspect.cpp`)**: `sanitize_glb_orphan_joints()` inspecciona y repara en memoria cualquier GLB previamente exportado que contenga `skin.joints` o `skin.skeleton` inalcanzables desde `scenes[0].nodes` antes de escribir el archivo en staging para `RuntimeLoader`.
  3. **En el validador glTF (`gltf_inspect.cpp` + `test_gltf_inspect.cpp`)**: `Checker::skins()` verifica que cada índice en `skin.joints` y `skin.skeleton` esté en `scene_reachable_`, emitiendo el error `NodeHierarchyInvalid` si algún joint es huérfano.
- **Poda inteligente de visibilidad en Blender (`forge_bridge.py`)**:
  - Uso de `obj.visible_get()` y filtro de `display_type in ("WIRE", "BOUNDS")` y sufijos `.S_DEF`, reduciendo `Sarah_(TLOU)_00X.blend` de 34 a **15 mallas visibles (`14,5 MB`)** y `Sarah-Classic/Sarah.blend` de 33 a **9 mallas visibles (`23,6 MB`)**, más eliminación de armatures no referenciados (`metarig`, `PenisRig`, `Tie`, `HairSchool`).
- **Sincronización y ciclo de vida en C++/QML**:
  - Guardia idempotente en `ForgeSceneViewModel::load()` para no cancelar ni recargar el mismo GLB cuando ya está en `"validating"`, `"loading"` o `"loaded"`.
  - Eliminación de la llamada duplicada a `forge.scene.load()` en `CharacterForge.qml`.
  - Auto-guardado de `project.json` al completar la conversión de Blender y preservación de `sourcePath` original (`.blend`) para exportación a Skyrim NIF/STAT/MO2.
  - Reemplazo de `Theme.tiny` por `Theme.small` y guardias de nulidad en `ForgeViewport.qml`.

---

## 5. Comando de compilación y despliegue

```powershell
cmake --build "D:\.Proyectos Games\SkyrimNpcStudio-ui2\build\claude-forge" --target SkyrimNpcStudio sns_engine_tests --parallel 4
C:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe --debug --qmldir "D:\.Proyectos Games\SkyrimNpcStudio-ui2\apps\desktop\qml" "D:\.Proyectos Games\SkyrimNpcStudio-ui2\build\claude-forge\bin\SkyrimNpcStudio.exe"
```

---

## 6. Resultados de las pruebas

- **Tests unitarios del motor glTF (`sns_engine_tests.exe "[gltf]"`)**:
  - `All tests passed (138 assertions in 9 test cases)` incluyendo el nuevo test de regresión para joints huérfanos en skins.
- **Verificación de integridad del archivo original**:
  - `C:\Users\juanc\Documents\blender.Sarah_(TLOU)_00X\Sarah_(TLOU)_00X.blend`: intacto (`605.767.438` bytes, SHA-256 `E54457C5357E00229F4FEE11CF2EC782FE7377103F20EA488B0A93DB73C24610`).
- **Prueba en ventana real (`SkyrimNpcStudio.exe --forge-preview`) con `Sarah_(TLOU)_00X` (`tlou-converted/model.glb`)**:
  - `PID=27620`, `EXIT=0`, `ELAPSED_MS=46479`, `state=loaded`, `15` mallas renderizadas (`100.113` vértices, `189.676` triángulos), `pixels=4.0%`, selección de partes en visor funcional (`Sarah_X.003`), `summary: PASSED`.
- **Ejecución interactiva independiente**:
  - `SkyrimNpcStudio.exe` lanzado y activo en escritorio (`PID=22872`, `WS_MB=395.3`, `ALIVE=True`).

---

## 7. Comparativa de memoria y tiempos (Antes vs. Después)

| Métrica | Antes (Crash / Sin poda `visible_get`) | Después (Con reparación y poda `visible_get`) |
|---|---|---|
| Estado al cargar Sarah en `RuntimeLoader` | **CRASH `0xC0000005`** en `assimpd.dll+0xcd0cd` | **`loaded` (`EXIT=0`, `PASSED`)** |
| Tamaño GLB `Sarah_(TLOU)_00X` | `125,4 MB` (4K) / `24,5 MB` (34 mallas con jaula `.S_DEF`) | **`14,5 MB`** (15 mallas visibles reales, con ojos) |
| Tamaño GLB `Sarah-Classic` | `54,4 MB` (33 mallas con ropa oculta superpuesta) | **`23,6 MB`** (9 mallas visibles reales, 1 armature) |
| Tiempo de exportación en Blender | `18,6 s` – `51,0 s` | **`7,8 s` – `10,3 s`** |
| Advertencias QML en `ForgeViewport` | `60+` (`Theme.tiny` undefined) | **`0`** |

---

## 8. Capturas reales del visor

- Render en ventana real de `Sarah_(TLOU)_00X`:
  `D:\.Proyectos Games\SkyrimNpcStudio-ui2\out\antigravity\character-forge-3.0\sarah-crash-verify\tlou-converted\forge-1-model.png`

---

## 9. Limitaciones pendientes

- **Cierre de Blender 5.2 tras `export_scene.gltf`**: Blender 5.2 presenta una corrupción interna en el teardown de sus addons C++/Draco al cerrar la escena después de `bpy.ops.export_scene.gltf` en rigs complejos como Sarah. Por ello, `forge_bridge.py` valida primero la integridad estructural completa del GLB (`_sanitize_and_verify_glb`) y ejecuta `os.fsync()` tanto en `model.glb` como en `report.json` antes de terminar el subproceso con `os._exit(0)`.
