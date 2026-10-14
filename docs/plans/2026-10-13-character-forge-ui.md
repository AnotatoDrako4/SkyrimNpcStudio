# Character Forge UI — ejecución UI-1 a UI-8

Rama única: `feature/character-forge-ui`, derivada de Fase 1 y con main reciente preservado. Backend nativo e interfaz se entregan juntos en el PR #3 sobre `feature/character-forge-phase-0-1`. Continuar en esta rama sin crear otras ramas ni fusionar PR automáticamente. Solicitud UI-first del usuario: completar la experiencia Qt/QML sin afirmar compilación Windows, renderizado o integración de servicios inexistentes.

- UI-1: tres columnas redimensionables, navegación y toolbar. Viewport central predominante, estado vacío real y toolbar de cámara inactiva sin geometría.
- UI-2: reutilizar NativeNpcDraft/RPC/persistencia v2 ya implementados; formularios independientes e historial propio.
- UI-3: seleccionar/validar archivos y leer metadatos glTF/GLB reales. Persistir rutas y biblioteca; no convertir ni copiar assets.
- UI-4: elegir/detectar Blender en PATH, guardar configuración no verificada; contratos tipados para bridge futuro y panel MCP independiente, sin llamadas inventadas.
- UI-5: descripción, proveedor futuro e imágenes front/side/back verificadas; solicitud local persistida. Generación inactiva sin adaptador.
- UI-6: esqueleto sin huesos ficticios, parámetros de material como borrador y outfit ligado al catálogo/RPC existente.
- UI-7: validación simple con inspector avanzado y wizard de cinco pasos. Exportación técnica opt-in, final bloqueada por FaceGen.
- UI-8: parseo QML Qt 6.11.2, C++ syntax-only/chequeo portable del dominio, suites .NET, documentación local y PR cuando API disponible.

## Componentes conectados (2026-10-13, rama `feature/character-forge-runtime-3d`)

| Componente QML | Backend | Estado |
|---|---|---|
| `ForgeViewport.qml` | `ForgeSceneViewModel` + `RuntimeLoader` | Funcional: GLB/glTF validados, encuadrar, frontal/lateral/posterior, restablecer, malla, errores, reintentar, cancelar |
| `ModelImporter.qml` | `CharacterForgeViewModel.selectModelFile` + `scene` | Funcional: selección, niveles declarado/validado/renderizado, armatures, «Recargar vista previa»; «Preparar adaptación» deshabilitado (bridge pendiente) |
| `ForgeModelLibrary.qml` | `selectModelFile` | Funcional: reabre modelos de la biblioteca (rutas locales; copia al proyecto pendiente en CF-3) |
| `ForgeAdvancedInspector.qml` | `npc`, `workspace`, `validation`, `scene` | Funcional: JSON técnico, incluida la inspección y los avisos del modelo |
| `WelcomeOverlay.qml` | señal `forgeWithoutProfile` | Funcional: entrar en Character Forge sin perfil MO2 |
| Toolbar del visor: «Huesos», «Original / adaptado» | — | Deshabilitados con explicación (sin datos reales en 6.11 / sin modelo adaptado) |
| `BlenderConnectionPanel.qml`, `BlenderOperations.qml`, `AIGenerationPanel.qml` | configuración local | Sin cambios: acciones de bridge/IA siguen deshabilitadas |

Persistencia: objeto opcional `workspace` en .sncf v2 (compatible con v2 anterior); herramienta activa, modelo seleccionado, biblioteca, Blender, IA y materiales. Guardado atómico e historial existente. Los archivos externos siguen siendo referencias locales y requieren restauración en el PC donde se probarán.
