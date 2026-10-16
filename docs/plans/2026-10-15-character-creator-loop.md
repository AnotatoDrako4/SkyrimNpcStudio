# Loop de estabilización de Character Creator (desde 2026-10-15)

Registro de iteraciones del loop autónomo (inspeccionar → detectar → corregir → compilar → probar →
documentar → commit). Plataforma de todas las pruebas: Windows 11 Pro 10.0.26100, MSVC 14.44, Qt
6.11.2, .NET 10.0.401, perfil MO2 `Default` (solo lectura). Nada de lo registrado aquí se ha
comprobado dentro de Skyrim: `PENDIENTE_DE_VALIDACION_SKYRIM` salvo que se indique evidencia real.

Estado de partida: PR #8 → #14 abiertos, sin fusionar; `main` en `78ea95c`.

## Iteración 1

- **Commit base:** `3576d06` (PR #14). Rama: `feature/facegen-install-above`.
- **Problema 1 — prioridad del FaceGen en MO2.** Evidencia: Lydia tiene su FaceGen suelto en
  `Farodadestin` y en `NITHI NPC Enhancement - Whiterun` (D'Ibella's Blessing está desactivado). El
  mod exportado solo funciona si queda por encima de todos; el estudio solo conocía al ganador y el
  README decía «comprobar que ningún otro mod…» sin nombrarlos.
  - **Solución:** `facegen_competitors` (estudio) lista, con `VirtualFileSystem::providers_of`, los mods
    con FaceGen/FaceTint suelto del NPC en orden de prioridad; se excluyen los BSA (un archivo suelto
    siempre gana) y la `Data` del juego. Se muestra en el paso 4 del Easy Mod Creator (o, si el FaceGen
    ganador está en un BSA, que el suelto lo sustituye) y se envía al worker como
    `faceGen.installAbove`. El worker lo escribe en `manifest.json` (`faces[].installAbove`), en el README
    («Prioridad en MO2: coloca este mod por encima de …») y en `pendingVerification`.
  - **Archivos:** `apps/desktop/viewmodels/appearance_editor.cpp`, `ExportWizardDialog.qml`,
    `services/BethesdaWorker/Contracts/OutfitDtos.cs`, `PatchBuilder/ModPackager.cs`,
    `tests/export/.../FacePatchExportTests.cs`, `apps/desktop/components/appearance_stress.cpp`.
  - **Pruebas:** .NET export 69/69 y worker 36/36 (con las aserciones nuevas de `installAbove`).
    Arnés `hair-safe,easy-mod` (comprueba `installAbove` del manifiesto = diagnóstico), PASSED en los tres:
    Lydia `[NITHI NPC Enhancement - Whiterun, Farodadestin]`; Shady Sam `[]` (su FaceGen está en el BSA
    de Alternate Start: el suelto del mod siempre gana); Serana `[ESP-FE Servant Priscilla …]`.
- **Problema 2 — indicador «Donante» muerto.** Evidencia (comprobación estática QML ↔ view models):
  `CharacterToolbar.qml` leía `appearance.donorFormKey`/`donorName`, que no existen; el indicador no
  aparecía nunca.
  - **Solución:** propiedad `faceSource` (el `FaceState.source` que ya escribe `applyDonor`), que sigue a
    deshacer/rehacer y a restablecer. Arnés `donor-export`: comprueba el texto tras aplicar el donante y
    que se vacía al restablecer.
    Ingrid `donor-export`: «Donante: donante» y vacío tras restablecer, PASSED.
- **ECC:** 2.2.0 instalado; datos del proyecto en `~/.local/share/ecc-homunculus/projects/414d959be4f6`;
  0 instincts (solo observaciones), nada que exportar ni promover.
- **Petición del usuario — filtro por mod en el catálogo de prendas.** El filtro existía como texto libre
  escondido en «Filtros avanzados» (nombre exacto del ESP). Ahora es un selector visible «Mod:» con los
  plugins que definen o modifican armaduras, su mod de MO2 y cuántas (`StudioController.armorPlugins`, de
  `catalog.listPlugins`; sin cambios en el worker). Arnés `armor-mod-filter` sobre el perfil real: 166
  plugins listados, `[Amsedillir] Runes of Roots.esp` → 33 prendas (= índice), «Todos los mods» → 8297.
- **Regresión:** ctest 138, 0 fallos; .NET 105/105.
- **Pendientes:** prueba en Skyrim (`PENDIENTE_DE_VALIDACION_SKYRIM`); revisión con ventana visible.
- **Próxima tarea:** seguir la auditoría del Character Creator (controles sin efecto, textos de error).
