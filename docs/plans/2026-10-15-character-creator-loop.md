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

## Iteración 2

- **Commit base:** `454520e` (PR #15). Misma rama.
- **Problema 1 (crítico) — el donante saltaba la protección facial.** Evidencia (código):
  `AppearanceEditor::applyDonor` no llamaba a `refuse_blocked`, a diferencia de sliders, peso y tono. En
  una cabeza protegida (Serana: morphs, Head Parts y tono bloqueados) aplicar un donante reemplazaba
  morphs, Head Parts y tono y regeneraba la cabeza: la deformación que la protección debe impedir.
  - **Solución:** `donor_blocked_reason()` (bloqueo de `face-morphs`, `head-parts` o `skin-tone`);
    `applyDonor` lo rechaza antes y después de leer el donante; los botones de donante se desactivan con
    el motivo.
  - **Pruebas:** `donor-export` ya no se salta las cabezas protegidas: Serana → «donor blocked …» y
    «protected head unchanged after the refused donor», PASSED; Ingrid → donante aplicado y exportado,
    PASSED.
- **Problema 2 — pestaña «Donantes» del Character Creator vacía.** Evidencia (comprobación estática):
  su lista usaba `studio.npcs`, que no existe. Ahora busca con `searchNpcs` y muestra `npcResults`.
- **Problema 3 — texto engañoso.** Ambas vistas de donante decían que se aplica el «FaceGen» del otro
  NPC; `applyDonor` regenera el FaceGen desde el registro del donante. Texto corregido.
- **Regresión:** ctest 138, 0 fallos.
- **Próxima tarea:** seguir la auditoría (operaciones disponibles que deberían bloquearse: presets
  RaceMenu y Head Parts en cabezas protegidas).
