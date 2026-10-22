# INFORME DE VERIFICACIÓN INDEPENDIENTE Y PREPARACIÓN DE INTEGRACIÓN — AG-FOLLOWER-BEHAVIOR-02

- **Misión:** `AG-FOLLOWER-BEHAVIOR-02` (Preparación de Integración Profesional)
- **Proyecto:** Skyrim NPC Studio Ultimate (`D:\.Proyectos Games\SkyrimNpcStudio-ui2`)
- **Agente:** Antigravity (QA Lab & Behavior Studio)
- **Fecha:** 2026-10-21
- **Estado de Integración Core:** `AISLADO_LISTO_PARA_INTEGRAR` (Permisos asignados a Claude Code; sin cesión explícita).

---

## 1. Verificación Independiente (Etapa 1)

### 1.1. Inspección de Archivos Reales y Re-ejecución de Pruebas
Se inspeccionaron directamente los archivos del laboratorio y se ejecutó la suite de 23 pruebas xUnit v3 bajo .NET 10 (`10.0.401`):
- **Resultado:** 23 / 23 aprobadas (`PASS`), 0 fallos, 0 omitidas en `3.12s`.
- **Integridad de artefactos originales:** Antes y después de la verificación, los archivos de `out/antigravity/follower-behavior-lab/` mantuvieron exactamente sus tamaños y hashes SHA-256 declarados en `manifest.sha256.json`.

### 1.2. Verificación de FormLists y Overrides
- `VoicesFollower` (`09B1F5:Skyrim.esm`):
  - Perfil `default`: `Skyrim.esm` (fuente) → `FollowerVoiceAddon.esp` (ganador). La voz `FemaleCommander` (`013AE3:Skyrim.esm`) mantiene `STATIC_EVIDENCE`, y `ModCustomVoice` (`000800:FollowerVoiceAddon.esp`) obtiene `MOD_DEPENDENT`.
  - Perfil `override-conflict`: `BrokenVoiceOverride.esp` gana la lista y retira `FemaleCommander`. El analizador detecta el conflicto exactamente, emite el issue `VoiceFollowerRemovedByOverride` e identifica a `Skyrim.esm` como proveedor y a `BrokenVoiceOverride.esp` como causante de la exclusión (`INCOMPATIBLE_BY_KNOWN_RULE`).
- `VoicesMarriage` (`0C790E:Skyrim.esm`):
  - La presencia de `PotentialMarriageFaction` (`019809:Skyrim.esm`) en un NPC con voz `FemaleCommander` no engaña al sistema: el analizador rechaza afirmar matrimonio, clasifica la evidencia en `UNKNOWN` y emite `MarriageFactionWithoutCompatibleVoice`.

### 1.3. Preservación de Identidad, Apariencia y Ubicación
Se verificó que `BulkArchetypeEngine` y `apply-archetype`:
1. Solo mutan las categorías de comportamiento autorizadas (`class`, `combatStyle`, `stats`, `skills`, `equipment`, `factions`, `packages`, `spells`, `perks`).
2. Preservan al 100% los campos protegidos:
   - `name`, `editorId`, `description` (identidad)
   - `raceFormKey`, `female` (raza y sexo)
   - `voiceFormKey` (voz del personaje)
   - `height`, `weight`, `face` (morfología y sliders faciales)
   - `headPartFormKeys`, `hairColorFormKey`, `headTextureFormKey` (FaceGen y peinados)
   - `level`, `levelMultiplier`, `minLevel`, `maxLevel` (escalado)
   - `placement` (ubicación en celda)
3. La prueba `B04` y `RPC03` confirman que `undoArchetype` restaura el borrador con coincidencia SHA-256 exacta bit a bit.

### 1.4. Comparativa de Benchmarks de Rendimiento
Medición repetida en condiciones idénticas sobre `DESKTOP-7AVE8QN`:

| Métrica | Corrida 01 (Línea Base) | Corrida 02 (Verificación) | Variación |
|---|---|---|---|
| Lectura de plugins (1.018 registros) | `1.46 ms` | `1.47 ms` | < 1% |
| Análisis y validación (1.018 registros) | `87.08 ms` | `97.69 ms` | Fluctuación normal de CPU |
| Memoria Working Set Pico | `137,961,472 B` | `138,014,720 B` | < 0.04% |
| Tamaño `inspect.json` (1.018 registros) | `411,688 B` | `411,688 B` | Idéntico (0.0%) |
| Tamaño `verdict.json` | `8,685 B` | `8,685 B` | Idéntico (0.0%) |
| Tamaño `report.html` | `14,684 B` | `14,684 B` | Idéntico (0.0%) |

---

## 2. Diseño y Preparación de Integración (Etapa 2)

### 2.1. Contrato JSON-RPC 2.0 (`CreationBehaviorRpcContracts.cs`)
Se diseñó y probó el adaptador `FollowerBehaviorRpcDispatcher` implementando 5 métodos bajo el protocolo wire estándar (`Protocol.Major = 1`, `Protocol.Minor = 6`):
1. `creation.analyzeVoice`:
   - Entrada: `voiceFormKey`, `female`, `factions`.
   - Salida: clasificación estricta de `VoicesFollower` y `VoicesMarriage`, trazabilidad de overrides, advertencia `REQUIRES_IN_GAME_TEST` e issues compatibles con Worker.
2. `creation.getBehaviorCatalog`:
   - Entrada: `filterText`, `limit`.
   - Salida: `classes`, `combatStyles`, `voices`, y los 6 arquetipos validados (`defensive-warrior`, `archer`, `assassin`, `destruction-mage`, `healer`, `spellsword`).
3. `creation.applyArchetype`:
   - Entrada: `npc` (`NativeNpcDraft`), `archetypeId`, `categories`, `dryRun: true/false`.
   - Salida: `proposedNpc`, diff por campo (`changes`), hash SHA-256 previo y propuesto, y `undoRestoreToken`.
4. `creation.undoArchetype`:
   - Entrada: `undoRestoreToken`, `expectedOriginalSha256`.
   - Salida: `restoredNpc` con validación criptográfica de coincidencia.
5. `creation.validateBehavior`:
   - Entrada: `npc` (`NativeNpcDraft`), `pluginName`.
   - Salida: extensión directa de `CreationValidateResponse` manteniendo el bloqueo `NativeFaceGenUnavailable` (`exportable = false`, `faceGenStatus = "pending"`).

### 2.2. Suite de Pruebas de Integración RPC (`PhaseEIntegrationRpcTests.cs`)
5 pruebas añadidas y aprobadas:
- `RPC01_AnalyzeVoice_ReturnsVoiceEvidenceAndRequiresInGameTest` (`PASS`)
- `RPC02_GetBehaviorCatalog_ReturnsClassesCombatStylesVoicesAndSixArchetypes` (`PASS`)
- `RPC03_ApplyAndUndoArchetype_PreservesIdentityAndRestoresExactBitMatch` (`PASS`)
- `RPC04_ValidateBehavior_LocksPlayableExportAndReportsIssues` (`PASS`)
- `RPC05_UnknownMethod_ReturnsJsonRpcMethodNotFoundCode` (`PASS`)

Total suite del proyecto: **28 pruebas aprobadas (100% PASS)**.

### 2.3. Componentes Drop-In Preparados en `tools/antigravity/follower-behavior-lab/DropIn/`
1. [`StatsEditor.integrated.qml`](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/tools/antigravity/follower-behavior-lab/DropIn/qml/StatsEditor.integrated.qml):
   - Drop-in para `apps/desktop/qml/characterforge/StatsEditor.qml`.
   - Añade la barra de selección de arquetipos, botones de Vista Previa (*dry-run*) y Aplicar, y selectores para Hechizos (`spellFormKeys`) y Perks (`perkFormKeys`) reutilizando `ForgeRecordSelector` y `root.forge`.
2. [`ForgeValidationPanel.integrated.qml`](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/tools/antigravity/follower-behavior-lab/DropIn/qml/ForgeValidationPanel.integrated.qml):
   - Drop-in para `apps/desktop/qml/characterforge/ForgeValidationPanel.qml`.
   - Añade filas de diagnóstico de Voz y Diálogo de Seguidor/Matrimonio y advertencia explícita de `REQUIRES_IN_GAME_TEST`, reteniendo el bloqueo final de FaceGen.
3. [`WorkerRpcTargetExtension.cs`](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/tools/antigravity/follower-behavior-lab/DropIn/worker/WorkerRpcTargetExtension.cs):
   - Métodos C# para registrar en `WorkerRpcTarget.cs` sin duplicar modelos.
4. [`character_forge_integration.patch`](file:///D:/.Proyectos%20Games/SkyrimNpcStudio-ui2/tools/antigravity/follower-behavior-lab/DropIn/patches/character_forge_integration.patch):
   - Parche unificado con los cambios exactos para aplicar cuando se autorice la cesión.
