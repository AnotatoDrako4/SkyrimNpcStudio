# INTEGRATION_HANDOFF.md — Follower Behavior & Voice Intelligence Studio (`AG-FOLLOWER-BEHAVIOR-01`)

- **Estado de Integración al Núcleo:** `BLOQUEADO_PENDIENTE_AUTORIZACION_DE_PROPIEDAD`
- **Propietario del Laboratorio:** Antigravity (`tools/antigravity/follower-behavior-lab/`, `tests/antigravity/follower-behavior-lab/`, `docs/antigravity/follower-behavior-lab/`, `out/antigravity/follower-behavior-lab/`)
- **Propietario de Destino (Core / Worker / QML):** Claude Code / Propietario del Repositorio
- **Archivos del núcleo modificados en esta misión:** **0** (verificado mediante huella criptográfica SHA-256 antes y después).

---

## 1. Resumen Ejecutivo del Handoff

El laboratorio `FollowerBehaviorLab` implementa en C# / .NET 10 (`Mutagen.Bethesda.Skyrim` 0.54.4) los motores verificados de:
1. **Análisis de Evidencia de Diálogo de Voces (`VoiceDialogueAnalyzer`):** Trazabilidad real sobre `VoicesFollower` (`0009B1F5:Skyrim.esm`) y `VoicesMarriage` (`000C790E:Skyrim.esm`), distinguiendo `STATIC_EVIDENCE`, `MOD_DEPENDENT`, `UNKNOWN`, `INCOMPATIBLE_BY_KNOWN_RULE` y `REQUIRES_IN_GAME_TEST`.
2. **Catálogo de Clases y Estilos de Combate (`CombatClassCatalog`):** Resolución real de registros `CLAS` (`IClassGetter`) y `CSTY` (`ICombatStyleGetter`), pesos de habilidades/estadísticas y multiplicadores de IA de combate, rechazando registros de plugins deshabilitados.
3. **Planificador de 6 Arquetipos (`ArchetypePlanner`) y Motor de Lotes Reversible (`BulkArchetypeEngine`):** Plantillas configurables (`defensive-warrior`, `archer`, `assassin`, `destruction-mage`, `healer`, `spellsword`), simulación *dry-run* por categorías autorizadas y deshacer bit-exacto (`UndoBatch`) garantizado por SHA-256.

---

## 2. Conexión con `NativeNpcDraft` y `BethesdaWorker` (Sin Duplicar Estructuras)

### 2.1. Campos ya existentes que se reutilizan directamente
En `engines/creation/include/sns/creation/native_npc_draft.hpp` (líneas 19–36) y `services/BethesdaWorker/Contracts/CreationDtos.cs` (líneas 4–33), `NativeNpcDraft` ya define:
- `voice_form_key` / `VoiceFormKey` (`VTYP`)
- `class_form_key` / `ClassFormKey` (`CLAS`)
- `combat_style_form_key` / `CombatStyleFormKey` (`CSTY`)
- `health`, `magicka`, `stamina`, `skills`
- `outfit_armor_form_keys` / `OutfitArmorFormKeys` (`ARMO`)
- `inventory` / `Inventory` (`WEAP`, `AMMO`, `ITEM`)
- `factions` / `Factions` (`FACT`)
- `package_form_keys` / `PackageFormKeys` (`PACK`)

### 2.2. Extensión opcional para Hechizos (`SPEL`) y Perks (`PERK`)
Cuando el propietario autorice extender el borrador nativo:
1. **`engines/creation/include/sns/creation/native_npc_draft.hpp`:**
   Añadir dos vectores opcionales serializados con `nlohmann::json` (con fallback a lista vacía `[]` para compatibilidad hacia atrás con proyectos `.sncf` existentes):
   ```cpp
   std::vector<std::string> spell_form_keys;
   std::vector<std::string> perk_form_keys;
   ```
2. **`services/BethesdaWorker/Contracts/CreationDtos.cs`:**
   Añadir en `NativeNpcDraft`:
   ```csharp
   public IReadOnlyList<string> SpellFormKeys { get; init; } = [];
   public IReadOnlyList<string> PerkFormKeys { get; init; } = [];
   ```
3. **`services/BethesdaWorker/PatchBuilder/NativeNpcBuilder.cs`:**
   En `Build(...)` (tras línea 60):
   ```csharp
   foreach (var s in d.SpellFormKeys) npc.ActorEffect.Add(new FormLink<ISpellRecordGetter>(K(s)));
   foreach (var pk in d.PerkFormKeys) npc.Perks.Add(new PerkPlacement { Perk = new FormLink<IPerkGetter>(K(pk)), Rank = 1 });
   ```
4. **`services/BethesdaWorker/Validation/NativeNpcValidator.cs`:**
   Incorporar la llamada a `VoiceDialogueAnalyzer.AnalyzeVoice(...)` y resolver `ISpellGetter` / `IPerkGetter` en `Validate(...)`, manteniendo intacto el bloqueo final `NativeFaceGenUnavailable` (línea 148).

---

## 3. Cambios Propuestos en QML (Solo Documentados — Archivos No Editados)

### 3.1. `apps/desktop/qml/characterforge/StatsEditor.qml`
Actualmente `StatsEditor.qml` (líneas 84–198) presenta selectores individuales `ForgeRecordSelector` para `class`, `combatStyle`, `skills`, `faction` y `package`.
Para conectar los arquetipos y la aplicación por categorías sin romper los selectores actuales:
1. **Barra de Plantilla de Arquetipo (insertar antes de línea 84):**
   - `ComboBox` con los 6 arquetipos (`defensive-warrior`, `archer`, `assassin`, `destruction-mage`, `healer`, `spellsword`).
   - Casillas de alcance de categoría (`CheckBox`: Clase/Estilo, Estadísticas/Habilidades, Equipo, Hechizos/Perks, Facciones/Paquetes IA).
   - Botón `Vista Previa (Dry-Run)` que invoca el método RPC propuesto `creation.previewArchetype` y muestra los cambios propuestos (`FieldChangeDto[]`) sin mutar rostro, peso, altura ni FaceGen.
   - Botones `Aplicar Selección` y `Deshacer Arquetipo` (usando `UndoRestoreToken` verificado por SHA-256).
2. **Selectores de Hechizos y Perks (insertar tras línea 198):**
   - `ForgeRecordSelector` con `kind: "spell"` (`spellFormKeys`) y `kind: "perk"` (`perkFormKeys`), siguiendo exactamente el mismo patrón `Repeater` + botón `×` de `packageFormKeys` (líneas 175–198).

### 3.2. `apps/desktop/qml/characterforge/CharacterProperties.qml` y `ForgeValidationPanel.qml`
1. En `CharacterProperties.qml` (tras el selector `voiceFormKey` en líneas 46–53):
   - Mostrar una insignia informativa de evidencia de diálogo (`STATIC_EVIDENCE`, `MOD_DEPENDENT`, `UNKNOWN`, `INCOMPATIBLE_BY_KNOWN_RULE`) y otra para matrimonio, acompañada siempre de la etiqueta explícita `Requiere prueba en Skyrim SE (REQUIRES_IN_GAME_TEST)`.
2. En `ForgeValidationPanel.qml` (líneas 22–52):
   - Añadir filas de estado para `Voz / Diálogo Seguidor` (`result.voiceAnalysis.followerDialogueClassification`) y `Comportamiento IA` (`REQUIRES_IN_GAME_TEST`), manteniendo la fila `Exportación final: Bloqueada por FaceGen` sin alteración.

### 3.3. `apps/desktop/qml/characterforge/NativeNpcEditor.qml`
En `NativeNpcEditor.qml` (líneas 10–28):
- Mantener las 5 pestañas actuales (`Identidad`, `Datos`, `Rostro`, `Equipo`, `Lugar`) sin reordenarlas para no interferir con las pruebas de UI ni con el trabajo de Claude en `Rostro`.
- El panel de arquetipos se aloja dentro de la pestaña `Datos` (`StatsEditor.qml`, índice 1), respetando la estructura de navegación existente.

---

## 4. Invariantes de Seguridad y Honestidad para la Integración

1. **Prohibido promover evidencia estática a garantía en juego:** Ningún indicador de UI o RPC debe emitir `recruitable: true`, `marriageWorks: true` ni `VERIFICADO_EN_SKYRIM` solo porque `VTYP` esté en `VoicesFollower`/`VoicesMarriage` o el NPC tenga `PotentialFollowerFaction`.
2. **Bloqueo FaceGen innegociable:** `CreationValidateResponse.Exportable` permanece `false` y `PlayableExportUnlocked` permanece `false` mientras el motor nativo de FaceGen NIF + FaceTint DDS no esté verificado.
3. **Protección de Apariencia en Lotes:** Cualquier aplicación de arquetipo o edición en lote tiene prohibido tocar `name`, `editorId`, `raceFormKey`, `female`, `height`, `weight`, `face`, `headPartFormKeys`, `hairColorFormKey`, `headTextureFormKey` y `placement`.
