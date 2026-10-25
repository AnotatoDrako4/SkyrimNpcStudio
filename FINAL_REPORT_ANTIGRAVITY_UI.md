# FINAL REPORT — GOOGLE ANTIGRAVITY: SKYRIM NPC STUDIO ULTIMATE
## Universal Mod Asset Workbench & Visual Mod Creator

- **Repositorio:** `https://github.com/AnotatoDrako4/SkyrimNpcStudio`
- **Directorio local:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2`
- **Stack técnico:** C++20 · Qt 6.11.2 (Qt Quick / QML + Qt Quick 3D) · .NET 10.0 (`BethesdaWorker` / Mutagen) · Google Stitch MCP Design System (`projects/9939426075158331863` / `assets/d2832f08b1974b10a01988e5b3ebf22a`)

---

## 1. Resumen Ejecutivo de Entregables

### AG-UI-01 — Explorador Universal de Recursos (`Explorador de recursos`)
1. **Separación estricta entre `selectArmor(formKey)` y `selectMesh(dataRelativePath)`:**
   - Se corrigió el defecto por el que `LooseResourceSection.qml` y `ArmorCatalog.qml` emitían rutas NIF sueltas hacia `studio.armorPreview.selectArmor(formKey)`.
   - `SceneViewModel::selectArmor` ahora valida y rechaza cualquier ruta de archivo (`.nif`, `.dds`, `.osp`, `.bsa`, `/`, `\`) registrando el diagnóstico explícito `InvalidArmorFormKey` sin enviar rutas falsas al worker IPC.
   - `SceneViewModel::selectMesh(dataRelativePath)` almacena la ruta normalizada en la nueva propiedad `selectedMesh` (`Q_PROPERTY(QString selectedMesh READ selected_mesh NOTIFY previewModeChanged)`), permitiendo previsualizar mallas NIF sueltas tanto de forma aislada (`piece`) como sobre el cuerpo/esqueleto del NPC (`mannequin`, `npc`, `outfit`).
   - En `ArmorCatalog.qml` se añadió la señal `meshPreviewRequested(string dataRelativePath)` conectada en `Main.qml` y `CharacterCreator.qml` a `studio.armorPreview.selectMesh(dataRelativePath)`.
2. **Asociación completa entre rutas NIF y registros `ARMO` (`replacer-mesh`):**
   - `StudioController::searchArmors` normaliza `maleModel` y `femaleModel` (incluyendo variantes de peso `_0.nif` y `_1.nif`) y sincroniza reactivamente `registered_armor_meshes_` con `LooseResourceLibrary::set_registered_meshes(...)` tras completarse `kCatalogIndex`.
   - `LooseOutfitIndexer::index` empareja variantes `_0.nif`, `_1.nif` y rutas base contra `registered_mesh_to_armo`, identificando replacers reales (`replacer-mesh`) con `confidence = "confirmed"` y su `armorFormKey` correspondiente.
   - En `LooseResourceSection.qml`, los recursos `replacer-mesh` habilitan el botón **Equipar ARMO** (`StudioController::equipArmorByFormKey`), mientras que las mallas sueltas sin plugin (`loose-mesh`) mantienen la equipación directa bloqueada con explicación clara hasta que cuenten con un registro `ARMO` válido.
3. **Detección multi-formato y vistas previas reales:**
   - **Mallas NIF de ropa, armas, objetos y FaceGen:** `loose-mesh`, `replacer-mesh`, `weapon-mesh`, `facegen-mesh` con validación de cabecera Gamebryo/NIF (`RES-03`) y vista previa 3D real en `CharacterViewport`.
   - **Proyectos BodySlide (`.osp` / `.xml`):** detección de SliderSets, familia corporal (`CBBE 3BA`, `CBBE`, `BHUNP`, `UBE`, `UNP`) y previsualización 3D cuando existe la malla construida en el VFS.
   - **Texturas DDS (`texture-dds`, `facegen-tint`):** `LooseResourceLibrary::inspectTexture(dataRelativePath)` decodifica el archivo DDS real (suelto o desde BSA mediante `AssetResolver` + `sns::textures::parse_dds` / `decode_rgba`), mide su canal alfa y genera una vista previa PNG 2D real en el diálogo `texturePreviewDialog` junto con resolución original, formato BC1–BC7, niveles mip y mod proveedor.
   - **Archivos BSA (`bsa-archive`):** `LooseResourceLibrary::inspectBsaArchive(dataRelativePath)` inspecciona la cabecera y el directorio del archivo BSA sin extraerlo por completo, mostrando versión, total de archivos empaquetados y muestra de entradas.
   - **Archivos sin motor visual directo (`unsupported-file`: `.hkx`, `.pex`, `.seq`):** reportan explícitamente `UNSUPPORTED_PREVIEW` con metadatos, tamaño, procedencia MO2 y motivo técnico, sin fingir vistas previas falsas.

---

### AG-UI-02 — Visualizador 3D Avanzado (`CharacterViewport.qml` & `SceneViewModel`)
- Rotación orbital, zoom, desplazamiento (pan), encuadre por pieza (`frame`) y presets de cámara frontal, lateral, posterior, rostro y cuerpo completo.
- Iluminación ambiental configurable, modos de diagnóstico de texturas/normales/UV/huesos y caché de geometrías y texturas en GPU (`geometry_pool_`, `uv_pool_`, `texture_pool_`).
- Nuevos métodos `SceneViewModel::setPieceVisible(keyOrPath, visible)` e `isPieceVisible(keyOrPath)` integrados en el **Inspector Técnico** para mostrar/ocultar piezas individuales en un clic.
- Nuevas secciones en el **Inspector Técnico** de `CharacterViewport.qml`:
  - **Piezas y Slots en Escena (`scene.outfitDiagnostics`):** muestra cada prenda, slots ocupados, ruta NIF exacta, mod proveedor ganador de MO2 y conflictos de slots (qué pieza desequipó a cuál).
  - **Cuerpo Cubierto por Slots (`scene.coveredBodyDiagnostics`):** detalla qué submallas del cuerpo quedan ocultas bajo qué slots de armadura.
- **Transparencia sobre físicas SMP/FSMP:** se inspecciona y valida el XML físico (`PhysicsXmlInvalid`, `PhysicsXmlMissing`, huesos declarados y shapes de colisión) sin fingir simulación física en tiempo real.

---

### AG-UI-03 — Editor Visual de Vestuario e Inventarios (`OutfitEditor` & `OutfitPanel.qml`)
- Flujo integrado **NPC → Rostro → Cabello → Cuerpo → Vestuario → Inventario → Exportar mod**.
- Se extendió `OutfitEditor` con soporte completo de inventario del NPC (`inventoryItems`, `inventoryModified`, `addInventoryItem`, `setInventoryItemCount`, `removeInventoryItem`, `resetInventory`) respaldado por comandos reversibles (`InventoryCommand`) en la pila global `EditHistory` (`undo()` / `redo()`).
- En `OutfitPanel.qml`:
  - Cada pieza equipada incluye el botón rápido `+Inv` para añadir copias al inventario del NPC.
  - Nueva sección **Inventario del NPC** con controles `−` / `+` de cantidad, eliminación individual y restauración al inventario original del registro `NPC_`.
- Exportación validada mediante `BethesdaWorker` (`ExportOutfitPatch` / `ExportCompleteOverhaul`) con bloqueo de seguridad sobre carpetas reales del juego y de MO2 (`export_destination.cpp`).

---

### AG-UI-04 — Diseño Visual con Google Stitch MCP y Traducción a Qt/QML
- Se descubrió y utilizó el servidor MCP `stitch` para crear el proyecto `projects/9939426075158331863` (*Skyrim NPC Studio Ultimate - Universal Mod Asset Workbench*) y el sistema de diseño `assets/d2832f08b1974b10a01988e5b3ebf22a` sincronizado con `docs/DESIGN.md`.
- Todos los patrones visuales (paleta oscura neutra de precisión, jerarquía tipográfica `Segoe UI` + `Cascadia Mono`, chips de filtro, badges semánticos de procedencia MO2 y diálogos de inspección 2D/BSA) fueron traducidos directamente a controles nativos Qt Quick Controls 2 respetando `Theme.qml` y el sistema de escala 85%–150%.

---

## 2. Resultados de Pruebas y Verificación en Windows

| Suite de Pruebas | Resultado | Detalles |
| :--- | :--- | :--- |
| `sns_engine_tests.exe` | **PASS (100%)** | 184 casos de prueba · 2,050 aserciones (`RES-01`, `RES-02`, `RES-03`, `Universal Resource Explorer`) |
| `dotnet test SkyrimNpcStudio.slnx` | **PASS (100%)** | 234/234 pruebas (`BethesdaWorker.Tests` + `SkyrimNpcStudio.ExportTests`) |
| `sns_workbench_tests.exe` | **PASS (100%)** | 13 casos · 195 aserciones (layouts, redimensionamiento, escala, persistencia) |
| `sns_settings_tests.exe` | **PASS (100%)** | 9 casos · 743 aserciones |
| `sns_portrait_cache_tests.exe` | **PASS (100%)** | 14 casos · 1,218 aserciones |
| `sns_export_destination_tests.exe` | **PASS (100%)** | 4 casos · 26 aserciones (protección de `Skyrim/Data` y `MO2/mods`) |
| `sns_forge_asset_tests.exe` | **PASS (100%)** | 18 aserciones |
| Verificación en vivo (`SkyrimNpcStudio.exe`) | **PASS** | Ejecución real contra `D:\Apps\ModOrganizer.ini` (`Serana` `002B6C:Dawnguard.esm`) |
