# Registro de Bloqueos y Dependencias (Known Blockers)

- **Directorio:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2`
- **Fecha:** 2026-10-19T10:50:00-03:00

| ID | Área | Descripción del Bloqueo / Dependencia | Estado | Mitigación / Alternativa |
|---|---|---|---|---|
| BLK-01 | Auditoría Externa | Archivos brutos físicos `AUDIT_SENIOR_REPORT_v2.md`, `audit-evidence.zip` y `Plan Maestro` no ubicados en disco Windows local. | `[BLOQUEADO_POR_INPUT]` | Datos y hallazgos documentados en `docs/AUDIT_V2_STATUS.md`; inputs hostiles embebidos como bytes en `tests/support/audit_repro_inputs.hpp`. Pruebas que requieran extracción de zip crudo se marcan `BLOCKED_BY_INPUT`. |
| BLK-02 | Coordinación Claude | Claude inició trabajos previamente; respuesta a `antigravity-to-claude.md` no registrada aún. | `[WAITING_CONFIRMATION]` | Antigravity trabaja estrictamente en carpetas exclusivas (`tools/antigravity`, `scripts/antigravity`, `tests/antigravity`, `docs/antigravity`, `out/antigravity`). |
| BLK-03 | Control de Versiones | Prohibición absoluta de Git y GitHub (ni status, diff, log, push, pull, api). | `[POLICY_ENFORCED]` | Huellas de archivos e histórico de cambios mantenidos mediante `Get-SnsSourceFingerprint.ps1` y manifests JSON locales. |
| BLK-04 | Validación In-Game | Validación en runtime de Skyrim SE/AE y MO2 real no autorizada para ejecución automatizada. | `[PENDIENTE_HUMANO]` | Las pruebas de modding e instalación usan exclusivamente sandbox mock en `out/antigravity/fixtures/mock-game/`. Estado reportado como `PENDIENTE_SKYRIM`. |
| BLK-05 | Settings Center | Modificación de `Main.qml`, `Theme.qml` o `workbench_state.*` requiere cesión formal. | `[RESERVADO_HASTA_GATE]` | Prototipado y pruebas de Settings Center se mantienen desacopladas hasta confirmación de F0/S0-S2 y traspaso explícito de archivos. |
