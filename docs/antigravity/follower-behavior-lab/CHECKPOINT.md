# AG-FOLLOWER-BEHAVIOR-01 & AG-FOLLOWER-BEHAVIOR-02 — CHECKPOINT CONSOLIDADO

- **Misiones:** `AG-FOLLOWER-BEHAVIOR-01` (Laboratorio de Comportamiento) & `AG-FOLLOWER-BEHAVIOR-02` (Verificación e Integración Profesional)
- **Directorio de trabajo:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2`
- **Ámbitos exclusivos utilizados:**
  - `tools/antigravity/follower-behavior-lab/`
  - `tests/antigravity/follower-behavior-lab/`
  - `scripts/antigravity/`
  - `docs/antigravity/follower-behavior-lab/`
  - `out/antigravity/`
- **Política Git/GitHub:** DESHABILITADA (0 comandos Git ejecutados).
- **Integridad de Archivos Core / Claude:** `HasDifferences = false` (archivos del núcleo protegidos al 100%).
- **Exportación jugable FaceGen:** BLOQUEADA (`PlayableExportUnlocked = false`, `NativeFaceGenUnavailable`).

---

## Ciclo A — Primer Incremento Operativo (TDD Red → Green)
- **Reconocimiento (< 5 min):** Verificados `.ai-coordination/claims/ownership.md`, `Directory.Packages.props` (`Mutagen.Bethesda.Skyrim` 0.54.4, `xunit.v3` 4.0.1), `CharacterProperties.qml`, `StatsEditor.qml`, `native_npc_draft.hpp`, `NativeNpcBuilder.cs` y `NativeNpcValidator.cs`.
- **TDD Red (`V01`):**
  - Comando: `dotnet test tests/antigravity/follower-behavior-lab/FollowerBehaviorLab.Tests.csproj --no-restore`
  - Resultado con implementación ingenua: `FAIL` (`Assert.False() Failure`, Exit Code `2`).
- **TDD Green (`V01`) + Ejecución CLI:**
  - Implementado `VoiceDialogueAnalyzer.cs` con inspección real de `FLST` (`VoicesFollower` y `VoicesMarriage`).
  - Resultado: `PASS` (`1/1`, Exit Code `0`, `2.84s`).
  - Comandos CLI ejecutados (`help`, `inspect`, `validate`): Exit Code `0`, generando `out/antigravity/follower-behavior-lab/inspect.json` y `out/antigravity/follower-behavior-lab/verdict.json`.

---

## Ciclo B — Voice & Dialogue Evidence (`V01`–`V08`)
- **Archivo de pruebas:** `tests/antigravity/follower-behavior-lab/PhaseBVoiceEvidenceTests.cs`
- **Escenarios verificados (`8/8 PASS`, Exit Code `0`, `2.63s`):**
  - `V01`: `VTYP` válida (`0A0100:Skyrim.esm`) sin listas → `UNKNOWN`, `HasStaticFollowerListEvidence = false`, `REQUIRES_IN_GAME_TEST`.
  - `V02`: Voz vanilla (`013AD2:Skyrim.esm`) en `VoicesFollower` y `VoicesMarriage` → `STATIC_EVIDENCE`.
  - `V03`: Voz añadida por mod (`000800:FollowerVoiceAddon.esp`) → `MOD_DEPENDENT`.
  - `V04`: Voz retirada por override (`013AE3:Skyrim.esm` eliminada por `BrokenVoiceOverride.esp`) → `INCOMPATIBLE_BY_KNOWN_RULE` + issue `VoiceFollowerRemovedByOverride` con proveedor (`Skyrim.esm`) y ganador (`BrokenVoiceOverride.esp`).
  - `V05`: Voz inexistente (`099999:Skyrim.esm`) → issue `VoiceNotFound` (`ERROR`).
  - `V06`: Lista `VoicesMarriage` ausente → `UNKNOWN` + issue `MarriageVoiceListMissing`.
  - `V07`: `PotentialMarriageFaction` sin voz en `VoicesMarriage` → `HasStaticMarriageListEvidence = false` + issue `MarriageFactionWithoutCompatibleVoice`.
  - `V08`: Cambio de perfil (`default` vs `override-conflict`) → huella `LoadOrderFingerprint` recalculada y veredicto invalidado/recalculado.

---

## Ciclo C — Combat, Archetypes y Edición de Lotes (`C01`–`C06`, `B01`–`B04`)
- **Código implementado:** `Engine/CombatClassCatalog.cs`, `Engine/ArchetypePlanner.cs`, `Engine/BulkArchetypeEngine.cs`.
- **Archivo de pruebas:** `tests/antigravity/follower-behavior-lab/PhaseCCombatAndBatchTests.cs`
- **Escenarios verificados (`10/10 PASS`):**
  - `C01`: `CLAS` resuelta por FormKey con pesos reales de habilidades y estadísticas.
  - `C02`: `CSTY` resuelto por FormKey con multiplicadores reales (`OffensiveMult`, `DefensiveMult`, `EquipmentScoreMult*`, `AvoidThreatChance`).
  - `C03`: Registros de `DisabledCombatMod.esp` excluidos del catálogo activo y rechazados al resolver.
  - `C04`: 6 arquetipos originales (`defensive-warrior`, `archer`, `assassin`, `destruction-mage`, `healer`, `spellsword`) con todos los enlaces válidos.
  - `C05`: Arquetipo `healer` con `SPEL` (4 conjuros) y `PERK` (2 perks) resueltos.
  - `C06`: Arquetipo con enlace roto (`0DEAD1:Skyrim.esm` y plugin deshabilitado) → issue `ArchetypeBrokenLink`.
  - `B01`: Aplicación *dry-run* con alcance de categorías (`class,combatStyle`) modifica únicamente esos campos y protege apariencia/identidad.
  - `B02`: Huella SHA-256 del borrador fuente idéntica antes y después del *dry-run*.
  - `B03`: Lote de 10 borradores sintéticos (`fixtures/draft.json`) genera 10 diffs independientes en `out/antigravity/follower-behavior-lab/diff.json`.
  - `B04`: `UndoBatch` restaura los 10 borradores con coincidencia SHA-256 bit a bit.

---

## Ciclo D — Informe, Estrés, Benchmark y Propuesta de Integración (`S01`–`S04`)
- **Código implementado:** `Engine/ReportGenerator.cs`, `Engine/SandboxGuard.cs`, `Program.cs` (`report`, `benchmark`), `docs/antigravity/follower-behavior-lab/INTEGRATION_HANDOFF.md`.
- **Archivo de pruebas:** `tests/antigravity/follower-behavior-lab/PhaseDReportStressAndSecurityTests.cs`
- **Escenarios verificados (`5/5 PASS`, total suite `23/23 PASS`, Exit Code `0`, `3.15s`):**
  - `S01`: JSON malformado rechazado con código de salida distinto de cero (`MalformedJson`, Exit Code `2`) sin crash.
  - `S02`: JSON con anidamiento excesivo (profundidad 50 > 32) y colección masiva (5.200 > 5.000) rechazados por límites seguros.
  - `S03`: Ruta de salida fuera de `out/antigravity/` (`../../outside_escape.json`) rechazada por `SandboxGuard` (`SandboxEscapeRejected`, Exit Code `3`).
  - `S04`: Texto malicioso (`<script>alert('XSS_NAME')</script>`, `" onload="alert(1)`) escapado íntegramente en `report.html`.
  - `Benchmark_OneThousandSyntheticRecords`: Análisis real sobre 1.018 registros sintéticos (`CLAS` + `CSTY` + `VTYP`) comparado contra baseline de 18 registros en `out/antigravity/follower-behavior-lab/benchmark.json`:
    - Baseline (18 registros): lectura `37.48 ms` (cold start), análisis `216.52 ms` (JIT warmup), JSON inspect `8,823 B`, HTML `14,684 B`, Peak Working Set `132,214,784 B`.
    - Escala 1.018 registros: lectura `1.46 ms`, análisis `87.08 ms`, JSON inspect `411,688 B`, JSON verdict `8,685 B`, HTML `14,684 B`, Peak Working Set `137,961,472 B`.

---

## Ciclo E — Contratos de Integración JSON-RPC y Despachador Wire (`RPC01`–`RPC05`)
- **Código implementado:**
  - `Integration/CreationBehaviorRpcContracts.cs`: DTOs wire compatibles con `NativeNpcDraft` (`Protocol.Major = 1`, `Minor = 6`).
  - `Integration/FollowerBehaviorRpcDispatcher.cs`: Despachador tipado y JSON con límites estrictos de seguridad y mapeo a códigos de error estándar.
  - Paquete Drop-In: `DropIn/qml/StatsEditor.integrated.qml`, `DropIn/qml/ForgeValidationPanel.integrated.qml`, `DropIn/worker/WorkerRpcTargetExtension.cs`, `DropIn/patches/character_forge_integration.patch`.
- **Archivo de pruebas:** `tests/antigravity/follower-behavior-lab/PhaseEIntegrationRpcTests.cs`
- **Escenarios verificados (`5/5 PASS`, total suite `28/28 PASS`, Exit Code `0`, `2.84s`):**
  - `RPC01`: `creation.analyzeVoice` retorna evidencia estática, clasificación de overrides y advertencia `REQUIRES_IN_GAME_TEST`.
  - `RPC02`: `creation.getBehaviorCatalog` retorna clases activas, estilos de combate, voces y los 6 arquetipos validados.
  - `RPC03`: `creation.applyArchetype` con *dry-run* y `creation.undoArchetype` preservan identidad/apariencia y restauran el estado original bit a bit (verificado por SHA-256).
  - `RPC04`: `creation.validateBehavior` mantiene el bloqueo incondicional de exportación (`exportable = false`, `NativeFaceGenUnavailable`).
  - `RPC05`: Métodos no reconocidos devuelven código JSON-RPC `-32601` (`MethodNotFound`).

---

## Ciclo F — Integración al Portón General de Regresión (`full-qa-gate`)
- **Script actualizado:** `scripts/antigravity/Invoke-SnsRegression.ps1`
- **Suites añadidas a `full-qa-gate`:**
  - `FollowerBehaviorLab_Unit_And_Rpc_Tests` (28 pruebas .NET 10 xUnit v3).
  - `Catch2_Reference_Integration_Tests` (60 casos de prueba Catch2 v3 de Claude Code A1–A3).
- **Resultados de Verificación Unificada (`regression-full-qa-gate-20261021-153122`):**
  - **Estado General:** `PASS` (17 suites / 17 aprobadas al 100%, 0 fallos, 0 omitidas).
  - **Duración:** 48.45s.
  - **Integridad de Código:** `HasDifferences = false` (huellas SHA-256 idénticas antes y después).
  - **Auditoría de Evidencia (`Test-SnsEvidence.ps1`):** `VALID` (35 comprobaciones aprobadas, 0 violaciones).
  - **Pruebas de Estrés UI de Claude:** `--stress-appearance source-compare` y `--stress-appearance preset-head-check` ejecutadas en ventana real con Exit Code 0 y sin avisos QML.

---

## Ciclo G — Integración al Núcleo Character Forge (`AG-FOLLOWER-BEHAVIOR-03` / SDD)
- **Plan Ejecutado:** `docs/antigravity/follower-behavior-lab/PLAN_INTEGRATION_AG03.md` (5/5 tareas completadas en bucle continuo SDD).
- **Entregables Integrados:**
  1. `protocol.hpp` & `native_npc_draft.hpp`: `kMinor = 6`, métodos `creation.*`, soporte `spell_form_keys` y `perk_form_keys`.
  2. `Protocol.cs` & `CreationDtos.cs`: `Minor = 6`, `SpellFormKeys`, `PerkFormKeys`, DTOs de comportamiento.
  3. `NativeNpcBuilder.cs` & `NativeNpcValidator.cs`: construcción y validación de hechizos/perks en registros Mutagen.
  4. `WorkerRpcTarget.cs`: registro de capacidades y cableado de endpoints `creation.analyzeVoice`, `creation.getBehaviorCatalog`, `creation.applyArchetype`, `creation.undoArchetype`, `creation.validateBehavior`.
  5. `StatsEditor.qml` & `ForgeValidationPanel.qml`: selector de arquetipos, vista previa reversible, y diagnósticos de voz/matrimonio con advertencia `REQUIRES_IN_GAME_TEST`.
  6. Compilación AOT de QML (`qmlcachegen`) y prueba de humo en ventana real (`smoke-test.ps1`): Exit Code 0.
- **Portón de Regresión Unificado (`regression-full-qa-gate-20261021-155318`):**
  - 17/17 suites aprobadas al 100% (`PASS`), duración 43.16s, `HasDifferences = false`.
  - Auditoría de evidencia: 35/35 comprobaciones aprobadas, 0 violaciones (`VALID`).
