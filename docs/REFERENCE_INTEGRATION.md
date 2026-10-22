# Integración de referencias (GenerateFollower · NPC Plugin Chooser 2 · Effigy)

Estado: **A1, A2, A3 y A4 `COMPILADO` + `TESTS_UNITARIOS_OK` + `INTEGRACION_WINDOWS_OK` (perfil real, solo lectura); `PENDIENTE_SKYRIM`.**
Fecha: 2026-10-21. Sin Git (orden del propietario). Evidencia: `out/reference-integration/20261021-refint-a1/CHECKPOINT.md`.
Inventario, hashes y licencias: `out/reference-intake/inventory.md`; brechas: `out/reference-intake/feature-gap.md`.

## Qué se tomó de las referencias (y qué no)

Se estudiaron de forma **estática** (sin ejecutar nada de terceros). Lo adoptado son *ideas*, reimplementadas desde cero sobre
Qt 6.11 / C++20 / Mutagen; **no se copió código**:

| Idea | Origen | Nuestra versión |
|---|---|---|
| Elegir el proveedor de la apariencia de cada NPC y compararlos con una imagen | NPC Plugin Chooser 2 (documentación) | *Appearance Source Matrix* + vista A/B |
| Ranking de **Export Head** por solape de nombres de formas con los headparts efectivos | GenerateFollower `HeadImports.cs` | A3: `match_export_head` (cobertura + pista de nombre), reimplementado |
| Asistente por pasos con *readiness* visible | Effigy (documentación externa, sin fuente) | pendiente (B2) |

No se importa: renderer WPF, AvalonDock, Three.js, `PinPhysicsBones` (fija huesos: no es SMP), instaladores, assets ni presets de terceros.

## Appearance Source Matrix (A1)

Una **fuente** = «versión del registro que fija la cara» + «proveedor del FaceGen». Para un NPC (FormKey) hay una fuente
*efectiva* (la que carga el juego) y una candidata por cada otro mod de MO2 con un FaceGen **suelto** propio.

- **Motor puro** (`engines/appearance`, sin Qt): `source_matrix.{hpp,cpp}`. Cada candidato se juzga ejecutando
  `analyze_appearance_control` con *sus* entradas (FaceGen/tinte del proveedor), así que no hay semántica nueva:
  - `VALID`: sin avisos y el FaceGen se abrió con el cargador seguro (o se midió su desviación).
  - `UNVERIFIED`: sin avisos, pero nada lo prueba (nunca se promueve a `VALID`).
  - `CONFLICT`: el mapa avisa (desajuste registro/FaceGen, archivo dañado, tinte de otro mod…). Un archivo ilegible es siempre `CONFLICT`.
  - `AMBIGUOUS`: aplicar esa cara descartaría ediciones propias de un mod → solo una persona (D1) puede decidirlo.
  - `MISSING`: no hay FaceGen que el editor pueda dibujar (también los plugins que cambian la cara sin FaceGen propio).
- **Borrador no destructivo** (`source_draft.{hpp,cpp}`): JSON determinista, ligado al SHA del perfil y al de las entradas; si cualquiera cambia,
  el borrador queda `stale-*` y no se aplica. **No alimenta ninguna exportación.**
- **Lectura confinada** (`engines/mo2/confined_file.*`, `sns::mo2::confined_file`): antes de leer la copia de *otro* mod de un archivo, la ruta de
  Data se trata como no fiable: vacía, absoluta, con unidad, con `..` o con `:` (flujo alterno) → `UnsafePath`; enlace simbólico → `SymbolicLink`;
  ubicación real fuera de la carpeta del mod (junction o enlace en el camino) → `EscapesFolder`; carpeta/dispositivo → `NotRegularFile`; sin archivo → `Absent`
  (no es un incidente). Probado con junctions reales en una carpeta temporal (el caso del enlace simbólico se omite si Windows no da el privilegio).
- **Capa de aplicación** (`apps/desktop/viewmodels/appearance_sources.*`): reúne los hechos en un hilo del pool (lectura confinada con lo anterior; límites de
  tamaño; SHA-256; `inspect_facegen`), construye la matriz, guarda el borrador (`QSaveFile`, un archivo ilegible nunca se sobrescribe) y pide las imágenes.
  Los borradores van a `AppLocalData/source-drafts/<perfil>.json` (`SNS_SOURCE_DRAFT_DIR` lo cambia, p. ej. en pruebas).
- **Imágenes**: `PortraitRenderer::submit_variant` dibuja la **misma cabeza con el mismo escenario, cámara y luces** que un retrato de la galería, pero
  con los archivos del proveedor puestos en un `AssetOverlay` en memoria (el mecanismo que ya usaba el editor de rostro). Pasan por la cola del
  renderer sin tocar los estados ni la caché de la galería; se guardan en `<caché de retratos>/variants/` con la misma entrada JSON + hash +
  *fingerprints* de los archivos (si un FaceGen cambia, la imagen caduca). Solo se piden con el diálogo A/B abierto.
- **UI** (`qml/charactercreator/`): sección «Fuentes de apariencia» en la pestaña «Origen» y diálogo «Comparar A/B…» (lado a lado o alternar,
  zoom/pan sincronizados, teclado: ←/→ cambia B, Espacio alterna, 0 restablece). Cada lado dice de dónde sale (render del estudio ahora / caché
  verificada / provisional) y que **no es una captura de Skyrim**.

- **Varias a la vez (3–4)**: un interruptor del diálogo A/B pasa a una cuadrícula con las fuentes que se puedan dibujar (A y B primero, hasta cuatro), con la misma cámara, luces y
  tamaño, zoom y arrastre compartidos y el aviso de que no es una captura de Skyrim. Con menos de tres fuentes dibujables el interruptor se desactiva. La tira de fuentes tiene ahora
  barra y rueda horizontal: con cinco o más fuentes las últimas eran inalcanzables.

## Inspector de la caché de retratos (A2)

Ya existían el uso global y «Vaciar retratos» (todo o nada). Lo nuevo es ver **qué es cada archivo** de la carpeta del perfil y quitar **solo lo inservible**:

- **`PortraitCache::inspect`** (`apps/desktop/viewmodels/portrait_cache.*`): una fila por entrada (retrato, retrato anterior, captura de Skyrim) y por imagen que ninguna entrada nombra.
  Cada retrato es `valid`, `stale` (cambió el renderizador, el perfil/orden de carga o un archivo con el que se hizo, con el primero que cambió) o `damaged` (falta la imagen, su huella no
  coincide, entrada ilegible/inválida/de otro NPC); el anterior y la captura son `kept` (el anterior *debe* diferir del perfil: se guarda para compararlo). Coincide con `lookup()` en cada retrato (probado).
- **Qué puede quitar el estudio** (`is_unusable` / `remove_unusable`): retratos desactualizados o dañados, anteriores dañados e imágenes `<nombre>-<hash>.png` sueltas. **Nunca** un retrato vigente, uno guardado
  para comparar, una captura de Skyrim ni un archivo ajeno; una imagen que aún nombra una fila que se queda (un retrato y su anterior pueden compartirla) no se borra. Vuelve a inspeccionar antes de borrar.
- **Aplicación**: `PortraitRenderer::inspectCache / removeUnusable / clearVariants` + propiedad `cacheReport` (en el pool, sin bloquear la GUI; informe acotado a 3000 filas). Las imágenes del A/B (`variants/`) se cuentan aparte y se vacían con su propio botón.
- **UI**: «Inspeccionar la caché…» en Configuración › NPC y retratos → `qml/settings/PortraitCacheInspector.qml` (filtros: requieren decisión / vigentes / anteriores y capturas / todo; motivo en cada fila; «Rehacer» por retrato; el borrado pide confirmación).
- **Rehacer los desactualizados**: botón con progreso y cancelar (`PortraitRenderer::redoStale / cancelRedo`, propiedad `redoProgress`). Pide los retratos desactualizados o dañados de
  la última inspección de cuatro en cuatro (cada uno se comprueba de nuevo contra el perfil; uno ya vigente solo se cuenta), cuenta los errores en vez de colgarse (un NPC que no existe)
  y al terminar vuelve a inspeccionar. No borra nada: el render nuevo sustituye al antiguo, que queda como «anterior» para comparar.
- **Nombres de NPC** junto al FormKey: `catalog.searchNpcs` acepta `formKeys` (opcional, parametrizado, ≤ 200) y `StudioController::resolveNpcNames / npcKey / npcNames` los pide una vez por NPC.
- Solo toca la carpeta de retratos propia del estudio; el escenario del arnés (`--stress-appearance cache-inspector`) se niega a correr sin `SNS_PORTRAIT_CACHE_DIR` aislado.

## Preset ↔ Export Head (A3, solo diagnóstico)

Un `.jslot` nombra sus piezas de cabeza por FormKey; el *Export Head* de RaceMenu (`SKSE\Plugins\CharGen\*.nif`) las nombra como **formas del NIF**
(el nombre de la forma es el EditorID de la pieza). El emparejamiento se hace por contenido y el nombre solo es una pista:

- **Motor puro** (`engines/presets/export_head_match.*`, `match_export_head`): puntúa cada cabeza con la **cobertura** de las piezas del preset (qué parte de
  ellas aparece como forma del NIF; las formas de más —un peinado entero son 100+— no cuentan en contra; un Jaccard simétrico hundía los emparejamientos
  reales, hallazgo con el perfil real); hace falta la pieza **Face** del preset en el NIF; umbrales 0,3 (nombre exacto) / 0,5 (nombre próximo) / 0,85 (sin pista); el
  nombre ignora extensiones apiladas (`Okiri.nif.nif`), palabras vacías (`head`, `preset`, `hph`…) y números/fechas; a igual puntuación gana la exportación
  más nueva, y si no se puede decidir pide **elección manual**. Veredictos: `READY_FOR_REVIEW`, `NEEDS_ASSET` (sin cabeza o faltan texturas),
  `NEEDS_CHOICE`, `FACEGEN_UNREADABLE`, `HEAD_PART_MISMATCH` (falta la pieza Face, raza o piezas sin resolver) y `UNSUPPORTED_TOPOLOGY` (otro número de
  vértices que la malla de la pieza Face). **Siempre** lleva el aviso `UNVERIFIED_IN_SKYRIM`; «hay un NIF» no es «FaceGen seguro». Si la cabeza con el
  nombre exacto del preset está dañada, no se sustituye por otra que solo encaje. Si el único apoyo es el contenido (el nombre no ayuda) añade el código
  `PairedByContentOnly` y lo dice en el mensaje: varios presets pueden encajar con la misma cabeza.
- **Texturas**: RaceMenu escribe la textura de la cabeza junto al NIF (`Data\SKSE\Plugins\CharGen\x.dds`) y la nombra desde la raíz del juego; además del
  `textures\` habitual se busca ahí, para no dar por faltante lo que existe.
- **Perfil real** (16 `.jslot`, 10 cabezas leídas, 0 ilegibles, ~3 s en el pool): 6 `READY_FOR_REVIEW`, 4 `NEEDS_ASSET` (2 sin cabeza exportada; Carrot: el NIF pide
  `carrot2.dds` y en disco hay `Carrot.dds`; ElisifElden: la textura está en `CharGen\Presets\` y el NIF la pide en `CharGen\`), 6 `HEAD_PART_MISMATCH`
  (5 presets UBE/Elowen cuyas piezas no resuelve el catálogo; Rallena: su NIF trae `00KLH_FemaleHeadDarkElf` y el preset pide la `FemaleHeadDarkElf` vanilla).
  Evidencia: `out/reference-integration/20261021-refint-a1/CHECKPOINT_A3.md`.
- **Worker**: `catalog.searchHeadParts` acepta ahora `formKeys` (opcional, SQL parametrizado, acotado a 200) para resolver los EditorID de las piezas del preset
  sin filtro de raza/sexo.
- **Aplicación** (`apps/desktop/viewmodels/preset_head_checker.*`): indexa las cabezas del perfil en el pool (confinadas con `mo2::confined_file`, abiertas con
  `nif::load_mesh`: formas, vértices y texturas que faltan), resuelve las piezas con el worker, calcula los vértices de la malla Face de la pieza y llama al
  motor. **No escribe nada**; la elección manual vive en la sesión.
- **UI**: botón «Export Head…» en cada preset de la pestaña «Presets» → `PresetHeadDialog.qml` (veredicto, mensaje, cabeza emparejada, candidatas con «Usar esta»,
  elección entre todas las cabezas del perfil).

- **Elección manual recordada**: se guarda por perfil en `AppLocalData/preset-head-choices/<perfil>.json` (`SNS_PRESET_HEAD_DIR` lo cambia): `{schema, profileKey, choices:[{preset, head, chosenAt}]}`,
  escrito de forma atómica. Una cabeza elegida que ya no está en el perfil se ignora y se avisa (`manualStale`); un archivo ilegible o inválido se avisa y **nunca se sobrescribe**.

## Piel de la cabeza y del cuerpo (A4, estimación)

La costura visible en el cuello aparece cuando la piel de la cabeza (textura base + tinte FaceGen) y la del cuerpo no tienen el mismo color. Se **estima** con lo que el estudio ya dibuja:

- **Medición** (`rendering::median_skin_color`, `engines/rendering`): color mediano por canal de la textura compuesta de cada forma de piel, **solo en los texels que cubren los triángulos UV de esa forma**
  (el resto de un atlas es relleno: en el perfil real el relleno de unas manos era un rojo oscuro uniforme y una mediana de toda la textura daba «costura probable» en 6 de 9 NPC; era un artefacto, y es
  la razón de medir por UV). Mediana y no media: maquillaje, tatuajes y cicatrices no la mueven.
- **Motor puro** (`sns::appearance::evaluate_skin_match`, `engines/appearance/skin_match.*`): ΔE CIE76 de la cabeza frente al cuerpo (slot 32), manos/brazos (33, 34) y pies (37); `CONSISTENT` (≤ 6),
  `NOTICEABLE` (≤ 12), `LIKELY_SEAM` (> 12) o `UNKNOWN` (cabeza o cuerpo sin medir: por ejemplo si la ropa cubre el cuerpo, que entonces no se dibuja ni se mide). Siempre `ESTIMATE` y `UNVERIFIED_IN_SKYRIM`.
- **UI**: tarjeta «Piel de la cabeza y del cuerpo (estimación)» en la pestaña «Origen» (`SkinMatchCard.qml`, `studio.scene.skinMatch`), con muestras de color y ΔE por parte. No bloquea nada.
- **Límites**: no es lo que dibuja Skyrim (iluminación, sombreadores y mezcla del juego no se reproducen); el color mediano no ve una costura localizada; si la ropa cubre el cuerpo no mide.

## Reglas que se respetan

- Solo lectura sobre MO2, el perfil y Skyrim: lo único que se escribe es el borrador y las imágenes, en carpetas del propio estudio.
- Ninguna exportación nueva; las puertas (Safe Patch, D1, FaceGen verificado) no se tocan. Para *aplicar* un rostro sigue existiendo la resolución asistida.
- «NIF presente» ≠ «FaceGen seguro»; «render del editor» ≠ «juego».

## Cómo probarlo

```powershell
. .\scripts\dev-env.ps1
cmake --build build\refint --target sns_reference_tests ; .\build\refint\bin\sns_reference_tests.exe     # 60 casos: matriz, borrador, confinamiento de rutas, Export Head (los de la caché de retratos: sns_portrait_cache_tests)
#   --stress-appearance preset-head-check          (A3: recorre los .jslot del perfil; escribe preset-head-report.json y capturas del diálogo)
# Contra el perfil real (solo lectura), con todo lo que escribe aislado en la carpeta de la corrida:
#   --stress-appearance source-compare   (NO forma parte de "all": necesita un NPC con dos fuentes dibujables, p. ej. 013364:Skyrim.esm)
```

## Límites conocidos

- Solo FaceGen **suelto** (un FaceGen dentro de un BSA no se distingue por mod). Solo A/B (3–4 opciones pendientes).
- La desviación FaceGen↔registro solo se mide para la fuente efectiva; las demás quedan `VALID` (por coherencia de proveedor y archivo legible) o `UNVERIFIED`.
- Los renders son del editor (Qt Quick 3D), sin verificación dentro de Skyrim.
- Registro de la integración en los archivos reservados: `.ai-coordination/messages/claude-to-antigravity.md`.
