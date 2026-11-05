# Skyrim NPC Studio NEXT-GEN — diseño de sistema

Fecha: 2026-11-05 · Rama: `claude/kind-volta-ejb012` · Base: `66096f5` · Protocolo IPC en este checkout: 1.7.
Etiquetas: **[Confirmado]** leído en este checkout · **[Inferencia]** deducción fuerte · **[Supuesto]** dato faltante.
Este documento no autoriza escrituras en `Skyrim/Data`, perfiles MO2 reales, `main` ni publicaciones (AGENTS.md).

## 0. Conclusión primero

1. **[Confirmado]** Hoy no hay ningún resultado `VERIFICADO_EN_SKYRIM`. Character Forge STAT está `INTEGRACION_WINDOWS_OK ·
   PENDIENTE_SKYRIM` (`CHARACTER_FORGE_BLENDER_TO_SKYRIM.md:7`); F0 tiene los checkpoints 01 y 03 `BLOQUEADO` y los 53 expedientes
   ambiguos con 0 aprobaciones (`2026-10-17-f0-f7-board.md`). El cuello de botella no es tecnológico: es el **ciclo de verificación
   en el juego**, que hoy es manual, lento y sin evidencia estructurada.
2. Por eso el «Skyrim Digital Twin» no es la fase 5: **su primera versión es el kit de prueba en juego (M2) más un almacén de
   evidencias**. Las 12 tecnologías propuestas se apoyan todas en ese ciclo; sin él, cada una añade otro `PENDIENTE_SKYRIM`.
3. **[Inferencia]** Producto A tal como está escrito («cambiar el cabello sin destruir su FaceGen») choca con el motor: en Skyrim
   el cabello del NPC está horneado en la malla FaceGen (`FaceGeom\…\<FormID>.nif`). Cambiar el HDPT de pelo sin regenerar
   FaceGen produce pelo antiguo o desajuste de cabeza. Hay dos caminos válidos (§4.A); el más seguro para el MVP no toca FaceGen.
4. Las 12 tecnologías se reducen a **un núcleo común + 3 productos**. Live Link de escritura, Neural Rigging y AI Copilot quedan
   fuera hasta que el núcleo tenga aceptación real.

## 1. Requisitos

### Funcionales (por producto)

| Producto | Meta mínima verificable | Ya existe en el código [Confirmado] |
| --- | --- | --- |
| A — Editor de NPC existentes | Serana con atuendo distinto, FaceGen intacto, verificado en juego | `NpcSafePatch` (máscara de campos con `DefaultOutfit`/`SleepingOutfit`), `OutfitPatchBuilder`, `FacePatchBuilder`, `engines/appearance` |
| B — Character Forge | Un STAT importado desde `.blend` visto dentro de Skyrim | `forge_bridge`, `tools/blender/forge_skyrim.py` (PyNifly), `StaticResourceBuilder`, `ModPackager` |
| C — Body & Outfit Studio | Prenda generada con BodySlide oficial ≡ salida del estudio, verificada en perfil aislado | `engines/body` (`bodyslide_build`, `bodyslide_morphs`, `bcng_rules`, `body_analysis`), `engines/outfit` (`outfit_fit_doctor`), `ConvertedOutfitBuilder` |

### No funcionales

- **Seguridad de datos:** escritura solo en staging; Safe Patch, máscara de campos, masters, relectura y exportación atómica.
- **Reproducibilidad:** mismo commit + inputs ⇒ mismo ZIP (SHA-256). Cada evidencia ligada a hash de build, perfil e inputs.
- **Escala real medida:** 1308 NPC, 415 plugins, 85 297 archivos (`next-generation-development.md`); `.blend` de hasta 577,7 MiB
  (`CHARACTER_FORGE_SARAH_CRASH_FIX.md`). Interfaz: guarda de regresión < 60 ms de bloqueo (tablero F0).
- **Límite de formato:** BSTriShape SSE ≤ 65 535 vértices/triángulos; nifly trunca sin error; particionado no implementado
  (`CHARACTER_FORGE_SKINNED_MESH.md:17`).
- **Honestidad de estado:** ningún componente marca `inGameVerified=true` sin acta humana.

### Restricciones

- Arquitectura fija: QML → viewmodels C++ → motores Qt-free / JSON-RPC → BethesdaWorker .NET/Mutagen.
- Cambios de protocolo solo aditivos (bump de *minor*); `ProtocolContractTests` compara `Protocol.Minor` con `kMinor`.
- Un desarrollador + agentes; Windows como plataforma real; CI `windows.yml` (estado de ejecución **no verificado** aquí).

## 2. Diseño de alto nivel

```
                        ┌──────────────────────── QML ────────────────────────┐
                        │  A: Editor NPC   B: Character Forge   C: Body/Outfit │
                        │            Panel «Prueba en Skyrim» + Evidencias      │
                        └───────────────┬───────────────────────────────────────┘
                                        │ viewmodels C++ (StudioController, CharacterForgeViewModel)
                 ┌──────────────────────┼──────────────────────────────┐
                 ▼                      ▼                              ▼
   ┌──── Núcleo común (Qt-free) ───────────────────────────────────────────────┐
   │ 1 Snapshot MO2/VFS (engines/mo2)   → estado ORIGINAL con hashes           │
   │ 2 ChangeSet tipado (por producto)  → estado EDITADO                       │
   │ 3 Validadores: masters, TRI/HighPoly, FaceGen, 65 535, esqueleto          │
   │ 4 Evidence Store (nuevo)           → ORIGINAL / EDITADO / OBSERVADO       │
   └────────────┬────────────────────────────────┬─────────────────────────────┘
                │ JSON-RPC (protocolo 1.x)       │ subprocesos supervisados
                ▼                                ▼
   BethesdaWorker .NET/Mutagen          Blender+PyNifly · BodySlide oficial
   Safe Patch → ModPackager → kit M2    (entrada/salida por archivos + hash)
                │
                ▼
   Perfil MO2 de prueba aislado ──► Skyrim ──► acta humana / capturas
                                         └──► (Fase 4) volcado SKSE solo lectura
```

**Decisión central:** los tres productos comparten un único camino de escritura:
`ChangeSet → Plan → Safe Patch → Paquete → Kit de prueba → Evidencia`. Ningún producto, IA ni extensión escribe registros por fuera.

## 3. Núcleo común (detalle)

### 3.1 ChangeSet tipado

```jsonc
{
  "schema": "sns.changeset/1",
  "target": { "formKey": "002B6C:Dawnguard.esm", "kind": "NPC_" },   // siempre FormKey
  "fieldsRequested": ["DefaultOutfit"],                              // máscara Safe Patch
  "inputs": [{ "path": "…", "sha256": "…" }],                         // hash de inputs
  "ops": [{ "op": "setOutfit", "outfit": "…:Studio.esp" }],
  "origin": "user" | "forge" | "bodystudio" | "ai-proposal"            // IA = propuesta, nunca autorización
}
```

[Supuesto] El FormKey de Serana del ejemplo debe resolverse con el escaneo real, no copiarse de aquí.

### 3.2 Evidence Store (el «Digital Twin» v1)

Una tabla SQLite (reutilizando el patrón de `CatalogStore` y sus pruebas de concurrencia entre procesos):

| Campo | Contenido |
| --- | --- |
| `evidence_id` | UUID |
| `form_key` / `asset_id` | objetivo |
| `original_hash` | snapshot MO2 del ganador (plugin + NIF + FaceGen efectivo) |
| `edited_hash` | manifiesto del paquete exportado |
| `build_hash`, `profile_hash`, `plugin_sha256` | lo que el juego cargó (M2 ya comprueba `plugin_sha256`) |
| `observed` | acta: `PASS`/`FAIL`/`DEFER`, capturas, notas, observador |
| `state` | estados exactos de AGENTS.md |

El comparador de 3 estados empieza siendo **diff de hashes + acta**. La comparación visual automática (Visual QA 2.0) y el volcado SKSE
se añaden como nuevas fuentes de `observed`, sin cambiar el esquema.

### 3.3 Kit de prueba en juego (M2) como primera fuente de `observed`

Flujo: exportación → FormID en tiempo de ejecución según orden de carga → `.bat` de consola + acta → mod MO2 instalable → humano
juega → acta vuelve al Evidence Store. **[Supuesto]** El motor y el empaquetador de M2 existen solo en el checkout local del
propietario (sin commitear); este checkout no los contiene.

### 3.4 Contratos RPC (aditivos)

| Método | Fase | Nota |
| --- | --- | --- |
| `ingame.prepareKit` | 1 | empaqueta kit M2 (minor +1) |
| `evidence.record` / `evidence.query` | 1 | puede vivir en C++ sin worker; solo pasa al worker si necesita Mutagen |
| `geometry.partition` | 2 | en `engines/nif`, Qt-free, no en el worker |
| `outfit.compareBodySlide` | 3 | compara salida oficial vs estudio |

Errores: reutilizar `ErrorCodes` del worker; clasificar subprocesos (Blender/BodySlide) en `timeout`, `exit≠0`, `salida inválida`,
`hash inesperado`, con reintento solo en `timeout` y siempre idempotente (salida en carpeta nueva por intento).

## 4. Productos

### A — Editor de NPC existentes (Serana)

- **MVP (no toca FaceGen):** cambiar `DefaultOutfit` con la máscara de campos existente. El pelo nuevo como **peluca/ARMO**
  (slot de pelo) dentro del outfit, no como HDPT. FaceGen original intacto por construcción.
- **Fase siguiente (sí toca FaceGen):** cambio de HDPT de pelo + reconstrucción del NIF FaceGen sustituyendo solo la forma del pelo,
  con guardias TRI/High Poly y procedencia FaceGen reconsultada. Requiere expediente y aprobación humana por NPC.
- **Diagnóstico cuerpo/armadura:** `outfit_fit_doctor` + `body_analysis` sobre el ganador MO2 del cuerpo y de cada ARMA.
- **Riesgo:** Serana tiene scripts y outfits que otros mods sobrescriben **[Inferencia]**; el kit debe registrar el plugin ganador.

### B — Character Forge avanzado

Orden de incrementos (dependencias reales, no deseos):

1. STAT verificado en juego (solo falta acta humana: es el `VERIFICADO_EN_SKYRIM` más barato de todo el proyecto).
2. **Particionado de geometría** antes de cualquier malla con esqueleto: dividir > 65 535 preservando UV, pesos y normales; prueba
   con un modelo sintético de 70 000 vértices que hoy se truncaría en silencio.
3. Esqueleto de referencia (`skeleton.nif` vanilla/XPMSSE), pesos, particiones, ARMO/ARMA (incremento 3, `NO_INICIADO`).
4. NPC completo (`BLOQUEADO` por FaceGen nativo).

La corrección del cierre de Sarah demuestra robustez de **carga**, no de **animación** (de acuerdo con el texto de origen).

### C — Body & Outfit Studio

- Detección CBBE / 3BA / UBE: partir de `body_analysis` + `bcng_rules`; el resultado debe citar el archivo ganador del VFS.
- **Primera meta:** construir una prenda con BodySlide oficial (subproceso, perfil aislado) y con `bodyslide_build`; comparar
  vértices, pesos y morphs con tolerancia numérica explícita.
- **CBBE→UBE:** no se diseña un «Body Transfer Engine» propio al principio. **[Probable]** Los proyectos de conversión de referencia
  de Outfit Studio son el método establecido; el estudio debe orquestarlos y **verificar** (clipping, costuras, pesos), no
  reimplementarlos. Un motor propio solo si la verificación demuestra que los de referencia no cubren el caso.

## 5. Las 12 tecnologías, recolocadas

| # | Tecnología | Decisión | Por qué |
| --- | --- | --- | --- |
| 12 | One-Click Verified Pipeline | **Núcleo, fase 1** | Es §3; sin él nada llega a verificado |
| 10 | Visual QA 2.0 | Fase 2, fuente extra de `observed` | Necesita capturas reproducibles primero |
| 11 | Asset Dependency Graph | Fase 1–2 | Extiende el snapshot MO2 existente |
| 7 | Intelligent Mod Repair | Fase 2, solo reversible | Cada reparación = ChangeSet + evidencia |
| 2 | Universal Character Converter | = Producto B | No es un componente aparte |
| 4 | Body Transfer Engine | = Producto C, orquestando herramientas oficiales | Ver §4.C |
| 6 | Renderer fiel | Fase 2 | Su valor se mide contra `observed`; sin evidencia en juego no hay referencia |
| 1 | Live Link | Fase 4, **solo lectura** | Plugin SKSE = binario nuevo por runtime (SE/AE/VR); escritura en caliente fuera de alcance |
| 5 | Animation Forge | Fase 4 | Depende de B incremento 3 |
| 8 | Procedural NPC Forge | Fase 5 | Depende de FaceGen desbloqueado |
| 3 | Neural Rigging | Fase 5 | Sin dataset ni métrica de aceptación definida |
| 9 | AI Copilot | Fase 5 | Solo genera ChangeSets `origin=ai-proposal`; nunca autoriza |

### Live Link: transporte

| Opción | Pro | Contra | Decisión |
| --- | --- | --- | --- |
| Archivo JSON volcado por comando de consola SKSE | auditable, sin socket, sin acoplar procesos | no es «en vivo» | **v1** |
| Named pipe | baja latencia, local | ciclo de vida acoplado al juego, más fallos | v2 si v1 demuestra valor |
| Socket localhost | herramientas genéricas | superficie de red, firewall | descartado |

## 6. Fases realineadas con los gates de AGENTS.md

| Fase | Entrada | Salida (gate) |
| --- | --- | --- |
| 0 — F0 | — | RC 1.1.0, ZIP reproducible, suites verdes |
| 1 — Ciclo verificable | F0 | M2 + Evidence Store; **primer `VERIFICADO_EN_SKYRIM`**: STAT de B y outfit de Serana (A MVP) |
| 2 — Geometría | Fase 1 | particionado > 65 535 con prueba; Visual QA v1; Dependency Graph |
| 3 — Cuerpos | Fase 2 | prenda BodySlide oficial ≡ estudio; una conversión 3BA→UBE verificada en perfil aislado |
| 4 — Esqueleto/animación/runtime | Fase 3 | ARMO/ARMA con pesos en juego; HKX ida y vuelta; volcado SKSE solo lectura |
| 5 — Copiloto | Fase 4 | ChangeSets propuestos por IA que pasan por el mismo pipeline |

## 7. Qué revisaría al crecer

- Evidence Store en SQLite local → exportable como paquete de evidencias si hay varios testers.
- Volcado SKSE por archivo → pipe, solo con medición de que la latencia limita el trabajo.
- Orquestar Outfit Studio → motor propio, solo con casos donde la referencia falle y estén documentados.

## 8. Datos no verificados

- Estado real de las ejecuciones de CI de `windows.yml` (no consultado).
- ~~Versión de Qt~~ → **[Confirmado]** la app requiere Qt 6.11 (`CMakeLists.txt:50`), así que MorphTarget de Qt Quick 3D está
  disponible en la versión objetivo; falta medir latencia.
- **[Confirmado]** El build solo configura en Windows x64 (`CMakeLists.txt:9`); en el contenedor Linux de esta sesión no hay Qt,
  .NET ni Blender. Todo cambio C++/.NET hecho aquí sería `IMPLEMENTADO_NO_COMPILADO`.
- Compatibilidad PyNifly 30.x y motor M2: solo en el checkout local del propietario.
- FormKey de Serana y su plugin ganador en el perfil real.

## 9. Registro de iteraciones del loop de diseño

Cada ejecución programada amplía **una** sección con evidencia nueva del repositorio; si no hay nada nuevo, no se modifica el archivo.

| Iteración | Fecha | Cambio |
| --- | --- | --- |
| 1 | 2026-11-05 | Diseño inicial |
| 2 (siguiente) | — | Producto A: contrato exacto de `NpcSafePatch` para outfit + peluca, leyendo `NpcSafePatch.cs` y `OutfitPatchBuilder.cs` |
| 3 | — | Producto B: diseño de `geometry.partition` en `engines/nif` con prueba de 70 000 vértices |
| 4 | — | Producto C: qué detecta hoy `body_analysis` y qué falta para CBBE/3BA/UBE |

Tras la iteración 4 el loop pasa a **modo mejora**: un incremento por ejecución, solo si es verificable en Linux (Python/pytest,
lógica pura con prueba, documentación con fuentes); lo demás va a la sección 10.

## 10. Backlog para Windows (requiere compilar C++/.NET o probar en Skyrim)

| Propuesta | Producto/fase | Por qué no se hizo en el loop |
| --- | --- | --- |
| Arreglar `ProtocolContractTests.cs:38` (7→8) y renombrar el test a `1_8` | M2 local | El cambio a 1.8 solo existe en el checkout local del propietario |
| `geometry.partition` en `engines/nif` | B, fase 2 | C++; requiere build Windows |
