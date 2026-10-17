# Skyrim NPC Studio Ultimate 1.1.0

Rama `release/v1.1`. Integra en una sola versión la cadena de PR apilados #8 → #19 (la punta, #19,
contiene a todos los anteriores) y la corrección de dependencias #20. Versión anterior: 0.1.0 (beta
candidata 1, PR #14).

**Estado:** integración local compilada, probada en Windows y empaquetada. Los PR que incluye siguen
**pendientes de revisión** por el dueño y nada se ha comprobado **dentro de Skyrim**; los NPC nuevos y
los parches exportados mantienen `readyForGame: false` / `inGameVerified: false`. No es una release
publicada en GitHub.

## Qué incluye

| PR | Contenido |
|---|---|
| #8 | Safe Patch Engine: escribe solo los campos pedidos; lectura conservadora de máscaras |
| #9 | Paquete de prueba en Skyrim (Serana, Lydia, Shady Sam) |
| #10 | Easy Mod Creator de 1100 a 1920 px (maquetación medida) y aviso de dependencias |
| #11 | Estabilidad: deshacer/rehacer exacto, cambio rápido de NPC, ciclo de proyecto (ASan limpio) |
| #12 | Character Forge: integra los PR #5 y #6 verificados |
| #13 | Release reproducible desde `dist`, ZIP verificado |
| #14 | Documentación sincronizada, beta candidata 1 (0.1.0) |
| #15 | Protección facial (donante, presets, proyectos), prioridad del FaceGen en MO2 (`installAbove`), filtro por mod en prendas |
| #16 | Visor: piel del cuerpo coherente con la cara (SkinTint) |
| #17 | Visor: normal maps `_msn`, normales geométricas, tinte de pelo por color de vértice |
| #18 | Mapa de control de la apariencia (pestaña Origen) y análisis de todo el perfil |
| #19 | «Usar el rostro de X»: parche asistido desde el mapa de control |
| #20 | Dependencias: MessagePack 2.5.305 (NU1902, GHSA-qhrr-8q5h-9q3h) |

## Sistemas de seguridad (sin cambios)

Hair Color Safe Mode, FaceGen Integrity Guard, NPC Safe Patch Engine, bloqueos de cabezas High Poly,
`installAbove`. El estudio no modifica Skyrim/Data ni los perfiles de MO2.

## Cómo se comprobó (Windows 11, MSVC 14.44, Qt 6.11.2, .NET 10)

Carpeta y compilación nuevas (`D:\.Proyectos Games\SkyrimNpcStudio-V1.1`, commit `f9d02b7`):

| Prueba | Resultado |
|---|---|
| Compilación debug desde cero (vía `dev-env.ps1`) | correcta |
| ctest | 162/162 |
| Pruebas del worker (`SkyrimNpcStudio.BethesdaWorker.Tests`) | 37/37 |
| Pruebas de exportación (`SkyrimNpcStudio.ExportTests`) | 75/75 |
| Arnés en el perfil MO2 real, 1100×700: Lydia `appearance-control, creator-layout, easy-mod-layout` | PASSED (39 comprobaciones) |
| Serana `hair-safe, switch-npc-rapid, undo-redo-exact` | PASSED (374) |
| Shady Sam `appearance-control` | PASSED (13) |
| Todo el perfil `appearance-scan` (1308 NPC únicos) | PASSED (9) |
| `release.ps1` dos veces | ZIP idéntico: `SkyrimNpcStudio-1.1.0-f9d02b7-win64.zip`, 112,2 MB, SHA-256 `3C29B309FD8B94A09E07FBFE5C898B6F3FA4C484C3058AFFE2669B63C1A409A9` |
| `verify-release.ps1` (app extraída del ZIP contra el perfil real) | 1771 archivos iguales al manifiesto, versión 1.1.0, `dirty=False`, 8686 NPC indexados, `appearance-control` PASSED, 0 avisos QML |
| Perfil MO2 `Default` (SHA-256 de sus 10 archivos) y `Data` (41 archivos, fecha más reciente) antes y después | sin cambios |

El ZIP se generó desde `f9d02b7`; los commits posteriores de la rama solo cambian documentación.

## Problema conocido

El primer arranque del arnés en la carpeta nueva falló al indexar con `SQLite Error 19: UNIQUE
constraint failed: plugin_files.signature`; repetirlo sin cambios pasó (8686 NPC). La caché del índice
(`%LOCALAPPDATA%\SkyrimNpcStudio\SkyrimNpcStudio\cache`) la comparten todas las compilaciones del
estudio, y el indexador busca la firma y luego inserta sin hacerlo de forma atómica: si dos procesos
indexan el mismo perfil a la vez pueden chocar. La causa exacta de este fallo no se demostró (no había
escritura concurrente al comprobarlo). Existía antes de 1.1.0; corrección propuesta aparte.
