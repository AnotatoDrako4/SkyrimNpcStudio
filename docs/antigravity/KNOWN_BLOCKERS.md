# Registro de Bloqueos y Dependencias (Known Blockers)

- **Directorio:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2`
- **Fecha:** 2026-10-19T10:50:00-03:00

| ID | Área | Descripción del Bloqueo / Dependencia | Estado | Mitigación / Alternativa |
|---|---|---|---|---|
| BLK-01 | Auditoría Externa | Archivos brutos físicos `AUDIT_SENIOR_REPORT_v2.md`, `audit-evidence.zip` y `Plan Maestro` no ubicados en disco Windows local. | `[BLOQUEADO_POR_INPUT]` | Datos y hallazgos documentados en `docs/AUDIT_V2_STATUS.md`; inputs hostiles embebidos como bytes en `tests/support/audit_repro_inputs.hpp`. Pruebas que requieran extracción de zip crudo se marcan `BLOCKED_BY_INPUT`. |
| BLK-02 | Coordinación Claude | Claude inició trabajos previamente; respuesta a `antigravity-to-claude.md` no registrada aún. | `[WAITING_CONFIRMATION]` | Antigravity trabaja estrictamente en carpetas exclusivas (`tools/antigravity`, `scripts/antigravity`, `tests/antigravity`, `docs/antigravity`, `out/antigravity`). |
| BLK-03 | Control de Versiones | Prohibición absoluta de Git y GitHub levantada explícitamente por el usuario para commit y push a `main`. | `[LIFTED_BY_OWNER]` | Cambios integrados y sincronizados en `main` y `origin/main` (commit `f13a3ae`). |
| BLK-04 | Validación In-Game | Validación en runtime de Skyrim SE/AE y MO2 real no autorizada para ejecución automatizada. | `[PENDIENTE_HUMANO]` | Las pruebas de modding e instalación usan exclusivamente sandbox mock en `out/antigravity/fixtures/mock-game/`. Estado reportado como `PENDIENTE_SKYRIM`. |
| BLK-05 | Settings Center / Workbench | Integración de Settings Center V4 y Workbench UI 2.0 completada y validada. | `[RESUELTO_Y_MERGEADO]` | Verificación multirresolución superada (1100×700 a 1920×1080), 100% CTest y .NET tests aprobados. |
