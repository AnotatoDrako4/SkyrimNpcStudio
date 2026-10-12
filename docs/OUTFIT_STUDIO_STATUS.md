# Outfit Studio 3D + SMP Compatibility: Estado de la integración

Auditoría y alcance de entrega completada en `main`.

## Contrato de vista previa

La selección del catálogo es un FormKey ARMO real. El worker resuelve los ARMA efectivos para el sexo, la raza y el Armor Race del NPC de referencia (o maniquí humanoide de respaldo `000A2C8E:Skyrim.esm` cuando no hay ningún NPC seleccionado). La escena usa los NIF equipados de ARMA, no el WorldModel del inventario. `AssetResolver` elige los NIF, XML y DDS ganadores entre archivos sueltos de MO2 y BSA; `build_character_scene` conserva el skinning y la pose seleccionada.

La escena de prendas tiene su propio `SceneViewModel`, cámara orbital 360°, selector de poses (Referencia/T-Pose, A-Pose, Relajada, Combate), inspector de materiales y diagnóstico SMP, compartiendo el pipeline y la caché de recursos del visor principal. La selección y los modos de vista son temporales: no alteran destructivamente el borrador ni el stack de deshacer/rehacer.

Modos disponibles:
- **Prenda aislada**: Geometría y materiales de la prenda seleccionada.
- **Maniquí**: Prenda montada sobre cuerpo y esqueleto compatibles (con fallback automático si no hay NPC activo).
- **Probar en NPC**: Superpone temporalmente la prenda sobre el atuendo actual del NPC sin modificar su borrador.
- **Outfit completo**: Visualiza todas las piezas activas del conjunto en edición.

## Capacidades confirmadas

- **Catálogo y Galería Visual**: `ArmorCatalog.qml` muestra tarjetas enriquecidas con FormID, Slots (30 Cabeza, 32 Torso, 33 Manos, 37 Pies, etc.), origen del mod en MO2, estado de resolución NIF y badge `✦ HDT-SMP / FSMP`. Filtros por slot, tipo de armadura (Clothing, Light, Heavy), mod/plugin y solo SMP.
- **Detección y Diagnóstico SMP / FSMP**: `mesh.cpp` y `smp_xml.cpp` analizan `NiStringExtraData` recursivamente en busca de rutas XML, extraen nodos de huesos físicos auxiliares vs esqueleto principal y colisionadores. El inspector en `CharacterViewport.qml` presenta el badge de salud SMP (`SMP VÁLIDO`, `SMP INCOMPLETO`, `SMP ROTO`, `SIN FÍSICAS`).
- **Motor de Poses (Reference, A-Pose, Relaxed, Combat)**: Cinemática directa sobre el esqueleto de Skyrim con rotación jerárquica de extremidades alrededor de sus pivotes naturales, evitando deformaciones bruscas o colapsos de vértices.
- **Creador Autónomo de Outfits**: Es posible iniciar un outfit desde cero sin un NPC cargado (`createNewOutfit`), añadir piezas, detectar conflictos de slots y exportar como registro `OTFT` independiente con validación completa.
- **Biblioteca de Presets de Vestuario**: Guardado, duplicación, cambio de nombre, favoritos y eliminación de presets en disco con recálculo de FormKeys contra el orden de carga activo.
- **Exportación Segura**: `OutfitPatchBuilder` y `ModPackager` empaquetan plugins ESP/ESL y carpetas para MO2 con staging atómico, sin escribir jamás en la carpeta del juego real.

## Verificación ejecutada

- **C++ Tests (Catch2 / CTest)**: 87/87 pruebas superadas al 100% (`ctest --preset debug -j 4`).
- **.NET Tests (xUnit)**: 45/45 pruebas superadas al 100% (`dotnet test SkyrimNpcStudio.slnx`).
- **Despliegue Release Autónomo**: Generado con éxito mediante `scripts/package.ps1` en `dist\SkyrimNpcStudio` (233.3 MB), incluyendo dependencias de Qt (windeployqt) y BethesdaWorker autocontenido.

