# Contrato: Face Source Studio v1 (Claude → Antigravity / Codex)

Fecha: 2026-10-24 · Propietario: Claude (`appearance_sources.*`, `engines/appearance/{face_source_index,face_transfer,source_matrix}.*`,
`qml/charactercreator/FaceSourceStudio.qml`) · Sin Git (carpeta ui2) · Protocolo worker: sin cambios (1.7; no hay RPC nuevo).

## 1. Qué es

Galería de rostros de TODO el perfil MO2 y transferencia guiada:
`Elegir NPC → Examinar → Comparar → Seleccionar → Validar → Crear parche`.
Las miniaturas son renders del `PortraitRenderer` del estudio con los archivos FaceGen de cada fuente (overlay en memoria);
nunca capturas de Skyrim ni imágenes generadas por IA.

## 2. API QML (`studio.sources`, tipo `AppearanceSources`)

| Miembro | Tipo | Contrato |
|---|---|---|
| `faceIndex` | `QVariantMap` | `{state: none\|scanning\|ready\|error, npcs, withAlternatives, looseFiles, archivedFiles, archives, scanMs, sources[], issues[], error}`. Se reconstruye al cambiar de perfil (el índice anterior se descarta antes de escanear). |
| `scanFaceIndex()` | invocable | Reescanea en un hilo del pool; cancelable (serial + `atomic<bool>`). |
| `queryFaceIndex(text, source, onlyAlternatives, flag, limit, names)` | `QVariantMap` | `{total, rows[]}`; fila: `{formKey, plugin, formId, name, geometryWinner, geometryArchived, tintWinner, alternatives, flags[], sources[], geometryPath, tintPath, looseMods[], archives[]}`. `names` = `studio.npcNames` (claves `npcKey`). `limit` 1..5000. |
| `galleryOpen` | `bool` RW | Mientras es `true`, se piden las imágenes de TODAS las fuentes dibujables del NPC (caché primero, el renderer las encola una a una). |
| `matrix.candidates[].archive` | `string` | Nuevo: BSA de una fuente `bsa:<archivo>` (copia que el juego no carga). |
| `transferPlan(id)` | `QVariantMap` | `{candidate, verdict, chosen, canCreatePatch, requirement, reasons[], actions[]}`. |

Flags del índice: `MultipleSources`, `TintFromOtherSource`, `NoTint`, `TintOnly`, `ArchivedOnly`.

Veredictos (`verdict`): `IN_USE` · `PATCH_READY` · `NEEDS_REVIEW` (D1) · `NEEDS_LOAD_ORDER` · `BLOCKED` · `WAITING` (matriz sin listar).
`requirement`: `REQUIERE_CREATION_KIT` cuando solo un FaceGen nuevo lo resolvería (el estudio no genera FaceGen: `NativeFaceGenUnavailable`).

Reglas de seguridad (engine puro `plan_face_transfer`, 6 tests):
- `canCreatePatch` solo si el borrador está FRESCO y elige esa fuente, y el veredicto es `PATCH_READY`/`NEEDS_REVIEW`.
- El único parche es la resolución asistida existente («Usar el rostro de X» → Easy Mod Creator → `ExportWizardDialog`),
  con su puerta de revisión D1 intacta. Solo existe para la fuente que el juego carga.
- Un FaceGen que no gana en MO2 → `NEEDS_LOAD_ORDER`: un plugin no puede elegir FaceGen (misma ruta para todos los mods);
  el estudio no cambia el orden de MO2 ni oculta archivos; da las instrucciones.
- Registro sin FaceGen propio / NPC sin FaceGen → `BLOCKED` + `REQUIERE_CREATION_KIT`. FaceGen dañado → `BLOCKED`.

## 3. Escrituras

- Borrador de elección: `source-drafts/<perfil>.json` (o `SNS_SOURCE_DRAFT_DIR`), atómico (`QSaveFile`); un archivo ilegible nunca se sobrescribe.
- Copias extraídas de BSA para leer/dibujar: `<caché de retratos>/variants/bsa/<sha256>.nif|.dds` (deduplicadas por contenido).
- Renders: `<caché de retratos>/variants/` (`PortraitCache`, clave por FormKey + id + SHA-256 de geometría/tinte + versión del renderer + digest del perfil).
- Nada en MO2, `Skyrim/Data`, mods, overwrite ni plugins.

## 4. Memoria, coste y cancelación

- El escaneo abre el directorio de cada BSA del orden de carga una vez; solo se retienen los BSA con FaceGen (shared_ptr
  en el índice). Medidas reales en `out/local-implementation/sns-face-20261024/`.
- Cambio de NPC o perfil: serial nuevo → resultados tardíos descartados; imágenes del NPC anterior eliminadas al instante.
- Al terminar el índice, la matriz del NPC en pantalla se reconstruye para incluir sus copias BSA.

## 5. Para Antigravity (QML)

`FaceSourceStudio.qml` (Popup) se abre desde «Origen → Fuentes de apariencia → Estudio de rostros…». objectNames para tests:
`faceSourceStudio, faceStudioOpen, faceStudioClose, faceIndexState, faceIndexSearch, faceIndexSource, faceIndexFlag,
faceIndexOnlyAlternatives, faceIndexList, faceIndexRow_<FormKey>, faceStep1..6, faceStudioNpc, faceStudioMatrixState,
faceCardGrid, faceCard_<id>, faceTransferVerdict (prop verdict), faceTransferReasons, faceTransferActions, faceCompareButton,
faceChooseButton, faceCreatePatchButton, faceClearChoice, faceStudioNotice`.
Podéis rediseñar la presentación (Theme/tokens) sin cambiar los nombres ni la lógica de habilitación; la señal `patchRequested`
llega a `AppearanceControlPanel.resolutionRequested` → `CharacterCreator` abre `ExportWizardDialog` (sin cambios vuestros).

## 6. Para Codex (exportación facial)

Sin cambios en `PatchBuilder`/`FacePatchBuilder`/RPC. Requisitos si en el futuro el ModRecipe incluye `selectedAppearanceSource`:
1. Aceptar solo `candidate == "effective"` con `verdict ∈ {PATCH_READY, NEEDS_REVIEW}` y borrador fresco (`inputDigest` = SHA-256 de `SourceMatrix::identity`).
2. Reconsultar la procedencia FaceGen efectiva en el worker antes de escribir (no fiarse del borrador).
3. `NEEDS_LOAD_ORDER` no se convierte en plugin: a lo sumo un documento de instrucciones MO2 en el ZIP.
4. No empaquetar FaceGen de terceros (permisos): el parche depende del mod original.
