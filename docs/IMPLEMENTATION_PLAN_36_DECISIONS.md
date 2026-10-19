# SKYRIM NPC STUDIO ULTIMATE — PROMPT MAESTRO DE IMPLEMENTACIÓN REAL, 36 DECISIONES

> **Documento operacional para Claude Code / Claude Desktop con acceso a repositorio local, Git y herramientas de desarrollo.**
>
> **Edición:** 2.0 detallada · **Corte de verificación:** 17/10/2026 · **Horizonte:** 2026–2029 · **Estado:** especificación de implementación, NO afirmación de que el producto final ya exista.
>
> **Repositorio:** https://github.com/AnotatoDrako4/SkyrimNpcStudio (privado; usar conexión autorizada o clon local).
>
> **Instrucción de uso:** pega todo este archivo como instrucciones de proyecto para Claude Code o dile a Claude que lo lea completo desde su ubicación local. Es un contrato técnico vivo. **Comienza exclusivamente con F0**, no con las 36 decisiones a la vez. Las secciones D1–D36 son órdenes condicionadas por las dependencias y las aprobaciones indicadas.

---

# PARTE A — MISIÓN, REGLAS Y BASE TÉCNICA

## A1. Tu mandato operativo

Actúa como equipo de ingeniería senior con roles de **arquitecto de software, ingeniero C++20/Qt Quick 3D, especialista .NET/Mutagen, ingeniero de datos y SQLite, especialista Skyrim SE 1.6.1170/MO2/FaceGen/NIF/TRI, QA/Release, diseñador técnico de UX, experto en Blender/Python e investigador de IA segura**. Cuando dispongas de múltiples agentes, reparte investigación o pruebas independientes, **nunca escrituras concurrentes sobre la misma rama, ruta o caché**.

**Tu tarea no es describir cómo se podría hacer: es producir incrementos reales que funcionen y se comprueben.** En cada ciclo: inspeccionar código → trazar función y dependencias → escribir pruebas que demuestren el fallo o nueva capacidad → implementar el cambio mínimo → compilar → ejecutar pruebas → corregir → registrar evidencia → commit aislado → reportar. Si hay bloqueo real, entregar un diagnóstico reproducible; jamás falsificar resultado.

Regla de lenguaje: **[VERIFICADO EN REPOSITORIO]**, **[DEDUCIDO/DISEÑO]**, **[EXTERNO CANDIDATO]**, **[NO IMPLEMENTADO]**, **[PENDIENTE DE SKYRIM]**. No confundir una función presente en un archivo QML con una integración funcional de extremo a extremo. Cada referencia a clase, método o parámetro que uses debe verificarse en la rama activa antes de modificarlo.

### Prioridades innegociables

1. **F0:** consolidar 1.1.0 de manera segura antes de fusionar en `main`.
2. **D1:** los **53 NPC ambiguos** se revisan uno por uno; ninguna aprobación automática o implícita.
3. **D3/D22:** validar con MO2 aislado y aceptación humana dentro de Skyrim, no solo con tests de consola.
4. No modificar destructivamente los archivos originales, `Skyrim/Data`, partidas personales ni el perfil MO2 habitual.
5. Aplicar Safe Patch, guardias TRI/High Poly, análisis de procedencia, readback y exportación atómica a TODA ruta de escritura, incluida IA y extensiones.
6. **D27D**: autonomía alta solo dentro de entornos autorizados y aislados; NO significa privilegios irrestrictos. Agentes pueden probar/proponer y escribir en staging, NO fusionar `main`, publicar, tocar perfiles habituales ni aprobar NPC ambiguos.
7. **D31A:** soporte oficial **Windows x64, Skyrim SE/AE ejecutable 1.6.1170, MO2**. Cualquier soporte adicional requiere nueva decisión.
8. No reconstruir el proyecto; preservar sus activos y contratos existentes.

### Prohibiciones explícitas

No pseudocódigo presentado como solución; no pantallas falsas o botones que no ejecutan nada; no `TODO` o `return success` en una funcionalidad declarada terminada; no mocks en producción simulando Skyrim; no `catch {}` que oculte fallos; no deshabilitar tests, firmas, verificaciones, NuGetAudit o TreatWarningsAsErrors para fabricar verde; no alterar locks de dependencias sin reproducibilidad; no autoeliminar conflictos; no saltarse validación por lotes; no sobrescribir assets de terceros; no afirmar «verificado en juego» sin evidencias reales; no usar IA para sustituir una validación determinista; no instalar herramientas sin revisar fuente/licencia/pinning y pedir permiso si requiere mutaciones ajenas al entorno de proyecto.

## A2. Lo que EXISTE: arquitectura que debes reutilizar

Los nombres siguientes se comprobaron en el repositorio en la rama `feature/conflict-intelligence-hardening` el 17/10/2026. **Reconfírmalos localmente; no supongas que la rama no cambió.**

| Capa / herramienta ya utilizada | Rutas reales / componentes de referencia | Qué responsabilidad conservar |
|---|---|---|
| App **C++20 + Qt 6.11.2 + QML/Qt Quick 3D** | `apps/desktop/qml/Main.qml`, `CharacterViewport.qml`, `Theme.qml`, `components/`, `charactercreator/`, `characterforge/` | interfaz, inspector, escenas y vista previa; **no escribir plugins directamente** |
| Controladores/viewmodels | `apps/desktop/viewmodels/studio_controller.{cpp,hpp}`, `appearance_editor.{cpp,hpp}`, `worker_client.{cpp,hpp}`, `scene_viewmodel`, `forge_scene_viewmodel`, `character_forge_viewmodel` | orquestación async, estados, cambios y validación previa |
| **MO2/VFS** | `engines/mo2/src/{instance,profile,vfs,load_order,game,ini}.cpp` | resolver archivos efectivos y plugin ganador en **solo lectura** |
| **Apariencia** | `engines/appearance/src/{control_map,compatibility_guardian,appearance_draft,easy_mod}.cpp` | trazabilidad facial, coherencia y propuestas de resolución |
| **FaceGen/NIF/TRI** | `engines/face/src/{facegen,face_recipe,face_morphs,face_tint,tri_file,head_parts}.cpp`; `engines/nif/` | compatibilidad de geometría, Hair Color Safe Mode, bloqueos High Poly |
| **Proyectos/comandos** | `engines/projects/src/{project,command_stack}.cpp`, `engines/creation/src/{forge_project,forge_workspace,native_npc_draft}.cpp` | `.snps`, `.sncf`, cambios reversibles |
| **GLB/glTF/Blender** | `engines/gltf/src/gltf_inspect.cpp`, `apps/desktop/viewmodels/forge_blender_bridge.cpp`, `tools/blender/forge_bridge.py` | verificar recursos importados y convertir a glTF para previsualización |
| **Texturas/render** | `engines/textures/src/dds.cpp`, `engines/rendering/src/{scene,smp_xml}.cpp`; QML visor 3D | lectura DDS y conversión a representación de escena |
| **Bethesda Worker .NET 10 / Mutagen** | `services/BethesdaWorker/{NpcCatalog,PatchBuilder,Contracts,Rpc,PluginReader,Validation,RecordEditor}/` | registros Bethesda, índice SQLite, exportación ESP/ESL, readback |
| Exportación | `services/BethesdaWorker/PatchBuilder/{NpcSafePatch,FacePatchBuilder,OutfitPatchBuilder,NativeNpcBuilder,ModPackager}.cs` | escritor único, guardias semánticos, staging, manifiesto, ZIP |
| Caché | `services/BethesdaWorker/NpcCatalog/CatalogStore.cs` | SQLite y transacciones; controlar concurrencia |
| Pruebas | `tests/{unit,integration,export,app,fixtures}/` | Catch2/CTest, xUnit/dotnet test, harness reales y fixtures sintéticos |
| Scripts reales | `scripts/{dev-env,build,smoke-test,local-review,release,verify-release,package}.ps1` | build, smoke, aceptación local y paquete reproducible |

**Fronteras que no debes romper:** QML → viewmodels C++ → motores Qt-free y/o JSON-RPC → worker .NET Mutagen. El **worker NO resuelve MO2**: el C++ prepara un snapshot efectivo de recursos y plugins; el worker opera sobre contratos tipados. Toda función adicional debe respetarlo o justificar un ADR aprobado.

### Estado de la release al último examen (revalidar al empezar)

- `main`: commit `78ea95c54daef8466c47500a53fbac0d2da6be19`; **NO** integra 1.1.0.
- PR #21 `release/v1.1` (abierto): integración #8→#20, pruebas locales de Windows declaradas y ZIP reproducible, todavía **NO** aceptado dentro de Skyrim.
- PR #22 `feature/conflict-intelligence-hardening` sobre #21 (abierto): comprobación de FaceGen efectivo, cierre de fallos «fail-open», readback `RemainingFaceDifferences`, 31 resoluciones claras y **53 ambiguas** de 84 propuestas, mejoras de escaneo.
- PR #23 `fix/catalog-cache-race` (abierto), basado en `fix/nu1902-messagepack` y NO directamente en #21: `INSERT ... ON CONFLICT(signature) DO NOTHING RETURNING id`, reproducción y prueba de colisión. **No certifica** seguridad de `ActivateLoadOrder` concurrente.
- PR #20: pinning transitivo MessagePack 2.5.305 por NU1902; ya forma parte de #21.
- GitHub Actions recientes: `startup_failure` antes de jobs. No es evidencia de compilación fallida ni de CI aprobada.
- Precedentes locales declarados: CTest 162/162, worker 37/37 y export 77/77 en ramas concretas; NO se heredan a un commit nuevo hasta repetirlos.

### No sobreprometer lo siguiente

- `Character Forge` importa y visualiza GLB/glTF; **no demuestra** conversión funcional arbitraria GLB→NIF/TRI/FaceGen para NPC nuevos.
- Qt Quick 3D puede representar esqueletos/morphs; **no significa** soporte directo de archivos HKX de Skyrim.
- `AIGenerationPanel.qml` o `SkeletonEditor.qml` pueden existir como vista; **no significa** generación IA operativa ni editor HKX comprobado.
- «Skyrim Accurate» es un objetivo y requiere comparación con capturas reales del juego. «Next-Gen Preview» es un modo de presentación y no altera mágicamente el motor gráfico de Skyrim.
- SKSE runtime bridge y agentes autónomos son investigación futura, no dependencias obligatorias.

## A3. Herramientas concretas y política de uso

**Tipo E (existente/confirmado en el repositorio):** antes de instalar nada, aprovechar lo siguiente. **Tipo X (externa documentada):** candidata verificable; **no asumir instalada**, validar necesidad y licencia. **Tipo R (investigación):** solo prototipo aislado y criterio go/no-go.

| Herramienta / tecnología | Tipo | Qué usar y para qué | Condición / comprobación |
|---|:---:|---|---|
| Git + `git worktree`, commits, PRs | E | branches aisladas, `diff`, `merge-base`, `cherry-pick` auditado, reversión | nunca escribir en `main` sin aprobación; no borrar cambios locales |
| MSVC 14.44+, CMake ≥3.25, Ninja, `ctest` | E | compilar C++20 y probar módulos | confirmar versiones efectivas en la máquina |
| Qt 6.11.2 / Qt Quick 3D / Qt Quick Controls | E | viewport, QML, SplitView, picking, Skeleton/MorphTarget si encaja | backend y alcance según versión fijada; **no** asumir soporte HKX |
| .NET SDK 10 + xUnit + Mutagen Bethesda | E | lectura/escritura tipada TES4, FormKeys, readback, tests | paquetes fijados, licencia y build `--locked-mode` |
| JSON-RPC / named pipes / StreamJsonRpc | E | contratos tipados y progreso/cancelación entre procesos | handshake versionado, límites, contextos cancelables |
| SQLite WAL + transacciones | E | catálogo, firmas, cachés y nuevas colas si justificado | pruebas multi-proceso, locks y escrituras serializadas donde sea necesario |
| nifly + NIF inspector actual | E | carga/inspección de NIF, triangulación y restricciones | conservar vértices/UV/skin/shader; no regenerar sin prueba |
| DirectXTex / lector DDS propio | E | texturas y validación DDS | no reescribir materiales si no hay exportador validado |
| Blender + Python `bpy` / bridge versionado | E (bridge); X (instalación Blender) | operaciones de geometría en proceso externo; exportaciones controladas | validar versión, CLI, scripts y archivos, timeout/cancelación |
| PowerShell + scripts del repositorio | E | build/review/release/verify-release | inspeccionar parámetros en cada rama, no inventar flags |
| Catch2 / xUnit / fixtures sintéticos | E | tests unitarios, integración, exportación y regresión | tests sobre código real, sin recursos de terceros en repo |
| xEdit / SSEEdit | X | inspección secundaria y diff manual de plugins TES5 | no sustituye readback Mutagen; no guardar sobre archivos originales. https://github.com/TES5Edit/TES5Edit |
| NifSkope / herramientas NIF revisadas | X | inspección visual/semántica de NIF cuando se necesite | uso auxiliar, nunca certificación exclusiva; localizar fuente/licencia antes |
| CommonLibSSE-NG + SKSE64 | X / R | solo D25: prototipo opcional DLL read-only de datos de runtime | validar runtime 1.6.1170 exacto, ABI, licencia, permiso y aislamiento. https://github.com/CharmedBaryon/CommonLibSSE-NG |
| Open Animation Replacer (OAR) | X / R | estudiar compatibilidad de formatos/condiciones de animación, no prometer conversión HKX | analizar contratos de condiciones y permisos/licencia. https://github.com/ersh1/OpenAnimationReplacer |
| Papyrus/Creation Kit | X / R | validación de misiones, scripts, diálogos y escenas | herramienta oficial necesaria en casos complejos; no afirmar reemplazo total |
| Python 3 + `pytest` si ya requerido | X | utilidades de fixtures, benchmark, scripts de Blender | solo si evita duplicaciones C++/.NET; pinning obligatorio |
| API de IA local/cloud vía adaptadores | R | sugerir planes, razonar sobre errores, generar variantes | no acceso directo sin políticas; claves nunca registradas |
| MCP como protocolo de **herramientas** | R | conectores declarativos/read-only inicialmente | servidor opcional aislado, whitelist, permisos y trazabilidad |
| GitHub Actions Windows | E (workflow) | CI de build y tests cuando runners funcionen | fallos `startup_failure` fuera del job no se ocultarán |

Fuentes técnicas para verificar APIs: Qt SplitView `https://doc.qt.io/qt-6/qml-qtquick-controls-splitview.html`; Qt Quick 3D `https://doc.qt.io/qt-6/qtquick3d-index.html`; Blender API `https://docs.blender.org/api/current/`; Mutagen `https://github.com/Mutagen-Modding/Mutagen`; MO2 `https://github.com/ModOrganizer2/modorganizer`; nifly `https://github.com/ousnius/nifly`; xEdit `https://github.com/TES5Edit/TES5Edit`. **Consultar versión/licencia antes de adoptar**, no copiar fragmentos ni assets externos sin respetar sus términos.

## A4. Estados y regla Definition of Done

Estado obligatorio de cada resultado: `NO_INICIADO`, `EN_DESARROLLO`, `IMPLEMENTADO_NO_COMPILADO`, `COMPILADO`, `TESTS_UNITARIOS_OK`, `INTEGRACION_WINDOWS_OK`, `PAQUETE_MO2_VERIFICADO`, `PENDIENTE_SKYRIM`, `VERIFICADO_EN_SKYRIM`, `BLOQUEADO` o `REGRESION`. Una funcionalidad puede tener varios niveles; nunca elevarlos silenciosamente.

**DoD general por feature:** (i) código integrado en ruta funcional real, (ii) UI/interacción real si aplica, (iii) contratos/errores/versiones cubiertos, (iv) tests unitarios negativos y positivos, (v) integración en máquina disponible, (vi) build con 0 errores, (vii) documentación técnica de cambios y límites, (viii) benchmark si toca UI/IO/render/caché, (ix) registro de seguridad/provenance, (x) si modifica un mod: readback, ZIP/manifiesto/hash, MO2 aislado y **SKYRIM humano pendiente hasta captura/acta**.

### Plantilla de reporte que DEBES usar por iteración

```
IMPLEMENTACIÓN <ID> — <título> — <estado real>
Repositorio / rama / SHA antes y después:
Herramientas instaladas realmente / versiones:
Archivos y métodos reales modificados (rutas):
Efecto funcional observable (flujo clic/CLI/worker):
Tests ejecutados, comando, exit code, PASS/FAIL/SKIP, logs:
Comparación antes/después y regresiones:
Seguridad: qué entradas protege y qué queda bloqueado:
Artefactos reales (rutas, sha256 y manifiestos si existen):
Pendiente en Skyrim (sí/no, evidencia si no):
Fallas, incertidumbres y decisión humana pendiente:
Siguiente incremento más pequeño y viable:
```

## A5. Política de dependencias, permisos y agentes

1. **Nuevas dependencias:** abrir README/licencia, commits/tag, mantenimiento, CVEs y transitive deps; comparar con stack actual; congelar versión/commit, locks y hash cuando aplique; probar rollback; documentar decisión ADR. Prohibido `curl|bash` / scripts arbitrarios sin revisión.
2. **Proveedores IA:** adaptadores por proveedor, configurable a ninguno, claves en secret store del sistema si se implementan, nunca en logs/repo, opt-in para transmisión de imágenes/NPC/modlist, presupuesto y cancelación.
3. **Plugins externos:** procesos fuera de proceso por defecto, whitelist de comandos/permisos/rutas, timeouts, aislamiento a nivel de OS cuando sea posible. Un proceso separado **no equivale automáticamente a sandbox**: probar restricciones efectivas de FS/red y de usuario.
4. **Autonomía D27D:** clasificar acciones `READ_ONLY`, `STAGING_WRITE`, `PROJECT_WRITE`, `MO2_TEST_PROFILE`, `MO2_REAL_PROFILE`, `GAME_DATA`, `GIT_MAIN`, `PUBLISH`. Los agentes solo ejecutan automáticamente las dos primeras en un sandbox autorizado; las demás requieren grant explícito y limitado por acción, con revocación. `GIT_MAIN`, `GAME_DATA` y `PUBLISH` requieren siempre permiso humano directo.
5. **Snapshots y provenance:** cada cambio sensible debe referenciar SHA/huella de inputs, FormKey, mod efectivo, masters, configuración y versión del motor. Una modificación de inputs después de aprobar invalida el permiso asociado.
6. **Paralelismo:** limitar escritores de perfil/caché, colas per-resource, staging único por job, commits atómicos. Tests de carrera antes de escalar.
7. **Scripts y herramientas:** ejecutar desde carpeta del proyecto y con cuenta no privilegiada cuando sea posible; no asumir que tienes acceso a archivos de Skyrim o MO2 del usuario.

---

# PARTE B — MAPA DE DEPENDENCIAS Y DESPLIEGUE

## B1. Orden técnico, NO numérico

| Fase / ventana orientativa | Prioridades de decisiones | Depende de | Entregable verificable |
|---|---|---|---|
| **F0 — 2026 T4** | D1, D2, D3, D22, D35, D36 | código actual + PR #21/#22/#23 | RC 1.1.0, suites, ZIP reproducible, kits MO2 y revisión humana |
| **F1 — 2027 T1** | D4, D14, D24B, D18 | F0; tests de concurrencia | Multi-NPC export atómico, provenance de conflictos y validación por NPC |
| **F2 — 2027 T2** | D11, D13, D16, D26, D32, D33 | proyecto estable, API y formatos versionados | Workbench flexible, proyectos/snapshots/layers, bibliotecas y accesibilidad |
| **F3 — 2027 T3/T4** | D5, D7, D8, D17, D28 | F2, guards de topología, benchmark/render | edición facial 3D comprobable, visores Accurate/Next Gen y comparación |
| **F4 — 2028 T1/T2** | D9, D10, D19, D29 | F3, contratos de geometría y Blender | previews de poses/morphs, bridge Blender y generador seguro parametrizado |
| **F5 — 2028 T2/T4** | D6, D12, D15, D23, D27, D30 | ejecución determinista, permisos, QA | plugins/MCP, Copilot, workflows y agentes en sandbox |
| **F6 — 2029 T1/T2** | D20, D21, D25C | worker extendido, pruebas CK y runtime opt-in | NPC comportamiento/quests básicos, puente SKSE read-only experimental |
| **F7 — 2029 T3/T4** | D34 y maduración transversal D22/D35 | F0–F6 según capacidades publicables | release center, paquetes verificables y garantías de regresión |

Estas fechas son objetivos de planificación, nunca garantías. Si una fase falla sus gates, la siguiente no empieza en áreas dependientes. QA, provenance y seguridad son **transversales desde F0**, no esperar hasta 2029.

## B2. Trazabilidad obligatoria de cada decisión

Cada D1–D36 debe terminar con: `owner`, `fase`, `dependencias`, `estado comprobado`, `artefacto demostrable`, `tests`, `limitaciones`, `risks`, `enlace commit/PR`, `gate humano si aplica`. Crear una tabla de cobertura que enlace cada decisión con sus pruebas concretas. No declarar 36/36 «implementadas» por haber escrito 36 páginas de documentación.

## B3. Propuesta de carpetas futuras (NO SON RUTAS EXISTENTES)

Las rutas siguientes son **propuestas**, únicamente crearlas cuando haya código concreto que lo exija y tras inspeccionar naming/convenios actuales:

```
engines/workflows/         # scheduler DAG, contratos y checkpoint
engines/provenance/        # digests, grafo acotado de recursos y licencias
engines/security/          # permisos, policy, audit
engines/visualqa/          # registro de cámara, captura y métricas
engines/animation/         # preview pose/skeleton/expresiones; NO HKX directo
engines/characters/        # capas no destructivas y variaciones
services/BethesdaWorker/QuestBuilder/       # solo al soportar registros reales
apps/desktop/qml/workbench/               # paneles QML y layouts
apps/desktop/qml/workflows/               # diseñador visual por nodos
adapters/ai/               # adaptadores aislados con consentimiento
adapters/skse/             # investigación opcional SOLO read-only
schemas/                  # DTO versionados, manifiestos y snapshots
```

No agregues módulos vacíos ni 20 carpetas en el primer sprint. Mantener coherente la separación UI/engines/worker.

---

# PARTE C — ESPECIFICACIONES TÉCNICAS DE CADA DECISIÓN

**El bloque D1–D36 es ejecutable por separado cuando su fase y gate están autorizados.** En todos los casos, las referencias a «herramientas exactas» son las opciones preferidas verificadas o candidatas etiquetadas. Claude debe comprobar la API real y la licencia de cada una antes de instalar o escribir código nuevo.

## D1 [A] — Revisión individual de los 53 NPC faciales ambiguos

**Resultado funcional:** una bandeja de 53 expedientes donde el usuario ve **qué rostro se propone usar, de qué plugin procede, qué campos se conservan, qué cambios se descartarían y por qué**, y aprueba/rechaza/difiere **cada NPC individualmente**. No es un botón «Resolver los 53».

**Reutilización real:** `engines/appearance/src/control_map.cpp`, `AppearanceControlPanel.qml`, `AppearanceEditor`, `FacePatchBuilder`, cadena de overrides Mutagen y resolución del VFS MO2. **Herramientas:** C++20; QML ListModel o modelo C++ tipado para tabla; worker JSON-RPC; `.snps`/almacenamiento persistente versionado; SHA-256 y manifests.

**Construcción paso a paso:**
1. Inspeccionar cómo `FaceResolution` distingue casos claros/ambiguos (`discards_own_edits`) y cómo se identifican los NPC: usar `FormKey` estable, **no** índice visual ni nombre.
2. Crear un `ReviewCase` tipado versionado: `npcFormKey`, `sourcePlugin`, `winningPlugin`, `sourceFaceGenProvider`, `inputSnapshotDigest`, `proposedFieldDiff`, `discardedEdits`, `evidence`, `decision`, `reviewedAt`, `approvalDigest`, `notes`.
3. Generar los 53 expedientes a partir del **escaneo real actual**; si ya no son 53, documentar el cambio de universo/instancia y NO rellenar casos ficticios hasta 53.
4. Añadir panel de inspección NPC por NPC: campo antes/después, contexto de cada override, proveedor de recursos loose/BSA y riesgos.
5. Persistir `APPROVE`, `REJECT`, `DEFER` por propuesta **y huella del perfil**; si cambian source/FaceGen/load order/campos, invalidar aprobación anterior. Los casos `DEFER` nunca se exportan.
6. Añadir enlace por expediente a capturas humanas y a los paquetes de prueba en staging; `readyForGame=false` mientras no exista evidencia de Skyrim.
7. Integrar la aprobación en `exportFaceResolution` y en futuros lotes Multi-NPC; no aceptar valor `approved=true` sin comprobar que el hash coincide con la propuesta actual.

**Pruebas obligatorias:** 53 revisiones independientes en fixture; aprobación de A no autoriza B; `DEFER`/`REJECT` bloquean; alteración de orden MO2 invalida aprobación; nombres duplicados no confunden `FormKey`; persistencia/reapertura; lote con 1 ambiguo sin aprobar rechazado completo.

**Entregable/gate:** expedientes y UI funcionales, decisiones auditables. **La persona usuaria debe aprobar cada caso**; ningún agente puede inventar firmas ni marcar los 53 como completados.

## D2 [A] — Consolidación, calidad y aceptación de la versión 1.1.0

**Resultado funcional:** un **Release Candidate 1.1.0** reproducible y seguro, sin merge prematuro. **Herramientas:** Git worktree/cherry-pick/diff; MSVC, CMake, Ninja, CTest/Catch2, .NET 10/xUnit, Mutagen, scripts `build.ps1`, `local-review.ps1`, `release.ps1`, `verify-release.ps1`; GitHub PR API.

**Construcción:**
1. Revalidar refs de PR #21, #22, #23 y #20; guardar SHA inicial, `git status` y bases reales, y crear **worktree aislado** sin tocar modificaciones del usuario.
2. Integrar el endurecimiento #22 sobre la rama de release; transportar solamente el delta de #23, porque ya hereda la corrección de dependencias #20 por otra rama. Examinar conflictos semánticos, no resolver todos con `ours`/`theirs`.
3. Confirmar guards `FaceSourceProviderUnknown`, proveedor MO2 **reconsultado** al exportar, `RemainingFaceDifferences` sobre registros releídos, verificación de masters y only-fields.
4. Añadir tests de contención SQLite `ActivateLoadOrder`, mismo perfil con dos procesos, cierre abrupto/cancelación y rollback; los fixes existentes no demuestran cobertura total.
5. Ejecutar compilación limpia, CTest, xUnit, harness Windows/MO2 donde estén disponibles; medir escaneo/control-map y no degradar significativamente UI sin explicación.
6. Inspeccionar flags de release; generar 2 ZIP desde la misma fuente, verificar inventario, reabrir y comparar hashes. No reutilizar un ZIP construido con otro commit.
7. Diagnosticar `startup_failure` de Actions; distinguir configuración/facturación de GitHub de errores reales de job. No desactivar CI para aparentar verde.
8. Preparar la matriz de aceptación real de Skyrim y el PR de consolidación **sin merge**; esperar aprobación humana.

**Pruebas:** build limpio x64, locks NuGet, suites C++/worker/export, Serana/Lydia/Shady Sam/Bolli/Brand-Shei, casos negativos de falla de FaceGen/SQLite/masters, no-write en MO2, empaquetado 2× y lectura de ZIP.

**Gate:** ningún cambio en `main` ni release oficial hasta aprobar pruebas y autorización. Si Actions sigue bloqueado, reportar estado externo, no inferir CI verde.

## D3 [A] — Validación híbrida real MO2 + Skyrim

**Resultado funcional:** laboratorio que prueba los plugins en Windows y entrega una guía/recolección de **pruebas dentro del juego**, sin contaminar guardados personales. **Herramientas:** `local-review.ps1`, `smoke-test.ps1`, `SKYRIM_ACCEPTANCE_TEST.md`, `SKYRIM_VERIFICATION_FACE_RESOLUTION.md`, MO2, hashes PowerShell `Get-FileHash`, capturas de pantalla humanas y manifiestos.

**Construcción:**
1. Implementar `TestRunManifest`: build SHA, versión Skyrim detectada, hash load order/perfil, lista de mods y hashes de ZIP, IDs de escenario, fecha y resultados.
2. Generar paquetes específicos para Serana, Lydia, Shady Sam, Bolli y Brand-Shei; inspeccionar flags `inGameVerified`, `readyForGame`, `loadAfter` y `installAbove`.
3. Proporcionar guía para **crear/copy profile MO2 aislado y partida de prueba**; nunca escribir en `Default` ni crear archivos en `Skyrim/Data` por cuenta propia.
4. Controlar condiciones A/B: mismo NPC, configuración de iluminación documentada, mod activado/desactivado, cámara similar, guardar capturas originales de ambos estados.
5. Recoger observaciones de cara oscura, costura de cuello, ojos, boca, peinado, atuendos, crashes y diálogo; distinguir «sin efecto observable» de «arreglo comprobado».
6. Exigir confirmación humana para marcar `VERIFICADO_EN_SKYRIM`; capturas y notas archivadas con el hash del build.
7. Bloquear aceptación si el perfil de prueba no usa el FaceGen/ESP previstos o si un master falta.

**Pruebas:** doble perfil, independencia de saves, cambios nulos en original (hash antes/después), exportación con y sin FaceGen y registración de evidencia A/B. **Gate:** final humano, no inferido por automatización.

## D4 [A] — Motor Multi-NPC inteligente y exportación unificada

**Resultado funcional:** seleccionar varios NPC, elegir orígenes faciales diferentes, revisar los ambiguos y construir **un único ESP/ESL atómico** que preserve los campos no editados. **Herramientas:** `AppearanceControlPanel.qml`, `appearance_editor.cpp`, `easy_mod.cpp`, DTO `ExportFaceOverhaulRequest.Assignments`, `FacePatchBuilder.Build`, Mutagen, `NpcSafePatch`, `ModPackager`.

**Implementación:**
1. Auditar el contrato real `Assignments[]` y las respuestas `ProjectedChain[]` ya existentes; **no crear otro PatchBuilder**.
2. Crear selección múltiple basada en `FormKey`; agrupar «origen de rostro → plugin ganador» solo para facilitar inspección. Los claros no conceden permisos a ambiguos.
3. Para cada NPC, resolver justo antes del build el proveedor FaceGen efectivo del perfil MO2 (loose/BSA) y comprobar que coincide con la autorización vigente.
4. Construir lote tipado con source, winner, only-fields, whitelist y estado de decisión. Calcular unión de masters sin duplicados, con load order correcto.
5. Pasar las N asignaciones al worker en **una transacción lógica**; si un NPC falla readback, comprobar que no quede plugin válido parcial en output.
6. Validar con Mutagen todas las cadenas proyectadas, `RemainingFaceDifferences`, atributos preservados, rutas, masters, límites de FormID y ESL (distinguir nuevos registros de overrides).
7. Empaquetar con staging + rename atómico y manifest por NPC, diff, dependencias y SHA-256.
8. Integrar cancelación, barra de progreso, errores por NPC y opción de corregir lote **antes de reintentar**.

**Pruebas:** 2 y 27 NPC de fixture, duplicados, divergencias parciales, 1 caso ambiguo sin aprobar, 1 fallo en NPC n-1, conflicto de masters, ESL inválido, cancelación y cero paquete publicado tras fallo. **Gate:** F0 aprobada, y D1 vigente.

## D5 [A] — High Poly Head Advanced Editor

**Resultado funcional:** edición avanzada de cabezas compatibles sin destruir esculpidos, vertex order, morphs, texturas personalizadas ni FaceGen. **Herramientas:** nifly, `engines/face/{facegen,face_morphs,tri_file,head_parts}`, `compatibility_guardian.cpp`, Blender para investigación, Qt Quick 3D para preview y fixtures High Poly autorizados.

**Implementación:**
1. Catalogar cabeza NIF efectiva, TRI de chargen y raza, topología, número y orden de vértices, índices, UV, normales, skin y slots de textura de rostro; registrar digest individual.
2. Construir matriz de `operationCapability`: `HairTintOnly`, `HeadPartSwap`, `MorphCompatible`, `TextureOnly`, `UnsupportedTopology`, `RequiresExternalGeometryReview`.
3. Permitir solo morph cuya base NIF y TRI estén probadamente alineados; nunca interpolar por índice entre geometrías distintas (p. ej. TRI 996 vs cabeza 3832 vértices).
4. Cuando falta correspondencia, bloquear y ofrecer diagnóstico; prototipar retargeting **offline** como investigación y comprobar distancias, correspondencias, UV y diferencias semánticas antes de habilitar exportación.
5. Separar propuesta de viewport de la receta de exportación; no modificar face slot 6 ni textura propia si la operación es solo cabello/vestuario.
6. Ejecutar guard de NIF antes/después y readback, comparar posiciones/normales/UV/materiales de todas las shapes no autorizadas.
7. Probar en MO2 aislado y Skyrim humanamente si un flujo llegara a ser exportable.

**Tests:** Serana con `TriVertexMismatch`, cambios HairTint sobre HPH, variantes headpart que no coinciden, topología modificada, resguardo de texturas personalizadas y errores de morph. **Gate:** el bloqueo correcto también es éxito, no «falló la feature».

## D6 [D] — AI Evolution Platform escalonada

**Resultado:** asistentes IA útiles **sin permitirles saltarse el motor determinista**. **Herramientas:** contratos tipados JSON/DTO y JSON-RPC existentes, cliente IA por adaptador opcional, Git/CI de pruebas, policy D27, motores `appearance`, `face`, Mutagen y audit log. Un LLM determinado NO es dependencia obligatoria.

**Etapa 1 — Engineering Copilot:**
1. Definir comandos de solo lectura (`describeNpc`, `explainOverrides`, `summarizeFaceConflict`, `suggestSafeEdit`) respaldados por datos reales con provenance y source hashes.
2. Estructurar salidas IA como **propuestas**, no cambios: FormKey, campo, operación, argumento, riesgo, evidencia, confianza y validaciones necesarias. Validar esquema, IDs y whitelist; ignorar instrucciones contenidas dentro de nombres/archivos externos.
3. Mostrar diff y exigir aprobación. El worker ejecuta solo operaciones tipadas ya soportadas y verificadas.

**Etapa 2 — Character Generator:** conectar D19/29 y permitir parámetros/rutas legalmente utilizables, con revisión visual y topológica.

**Etapa 3 — Agentes:** planificación multi-paso, herramientas sandbox read-only/staging y aprobación en fronteras; ejecutar cada acción mediante policy D27, timeout y bitácora. Implementar reintentos idempotentes, evitar bucles sin límite y costos descontrolados.

**Tests:** JSON malformado, prompt injection en metadatos, proveedor fuera de línea, expiración de permiso, cambios del load order tras aprobación, envío de rutas fuera del proyecto; ninguna exportación si IA falla. **Gate:** offline el editor debe seguir funcionando.

## D7 [C] — Hybrid Next-Gen Character Editor 3D

**Resultado:** seleccionar parte del rostro 3D y modificarla mediante controles paramétricos reales cuando la geometría lo permita. **Herramientas:** Qt Quick 3D `View3D.pick`, scene/viewmodel, `FaceCustomizer.qml`, `FaceSliderGroup.qml`, `AppearanceEditor`, commands `.snps`, `engines/face`.

**Implementación:**
1. Diseñar mapping verificable `hit/mesh/shape → región facial → parámetro/HeadPart/Morph` usando geometría validada, no coordenadas aproximadas universalmente.
2. Habilitar selección de región (nariz, boca, ojos, mandíbula) con gizmos/handles QML; región sin mapping → modo inspección, no edición.
3. Sincronizar selección 3D y sliders existentes en ambos sentidos; mantener fuente única de verdad en draft C++.
4. Aplicar cambios como `Command` con undo/redo, debounce y cancelación; vista previa debe mostrar estado exportable, no una malla cosmética desconectada.
5. Incluir overlays opcionales de wireframe/topología, valores numéricos y diferencias sobre rostro original.
6. Antes de exportar, comprobar compatibilidad TRI y Safe Patch; si un morph no existe, marcar «no soportado».
7. Evaluar accesibilidad y UI a 1100×700/1920×1080 y configuraciones escaladas.

**Tests:** picking coincide con regiones en fixtures, slider<->3D redondo, undo/redo exacto, guard HPH, reapertura de proyecto, preview/export coincide semánticamente. **Gate:** no basta seleccionar un triángulo si no realiza modificación real.

## D8 [C] — Hybrid Rendering Architecture

**Resultado:** dos presets de visualización sobre los mismos recursos: **Skyrim Accurate (aproximación calibrada al juego)** y **Next-Gen Preview (visualización PBR estética)**. **Herramientas:** Qt Quick 3D, `engines/rendering/src/scene.cpp`, QML `CharacterViewport.qml`, NIF/nifly, DDS/DirectXTex, herramientas de comparación D17 y benchmarks D28.

**Implementación:**
1. Inventariar qué propiedades BSLightingShader/Alpha y FaceTint ya se traducen al visor y registrar gaps con capturas del mismo NPC.
2. Separar `SourceMaterialDescriptor` del `RenderMaterialPreset`; **no duplicar parsers NIF ni escenas**.
3. Crear modo Accurate para tinte de piel/ojos/pelo, alpha, normal `_msn`, blending y luces de referencia, con limitaciones explícitas.
4. Crear modo Next-Gen con iluminación, ambiente, roughness y presentación PBR ajustables, sin modificar NIF/DDS/FaceGen exportables.
5. Mismo mesh/pose/cámara para alternar modo y capturar pares; guardar preset de visualización separado del proyecto NPC.
6. Probar mapas con UV fuera de rango/no finitos antes de `bake_model_space_normals`; proteger loops y memoria, con límites medidos.
7. Repetir benchmarks de Lydia, Ingrid y Serana en GPU objetivo cuando exista; desactivar por capacidad si el modo degrada o falla.

**Tests:** modo no cambia hashes de assets, fallback al backend actual, screenshots A/B reproducibles, textura alpha y model-space normals válidos, inputs UV adversariales, FPS/frametime. **Gate:** «Accurate» es aproximación hasta comparaciones reales Skyrim; no prometer igualdad perfecta.

## D9 [C] — Hybrid Character Animation Studio

**Resultado:** preview de poses corporales y expresiones **compatibles** con esqueletos/morphs verificados, no conversión HKX automática. **Herramientas:** Qt Quick 3D `Skeleton`, `Joint`, skinning y morph animation donde estén disponibles; `engines/skeleton`, `forge_scene_viewmodel`, Blender `bpy`; Open Animation Replacer **solo para estudiar formatos/condiciones**.

**Implementación:**
1. Inventariar `bones`, pesos, jerarquía, bind poses, rangos de joints y limitaciones de los modelos existentes.
2. Crear `AnimationPreviewClip` interno para poses interpoladas y keyframes propios de previsualización; reproducción con timeline, pause, scrub y reset.
3. Importar animaciones GLB que Qt soporte realmente cuando se compruebe ruta en el viewer, con asset limits; no afirmar que HKX es GLB.
4. Para expresiones, mapear TRI/blendshapes válidos a parámetros faciales y restringir HPH incompatible; sin blendshape compatible → no disponible.
5. Añadir validación de penetración visible, clipping de ropa, geometría degenerada y diferencias de peso; separar avisos visuales de errores formales.
6. Guardar poses y previews como metadatos de proyecto; exportar animaciones de Skyrim **solo** tras pipeline HKX específico probado en juego, que queda en investigación independiente.

**Tests:** skeleton cycles, índices inválidos, pesos no normalizados, reset de pose, reproducción determinista, clip corrupto rechazado, preview GLB no afecta ESP. **Gate:** preview ≠ mod animado listo para Skyrim.

## D10 [C] — Ultimate Character Production Pipeline con Blender

**Resultado:** workflow de activos verificables Blender ↔ Character Forge ↔ NIF/TRI/FaceGen cuando exista una conversión demostrada, con **estados separados**. **Herramientas:** bridge real `forge_blender_bridge.cpp`, `tools/blender/forge_bridge.py`, `gltf_inspect.cpp`, Blender CLI/bpy versionado, nifly, DirectXTex, Mutagen, pruebas GLB actuales.

**Implementación:**
1. Revisar la conversión existente FBX/OBJ/BLEND→GLB, cancelación, límites de archivo, hashes y copias en `.sncf`; no reescribirla.
2. Exportar desde el editor un paquete de intercambio con malla+UV+material+snapshot de origen cuando esté permitido, sin redistribuir assets ajenos.
3. Añadir script Blender que valide y registre: número/orden de vértices, topología, UV loops, materiales, pesos, bind pose, referencias de texturas y transformaciones.
4. En importación de vuelta, calcular diferencia semántica de topología, bones, shaders y rutas, mostrando compatibilidad: preview-only / editable / convertible / Skyrim-verified.
5. Separar pipeline experimental de conversión glTF→NIF: seleccionar **un caso reducido** con licencia permitida, comparar NIF pre/post y exigir salida con nifly y validador externo; no convertir arbitrary rig sin pruebas.
6. Para FaceGen y TRI, verificar morph basis, orden de vértices, cabeza y FaceTint; si faltan datos, bloquear etapa exportable.
7. Hacer un test de ida/vuelta con geometría synthetic simple, luego caso real autorizado, y por último Skyrim.

**Tests:** gltf truncado, texturas externas inseguras, pesos inválidos, cambio de topología, operaciones Blender canceladas, readback de NIF, importación a proyecto `.sncf`. **Gate:** conservar la etiqueta «solo vista previa» hasta validar todas las capas.

## D11 [C] — Ultimate Adaptive Workbench

**Resultado:** interfaz tipo VS Code/Blender realmente redimensionable, escalable, persistente y con espacios de trabajo. **Herramientas:** Qt Quick Controls `SplitView`, `Settings`/`QSettings`, `StackView`, `Loader`, model/view QML, iconografía existente, Qt Quick 3D; librería docking externa únicamente tras POC de licencia y compatibilidad.

**Implementación:**
1. Auditar `Main.qml`, `BottomPanel.qml`, `NpcBrowser.qml`, `CharacterViewport.qml`, `Theme.qml` y vistas Forge; cartografiar el shell actual y sus conexiones.
2. Crear primera iteración con **SplitView** horizontal/vertical sobre paneles existentes; usar `saveState()/restoreState()` persistido por versión/esquema, con defaults válidos si monitor/resolución cambian.
3. Añadir configuración de font-scale independiente de UI-scale, accesible desde ajustes reales, con límites razonables y prueba 100/125/150/200%; no multiplicar tamaño de fuente dos veces por herencia.
4. Habilitar mostrar/ocultar paneles, presets NPC Editor/Forge/Multi-NPC/Animation según existan, navegación keyboard y paleta de comandos con acciones funcionales registradas.
5. Sólo cuando el layout básico sea estable, investigar desacoplar paneles con ventanas nativas (`Window`/`ApplicationWindow`) sin mover directamente objetos QML entre escenas invalidadas.
6. Persistir DPI, geometry y workspace por pantalla, con recuperación segura si se desconecta monitor o se restaura layout antiguo.
7. Probar render 3D y errores QML en tamaños 1100×700 y 1920×1080 y bajo múltiples monitores si existen.

**Tests:** interactuar splitter y reiniciar, panel fuera de pantalla recuperado, escalado tipográfico, pérdida de preferences, hotkeys, GPU surface recreated. **Gate:** una maqueta no cumple; los divisores y presets deben funcionar y persistir.

## D12 [C] — Ultimate Extensible Modding Platform

**Resultado:** SDK API versionada para leer datos y cargar extensiones controladas, conectores externos/MCP y eventualmente operaciones Safe Patch aprobadas. **Herramientas:** DTO del JSON-RPC ya existente, C++ plugin host **fuera de proceso**, schema JSON, IPC, motor de permisos D27, Qt panel adapter, TypeScript/Python solo si justificado y aislado.

**Implementación:**
1. Inventariar las consultas disponibles (`catalog`, `appearance`, `projects`) y exponer primero contratos **read-only** `extension.api.v1` sin cambiar el worker innecesariamente.
2. Diseñar manifest firmado/hashed con `id`, versión API, capacidades solicitadas, entrypoint, licencia, recursos máximos y confianza; no confiar en nombres de extensión.
3. Ejecutar extensión en **proceso separado**, validar CLI, working directory, rutas, timeouts y payload sizes; añadir sandbox efectivo del OS para FS/red donde sea posible. Fallo de plugin no debe cerrar la app.
4. Crear gestor QML de extensiones activadas, versión, permisos, diagnóstico, updates manuales y revocación.
5. Crear plugin ejemplo que **lea** y reporte discrepancias de NPC mediante datos sintéticos, sin escribir archivos.
6. Para acceso de escritura posterior: extensión genera `ChangeProposal` tipado, motor de política valida, usuario confirma y Safe Patch realiza exportación; nunca escribir ESP desde script externo arbitrario.
7. MCP: separar servidor/adaptador opcional y soportar list/read de recursos, sin dar acceso al directorio del juego por defecto; comprobar protocolos reales antes de integrar.

**Tests:** plugin colgado/crash, API version mismatch, path traversal, payload excesivo, permiso denegado, revocación, errores de red y serialización. **Gate:** el plugin test funciona con el .exe empaquetado en carpeta limpia.

## D13 [C] — Ultimate Character Project Ecosystem

**Resultado:** proyecto Multi-NPC que enlaza personajes, variantes, recursos, bibliotecas de presets y versiones, con cambios reproducibles y migración de `.snps`/`.sncf`. **Herramientas:** `engines/projects/src/project.cpp`, `command_stack.cpp`, `engines/creation/src/forge_project.cpp`, `forge_asset_library.cpp`, SQLite sólo si hace falta índice de gran escala, JSON schemas y SHA-256.

**Implementación:**
1. Leer especificaciones reales de `.snps` y `.sncf`; identificar datos que se guardan por valor frente a referencias externas y versiones de formato; construir fixtures de proyectos viejos.
2. Diseñar un **manifest del workspace** que separe `projectId`, snapshot MO2, `FormKey`, drafts por NPC, referencias a assets (URI locales permitidas, hashes), catálogo de variantes y decisiones de revisión; no duplicar texturas de terceros.
3. Implementar operaciones `addNpc`, `removeNpc`, `linkPreset`, `snapshot`, `restore`, `compareVersions` mediante comandos existentes; versión del schema y migradores hacia delante, con respaldo del original.
4. Crear navegador visual con miniaturas generadas localmente, filtros por raza/mod/estado y dependencias; miniaturas son vistas, **no** FaceGen exportado.
5. Comparar entrada del proyecto contra VFS MO2 activo: si cambian proveedor, load order o hash, mostrar diff y congelar operaciones sensibles hasta resolver la incompatibilidad.
6. Añadir export reproducible desde snapshot bloqueado, con listas exactas de recursos autorizados y licencias.
7. Crear recuperación de guardados corruptos mediante backup/copia válida y checksum; no inferir integridad solo del nombre.

**Tests:** abrir versiones anteriores, migrar/reabrir, 100 NPC sintéticos, permutar orden, missing assets, dos recursos mismo hash, `FormKey` duplicado, rollback y permisos/licencias. **Gate:** no cambiar formatos sin tests backward-compatible.

## D14 [C] — Ultimate Verified Mod Production Pipeline

**Resultado:** un único flujo de construcción de mods completo, desde drafts autorizados hasta ESP/ESL + recursos permitidos + manifest + ZIP leído y verificado. **Herramientas:** Mutagen, `OutfitPatchBuilder.cs`, `FacePatchBuilder.cs`, `NpcSafePatch.cs`, `ModPackager.cs`, `ExportWizardDialog.qml`, scripts de release existentes, MO2 read-only.

**Implementación:**
1. Centralizar entrada de construcción en receta tipada de cambios autorizados; no unificar las implementaciones mediante duplicación de código.
2. Validar before-build: FormKeys, IDs, versión Skyrim, masters activos, prioridad efectiva de FaceGen, compatibilidad HPH, assets autorizados y licencias.
3. Separar compilación de registros Mutagen, preparación de archivos FaceGen/FaceTint, recursos DDS/NIF y empaquetado. Una etapa no debe inventar la salida de otra.
4. Cada escritor produce `ChangeSet` semántico; `NpcSafePatch` preserva todo campo que no forma parte de la whitelist.
5. Escribir a staging único con rutas confinadas, sin colisiones y sin sobreescritura; abrir nuevamente plugin con Mutagen y recursos con sus lectores, validar masters y checksums.
6. Construir `manifest.json` que contenga hashes, provenance, mod/FaceGen requirements, warnings, `inGameVerified=false` y versión del pipeline.
7. Producción ZIP determinista donde sea posible (orden de entradas, timestamps normalizados, settings compresión); verificar reproducibilidad real dos veces.
8. Integrar «dry-run» y visualización completa antes de exportar; problemas de una sola entrada bloquean lote.

**Tests:** NIF no autorizado, master ausente, FormKey inesperado, dobles outputs, ruta traversal, ZIP truncado, readback discordante, cancellation, 100 NPC, mismo input mismo hash. **Gate:** no publicar ni instalar en perfil habitual por defecto.

## D15 [C] — Ultimate Diagnostic & Recovery Platform

**Resultado:** diagnóstico forense trazable y corrección propuesta reversible, priorizando FaceGen, registros y recursos de NPC. **Herramientas:** `control_map.cpp`, `compatibility_guardian.cpp`, VFS MO2, nifly, DirectXTex, Mutagen, `CatalogStore`, logs de Qt/.NET y optativamente análisis IA D6. No crear un «reparador mágico» de save corrupt.

**Implementación:**
1. Crear catálogo estable de códigos de diagnóstico: `MissingFaceGen`, `RecordFaceMismatch`, `MissingMaster`, `BadNifTopology`, `InvalidTriMapping`, `MissingTexture`, `SourceProviderUnknown`, `UnexpectedNpcDiff`, etc.; no duplicar códigos existentes.
2. Construir **evidence bundle** por diagnóstico: snapshot input, rutas de proveedor, IDs, hashes, campo discrepante, explicación y grado `CONFIRMED`, `LIKELY`, `UNKNOWN`.
3. Correlacionar un error con cadena de conflictos y recursos efectivos, incluyendo loose/BSA; atribución inferida no es autoría demostrada.
4. Asociar reparaciones propuestas a operaciones Safe Patch existentes con preview/diff, no a mutaciones arbitrarias sobre `.esp` o `.nif`.
5. Guardar checkpoint pre-reparación y permitir rollback de staging/proyecto; una reparación imposible debe devolver diagnóstico y permanecer bloqueada.
6. Exponer asistente visual «Problema → causa → evidencia → reparación propuesta → pruebas» sin botones de reparar si no existe implementación funcional.
7. Incorporar errores de runtime/Crash Logger **solo cuando haya logs compartidos y parsers verificables**; no declarar causa raíz de crash por texto aislado.

**Tests:** falso positivo por traducción, dos proveedores de FaceGen, BSA prioritario, recurso cambiado entre análisis/export, rollback, mala clasificación evitada, evidencia reproducible. **Gate:** una reparación no es «exitosa en Skyrim» sin D3.

## D16 [C] — Ultimate Non-Destructive Character Engine

**Resultado:** capas de edición activables y reversibles, stack ordenado y snapshots, sin alterar archivos de origen. **Herramientas:** `engines/projects/src/command_stack.cpp`, `appearance_draft.cpp`, `project.cpp`, viewmodel `edit_history.cpp`, `.snps`, `NpcSafePatch`, NIF Integrity Guard.

**Implementación:**
1. Identificar operaciones actuales `Command::apply/revert`, persistencia y tipos de draft; construir pruebas de undo/redo sobre rostro/ropa.
2. Definir `ModificationLayer` como **receta**, no malla NIF copiada: `layerId`, tipo, inputs/hashes, `operations[]`, dependencias, habilitado, orden, warnings.
3. Componer capa sobre snapshot del NPC ganador de MO2 y calcular vista previa; no escribir al origen. La operación de orden cambia semántica cuando dependa de otra y debe validarse.
4. Permitir activar/desactivar, reordenar solo capas compatibles, deshacer/rehacer exacto y snapshots inmutables del proyecto.
5. Comprobar antes de aplicar: incompatibilidad TRI, cambio de donor/HeadParts, disponibilidad de recurso, campos superpuestos y dependencias circulares.
6. Reconstruir `ChangeSet` final desde las capas activas; llamar a los exportadores seguros existentes y garantizar reproducibilidad del resultado.
7. Recuperar autosaves tras crash mediante journal/checkpoint con escritura atómica; no tocar backups de juego.

**Tests:** undo 100×, ciclo undo-redo tras reorden, autosave truncado, layer A depende B deshabilitada, inputs cambiaron, SkinTint de HPH bloqueado, diff final solo autorizado. **Gate:** ocultar una capa debe afectar la representación de resultado y no solo su checkbox.

## D17 [C] — Ultimate Visual Fidelity & Intelligence Platform

**Resultado:** herramienta que compare la misma cara entre viewport y capturas reales de Skyrim, con métricas, mapas de diferencias y diagnóstico justificado. **Herramientas:** Qt Quick 3D screenshot/cámara, formatos PNG, utilidades de procesamiento de imagen preferentemente C++/Qt ya disponible; OpenCV **sólo candidato** si coste/licencia/beneficio justifican su incorporación; geometría NIF/Morphs; IA opcional.

**Implementación:**
1. Definir `VisualEvidence` (NPC FormKey, hash de plugin/modlist, viewport mode, cámara, FOV, resolución, iluminación, captura Skyrim, timestamp), y exigir las capturas originales sin IA generativa.
2. Construir overlay y split viewer con zoom/sincronización pan; primera versión totalmente manual pero funcional.
3. Registrar pares de puntos/landmarks faciales manuales o detectables y calcular alineación geométrica; documentar sensibilidad a pose/iluminación.
4. Implementar diferencias simples reproducibles: histograma/Delta E aproximado, borde/silueta, región ojo/boca/cuello, mapas de calor; **no** presentar un escalar de similitud como verdad absoluta.
5. Correlacionar con datos deterministas NIF/TRI/FaceTint/HeadParts y clasificar `geometry`, `lighting`, `material`, `unknown` con incertidumbre.
6. Integrar Copilot solo para explicar hallazgos con links a evidencias, sin emitir «verificado» automáticamente.
7. Documentar protocolo A/B humano para Serana/Lydia y casos de Bolli/Brand-Shei.

**Tests:** par idéntico, foto reescalada, luz diferente sin geometría distinta, cámara con FOV distinto, máscara fuera de cara, ausencia de captura real, histogramas reproducibles. **Gate:** si no hay captura in-game, solo «pendiente de comparación».

## D18 [C] — Ultimate Adaptive Processing Platform

**Resultado:** análisis masivo con UI reactiva, workers limitados por recursos, caché incremental correcta, colas recuperables y exportaciones atómicas. **Herramientas:** C++ `QThreadPool`, worker pool actual, `WorkerState`, SQLite WAL, scopes/cancelación JSON-RPC, `CatalogStore`, benchmarks de scan y tests de carrera.

**Implementación:**
1. Instrumentar perfil base: wall-time del escaneo, tiempo máximo en UI, memoria pico, busy SQLite, CPU, latencia IPC; medir en mismo perfil y commit.
2. Revisar comportamiento actual del mapa de control en slices y background; eliminar **únicamente** bloqueos medidos, como lectura de BSA en UI, sin rehacer la indexación.
3. Diseñar `JobDescriptor` versionado: input digest, tipo, prioridad, dependencies, budget memoria, estado, progreso, `cancelToken`, `stagingId`, `resultDigest`.
4. Reutilizar procesos/hilos existentes para lecturas independientes; serializar escritores por caché/perfil cuando no se demuestre seguridad multiwriter de `ActivateLoadOrder`.
5. Persistir cola simple mediante SQLite o archivos atómicos después de pasar tests de crash; idempotencia y transacciones de recuperación, no reejecutar exportación ya publicada.
6. Usar caché incremental por content fingerprint; invalidar si load order, proveedores de recursos, versiones o parámetros relevantes cambian.
7. UI progreso por lotes y cancelación real; descartar respuestas stale; impedir que un trabajo concluido publique resultados sobre un NPC cuya selección ya cambió.
8. Medir 1.308 NPC o fixture equivalente; evaluar 1/2/4 workers, medir contención y no asumir que más threads acelera.

**Tests:** dos procesos mismo perfil, carreras `plugin_files.signature`/`ActivateLoadOrder`, pausa/reanuda, kill worker, corrupción de queue, cambio de inputs, UI responde, cancelación export atómico. **Gate:** mejora medida y sin nuevas carreras.

## D19 [C] — Ultimate AI Character Creation Engine

**Resultado:** generación asistida de **parámetros compatibles**, variantes y preparación Blender; no garantía automática de que imagen→NIF sea jugable. **Herramientas:** `NativeNpcDraft`/Forge, catálogos Mutagen, `head_parts.cpp`, `FaceRecipe`, QML Character Creator, adaptadores IA D6/D30, Blender sólo para geometría avanzada autorizada.

**Implementación:**
1. Definir `CharacterDesignSpec` tipado: raza, género/sexo del registro cuando aplique, edad aparente sólo como descripción, tono, rasgos, estilo, inventario, restricciones, assets permitidos, presupuesto y semilla procedural.
2. Especificar generación **Text→Spec**; validar contenido y no inferir plugins/assets que no aparecen en el catálogo efectivo.
3. Implementar algoritmo determinista `Spec→Draft` usando sliders/HeadParts/presets realmente compatibles, límites de TRI/HPH y seeded PRNG; proporcionar tres variantes reproducibles.
4. Para imagen de referencia, pedir selección/consentimiento y registrar que el matching de rasgos es heurístico, no reconstrucción exacta; no enviar imágenes a terceros sin opt-in.
5. Mostrar a usuario todos los cambios, variantes y dependencias; permitir ajustes manuales y guardar recipe.
6. Reutilizar Safe Patch para NPC existentes; para NPC nuevo depender de `NativeNpcBuilder` funcional **verificado** y del pipeline NIF/FaceGen real, no de una maqueta Forge.
7. Etapa posterior: conexión Blender para retopología e inspección, permitiendo exportación sólo si passes D10.

**Tests:** semilla misma salida, recursos incompatibles rechazados, proveedor IA caído, nombre HeadPart inexistente, imagen sin consentimiento, variante sin licencia, export NIF no disponible → bloqueo explícito. **Gate:** fases reales `PREVIEW_ONLY`/`EXPORTABLE`/`TESTED_IN_SKYRIM`.

## D20 [C] — Ultimate Intelligent NPC Creation Studio (personalidad/diálogo/voz)

**Resultado:** diseñador de personalidad, diálogos condicionados y paquetes básicos de IA vinculados a registros reales de Skyrim; generación narrativa como herramienta opcional. **Herramientas:** .NET Mutagen records, `NativeNpcBuilder`/`RecordEditor`, Character Forge, Qt graph UI, Creation Kit/Papyrus para validaciones complejas, herramienta de producción de voz **sólo con derechos**.

**Implementación:**
1. Separar **metadatos narrativos de proyecto** (`persona`, biografía, objetivos, límites) de registros nativos `NPC_`, factions, AI packages y diálogos.
2. Investigar en Mutagen la capacidad específica de leer/crear tipos de registros de diálogo/paquetes/quest. Crear fixture mínimo y validar roundtrip antes de diseñar editor de nodos.
3. Implementar primero editor de relaciones/facciones y rutinas *existentes*: selección de FormKeys, validación masters, consecuencias y cambios exactos.
4. Introducir `DialogueNode` tipado con condiciones/branch/results compatibles solo después de demostrar serialización y enlace correctos; si requiere Creation Kit, generar paquete de intercambio sin afirmarlo como plugin final.
5. Para voces: establecer mapeo `voice type`/línea/idioma/texto, rutas y metadatos, controles de licencias y autorizaciones. No clonar voces de intérpretes sin permiso.
6. Copilot propone texto/persona/branch graph; humano revisa, worker exporta, QA in-game comprueba línea, condición, audio y subtítulo.
7. Separar **runtime LLM** opcional de diálogos nativos. No exigir Internet ni servicios IA al reproducir mod nativo.

**Tests:** graph sin salida, condiciones cíclicas, links inválidos, masters faltantes, subtítulo sin voz, permisos de audio, regression de NPC no facial, lector CK/xEdit auxiliar y prueba real in-game. **Gate:** no declarar «NPC inteligente» únicamente por un perfil textual.

## D21 [C] — Ultimate NPC World & Quest Integration Platform

**Resultado:** colocar NPC y vincular rutinas/misiones sencillas a celdas/referencias válidas, con flujo de inspección y validación real. **Herramientas:** Mutagen `NPC_`, `ACHR`/`CELL`/`WRLD`/`QUST` y registros relacionados según APIs efectivamente soportadas, Creation Kit para navmesh/escenas especiales, MO2, xEdit read-only, Qt graph editor.

**Implementación:**
1. Reutilizar lectura de ubicaciones ACHR y `NpcOverview.qml`, verificando FormKey de NPC base vs referencia de actor en el mundo.
2. Añadir selector de celda/ubicación desde catálogo real y comprobación de masters y worldspace; nunca confundir `FormID` base con referencia ACHR.
3. Construir primero una referencia nueva colocada en **celda de fixture sintético**; Mutagen write → readback → xEdit auxiliar; validar persistencia y face resources si corresponde.
4. En iteración siguiente, asociar AI packages/rutinas y condiciones, validando que sean compatibles con la celda y el estado del NPC.
5. Para quests, construir grafo con `QuestStage`, alias, condiciones y objetivos; registrar qué elementos son representables en Mutagen y cuáles requieren CK/Papyrus, con bloqueo claro.
6. Añadir análisis de colisiones de FormKey/celdas y lista de masters; no sobreescribir referencias existentes ni navmesh por heurística.
7. Preparar perfiles MO2 test, saves dedicados y protocolo de verificar aparición, paquetes y etapas dentro de Skyrim.

**Tests:** ACHR reference vs NPC base, celda/mundo ausente, master inactivo, referencia duplicada, quest alias inválido, stage unreachable, readback, navegación/AI en partida de prueba. **Gate:** las escenas/navmesh no se consideran editables de forma segura sin soporte oficial probado.

## D22 [C] — Ultimate Verified Skyrim Testing Platform

**Resultado:** laboratorio QA reproducible con dos niveles claros: análisis estático automatizado + validación humana in-game. **Herramientas:** scripts `local-review.ps1`, `smoke-test.ps1`, `release.ps1`, `verify-release.ps1`, MO2, PowerShell Hash, Mutagen, pruebas de regresión C++/xUnit, overlays/capturas D17.

**Implementación:**
1. Crear `TestScenario` y `TestRunManifest` versionados: caso, buildSHA, modSHA, FormKey, perfil MO2, pasos, expectativas y artefactos.
2. Construir runner estático: load/readback ESP/ESL, NIF/Tri/FaceTint checks, FileInventory/hashes, dependencias, seguridad y warnings.
3. Preparar copia aislada del perfil de pruebas sin tocar el perfil habitual; usar instrucciones y permisos claros, y si se automatiza creación, verificar estado de MO2 cerrado y rutas antes de escribir.
4. Mostrar checklist de juego por NPC y escenarios específicos (Serana, Lydia, Shady Sam, Bolli, Brand-Shei); guardar screenshots y notas humanas como evidence con hash.
5. Comparar capturas A/B y regressions; `PASS_IN_GAME` solo mediante acta humana. Guardar estados `NOT_RUN`, `PASS_STATIC`, `PASS_WINDOWS`, `WAITING_HUMAN`, `PASS_IN_GAME`, `FAIL`.
6. Los fallos de juego generan reporte con Input Digest y pasos de reproducción, no cambios automáticos al juego.
7. Integrar release dashboard que impida publicación definitiva cuando un gate requerido está pendiente.

**Tests:** reuso de evidencia de otro hash rechazado, screenshot faltante, perfil de prueba no coincide, manifest adulterado, fallo de NPC individual en batch, recuperación crash del runner. **Gate:** aceptar en Skyrim exige prueba real, no sintética.

## D23 [C] — Ultimate Intelligent Workflow Orchestration Platform

**Resultado:** editor visual real de DAGs para automatizar análisis, revisión humana, exportación y QA, con checkpoints y seguridad D27. **Herramientas:** Qt Quick/QML graph UI, C++ engine Qt-free de DAG, JSON schemas/versiones, task scheduler D18, DTO JSON-RPC, Safe Patch, project snapshots D13; IA D6 opcional.

**Implementación:**
1. Definir primero **tipos de nodos que existen**: `LoadProject`, `ListNpc`, `ResolveMO2`, `AnalyzeConflict`, `CheckFaceGuard`, `HumanApproval`, `ExportPatch`, `VerifyPackage`, `PrepareGameTest`. Evitar nodos inertes.
2. Cada nodo declara `inputs`, `outputs`, `capabilities`, efectos secundarios, versión, timeout, política de reintento, idempotencia, restricciones de permisos y regla de fallo.
3. Implementar runner determinista con validación DAG (sin ciclos no admitidos), propagación de errores, cancelación, logs y checkpoint por hash de inputs.
4. Exponer editor QML de conexiones tipadas; impedir conectar `FaceSource` a un `FilePath` incompatible. Guardar y recargar grafo con schema versionado.
5. Primer flujo de prueba real **solo lectura**: NPC → VFS → mapa conflicto → informe; ejecutar desde UI y comparar con herramienta manual.
6. Segundo flujo con revisión individual D1 y exportación de staging: siempre pausa antes de acciones con permiso.
7. Integrar scheduling D18 una vez que los tests de carreras sean confiables; uso de AI Copilot: traducir un texto a **borrador de grafo**, nunca ejecutar directamente.
8. Garantizar que al reiniciar el app, un checkpoint de `ExportPatch` completado no vuelva a publicar por accidente.

**Tests:** DAG inválido, tipos incompatibles, nodo error, recuperación tras kill, permisos revocados, plan con 53 aprobaciones, reconexión de outputs, idempotencia. **Gate:** flujo ejecutable end-to-end, no solo canvas de nodos.

## D24 [B] — Mod Compatibility Intelligence enfocado a NPC + FaceGen + MO2

**Alcance aprobado preciso:** analizar conflictos de **NPC**, registros faciales, HeadParts, FaceGen/FaceTint y archivos que ganan/perdieron por prioridad de MO2. **No** ampliar automáticamente a todos los sistemas de plugins (navmesh, quests universales, combat overhaul, etc.). **Herramientas:** VFS `engines/mo2/src/vfs.cpp`, `load_order.cpp`, appearance control map, Mutagen `catalog.getNpc`, asset resolver de loose/BSA.

**Implementación:**
1. Construir grafo **acotado** por NPC: `NpcBaseFormKey → winningRecord → recordOverrides[] → FaceGenPath → effectiveProviderMod → HeadParts/TRI/texture providers → masters`.
2. Indexar nodos usando IDs estables `FormKey`/path canonizada/modID e incorporar prioridad izquierda MO2 y orden plugins por separado; no mezclarlos.
3. Calcular discrepancias `record vs resource provenance`, `missingMaster`, `resourceShadowed`, `staleOverride`, `potentialIntentionalChange`; no confundir un warning probabilístico con un defecto confirmado.
4. Mostrar explicación por NPC «qué lo controla» y qué consecuencia tendría alterar load order o instalar un parche, sin escribir `modlist.txt` ni `plugins.txt`.
5. Reutilizar `FaceResolution` solo para propuestas con guardias; ruta de arreglo de registro ≠ ruta de arreglar textura, y ninguna debe modificar fuentes.
6. Soportar escaneo de cientos/miles de NPC con caché incremental D18 y búsqueda por severidad, plugin y origen.

**Tests:** loose vs BSA, par de replacers, traducción, USSEP con HeadParts intencionales, plugin ESL/light FormKeys, orden mod/orden plugin divergentes, sin false positives conocidos. **Gate:** no prometer resolución universal de mods, limitarse a D24B.

## D25 [C] — Skyrim Live Connection / SKSE Runtime Inspector (OPCIONAL, read-only)

**Resultado:** puente opcional para **leer** y comparar hechos del juego ejecutándose con el snapshot del proyecto: versión ejecutable, NPC activo, ID, celda y datos de diagnóstico solo cuando la API de runtime permita hacerlo sin riesgo. **Herramientas candidatas:** SKSE64 correspondiente a Skyrim **1.6.1170** y CommonLibSSE-NG; CMake/MSVC; plugin DLL opcional; named pipe o IPC versionado/handshake; logging local. **NO existe evidencia aquí de que el repositorio ya tenga ese puente.**

**Implementación:**
1. No iniciar esta fase hasta tener F0, lab D22 y gating de permisos D27. Confirmar versión exacta de SkyrimSE.exe y SKSE, ABI y CommonLib; elaborar prototipo **en repositorio/carpeta experimental aislada**.
2. Diseñar esquema read-only con capacidades muy específicas (`GetRuntimeVersion`, `GetCurrentCell`, `GetLoadedActorBaseFormKey`, `GetActorVisualResourceContext` *solo si API real permite*); **no inventar** APIs: extraer firmas de headers del SDK real.
3. Evitar hooks agresivos; preferir lifecycle estándar de SKSE, lectura en thread apropiado y eventos oficiales, con timeout y checks de validez de punteros.
4. Implementar DLL de prueba que arranque solo bajo instalación MO2 de pruebas y exponga un handshake autenticado de usuario local al editor; el modo predeterminado del editor funciona sin SKSE.
5. Prohibir RPC de escritura, ejecución Papyrus, edición de actores, comandos consola o mutaciones de partidas en esta fase. Permisos y desconexión por defecto.
6. Comparar FormKey/versión/procedencia observados vs expectativas del manifiesto; guardar observaciones etiquetadas **runtime**, no certificar apariencia sin captura humana.
7. Probar cierre/recarga/fast travel, fallo de DLL, Skyrim no abierto, corrupción de mensaje, desajuste ABI y desconexión; si hay crashes, retirar bridge y mantener app operativa.

**Pruebas:** versión correcta/incorrecta, juego no abierto, referencia nula, comunicación fallida, no mutación del save y mínimo impacto. **Gate:** prototipo experimental hasta pruebas dentro del juego y aprobación específica de instalación.

## D26 [C] — Asset Provenance & Content Integrity

**Resultado:** bibliotecas con origen, huella, autorizaciones y dependencias claras; reutilizar assets sin copiarlos ilegal o destructivamente. **Herramientas:** `forge_asset_library.cpp`, VFS MO2/asset resolver, SHA-256 (`QCryptographicHash` o .NET `SHA256` donde corresponda), SQLite opcional, manifiestos `.sncf`/`.snps`, ZIP validator actual.

**Implementación:**
1. Definir `AssetIdentity` por bytes/hash + tipo + metadata de origen (mod, ruta efectiva, loose/BSA, licencia, autor si verificable, versión de origen y si permite redistribución). `SHA` demuestra identidad de contenido, **no** autoría legal.
2. Expandir la biblioteca actual manteniendo su política de enlaces externos/copias controladas y deduplicación para GLB; no copiar sin permiso NIF/DDS de mods activos.
3. Implementar `AssetDependencyGraph` únicamente para recursos referenciados por NPC/proyecto (texturas, NIF, TRI, mats, HeadParts, masters). Resolver sin modificar VFS.
4. Al abrir un proyecto, comparar digest del recurso referenciado y notificar `MISSING`, `CHANGED`, `SAME_BYTES_NEW_PROVIDER`, `UNLICENSED_COPY`, `UNKNOWN_LICENSE`.
5. En exportación, incluir únicamente recursos permitidos y registrar requisitos de dependencias; no deduplicar por nombre si bytes difieren.
6. Crear motor de «limpieza de staging» con lista de referencias, pero nunca borrar copias originales ni archivos de MO2 por heurística.

**Tests:** path collision, archivo con mismo nombre distinto hash, mismo hash distintos orígenes, BSA reemplazada, licencia desconocida, symlink fuera de proyecto, ZIP manifest alterado. **Gate:** trazabilidad reproducible y sin copias no autorizadas.

## D27 [D] — Secure Autonomous AI & Extension Execution (autonomía máxima CONTROLADA)

**Resultado:** runtime de agentes capaces de investigar y procesar operaciones aprobadas en un entorno desechable con permisos, auditoría y posibilidad de cortar ejecución. **Herramientas:** Windows Job Objects (ya conocidos en IPC), named pipes seguros, procesos independientes, ACLs de usuario/FS, AppContainer u otras restricciones Windows **solo si probadas**, C++ policy engine Qt-free, manifests, tokens de capacidad efímeros y logs; no confiar en una mera carpeta temporal como sandbox.

**Implementación:**
1. Crear matriz de permisos con categorías: `READ_ONLY`, `STAGING_WRITE`, `PROJECT_WRITE`, `TEST_PROFILE_WRITE`, `REAL_PROFILE_WRITE`, `GAME_DATA_WRITE`, `GIT_MAIN`, `PUBLISH`, `NETWORK`, `EXECUTE_EXTERNAL`.
2. Diseñar `CapabilityGrant` firmado internamente/derivado de sesión, ligado a agente, acción, hash de inputs, ruta confinada, expiración, volumen máximo, CPU/RAM/tiempo y aprobación humana. Por defecto: **denegar**.
3. Probar aislamiento real de OS para agentes externos: acceso denegado a `Skyrim/Data` y perfiles habituales, manipulación de symlinks/junctions, denegación de network cuando no esté autorizado, kill de procesos hijos.
4. Permitir ciclos autónomos de análisis, propuesta y ejecución **en staging** con revisión de errores, checkpoints e idempotencia. Un agente puede planificar la siguiente operación, pero los límites no los decide el LLM.
5. Para `PROJECT_WRITE` u operaciones sobre mod instalado de prueba: exigir grant específico y revisión; para `MAIN/PUBLISH/GAME_DATA` autorización humana siempre.
6. Implementar UI de permisos/revocación y reporte «qué quiso hacer / qué se permitió / qué se rechazó»; auditar secrets, stdout y APIs de plugins.
7. Añadir red team de prompt injection: texto malicioso en nombres de NPC/README, rutas burlando ACL, salida IA no tipada, intentos de confundir staging con Data.
8. Si no se logra aislamiento demostrable, deshabilitar agentes autónomos que ejecuten terceros; mantener solo propuestas read-only/ejecución supervisada.

**Tests:** escape de ruta, proceso hijo, `\..`, junction, permiso vencido, replay de grant, cambio de input tras grant, plugin malicioso, crash kill y no modificación de original. **Gate:** medir seguridad efectivamente; no reclamar «sandboxed» por usar un proceso separado.

## D28 [C] — Adaptive Rendering & Performance Optimizer (GPU objetivo RX 5700 XT)

**Resultado:** adaptador de calidad de render para RX 5700 XT y otros equipos, preservando el viewport Qt Quick 3D como tecnología base. **Herramientas:** Qt `QQuickWindow`/`QSGRendererInterface` o RHI según APIs reales de 6.11.2, Qt Quick 3D profiling, Windows GPU profiler (PIX/RenderDoc **candidato externo** revisado), timers C++ `QElapsedTimer`, caché de textura/mesh, bench fixture.

**Implementación:**
1. Registrar backend realmente utilizado por Qt en Windows, GPU/driver, VRAM reportada si API confiable, tamaño escena, cargas CPU/GPU, frame time y uso de RAM. No asumir backend Vulkan.
2. Crear 3 perfiles **Low/Standard/High** para resolución/samples/sombras y costes de efectos; mismas geometría y assets exportables, solo parámetros de presentación.
3. Optimizar cache de DDS/NIF keyeado por digest + parámetros relevantes (incluido mesh/geometry para normales); invalidar ante cambio de shaders o headparts.
4. Medir cuellos de botella de `_msn` (horneado UV, costuras, geometría), render de mallas extensas y recargas de NPC. Proteger entrada UV NaN/Inf, loops de rasterización y allocs gigantes.
5. Permitir ajuste adaptativo con histéresis y presupuesto de frame, sin reconfigurar cada frame ni perder foco de controles faciales.
6. Comprobar compatibilidad de backend en RX 5700 XT real antes de afirmar FPS o mejoras; si no está disponible, documentar limitación de hardware.
7. Comparar median/p95 frametime y memoria antes/después en mismo NPC, escena y resolución.

**Tests:** texturas grandes, backend desconectado, valores UV adversariales, cache stale, recarga repetida, app sin GPU dedicada, export ZIP intacto. **Gate:** mejora empírica, no «Vulkan = FPS gratis».

## D29 [C] — Procedural NPC Population Studio

**Resultado:** crear grupos de NPC diversos mediante plantillas de atributos y reglas de compatibilidad, con identificación individual, presupuestos y validación. **Herramientas:** `NativeNpcDraft`, catálogos de HeadParts/outfits, RNG determinista C++ (seed), proyectos D13, Multi-NPC D4, Safe Build D14; IA D19 **solo** para sugerencias narrativas.

**Implementación:**
1. Crear `PopulationSpec`: cantidad, razas/géneros permitidos, rango de atributos, distribución de headparts, roles, facciones, recursos permitidos, seed, presupuesto de variaciones y perfiles.
2. Muestrear parámetros mediante RNG reproducible y filtros duros de incompatibilidad, no aleatorizar caras con morphs sin base TRI.
3. Calcular diversidad semántica sin prometer diversidad visual perfecta: repetición máxima de headparts/colores/vestuario, proporciones y constraints del usuario.
4. Presentar **lista de personajes concretos**, con `CharacterDesignSpec` por NPC, preview, editar/excluir uno a uno, registro de seed/catálogo/versiones.
5. Para población de NPC nuevos, comprobar si el pipeline NativeNPC + FormKeys + FaceGen es exportable; si no, permitir biblioteca de concepts/preview sin botón de mod listo.
6. Si se crean referencias en el mundo, dependen de D21 y de pruebas de celda/navmesh/rutinas; no colocar automáticamente miles de actores en Skyrim.
7. Exportar únicamente subconjunto aprobado con validación de recursos y copyright por miembro.

**Tests:** misma seed mismo resultado en mismo catálogo, semilla diferente variedad, límite de recursos, razas sin partes, duplicación FormKeys, 1000 specs con tiempos medidos, batch atómico. **Gate:** población generada de manera reproducible, jugable solo tras D10/D21 y Skyrim QA.

## D30 [C] — Hybrid AI Model Orchestration (local + cloud)

**Resultado:** seleccionar proveedor/modelo según tipo de tarea, política de privacidad, coste, latencia y disponibilidad; no bloquear las capacidades deterministas sin IA. **Herramientas:** adaptadores por proveedor con HTTP client autorizado, local OpenAI-compatible API u otro protocolo **solo después de verificar su versión**, secret store del SO, JSON schemas, policy D27, métricas opt-in.

**Implementación:**
1. Definir `AIProviderContract` con `capabilities` (`TEXT_REASONING`, `IMAGE_ANALYSIS`, `STRUCTURED_JSON`), versión, contexto máximo declarado, coste configurado, política de datos, timeout y modelo ID real consultado al backend.
2. Configurar primero **modo sin IA**; luego stub solo para tests, **no en producción**; después un adaptador real con validación de credenciales y formatos.
3. Modelo de enrutamiento: análisis de conflictos texto local si válido, OCR/visión externa solo bajo consentimiento, tareas creativas en proveedor escogido y límites de coste. No inventar precio ni tokens si API no los expone.
4. `AIRequest` debe contener mínimo necesario; redacción de rutas personales, IDs sensibles y metadata no necesaria. No enviar archivos de mods de terceros sin permiso.
5. Estandarizar respuestas en DTO tipados, validar schema y Safe Patch; un modelo no puede devolver código arbitrario a ejecutar como parte de una «solución».
6. Añadir fallback entre modelos **solo** cuando no cambia consentimiento/costo de manera inadvertida y se conservan garantías de privacidad.
7. Implementar panel de tareas, presupuesto, cancelación, latencia, resultados y provenance del modelo/prompts con confidencialidad.

**Tests:** proveedor caído, JSON inválido, respuestas alucinadas (FormKey inexistente), fuga de secret en logs, cancelación, límite de coste, consentimiento retirado, red no disponible. **Gate:** no presentar predicciones LLM como validación técnica.

## D31 [A] — Game Version & Mod Manager Compatibility: Skyrim 1.6.1170 + MO2

**Resultado:** la aplicación soporta oficialmente solo **Skyrim SE/AE 1.6.1170 y MO2** con detección temprana, mensajes claros y pruebas bajo esta configuración. **Herramientas:** `engines/mo2/src/game.cpp`, lector de versión Windows ejecutable, `modorganizer.ini`, load order, tests de fixture y `README/COMPATIBILITY.md`.

**Implementación:**
1. Definir capabilities/versión objetivo centralizada; no hardcodearla en cinco pantallas. Medir versión de `SkyrimSE.exe` antes de operaciones sensibles.
2. Distinguir Skyrim VR, LE, SE versión antigua, AE diferente, instalación incompleta y MO2 portable/global; exponer resultados `SUPPORTED`/`DETECTED_UNSUPPORTED`/`UNDETERMINED`.
3. Bloquear **funciones de exportación o puente runtime específicas 1.6.1170** fuera de versión; permitir análisis de solo lectura cuando sea seguro y documentado.
4. Validar load order y masters bajo reglas del juego elegido; no mezclar drivers o instaladores de Vortex como si fueran MO2.
5. Documentar claramente requerimientos de SKSE y runtime solo para plugins opcionales (D25) y no volverlos obligatorios.
6. Crear fixtures de versión válida e inválida y pruebas de rutas MO2 incorrectas.

**Tests:** 1.6.1170 detectado, 1.6.640/1.5.97/VR rechazados para export específico, exe ausente, versión spoof imposible de determinar, perfiles válidos. **Gate:** calidad de soporte acotado, no «compatible con todos los Skyrim».

## D32 [C] — Collaborative Character Development con Git y revisiones

**Resultado:** compartir proyectos de forma revisable con historial, diff semántico y bloqueo de conflictos, sin necesitar una plataforma cloud en tiempo real. **Herramientas:** Git como backend externo/opcional, formatos `.snps` `.sncf` versionados, JSON canonizado, snapshots D13, SHA assets D26; no subir recursos no licenciados.

**Implementación:**
1. Diseñar representación de proyecto en texto determinista y separable por NPC cuando sea viable, sin romper compatibilidad con formato actual.
2. Crear `SemanticProjectDiff` que compare `FormKey`, operaciones, layers, source digests, presets, materiales, aprobaciones y referencias de assets; binarios se comparan por hash y metadata.
3. Integrar export/import de proyecto colaborativo sin duplicar contenido de terceros; listas de dependencias y enlaces a sus fuentes permitidas.
4. Implementar detección de conflictos al importar un proyecto: dos cambios al mismo campo, distintos ganadores MO2, FaceGen proveedor diferente y propuesta aprobada cuyo digest cambió.
5. Ofrecer merge asistido para cambios realmente independientes; **no** resolver automáticamente dos cambios faciales incompatibles ni aprobar de nuevo por usuario ausente.
6. Añadir reportes de revisión HTML/Markdown y opcional creación de PR con consentimiento; nunca push a remoto por defecto.
7. Probar intercambio entre dos clones y reapertura de proyecto en ambiente distinto con dependencias ausentes.

**Tests:** merge campos disjuntos vs superpuestos, sin lock perdido, orden de capas, cambio de catálogo, author/licensing, caso de 53 aprobaciones invalidadas. **Gate:** diffs semánticos y reconciliación real, no solamente `git commit` en la UI.

## D33 [C] — Accessibility, Localization & User Control

**Resultado:** aplicación internacionalizable, usable con teclado, escalado/contraste y distribución configurable, integrada con D11. **Herramientas:** Qt `qsTr`, `lupdate/lrelease` según build QML existente, `QTranslator` para C++, Qt Quick Controls focus/accessibility, preferencias QSettings, tests UI y lectores de pantalla Windows si disponibles.

**Implementación:**
1. Extraer cadenas de usuario de QML/C++ con claves y contextos, preservando español actual y preparando inglés como segundo idioma; no traducir `FormKey`, nombres de registros, paths, códigos de error o JSON.
2. Implementar language switch real con estado persistido y prueba de reinicio; mensajes worker traducibles por código estable y no por parseo de texto inglés.
3. Centralizar font scale/UI scale/theme/contrast y navegación por Tab/Shift-Tab, atajos configurables y foco visible.
4. Proporcionar nombres accesibles a controles, avisos de error no dependientes solo de color y tamaños de target usables; evitar romper viewport y tooltips.
5. Layout responsive a 1100×700, 1366×768, 1920×1080 y 150% DPI; tratar escalado visual separado de tamaño de fuente.
6. Probar navegación sin mouse y anuncios accesibles en errores de exportación, revisión de 53 NPC y permisos D27.

**Tests:** idioma, recursos de traducción faltantes, foco invisible, zoom 200%, persistencia, tooltip overlap, contraste, cadenas QML sin localizar. **Gate:** funcionalidad real en UI empaquetada, no solo archivos `.ts` vacíos.

## D34 [C] — Mod Release & Distribution Center

**Resultado:** centro de releases para producir y preparar distribución autorizada de mods verificados, con changelog, hashes, licencias y export para Nexus/GitHub **asistida**. **Herramientas:** `ModPackager.cs`, `scripts/release.ps1/verify-release.ps1`, manifests D14/D26, Git release metadata opcional, Nexus Mods API **solo si se confirma documentación/permisos**.

**Implementación:**
1. Crear `ModReleaseCandidate`: version semver, proyecto/build SHA, entradas, manifiesto por NPC, requisitos, estado in-game, licencia de assets, changelog y firmas/digests.
2. Validar package y mantener distinción `DRAFT`, `STATIC_VERIFIED`, `WINDOWS_VERIFIED`, `GAME_ACCEPTED`, `PUBLISHABLE`; no permitir `PUBLISHABLE` por texto de IA.
3. Construir generador README/FOMOD **solo si hay beneficio real y formato verificado**, licencias y créditos; no incorporar assets ajenos.
4. Agregar diff entre versiones, changelog calculado de modificaciones de campos y lista de dependencias MO2.
5. Exportar ZIP para publicación manual y permitir navegación a portal oficial; automatización de upload sería módulo independiente con API/autorización explícita.
6. Registrar checksum reproducible y comprobación del ZIP después de crear/extraer; verificar nombre, ruta, manifest y conflictos de FormID.

**Tests:** conflicto de nombre, archivo no licenciado, estado pending Skyrim, ZIP contaminado, mismo source diferentes timestamps, rollback/release version, falta master. **Gate:** sin publicación silenciosa.

## D35 [C] — Reliability, Diagnostics & Recovery Engineering

**Resultado:** aplicación robusta ante crashes, datos corruptos, cachés concurrentes y fallos de addons; diagnóstico reproducible y recuperación sin pérdida del proyecto. **Herramientas:** scripts `enable-crash-dumps.ps1`, `local-review.ps1`, logs estructurados C++/.NET, SQLite integrity check, Windows Event Viewer/dumps y WinDbg **candidato externo**; sanitizers/MSVC instrumentados cuando compatibles.

**Implementación:**
1. Inventariar rutas críticas y modos de fallo: app QML, worker RPC, cache SQLite, NIF/GLB parsing, Blender subprocess, plugins externos, GPU device lost y proyectos corruptos.
2. Crear formato `DiagnosticBundle` opt-in con versiones, hashes, código error, stack/dump si autorizado, logs redactados y reproducción; no incluir datos personales o activos de mods por defecto.
3. Añadir timeouts/cancelación por capa y restart del worker sin perder draft; errores estructurados en lugar de UI colgada.
4. Implementar recuperación de autosave/proyecto mediante journal+snapshot atómico, check de integridad y restauración manual antes de sobrescribir.
5. Introducir stress tests repetibles (abrir 200 NPC, cambiar 1000 veces selecciones, cancelar export, recargar Forge, concurrencia SQLite, cierre brutal); registrar leak/memoria/frametime.
6. Monitoreo local de excepciones, stall UI y errores del worker; telemetría externa desactivada salvo opt-in explícito.
7. Hacer bisect de regresiones contra commit base, no atribuir a una librería sin reproducción.

**Tests:** crash worker durante export, crash app durante autosave, cache WAL incompleta, recovery idempotente, sin archivos parciales, logs sin secrets, slow GPU. **Gate:** tiempo de recuperación y ausencia de corrupción medidos.

## D36 [C] — Master Development Strategy: entregas secuenciales 2026–2029

**Resultado:** 36 decisiones convertidas en trabajo **programable y auditable** sin dispersar el desarrollo. **Herramientas:** GitHub Issues/PR/projects si disponibles, `docs/DEVELOPMENT_PLAN.md`, ADRs, CMake/xUnit/CTest, scripts de release, prompts de Claude/Codex; tableros locales Markdown cuando API no permita crear tickets.

**Implementación:**
1. Crear un **issue/épica por fase F0–F7**, vinculada a decisiones D# concretas y criterios verificables, sin abrir 36 PR vacíos.
2. Para cada sprint, elegir máximo 1 objetivo principal + 1 de deuda técnica; definir entrada, archivos/contratos a inspeccionar, trabajo de código y prueba de aceptación; no saltar a «vNext» sin RC aprobada.
3. Definir gates: `CODE_REVIEW`, `LOCAL_GREEN`, `STATIC_EXPORT_VALID`, `MO2_ISOLATED`, `HUMAN_SKYRIM`, `APPROVE_MAIN`, `RELEASE`; no todos aplican a una feature UI pura, pero sí los sensibles.
4. Mantener registro de decisiones y riesgos: D27D autonomía limitada, D31A compatibilidad cerrada, D1 53 revisiones humanas, D36C secuencial.
5. Automatizar generación de evidencia por commit para que Codex/segundo agente audite resultados, no declaraciones del implementador.
6. Publicar estado real por hito y actualizar documentación **solo después** de tests. Si un experimento fracasa, registrar el fracaso y adoptar alternativa sin falsificar completitud.

**Tests de proceso:** PR sin tests no pasa, CI no iniciada no equivale a PASS, changelog sin código no avanza feature, falta evidencia Skyrim bloquea flags de aceptación. **Gate:** priorizar producto ejecutable, no cantidad de features anunciadas.

---

# PARTE D — RUNBOOK REAL: PRIMERA EJECUCIÓN DE CLAUDE CODE (F0)

**No empieces por IA, Vulkan, HKX, SKSE o una UI moderna.** La primera ejecución operativa produce una rama de integración 1.1.0 y pruebas. Si falta un permiso o una máquina Windows, ejecuta hasta donde se pueda y registra qué no fue posible.

## D0. Comprobaciones de disponibilidad (sin mutaciones)

Desde terminal Windows PowerShell en la raíz del clone autorizado (no asumir ruta local; si el usuario entregó una ruta, comprobarla):

```powershell
# Localiza/revisa el checkout REAL; no ejecutar antes de ubicar la carpeta del proyecto.
Get-Location
git rev-parse --show-toplevel
git remote -v
git status --short
git branch -avv
git worktree list
git log -1 --format='%H %s'
# Verifica herramientas, NO las instales ciegamente:
git --version
cmake --version
dotnet --version
python --version
```

Usar `git fetch` únicamente después de verificar remoto, credenciales y estado del usuario. Si hay cambios sin commit, trabajar en otro worktree. Las llamadas a GitHub PR son opcionales si GitHub CLI o conector están autenticados; nunca inventar output de `gh`:

```powershell
gh pr view 21 --json number,title,state,baseRefName,headRefName,headRefOid
gh pr view 22 --json number,title,state,baseRefName,headRefName,headRefOid
gh pr view 23 --json number,title,state,baseRefName,headRefName,headRefOid
```

Si `gh` no existe o no está autenticado, usar `git` y la URL del repositorio para consultar; no detener la inspección local por ausencia de una CLI opcional.

## D1. Procedimiento real de integración de ramas (sin merge en `main`)

**Primero elaborar un plan de commits** inspeccionando:

```powershell
git fetch origin --prune
git merge-base origin/release/v1.1 origin/feature/conflict-intelligence-hardening
git log --oneline --graph --decorate origin/release/v1.1..origin/feature/conflict-intelligence-hardening
git log --oneline --graph --decorate origin/fix/nu1902-messagepack..origin/fix/catalog-cache-race
git diff --stat origin/release/v1.1...origin/feature/conflict-intelligence-hardening
git diff --stat origin/fix/nu1902-messagepack...origin/fix/catalog-cache-race
```

No ejecutar comandos si alguna rama no está disponible; primero confirmar refs con `git branch -r`. Verifica los scripts/reglas de Git antes de aplicar ramas. Si la punta de #22 ya contiene todo #21, puede usarse como referencia de una **nueva rama RC**; #23 necesita revisar que su delta no esté presente antes de cherry-pick. **No ejecutar cherry-pick ciegamente con números de PR**: primero mapear commit SHA y diff real, confirmar compatibilidad y ausencia de cambios del usuario.

Propuesta de nueva rama, **solo después** de comprobar que `origin/feature/conflict-intelligence-hardening` existe y el worktree está limpio:

```powershell
# EJEMPLO de comando condicional: escoger una carpeta de worktree nueva fuera del checkout actual.
# git worktree add -b release/v1.1-rc-integrated <RUTA_SEGURA_NUEVA> origin/feature/conflict-intelligence-hardening
# En esa rama: integrar SOLAMENTE el delta faltante de #23 tras auditarlo.
```

`#` indica **ejemplo condicionado**, no un comando ya ejecutado ni ruta autorizada. Nunca aplicar `git reset --hard` o `git clean -fdx` para «solucionar» un conflicto sin consentimiento.

## D2. Matriz obligatoria de integración

| Gate / área | Archivos reales que revisar | Prueba para avanzar | Fallo bloqueante |
|---|---|---|---|
| NIF HairTint | `engines/face/src/facegen.cpp` | Bytes ajenos al HairTint iguales | Diferencia geométrica o material no autorizada |
| TRI High Poly | `engines/face/src/tri_file.cpp`, `face_morphs.cpp` | Bloqueo de malla incompat. | Regeneración HPH silenciosa |
| Origen de FaceGen | `appearance_editor.cpp`, `control_map.cpp` | Reconsulta VFS MO2 al exportar | `effectiveFaceGenMod` inventado o tautológico |
| Worker seguridad | `FacePatchBuilder.cs`, `NpcSafePatch.cs` | `RemainingFaceDifferences`, máscara NPC, masters | Campo no solicitado modificado |
| Caché | `CatalogStore.cs` | Concurrencia 2 procesos + `ActivateLoadOrder` | `SQLITE_BUSY` sin política o corrupción |
| UI/orígenes | `AppearanceControlPanel.qml` | Selección real y explicación consistente | Aprobar ambiguous sin revisión |
| Empaquetado | `ModPackager.cs`, scripts release | ZIP 2x, hashes iguales, readback | ZIP parcial / dirty no avisado |
| Qt 3D | `CharacterViewport.qml`, `engines/rendering/` | Render smoke + tiempo UI | Bloqueo de hilo, UV no finitos |

## D3. Comandos de build y pruebas confirmados en la rama revisada

En PowerShell de desarrollador con herramientas disponibles:

```powershell
# Requiere setup del proyecto existente, leer scripts/README antes.
. .\scripts\dev-env.ps1
.\scripts\build.ps1 -SkipTests
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación' }

dotnet test --solution SkyrimNpcStudio.slnx
if ($LASTEXITCODE -ne 0) { throw 'Fallaron los tests .NET' }

ctest --preset debug -j 4 --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Fallaron los tests C++' }
```

Cuando exista una instancia MO2 real **verificada y autorizada en modo lectura**, ejecutar el arnés con parámetros reales:

```powershell
# EJEMPLO: sustituye los valores por la ruta existente comprobada en el equipo.
# .\scripts\local-review.ps1 -Instance '<RUTA_REAL_ModOrganizer.ini>' -Profile '<PERFIL_REAL>' -Npc '0A2C8E:Skyrim.esm'
```

El script `local-review.ps1` contiene pasos build/dotnet/ctest/ctest-local/smoke/forge-preview, y asigna directorio propio a `SNS_LOCAL_CACHE_DIR`; verificar su contenido antes de lanzar. Comparar efectivamente `Data`, archivos de perfil y mods originales antes/después; nunca asumir que ejecución fue read-only solo porque un flag lo dice.

## D4. Comandos de release realmente existentes

El script `scripts/release.ps1` declara los parámetros `-AllowDirty`, `-SkipPackage`, `-Instance`, `-MO2Profile`, `-Npc`, `-Scenarios`. El verificador `scripts/verify-release.ps1` declara `-Zip` obligatorio y opcional `-OutDir`, `-Instance`, `-MO2Profile`, `-Npc`, `-Scenarios`.

```powershell
# Después de compilar, probar e identificar el commit RC:
.\scripts\release.ps1
if ($LASTEXITCODE -ne 0) { throw 'Falló el release candidate' }

# El script anterior llama internamente a verify-release.ps1.
# Para un ZIP real ya generado:
# .\scripts\verify-release.ps1 -Zip '<RUTA_REAL_ZIP>'
```

`release.ps1` rechaza worktree con cambios versionados sin commit por defecto; `-AllowDirty` solo para exploración, **no** release final. Genera `release-manifest.json`, `.sha256` y ejecuta verificación desde extracción limpia, comprobando `Qt6Test.dll`, self-contained .NET Worker y otras dependencias. **No inventar nombre del ZIP**: capturar el path real de salida y sha256.

**Prueba de reproducibilidad:** ejecutar la creación 2× desde el **mismo commit y mismos inputs** y comparar SHA-256 y file manifest. Registrar sourceSHA, scripts versiones, toolchain, estado dirty, resultado.

## D5. Pruebas MO2/Skyrim y expedientes individuales

1. Reutilizar `docs/SKYRIM_ACCEPTANCE_TEST.md` y `docs/SKYRIM_VERIFICATION_FACE_RESOLUTION.md` desde **la rama RC**.
2. Para Lydia/Serana/Shady Sam generar casos de cabello/atuendo; para Bolli/Brand-Shei resolución de origen facial. No reutilizar ZIP de commit anterior como si fuese generado con RC.
3. Documentar perfil MO2 aislado con nombre, load order, archivo fuente, fecha y hashes. Instalar/testear **solo tras consentimiento** en ese perfil.
4. Guardar capturas humanas A/B de frente, perfil y cuello, condiciones de iluminación y mods activos, sin IA alterando capturas.
5. Crear 53 expedientes desde escaneo real; cada `ReviewCase` contiene input hash. Si el escaneo varía, registrar cambios: el número 53 es baseline de ese perfil y branch, no un número a fabricar.
6. Un expediente aprobado para una revisión/manifest no autoriza futuras versiones que cambien source/hash/diff; pedir revisión nuevamente.

## D6. Checkpoints de implementación y seguridad

- `F0-CHECKPOINT-00`: rama/estado/repositorio y evidencia de PR, **sin mutar**.
- `F0-CHECKPOINT-01`: worktree RC aislado + integración revisada #22/#23; commit real.
- `F0-CHECKPOINT-02`: compilación/test/SQLite/safe guards y medición.
- `F0-CHECKPOINT-03`: ZIP reproducible y verificado, sin `inGameVerified=true`.
- `F0-CHECKPOINT-04`: kits MO2 y formularios de aceptación humana listos, 53 expedientes sin aprobación simulada.
- `F0-CHECKPOINT-05`: solo tras evidencia humana, propuesta de merge preparada; **no merge automático**.

El agente puede realizar checkpoints 00–04 dentro de permisos; el 05 requiere datos y aprobación humana. La documentación no reemplaza ninguna validación.

---

# PARTE E — CÓMO EJECUTAR LAS DEMÁS FASES SIN INVENTAR CAPACIDADES

## E1. Regla de incremento mínimo funcional vertical

Para cada etapa futura, implementar **un recorrido end-to-end verificable** antes de multiplicar controles. Ejemplo: D11 no empieza con 8 espacios de trabajo; comienza con UN `SplitView` que realmente redimensiona `NpcBrowser` y `CharacterViewport`, guarda el estado, lo restaura, tiene tests UI y no rompe el visor. D19 no empieza con «Crea 1000 NPC por IA»; primero genera 1 preset real con 1 plugin y una ruta de exportación comprobable. D21 no empieza con una misión épica: primero crea 1 referencia sintética `ACHR` y la relee.

### Escalera de madurez

1. `SCHEMA`: contrato y fixture sintético.
2. `CORE`: motor determinista con tests positivos/negativos.
3. `INTEGRATION`: conectado al código existente y contrato versionado.
4. `UI_REAL`: flujo clic/teclado funcional, errores reales.
5. `EXPORT_STATIC`: paquete/registros validables donde corresponda.
6. `WIN_VERIFIED`: programa empaquetado y probado en Windows.
7. `MO2_ISOLATED`: paquete/recursos verificados contra perfil aislado.
8. `SKYRIM_ACCEPTED`: humano observó resultado dentro de Skyrim y vinculó evidencia al SHA.

Una feature que sólo ofrece configuraciones de UX puede terminar en `WIN_VERIFIED`; una que promete modificar NPC dentro del juego exige superar `SKYRIM_ACCEPTED` antes de afirmarlo.

## E2. Modelo de feature request que Claude debe usar (internamente)

```yaml
id: SNS-D04-F1-001
name: 'Multi-NPC: dos NPC claros en un solo plugin'
phase: F1
decisions: [D1, D2, D3, D4, D14, D22, D24]
source_branch: '<COMPROBAR>'
verified_components:
  - 'FacePatchBuilder.Build'
  - 'ExportFaceOverhaulRequest.Assignments'
preconditions:
  - 'F0 aprobada'
  - 'No hay ambiguos no aprobados'
input: '2 FormKeys + snapshot MO2 + FaceGen provider real'
expected_output: 'Un ESP con 2 overrides y manifest exacto'
no_write_paths: ['Skyrim/Data', 'perfil MO2 habitual', 'saves personales']
rollback: 'borrar sólo staging propio no publicado / restaurar draft'
tests:
  - '2 NPC claros readback completo'
  - '1 NPC falla → no se publica el ZIP'
  - 'master perdido → rechazo'
  - 'cambio FaceGen efectivo → rechazo'
evidence: '<COMPLETAR SOLO DESPUÉS DE EJECUTAR>'
human_gate: 'Skyrim QA antes de declarar probado en juego'
```

## E3. Matriz de pruebas de seguridad compartida

| Tipo | Qué simular | Resultado exigido |
|---|---|---|
| Privilegios | extensión intenta escribir `Skyrim/Data` | denegado; log; sin modificaciones |
| Provenance | plugin origen no coincide con FaceGen efectivo | rechazo antes de exportar |
| Invalidez HPH | cabeza/archivo TRI con orden vértices incompatible | bloqueo de morph/FaceGen |
| Atomicidad | cancelar proceso con un NPC a mitad | no ZIP ni ESP final parcial |
| SQLite | 2 procesos indexan simultáneamente mismo perfil | sin UNIQUE ni corrupt, o lock/backoff seguro y explicable |
| Workers | respuesta tardía cambia selección QML | ignorar stale result, sin UI corrupta |
| Colisiones | 2 NPC comparten nombre pero distinto FormKey | procesos separados y referencias correctas |
| Dependencias | falta master o asset | error estructurado y manifiesto pendiente |
| Data privacy | plugin/IA intenta exfiltrar rutas/archivos | red/FS limitado o feature bloqueada |
| Reproducibilidad | mismo commit+entradas → 2 builds | checksums iguales o diferencias explicadas |
| Skyrim tests | screenshot sin contexto ni hash | NO aceptar como verificación certificada |
| Licencias | el proyecto incluye asset ajeno sin permiso | paquete «no publicable» y sugerir dependencia |

## E4. Catálogo de entregables medibles

**F0**: `release-candidate SHA`, `TEST_RESULTS`, `ZIP/sha256`, matriz PR #21/22/23, baseline de SQLite/FaceGen, 53 review cases, paquete de prueba Skyrim y acta de aceptación pendiente.

**F1**: plugin único con mínimo 2 NPC y readback, luego grupos medidos, flujos de aprobación por NPC, manifest atómico y análisis MO2 acotado.

**F2**: layout con splitter/redimensionamiento persistente, 1 proyecto Multi-NPC serializado y migrable, 1 layer reversible efectiva, 1 asset manifest con hashes, 1 reporte Git semántico.

**F3**: 1 interacción 3D→morph compatible real, 1 caso HPH incompatible correctamente bloqueado, modos visuales comparables, capturas A/B, benchmark.

**F4**: 1 GLB rigged con pose reproducible, 1 ida/vuelta Blender con tests, 1 generador de preset con seed determinista, casos exportables separados de preview-only.

**F5**: 1 extensión read-only aislada, 1 tool MCP limitada, 1 workflow de conflicto ejecutable, 1 Copilot que propone cambios tipados sin escribir, 1 ensayo de ataque por permisos.

**F6**: 1 relación/facción real leída y escrita/releída, 1 diálogo/quest sintético que pasa CK/Mutagen según disponibilidad, 1 colocación ACHR con prueba Skyrim, 1 bridge SKSE **opt-in read-only** sin crashear y con apagado seguro.

**F7**: 1 release de mod con manifest/licencias/changelog verificables, regresiones 0 conocidas críticas, documentación de límites y proceso de publicación asistido.

## E5. Definición de «implementación falsa» y cómo corregirla

**Ejemplo malo:** «Se agregó AI Character Generator», pero `AIGenerationPanel.qml` muestra texto fijo y botón que genera tres cadenas inventadas.

**Corrección exigida:** contrato `CharacterDesignSpec` parseado de texto real, validación del catálogo efectivo, generación seed-reproducible de draft real, visualización en Forge, persistencia en proyecto, errores explicados y output clasificado `PREVIEW_ONLY` o `EXPORTABLE` según su ruta comprobada.

**Ejemplo malo:** «Blender integration complete» porque muestra un GLB importado.

**Corrección:** comprobar que el bridge se ejecutó realmente, glTF se parseó y comparó, NIF/TRI aún no exportables quedan bloqueados, y cualquier mod nuevo requiere pipeline de conversión validado y juego.

**Ejemplo malo:** «Multi-NPC terminado» porque permite marcar checkboxes de 53 NPC.

**Corrección:** llegar a un ESP con múltiples overrides readback, whitelist, masters, manifeste, aprobación individual y atomicidad del lote.

**Ejemplo malo:** «Workflows ejecutan automáticamente» porque se ven nodos y flechas.

**Corrección:** nodes tipados vinculados a APIs reales, persistencia, ejecución observable, cancelación, checkpoints, pruebas de permisos y salida comprobable.

**Ejemplo malo:** «Vulkan Next Gen» porque añade una opción de renderer en la UI.

**Corrección:** lectura real del backend Qt, baseline de frametime, implementación render/caché efectiva, captura/benchmark y fallback; Vulkan es opcional y no se promete mejora si no se midió.

**Ejemplo malo:** «Todos los NPC reparados» porque el modelo IA clasifica 84 casos como «solucionados».

**Corrección:** 31 claros y 53 ambiguos son **propuestas, no arreglos completados**. Solo operaciones aprobadas, readback y validación in-game pueden sostener el resultado.

## E6. Procedimiento de auditoría independiente (Claude vs Codex)

Claude implementa en rama aislada. Un revisor independiente (Codex u otra persona/agente) debe:

1. Leer *diff real* y commits, no solo informe de Claude.
2. Comparar cambios de `NpcSafePatch`, `FacePatchBuilder`, `CatalogStore`, `mo2` y guardias NIF/HPH con reglas anteriores.
3. Ejecutar tests críticos en entorno limpio si posee herramientas/runner; si no, comunicar limitación.
4. Buscar fallos de seguridad por ruta, carreras, uso de input confiado, pérdida de campos NPC y fuga de material.
5. Reportar `BLOCKER`, `MAJOR`, `MINOR` con path/line, reproducción y test sugerido.
6. La persona titular aprueba merge solo después de revisar las correcciones y pruebas humanas pendientes.

El revisor no debe modificar código en la misma rama simultáneamente a Claude. Si encuentra defectos, abrir issue o PR separado.

---

# PARTE F — PROMPT DE ARRANQUE, ORDEN EXACTA PARA CLAUDE

**Claude: lee todas las secciones anteriores, en especial las 36 fichas D1–D36. Esta es la orden concreta de ejecución inicial.**

1. Verifica localmente que estás en el **checkout auténtico y autorizado** `AnotatoDrako4/SkyrimNpcStudio`, y determina el estado exacto de ramas, PR y herramientas, sin modificar archivos originales ni borrar datos del usuario.
2. Relee implementación, no solo documentación, de `AppearanceEditor`, `FacePatchBuilder`, `CatalogStore`, `control_map`, `NpcSafePatch`, `ModPackager`, PR #21/#22/#23 y scripts `build/release/verify-release`.
3. Expón en máximo 20 líneas el resultado del preflight (ramas, bloqueos, componentes, **primer cambio real**). Esto NO es una solicitud de permiso para cambios ordinarios en un worktree seguro; continúa programando si no hay gate humano.
4. Crea una rama RC aislada cuando el árbol sea seguro. Integra #22 y delta no duplicado de #23, corregiendo conflictos semánticos con tests. No tocar `main`.
5. Añade/corrige **una prueba de regresión demostrablemente necesaria** (por ejemplo un escenario no cubierto de concurrencia SQLite o guardia de FaceGen), reprodúcela si es posible, corrige el código y demuestra que pasa.
6. Compila app y worker, ejecuta suites, no deshabilites auditoría/dependencias; arregla errores causados por tus cambios.
7. Prepara release candidata usando scripts reales y genera evidencia completa; si careces de Windows/Qt/MO2, reporta **qué prueba no se pudo ejecutar**, sin inventarla.
8. Genera expedientes de 53 casos **solo con el perfil real autorizado** y deja aprobaciones en pendiente; prepara kits MO2/Skyrim sin usurpar observación humana.
9. Tras completar F0 en la medida de permisos y evidencia disponible, **solicita al usuario** los resultados de prueba humana y permiso de merge; NO saltes a F1 si F0 está bloqueada.
10. Después de que F0 supere su gate, ejecuta D4 Multi-NPC como primer feature vertical. Las demás D siguen este documento y sus dependencias.

### Informe final exigido por iteración

```text
REPO / WORKTREE / COMMIT:
OBJETIVO D# / FASE:
CAMBIO FUNCIONAL REAL:
RUTAS Y FUNCIONES EFECTIVAMENTE MODIFICADAS:
DEPENDENCIAS INSTALADAS/CAMBIADAS Y LICENCIAS:
COMANDOS DE COMPILACIÓN Y EXIT CODES:
PRUEBAS: PASSED / FAILED / SKIPPED (separados):
RIESGOS Y REGRESIONES:
ARTEFACTOS EVIDENCIALES (ZIP/hash/log/capturas, si existen):
GUARDIAS DE SEGURIDAD Y APROBACIONES:
ESTADO REAL (no exagerar):
SIGUIENTE CAMBIO DE CÓDIGO:
```

**Cierre:** No quiero otra propuesta de futuro sin ejecución. Quiero commits trazables, botones funcionales, conversiones y exportaciones probadas, validaciones que fallen cerrado, informes reproducibles y distinción absoluta entre «compilado», «probado en Windows» y «verificado por una persona dentro de Skyrim». **Comienza ahora con F0.**
