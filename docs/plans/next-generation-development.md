# Desarrollo de nueva generación — registro

Base: PR #17 (`feature/render-fidelity`, 2200dba). Rama: `feature/appearance-control-map` (PR apilado
sobre #17, sin merge). Plataforma: Windows 11, MSVC 14.44, Qt 6.11.2, .NET 10, perfil MO2 `Default`
de `D:\Apps\ModOrganizer.ini` en solo lectura. Nada de este registro se ha comprobado dentro de Skyrim.

## 1. Auditoría de partida

Lo que ya existía y se reutiliza (no se reescribe):

- Worker Mutagen: cadena de versiones de cada NPC (`overrides[]` con `changedFields` = campos cuyo valor
  difiere de la versión anterior) y ediciones perdidas (`conflicts[]`).
- MO2: VFS con `providers_of(path)` (todas las copias sueltas por prioridad) y `AssetResolver::locate`
  (copia efectiva, suelto frente a BSA, mod que aporta el BSA).
- Análisis facial (`diagnose_face_compatibility` + `compare_facegen`): regenera la cabeza desde el
  registro y mide la desviación frente al FaceGen del perfil.
- Avisos de procedencia sueltos: `npc.faceGenWarning` (mod del plugin ganador ≠ proveedor del FaceGen
  suelto) y `provenance.conflict` en Protección facial. Ambos comparan con el **ganador** del registro,
  aunque el ganador no sea quien fija la cara, y no usan la medición: falsos positivos (un parche de
  atuendo que gana el registro) y falsos negativos (FaceGen en BSA no visto por `faceGenWarning`).

## 2. Evaluación de proyectos

| | A — Conflict Intelligence | B — Creador inteligente / High Poly | C — Estudio multi-NPC |
|---|---|---|---|
| Problema | «Cara oscura» y caras que no corresponden: el problema más frecuente al combinar replacers | Crear o adaptar caras High Poly | Editar varios NPC en un mod |
| Datos disponibles | Todos (cadena, VFS, BSA, medición) | Faltan: morphs/TRI High Poly fiables, bloqueos vigentes por seguridad | Proyecto y exportación son de un NPC |
| Riesgo | Bajo: solo lectura | Alto: geometría, tocaría bloqueos de seguridad | Medio: cambia contratos de exportación |
| Verificable sin Skyrim | Sí, con datos sintéticos y el perfil real | Parcial | Sí, pero sin valor nuevo inmediato |

Elegido **A**. B queda como prototipo aislado posterior (sus bloqueos High Poly no se relajan); C
después, cuando el análisis por NPC exista para cada uno.

Cuestionamiento del enfoque: un «motor de conflictos» genérico (todas las diferencias campo a campo)
repetiría xEdit. La aportación útil es más estrecha: unir **autoridad por campo del registro** +
**archivo efectivo en MO2 (suelto/BSA)** + **coherencia geométrica medida** en un veredicto por NPC
con la acción concreta. Ninguna de las tres fuentes basta sola:

- xEdit muestra versiones y campos, pero no los archivos FaceGen ni su procedencia en MO2.
- El panel de conflictos de MO2 muestra archivos, pero no qué plugin fija la cara.
- Herramientas tipo EasyNPC eligen de qué mod tomar cada NPC y generan un parche; no explican por qué
  una combinación existente falla ni miden si el FaceGen sale del registro.

## 3. Diseño

- `sns::appearance::analyze_appearance_control(ControlInputs) → ControlReport` (puro, sin E/S).
- `StudioController::appearanceControl` reúne las entradas (worker, VFS, resolver, análisis facial) y
  recalcula al cargar el NPC y cuando termina el análisis facial (`faceGenChanged`); solo notifica si
  el mapa cambió. La desviación solo cuenta si el análisis examinó el mismo FaceGen del NPC actual.
- Pestaña **Origen** del Character Creator y avisos de la ficha del NPC (sustituye a `faceGenWarning`).
- Reglas y límites: `docs/APPEARANCE_CONTROL_MAP.md`.

## 4. Iteración 1 — motor, integración, panel

- Motor + 14 casos unitarios sintéticos (90 comprobaciones): vanilla en BSA, control por campo frente
  al ganador, cadena con dos mods faciales, desajuste «cara oscura» y sus tres sugerencias, coincidencia
  medida, umbral, sin FaceGen/FaceTint, FaceTint de otro mod, ediciones perdidas frente a replacer,
  copia traducida del plugin, cadena vacía, escala.
- Escenario de arnés `appearance-control` (perfil real, 1100×700): contrasta el mapa con la cadena del
  worker (el controlador es una versión de la cadena, su mod es el proveedor MO2 del plugin y ninguna
  versión posterior vuelve a cambiar esos campos), con el análisis facial (mismo mod del FaceGen, misma
  desviación), con el VFS (copias sueltas que pierden) y comprueba la pestaña Origen (veredicto,
  maquetación sin recortes). Guarda `appearance-control.json` y `.png`.

### El perfil real corrigió el motor (3 falsos positivos)

Primera pasada con las reglas iniciales:

| NPC | Resultado inicial | Causa real |
|---|---|---|
| Lydia | `LostFaceEdits` (aviso): HeadParts, HairColor, FaceMorph, FaceParts, TintLayers de NITHI | Helen_Lydia_replacer (mod «Farodadestin») sustituye la cara de NITHI **y** aporta el FaceGen: replacer completo, no edición perdida |
| Serana | `LostFaceEdits` (aviso): HeadParts de USSEP | Igual: Priscilla replacer sustituye cara y FaceGen |
| Shady Sam | `FaceGenRecordMismatch` (aviso) | El `.esp` gana desde la traducción («Castellano con voces»), que sustituye archivo por archivo el `.esp` del mod original; el FaceGen está en el BSA del original («…SSE»), que también trae el `.esp` (comprobado en `F:\Mods Skyrim\mods`, solo lectura) |
| Ingrid | `FaceSourcesConsistent` | vanilla, correcto |

Correcciones (con prueba unitaria de cada forma real):

- Un campo facial «perdido» cuyo controlador pertenece al mod que aporta el FaceGen efectivo es un
  reemplazo completo → `FaceReplaced` (info). Sigue siendo aviso si lo tomó un mod sin FaceGen (parche
  hecho sobre una copia antigua del registro).
- Si el mod del FaceGen trae su propia copia del plugin que fija la cara y gana otra copia del mismo
  archivo → `FaceGenFromReplacedPluginCopy` (info), con la consecuencia posible explicada.
  `OverrideLink.plugin_mods` = `providers_of(plugin)`.

Segunda pasada: Lydia, Serana y Shady Sam «Sin conflictos graves (ver notas)», Ingrid «encajan».

### La medición depende de las mallas de cabeza del perfil

Desviaciones medidas: Ingrid (vanilla, BSA del juego) 0,699; Shady Sam 0,834; Lydia 1,129; Serana
2,797. Con las mallas **vanilla** la regeneración coincide con el Creation Kit (≤ 0,01 cabezas
femeninas, ≤ 0,09 barbas: prueba local `FaceGen regenerated from vanilla records matches the Creation
Kit output`). En este perfil `meshes\actors\character\character assets\femalehead.nif` lo sustituyen
mods («Expressive Facegen Morphs SE», «HPH HalfDragon Race…», comprobado en `F:\Mods Skyrim\mods`):
el estudio regenera con esa cabeza y el FaceGen vanilla, hecho con la original, ya no coincide. Por eso
la medición se muestra como dato y solo rebaja un aviso cuando es < 0,05; el veredicto se apoya en la
procedencia. Documentado en `docs/APPEARANCE_CONTROL_MAP.md`.

### Rendimiento

| Medida | Valor (Debug, MSVC) |
|---|---|
| Análisis en la app por NPC (cadena + VFS + resolver + motor) | 1,5–2,8 ms (Lydia 1,7; Serana 1,5; Shady Sam 2,2; Ingrid 2,8) |
| Motor, caso extremo: 400 versiones del registro y 300 mods con FaceGen suelto | 11 ms por análisis |
| Recalcular en cada reconstrucción del FaceGen | solo notifica a QML si el mapa cambió |
| Memoria | un `QVariantMap` por NPC seleccionado (decenas de cadenas) |

El análisis no añade E/S de disco: VFS y resolver ya están indexados en el snapshot del perfil.

## 5. Iteración 2 — todo el perfil

`StudioController::scanAppearanceControl` aplica el mismo motor a cada NPC único del perfil (sin la
medición facial, que exige regenerar cada cabeza): lista por páginas de 1000 (`searchNpcs`, `kind:
unique`); los de una sola versión (`override_count` = 1, incluye el origen, comprobado en
`CatalogStore`) se analizan sin RPC con la ruta FaceGen derivada de la FormKey (misma regla que
`FaceGenFor` del worker); los de varias versiones piden `getNpc`. Panel «Todo el perfil» en Origen:
progreso, recuento por código y lista (avisos primero) que abre el NPC al pulsar. Escenario de arnés
`appearance-scan`: totales coherentes, tres NPC marcados abiertos uno a uno conservan sus hallazgos
(la medición solo puede rebajar un desajuste), panel sin recortes.

### El perfil completo corrigió el motor otra vez

| Pasada | Avisos | Causa del ruido | Corrección |
|---|---|---|---|
| 1 | 440 (`NoFaceGen` 210) | criaturas (Alduin, cabra, perro) y NPC de plantilla de Immersive Wenches: el BSA de Wenches trae 400 FaceGen y Abigail (`00C3BC`) no está porque toma la cara de su plantilla (comprobado leyendo los nombres del BSA, solo lectura) | `facegen_expected`: plantilla con rasgos, o raza sin FaceGen en ningún NPC del perfil → `FaceGenNotUsed` (ok) |
| 2 | 232 (`FaceGenRecordMismatch` 215, 112 «USSEP en castellano» → BSA del juego) | Aeri: USSEP solo cambia HeadParts; misma topología en la medición | `FaceGenHasRecordHeadParts`: si el otro mod solo fija HeadParts y cada pieza con malla tiene su forma en el FaceGen → info |
| 3 | 215 (solo 18 rebajados) | USSEP añade `MarksFemaleHumanoid00NoGash`, pieza sin malla que nunca genera forma (registro de evidencia del arnés: `head parts [...] FaceGen shapes [...] absent [...]`) | las piezas sin `modelPath` no se comparan |

El arnés de la pasada 1 también se colgaba: esperaba a que el editor facial «cargara» un NPC de
plantilla, que el editor nunca carga. El escenario espera ahora al mapa y al análisis de ese NPC.

### Resultado final en el perfil real (1308 NPC únicos, 415 plugins, 85 297 archivos)

| | Pasada 1 | Final |
|---|---|---|
| Con avisos | 440 | **128** |
| Con notas | 449 | 553 |
| Correctos | 419 | 627 |

Códigos finales: `FaceSourcesConsistent` 857, `FaceReplaced` 445, `FaceGenNotUsed` 208,
`ShadowedFaceGen` 161, `FaceGenRecordMismatch` 108, `FaceGenHasRecordHeadParts` 107, `LostFaceEdits`
82, `FaceGenFromReplacedPluginCopy` 19, `FaceTintFromOtherMod` 11, `NoFaceGen` 2.

Desajustes que quedan, por pareja (datos faciales → FaceGen): «AI Overhaul 1.8» → «NITHI NPC
Enhancement - Riften» 28; USSEP en castellano → BSA del juego 16; «Open World Loot - Spanish» → «NITHI
… Dawnguard» 8; «AI Overhaul 1.8» → «BeastHHBB» 7; «Sons of Nirn - Whiterun» → «NITHI NPCS -
Companions» 6; juego → «High Poly Heads for All AE Content» 6. El primero es el caso clásico de cara
oscura: un plugin posterior hecho sobre el registro vanilla devuelve los datos faciales al original
mientras el FaceGen de NITHI sigue ganando en MO2 — necesita un parche (o el orden inverso). No se ha
verificado dentro de Skyrim.

Pendientes conocidos: `NoFaceGen` 2 (el jugador `000007` —no tiene FaceGen— y Vanto Loreio, sin
revisar); los 108 desajustes no se han revisado uno a uno.

### Rendimiento del análisis del perfil (Debug, MSVC, perfil real)

| Medida | Valor |
|---|---|
| Análisis completo | 38,3 s (listado 1,4 s; 848 llamadas al worker) |
| Por NPC | 29 ms de media; 462 NPC de una versión sin RPC (846 `getNpc` + 2 páginas de listado) |
| Coste de leer el FaceGen para la comprobación de piezas | 25,8 s → 38–45 s (solo cuando HeadParts lo fija otro mod) |
| Mapa de un NPC al seleccionarlo | 1,7–2,7 ms; 37 ms si lee un FaceGen de un BSA (Shady Sam) |
| Motor, 400 versiones × 300 mods | 11 ms |

Siguiente mejora de rendimiento si hiciera falta: un método por lotes en el worker (cadena de varios
NPC por llamada) y leer solo el bloque de nombres del NIF en vez del archivo completo.
