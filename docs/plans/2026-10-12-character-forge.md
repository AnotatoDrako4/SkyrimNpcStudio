# Character Forge Studio — Auditoría y ejecución por fases
Fecha: 2026-10-12. Base auditada: main @ 9fb4d5bed9b7af46629ef534bf8145aba180466d.
Documento de trabajo basado en la especificación del proyecto Character Forge Studio 1.0.

## Decisión arquitectónica

Character Forge es un **tercer workspace** dentro del mismo ejecutable Windows/Qt. No es una función del editor de NPC existente y no reutiliza su borrador. Los tres espacios son:

1. Character Creator: cambios sobre NPC_ existentes.
2. Outfit Studio: edición de conjuntos y OTFT.
3. Character Forge: **creación de NPC nuevos** y conversión asistida de modelos externos.

Mantener C++20/Qt 6.11.2 + Quick3D; motores sin Qt; .NET 10/Mutagen para registros; JSON-RPC 2.0/named pipes; VFS MO2 de solo lectura; nifly y DirectXTex para lectura. La reutilización de librerías no implica compartir estado mutable entre workspaces.

## Auditoría de componentes (código de la base)

| Componente existente | Reutilización | Ampliación |
| --- | --- | --- |
| apps/desktop/qml/Main.qml | Tabs, layout Creative Workspace, tema | Tercer tab Character Forge; acciones sensibles al workspace |
| StudioController y WorkerClient | Estado de instancia/perfil, comunicación | Propiedad de ForgeViewModel independiente, sin acceder a AppearanceDraft |
| engines/projects y engines/core/file_io | CommandStack y guardado atómico | Tipo de proyecto nuevo .sncf; nunca alterar .snps |
| engines/nif, rendering, textures, assets | Diagnósticos y visor en una fase posterior | Renderizar asset importado SIN suplantar NPC existente |
| OutfitEditor y OutfitPatchBuilder | Patrones de validación/OTFT | Builder de NPC nuevos separado; no llamar el builder de overrides |
| BethesdaWorker/Mutagen | API Skyrim/masters, exportaciones | createNpc + nuevas referencias; validar FaceGen |
| tests/unit/engines y tests/export | Catch2/xUnit, fixtures | Test de persistencia, exportación de registros nuevos e independencia |
| Blender/Python y Blender MCP | **No implementados** | Dos adaptadores separados y opcionales |

Riesgos que no pueden obviarse: crear un NPC_ nuevo no equivale a ponerlo en el mundo (ACHR); no basta una malla FBX/GLB para producir un NIF válido; FaceGen coherente es indispensable; los recursos no pueden redistribuirse sin autorización; la simulación SMP en Qt no es FSMP del juego.

## Contratos y fronteras

- **Proyecto .sncf v1**: carpeta aislada con project.json versionado, y assets/ en fases posteriores. No editar ni migrar automáticamente los .snps.
- **Motor engines/creation**: ForgeProject, persistencia, validación de datos, comandos; no incluye Qt, ni lógica del worker.
- **ViewModel propio**: vida del proyecto, deshacer/rehacer, rutas, errores; no depende de seleccionar un NPC ni modifica OutfitEditor/AppearanceDraft.
- **RPC nuevos** (fase 2): creation.validateNpc, creation.exportNpc, con DTO versionados. Las operaciones son de creación de registros, NO overrides.
- **Blender Bridge** (fase 3): proceso Python controlado con scripts aprobados, versiones fijadas y resultados estructurados; MCP externo separado de los comandos deterministas.
- **Seguridad**: solo lectura sobre Skyrim/MO2, staging atómico, rutas confinadas al proyecto, APIs de pago solo con acción explícita y secretos fuera de logs.

## Plan de entregas

### Fase 0 — Auditoría y plan [EN CURSO]
Archivos: este documento. Confirmar módulos actuales, arquitectura, rutas de extensión, riesgos, dependencias y pruebas. No prometer compilación sin ejecución real.

### Fase 1 — Base independiente y proyecto .sncf [EN IMPLEMENTACIÓN]
Archivos esperados: engines/creation/, apps/desktop/viewmodels/character_forge_viewmodel.*, qml/characterforge/CharacterForge.qml; registrar CMake y tercera pestaña en Main.qml; tests/unit/engines/test_character_forge.cpp.
Criterios: crear proyecto sin NPC seleccionado; modificar nombre/identidad con deshacer/rehacer independientes; guardar, cerrar/reabrir .sncf; rechazar versiones incompatibles y JSON corrupto; ni un byte alterado en un .snps existente. Los controles de Blender, 3D importado o Exportar NPC permanecen deshabilitados hasta tener implementación real.

### Fase 2 — NPC nuevo nativo y primera exportación [PENDIENTE]
Crear NPC_ con EditorID único, nombre, raza/sexo, altura/peso, headparts, voz, nivel, inventario, OTFT y facciones opcionales; masters y ESL comprobados. Generar FaceGen compatible o bloquear exportación cuando no pueda generarse. Crear ACHR solo si el usuario selecciona celda y condiciones válidas. El worker debe escribir y releer el plugin y comprobar que el NPC nuevo tiene FormKey propio (no un override). Pruebas xUnit con fixtures y validación negativa.

### Fase 3 — Blender Python Bridge + MCP opcional [PENDIENTE]
Detectar Blender, verificar versión, abrir .blend, importar/exportar intermedios y producir reportes JSON, cancelar procesos. Invocar solo scripts controlados. MCP se configura aparte para agentes externos; la app debe funcionar sin MCP.

### Fase 4 — Importación de personajes externos [PENDIENTE]
Importar GLB/glTF/FBX/OBJ mediante Blender; analizar geometría, UV, armature, texturas, shape keys y componentes; copiar a workspace aislado. Vista previa 3D desde los recursos importados, sin confundirla con la apariencia final de Skyrim.

### Fase 5 — Adaptación a Skyrim SE/AE [PENDIENTE]
Mapeo de huesos, skinning, particiones, body slots, _0/_1, ejes y escala; escritura NIF con herramientas compatibles, validación independiente con nifly/NifSkope. Diferenciar cabeza/cuerpo vanilla frente a geometrías/esqueleto personalizados; XML SMP preservado y dependencias declaradas. Nunca asegurar animación/física sin prueba en juego.

### Fase 6 — Generación 3D por IA [PENDIENTE]
Adaptadores opcionales con API documentada para proveedores compatibles. Cotización/consumo visible, aprobación antes de cobrar, secretos en almacén seguro del SO, procedencia/licencias y adaptador de prueba sin gasto; toda salida atraviesa importación/validación común.

### Fase 7 — Mod completo de NPC creado [PENDIENTE]
Worker emite NPC_ original, OTFT, ARMO/ARMA/TXST/HDPT/ACHR solo cuando aplican. Assets/FaceGen comprobados, masters/ESL/rutas/rig/slots validados. Carpeta MO2 + ZIP atómicos; manifiesto con inGameVerified=false hasta comprobación efectiva.

### Fase 8 — Integración, rendimiento y certificación [PENDIENTE]
Catch2, Python/Blender, xUnit, integration tests, regresiones .snps/Outfit Studio y cierres limpios. Pruebas sin Blender/MCP/IA, errores de proceso, bloqueo de recursos no permitidos y validación dentro de Skyrim en perfil de ensayo.

## Secuencia crítica

Primer vertical: crear .sncf → nuevo NPC nativo simple → validar → exportar ESP → releer registros (Fases 1–2).
Segundo vertical: importar modelo → editar en Blender → adaptar → validar NIF/FaceGen → exportar mod (Fases 3–5 y 7).
MCP e IA de pago se integrarán al final, no condicionan las capacidades básicas.

## Evidencia y criterio de cierre

Cada fase exige: archivos cambiados, pruebas realizadas con sus comandos y resultados, errores y limitaciones. **No confundir código revisado con compilación verificada, ni plugin releído con prueba dentro de Skyrim.** Los casos de integración real dependen de Windows con Qt, MSVC, .NET, MO2 y juego instalados.