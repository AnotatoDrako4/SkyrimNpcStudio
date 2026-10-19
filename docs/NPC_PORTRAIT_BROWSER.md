# Ultimate NPC Portrait Browser

Selector visual de NPC del Character Creator (pestaña **NPC** del panel izquierdo). Inspiración funcional:
RaceMenu Portraits (Nexus 194382). No se usa código, DLL, interfaz ni recurso de ese mod: todo sale de la
arquitectura del estudio (QML → viewmodels C++ → motores NIF/DDS/FaceGen → snapshot MO2/VFS).

Estado: `INTEGRACION_WINDOWS_OK` en el perfil MO2 real (ver «Evidencias»). Nada de esto se ha observado
dentro de Skyrim: los retratos son renders del estudio, no capturas del juego, y la función no escribe nada
que el juego cargue.

## Fase 1 — Selector visual

| Pieza | Dónde |
|---|---|
| Conmutador Lista / Galería (la lista no cambia) | `qml/NpcBrowser.qml` (`npcListModeButton`, `npcGalleryModeButton`) |
| Cuadrícula responsive (`GridView`, columnas = ancho / 92 px, una fila de `cacheBuffer`) | `NpcBrowser.qml` (`npcGallery`) |
| Tarjeta: retrato, nombre, raza y sexo, plugin ganador, estado de apariencia | delegado del `GridView` |
| Búsqueda y filtros (texto, sexo, tipo, plugin, apariencia) | los mismos de la lista: `StudioController::searchNpcs`, `setAppearanceFilter` sobre el mismo `VariantListModel` (`npcResults`) |
| Catálogos de miles de NPC | páginas de 500 (`StudioController::kNpcPage`): al llegar al final de la lista o la galería, `loadMoreNpcs()` pide la siguiente página de la misma búsqueda (orden estable nombre + FormKey) y la **añade** sin reiniciar la vista; un filtro que deja la vista corta o vacía sigue pidiendo páginas. «· cargando más…» mientras llega |
| Búsqueda de donantes | modelo propio (`donorResults`, `searchDonors`): ya no reemplaza las filas del explorador |
| Clic: selecciona por FormKey | `StudioController::selectNpc(formKey)` |
| Doble clic / Enter: abre el editor (pestaña Rostro) | señal `editRequested` → `CharacterCreator.qml` |
| Teclado: flechas (navegación del `GridView`), Espacio selecciona, Enter abre | `keyNavigationEnabled`, `Keys.on*Pressed` |

Una tarjeta sin retrato dice por qué («Retrato en cola», «Generando retrato…», «Sin retrato: <motivo>»);
nunca se muestra una cara de relleno.

## Fase 2 — Motor de retratos

- **Escena**: un `SceneViewModel` propio (`StudioController::portraitScene`) en modo retrato: solo la cabeza,
  texturas reducidas, caché de recursos propia de 96 MB (`rendering::SceneCache`), mismo código NIF/DDS/FaceGen
  que el visor principal. No hay un segundo parser.
- **Escenario**: `qml/PortraitStage.qml`: piezas de cabeza, cámara frontal fija encuadrada en la pieza de
  cabeza más grande que no es pelo (distancia 2,9 × radio, FOV 30°), dos luces neutras, sin interfaz.
- **Captura**: `PortraitRenderer` (`viewmodels/portrait_renderer.*`) crea el escenario directamente en una
  `QQuickWindow` de un `QQuickRenderControl` (nunca en la ventana principal), en el contexto QML de su
  componente y con la escena como propiedad inicial, y lo renderiza en una textura QRhi propia de 256×256
  **en un hilo de render propio** (`PortraitRender`), con las reglas de Qt para un render control con hilo:
  `prepareThread()`, e `initialize()`, recursos QRhi, `beginFrame()`/`sync()`/`render()`/lectura/`endFrame()`
  en ese hilo; `polishItems()` en el hilo GUI, que espera solo durante `sync()` (~230 ms). Los objetos de
  Qt Quick 3D solo se tocan en el hilo GUI; la cabeza que un fotograma está dibujando no se borra hasta que
  vuelve, y un fotograma de un render cancelado (aunque sea del mismo NPC: contador de generación) se
  descarta. Al salir (`aboutToQuit`) el hilo se libera primero, con un límite de 5 s. Una captura con menos
  del 2 % de píxeles distintos del fondo se repite un fotograma y, si sigue vacía, es un error, no un retrato.
- **Caché** (`viewmodels/portrait_cache.*`): carpeta por perfil
  (`%LOCALAPPDATA%/…/portraits/<instancia>-<perfil>-<hash>`, o `SNS_PORTRAIT_CACHE_DIR`), un PNG y un JSON por
  NPC. Una entrada vale solo si coinciden: FormKey, versión del renderizador (`portrait-v3`), digest del
  snapshot MO2 (perfil + plugins activos con proveedor, tamaño y fecha), hash SHA-256 de la imagen y cada
  recurso usado (ruta de datos → archivo suelto con tamaño y fecha, o BSA + mod proveedor). Las texturas
  compuestas (`<difusa>|facetint:<tinte>`, `|skintint:`, `|tangent:`) registran cada archivo de entrada.
  Escritura atómica (`QSaveFile`); la tarjeta solo muestra lo que la caché confirma al releerlo.
- **Planificación**: las tarjetas creadas por el `GridView` (visibles + una fila) piden su retrato y lo
  liberan al destruirse; cola de 48 como máximo (lo más reciente primero; lo más viejo se descarta),
  8 consultas de caché por pasada del bucle de eventos, un retrato renderizándose a la vez, vigilancia de
  45 s por cabeza (error, no bloqueo). `cancelAll()` vacía la cola y cancela la cabeza en curso; un cambio
  o recarga de perfil lo hace al empezar (`StudioController::loadProfile`).
- **Errores**: NPC inexistente, plantilla sin adaptador, sin piezas de cabeza, encuadre imposible, render o
  lectura fallidos: la tarjeta queda en estado `error` con el motivo y la cola sigue.
- **Recursos ausentes**: si la cabeza se dibujó sin archivos que el perfil no tiene (p. ej. una textura base
  de cabeza que un mod referencia y no instala), el retrato se guarda tal cual (geometría real sin esa
  textura) y la tarjeta muestra «N ausente(s)»; el inspector lista las rutas.

## Fase 3 — Inspector y conflictos

Bajo la galería, para el NPC seleccionado (`npcGalleryInspector`), reutilizando el Appearance Control Map:
plugin y mod de los datos faciales, origen (mod; suelto, BSA o ausente) del FaceGen y del FaceTint, avisos,
casos ambiguos con su estado de revisión (se deciden en la pestaña Origen; nunca se aprueban solos) y la
procedencia del retrato. El filtro «Apariencia: con avisos faciales / casos ambiguos / sin analizar» usa el
escaneo del perfil.

- **Comparación con evidencia**: al rehacer un retrato, la caché conserva el render reemplazado
  (`<npc>.previous.json` + su PNG, como máximo uno) cuando difiere en imagen, recursos, snapshot o
  renderizador. El inspector muestra «Render actual» y «Render anterior» con sus fechas y los recursos que
  cambiaron entre ambos (`PortraitCache::changed_resources`). Solo se comparan renders de la misma versión
  del renderizador; si no, lo dice. Una imagen alterada en disco deja de ofrecerse.
- **Capturas de Skyrim**: «Adjuntar captura de Skyrim…» copia una imagen elegida por el usuario
  (≤ 32 MB, legible como imagen) como `<npc>.skyrim.json` con `source: "skyrim-capture"`, ruta, tamaño y
  fecha del archivo original. Se muestra aparte, con borde de aviso y el texto «la aportaste tú; el estudio
  no la verifica ni la usa como retrato». Nunca sustituye al render de la tarjeta.

## Evidencias (2026-10-18, perfil real Default: 415 plugins, 8686 NPC)

Ejecuciones en `out/local-implementation/20261018-portraits-c2/` (registro: `changes-cycle2.md`). Arnés:
`scripts/smoke-test.ps1 -StressAppearance npc-portraits|npc-gallery -ShowWindow`. Cifras de r4 (primera
apertura), r5 (segunda apertura, misma caché) y r7 (final); debug build, una sola máquina.

| Medida | Valor medido |
|---|---|
| Primera tarjeta tras abrir la galería | 153–154 ms (r4, r5, r7, g1) |
| Primer retrato, primera apertura | 22,4 s (compite con la construcción del visor principal al arrancar) |
| Tiempo por retrato (11 renders) | media 3,6 s, máximo 6,8 s (r4, r7) |
| Captura más larga en el hilo GUI | 3,4–3,5 s |
| Segunda apertura | primer retrato en 309 ms desde la caché; 11 aciertos, 0 renders (r5) |
| Desplazamiento rápido (≈250 peticiones) | cola máx. 18 de 48, 0 descartadas; pausa máx. del bucle 386–409 ms |
| 20 s generando retratos | 6–7 retratos; pausa máx. del bucle 205–209 ms (r4, r7); 2,8 s en r5 |
| Memoria | privada 2358 → 2615 MB al abrir la galería; pico working set 2258–2399 MB |
| Cancelar | cola 0 y nada renderizándose al instante |
| Recarga del perfil | trabajo cancelado al instante; primer retrato a los 20–21 s; tarjetas visibles resueltas a los 25–27 s |
| Recurso modificado | entrada caducada («resource changed: …0008774F.nif»), rehecha, render anterior conservado con el archivo cambiado |
| NPC inexistente / sin cabeza / plantilla sin adaptador | estado de error con motivo; el siguiente retrato se genera |
| Visor principal tras los retratos | 18,6 % dibujado antes y después (ventana expuesta) |
| Filtros (g1) | avisos 43/43, revisión 18/18, sin analizar 0/0, 0 incorrectos |
| Pruebas | CTest 187: 178 PASS, 0 FAIL, 9 SKIP; `[local]` 6/6 PASS; caché 7/7 PASS |
| Catálogo completo por la galería (p3, ciclo 3) | 8686 de 8686 filas en 17 páginas adicionales, 20,1 s; 0 repetidas entre páginas; tarjetas finales = sus filas |
| Desglose de la captura más larga en el hilo GUI (r8, antes del hilo de render) | 4131 ms = 4058 ms fotograma + lectura de la textura + 76 ms PNG y caché |
| Fases del fotograma más largo (s1) | polish 0 ms, beginFrame 1 ms, sync 227 ms, **render 3440 ms**, endFrame+lectura 15 ms |
| Hilo de render (z1, final) | bloqueo máx. del hilo GUI por un retrato **224 ms** (antes 3752–5027 ms); pausa máx. del bucle de eventos mientras se hacen los primeros retratos **227 ms**; 20 s generando: 84 ms; render 3339 ms ya fuera del hilo GUI |
| Caché de pipelines QRhi (probada y retirada) | se guardaba y cargaba (146–280 KB) pero render 3605 ms en frío y 3629 ms en caliente: el coste no es D3DCompile |

### Build Release (`build/package`, 2026-10-18, `out/local-implementation/20261018-release-perf/`)

Las cifras de arriba son del build Debug. Con el build Release, mismo perfil y escenarios, ventana visible, todo PASS y exit 0:

| Medida | Release |
|---|---|
| Primera tarjeta | 23–24 ms |
| Primer retrato, primera apertura | 2994 ms |
| Tiempo por retrato (11 renders) | media 470 ms, máximo 1207 ms |
| Bloqueo máx. del hilo GUI por un retrato | 66 ms (sync 65 ms; render 232 ms en el hilo de render) |
| Pausa máx. del bucle mientras se hacen los primeros retratos | 66 ms |
| 20 s generando retratos | 13 retratos; pausa máx. 69 ms |
| Desplazamiento rápido | cola máx. 36 de 48, 0 descartadas; pausa máx. 90–128 ms |
| Segunda apertura (caché) | primer retrato en 126 ms; 11 aciertos, 0 renders |
| Catálogo completo por la galería | 8683 filas en 17 páginas, 8051 ms; pausa máx. 69 ms |
| Recarga del perfil | primer retrato a los 4,0–4,4 s; tarjetas visibles resueltas a los 4,4–4,9 s |
| Memoria privada del proceso | 2209–2211 MB antes de abrir la galería, 2426–2457 MB después |

Límites conocidos:
- El primer fotograma de las primeras cabezas de cada ejecución sigue tardando ~3,3–3,6 s (Qt Quick 3D genera sus
  materiales), ahora en el hilo de render: el retrato tarda, la interfaz no se congela.
- Con la ventana oculta del arnés las pausas grandes que quedan (≈2 s) son del propio arnés al capturar la ventana
  no expuesta; el bloqueo por retrato es el mismo (~238 ms).
- (Resuelto) duplicados del catálogo por mayúsculas del master: columnas FormKey `COLLATE NOCASE`, ganadores con la
  grafía del origen, esquema 4 (perfil real: 8683 NPC distintos, antes 8686 con 3 duplicados; armaduras 8303, antes 8307).
  NOCASE de SQLite solo pliega ASCII: nombres de plugin no ASCII con distinta grafía seguirían duplicándose.
- Con la ventana **oculta** (arnés sin `-ShowWindow`) Qt captura la ventana con un QRhi temporal y la
  segunda captura sale vacía aunque no haya retratos; el arnés lo marca `SKIP`.
- Las tarjetas «ausente(s)» son reales: p. ej. `01411D` referencia `textures\actors\character\imperialfemale\femalehead.dds`,
  que el perfil no tiene; su retrato muestra la cabeza sin esa textura.
- (Resuelto en el ciclo 3) la búsqueda de donantes ya no comparte `npcResults` con la galería.
