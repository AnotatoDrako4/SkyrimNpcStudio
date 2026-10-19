# QA Runbook — Antigravity QA Lab

- **Directorio Raíz:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2`
- **Responsable:** Antigravity (QA Lab Independiente)
- **Fecha de creación:** 2026-10-19
- **Estado de coordinación:** WAITING_CLAUDE_CONFIRMATION

---

## 1. Principios Operativos

1. **Aislamiento Total:** El laboratorio de QA corre en directorios exclusivos (`tools/antigravity/`, `scripts/antigravity/`, `tests/antigravity/`, `docs/antigravity/`, `out/antigravity/`).
2. **Cero Git / GitHub:** No se ejecutan comandos de git ni herramientas remotas. Todas las inspecciones de archivos usan APIs nativas de Windows/PowerShell.
3. **No interferencia con Claude:** No se tocan archivos de core/worker/rendering/nif/face asignados a Claude, ni se usan las carpetas `build/debug`, `build/package`, `bin` o `dist` durante compilaciones activas de Claude.
4. **Protección de Datos Reales:** Nunca se escribe en carpetas de Skyrim (`Data/`), ni perfiles de MO2 activos, ni partidas guardadas (`saves/`). Cualquier prueba destructiva o de seguridad se ejecuta contra sandbox sintético en `out/antigravity/fixtures/mock-game/`.
5. **Detección de Edición Concurrente:** Antes de cada prueba se calculan huellas SHA-256 de los archivos de entrada. Si un archivo cambia durante la ejecución, el resultado se marca inmediatamente como `INVALIDATED_BY_CONCURRENT_EDIT`.
6. **Manejo de Locks Pesados:** Compilaciones o tests que consuman altos recursos de CPU/RAM deben adquirir `.ai-coordination/locks/heavy-build.lock` de forma atómica.

---

## 2. Herramientas y Toolchain Verificado

- **MSVC Toolset:** 14.44.35207 (Visual Studio 2022 Build Tools)
- **.NET SDK:** 10.0.401
- **Qt SDK:** 6.11.2 MSVC 2022 x64 (`D:\DevTools\Qt\6.11.2\msvc2022_64`)
- **CMake:** 4.3.2
- **Ninja:** 1.13.0
- **Python:** 3.11.15
- **PowerShell:** 5.1 / Windows PowerShell

---

## 3. Catálogo de Scripts de Antigravity QA Lab

| Script | Propósito | Entradas | Salidas |
|---|---|---|---|
| `scripts/antigravity/Get-SnsSourceFingerprint.ps1` | Calcula huellas SHA-256, tamaño y timestamps de fuentes | Lista de rutas / carpetas | Objeto PS / JSON de huellas |
| `scripts/antigravity/Request-SnsBuildSlot.ps1` | Adquiere cerrojo atómico para compilación pesada | Timeout, Motivo | Handle / Exit 0 si adquirido |
| `scripts/antigravity/Release-SnsBuildSlot.ps1` | Libera cerrojo atómico de compilación | None | Lock eliminado |
| `scripts/antigravity/Invoke-SnsQaBaseline.ps1` | Ejecuta suite de pruebas con verificación pre/post | Target suite, config | Manifest JSON del run con exit code y tiempos |
| `scripts/antigravity/Write-SnsQaReport.ps1` | Genera reporte formateado del run | Manifest JSON | `summary.json`, `summary.md` |
| `scripts/antigravity/Test-SnsEvidence.ps1` | Audita integridad de evidencias y consistencia de hashes | Ruta de run / artefactos | PASS/FAIL de auditoría de evidencia |

---

## 4. Guía de Ejecución de Suites de Prueba

### 4.1. Verificación Rápida de Fingerprints
```powershell
. .\scripts\antigravity\Get-SnsSourceFingerprint.ps1
$fp = Get-SnsSourceFingerprint -Paths @('engines/nif', 'services/BethesdaWorker')
```

### 4.2. Ejecución con Detección de Concurrencia
```powershell
. .\scripts\antigravity\Invoke-SnsQaBaseline.ps1
Invoke-SnsQaBaseline -Suite 'unit-core' -OutDir 'out\antigravity\runs\run-001'
```
