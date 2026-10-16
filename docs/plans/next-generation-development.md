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
