# Estudio de rostros (Face Source Studio)

Elegir rostros de los mods de forma visual y segura, sin editar FaceGen a mano.
Se abre en **Personaje → pestaña Origen → Fuentes de apariencia → «Estudio de rostros…»**.

## Recorrido

1. **Elegir NPC** — la lista de la izquierda es el índice de TODO el perfil MO2: cada NPC con FaceGen (FaceGeom NIF y FaceTint
   DDS), con quién lo aporta (mod suelto o BSA) y cuántas alternativas tiene. Filtros: texto (nombre, FormKey, plugin o mod),
   fuente concreta, estado (varias fuentes, tinte de otra fuente, solo en BSA, sin FaceTint) y «solo con rostros alternativos».
   Un FaceGen sin registro NPC en el perfil aparece como «FaceGen huérfano» y no se puede abrir.
2. **Examinar** — cada fuente del NPC en una tarjeta: el render del estudio (mismo escenario, cámara y luz para todas, con los
   archivos de esa fuente), su veredicto (VALID / UNVERIFIED / CONFLICT / AMBIGUOUS / MISSING), plugin del registro y origen de la
   imagen («Render del estudio» o «desde caché»). Las copias que están dentro de un BSA y el juego no carga (porque una copia suelta
   las tapa) también son fuentes.
3. **Comparar** — «Comparar A/B…» abre la comparación existente (A = lo que carga el juego, B = la tarjeta elegida; zoom y
   desplazamiento compartidos, alternar, 3-4 a la vez).
4. **Seleccionar** — «Elegir este rostro» guarda un borrador del estudio (no cambia el perfil). Queda ligado al perfil y a los
   archivos: si algo cambia, pasa a «desactualizado» y no se usa.
5. **Validar** — el plan de transferencia de la tarjeta dice qué hace falta:
   - **En uso**: es el rostro que carga el juego y su registro coincide; nada que hacer.
   - **Parche posible** / **con revisión D1**: el registro que gana no coincide con el FaceGen que gana; el parche copia los datos
     faciales del plugin de ese FaceGen (la resolución asistida «Usar el rostro de X»). Los casos ambiguos necesitan aprobación.
   - **Requiere cambiar MO2**: ese FaceGen existe pero no gana. Un plugin no puede elegir qué FaceGen se carga (la ruta es la misma
     para todos los mods); el estudio no cambia el orden de MO2 y explica qué mover u ocultar.
   - **Bloqueado**: sin FaceGen propio (REQUIERE_CREATION_KIT), archivo dañado o aviso sin resolución.
6. **Crear parche** — solo habilitado cuando el plan lo permite y la elección está guardada: abre el asistente de exportación
   existente, que escribe un mod aparte (con su revisión D1 y lectura de vuelta) en la carpeta que elijas.

## Garantías

- Nada se escribe en MO2, `Skyrim/Data`, mods, overwrite ni plugins. El estudio escribe solo su borrador y su caché
  (renders y copias extraídas de BSA en `<caché de retratos>/variants/`).
- Las imágenes son aproximaciones del editor, nunca capturas de Skyrim ni imágenes generadas por IA.
- No se genera FaceGen ni se declara compatible un rostro sin datos: los veredictos salen del mapa de control
  (`analyze_appearance_control`) aplicado a cada fuente.
- El resultado en el juego queda `PENDIENTE_SKYRIM` hasta comprobarlo en un perfil de prueba.

## Técnica

Motor puro: `engines/appearance/{face_source_index,face_transfer,source_matrix}.*`. Viewmodel: `AppearanceSources`
(`faceIndex`, `queryFaceIndex`, `galleryOpen`, `transferPlan`). QML: `FaceSourceStudio.qml`. Contrato:
`.ai-coordination/contracts/face-source-studio-v1.md`. Pruebas: `sns_reference_tests "[facestudio]"` y el escenario GUI
`--stress-appearance face-studio`.
