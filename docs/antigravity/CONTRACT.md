<USER_REQUEST>
Lee íntegramente el archivo `SkyrimNpcStudio_Antigravity_Trabajo_Paralelo_Claude_2026-10-19.md`.

Quiero que trabajes directamente en mi PC, simultáneamente con Claude Code, sobre:

`D:\.Proyectos Games\SkyrimNpcStudio-ui2`

Claude ya está implementando las correcciones críticas de seguridad. No modifiques sus archivos ni interfieras con sus compilaciones.

Tu responsabilidad inicial es implementar un laboratorio QA independiente, con pruebas reales, detección de regresiones, verificación de resultados y reportes reproducibles.

Establece el protocolo de coordinación descrito en el documento y utiliza exclusivamente tus carpetas asignadas.

Una vez estabilizada la fase crítica y recibida la autorización de transferencia, comenzarás el desarrollo del Settings Center y las mejoras de interfaz.

No quiero solamente análisis o documentación. Implementa código real, ejecútalo, corrige errores y entrega evidencia verificable.

No uses Git ni GitHub. No modifiques archivos originales de Skyrim ni perfiles MO2 habituales.

**Comienza ahora con la primera implementación funcional.** /using-agent-skills /agentkit-seo SKYRIM NPC STUDIO ULTIMATE — ANTIGRAVITY / CLAUDE EN PARALELO
Contrato de implementación real, aislamiento por archivos, laboratorio QA independiente y cesión controlada de módulos
Edición: 1.0 · Fecha: 2026-10-19
Ejecución prevista: Antigravity en Windows, con acceso real al disco local.
Único directorio de código autorizado: D:\.Proyectos Games\SkyrimNpcStudio-ui2
Papel principal de Claude: ingeniería del núcleo, seguridad, exportaciones y corrección de hallazgos críticos.
Papel principal de Antigravity: QA local independiente y automatización verificable; posteriormente Settings Center/UX, únicamente por cesión explícita.
Control de versiones: NO ejecutar Git ni utilizar GitHub, ni siquiera para inspeccionar ramas, estado o diff, mientras el propietario mantenga esta restricción.
Estado inicial de colaboración: NO COORDINADO hasta que ambos agentes acepten el reparto. Este documento no demuestra que Claude lo haya leído.
0. TU ORDEN OPERATIVA
Antigravity: TRABAJA EN MI PC, NO EN GITHUB. Claude Code ya está trabajando de manera activa sobre el mismo proyecto. Tu trabajo debe avanzar simultáneamente, sin interferir. No eres el supervisor de Claude ni puedes cancelar, editar, reemplazar o hacer rollback de sus cambios.

Tu primera misión será construir un laboratorio de QA y regresiones reales que permita comprobar las correcciones de seguridad y estabilidad que desarrolla Claude. En paralelo, prepara —sin integrar todavía en la aplicación— el diseño técnico verificable de Settings Center V4. Una vez liberado el bloqueo P0/P1 y entregada formalmente la propiedad de los archivos, implementarás el centro de configuración y las mejoras visuales reales. No saltes los gates por producir pantallas vistosas.

Valor esperado: mientras Claude corrige P0-01, F-10, F-11, F-12, F-13 y demás defectos, tú creas instrumentos independientes para detectar regresiones, evidenciar que los cambios funcionan y mantener trazabilidad de los resultados. Dos personas cambiando el mismo scene.cpp no equivalen a trabajar en paralelo de forma útil.

Regla central: ningún archivo fuente, configuración o salida mutable puede tener dos escritores simultáneos.
1. FUENTES OBLIGATORIAS, ORDEN DE LECTURA Y VERIFICACIÓN
1. SkyrimNpcStudio_Ultimate_Plan_Maestro_Revisado_Auditoria_2026-10-19.md (plan rector aprobado).
2. AUDIT_SENIOR_REPORT_v2.md (hechos y categorías F-10 a F-20, riesgos P0/P1).
3. audit-evidence.zip (fixtures y sondas deliberadamente dañados).
4. Archivos locales existentes del proyecto: README.md, AGENTS.md, docs/ARCHITECTURE.md, docs/DECISIONS_COVERAGE.md, docs/plans/2026-10-17-f0-f7-board.md, scripts/, tests/, apps/desktop/, engines/ y services/BethesdaWorker/.
Los tres primeros son documentos/adjuntos que el propietario debe proporcionar a Antigravity si no aparecen físicamente en la carpeta Windows. No simules su lectura ni supongas que /mnt/data existe en Windows. La documentación local puede estar desactualizada por trabajo en curso: comprueba fuentes y timestamps, sin alterar nada.

Límites de la auditoría: v2 complementa una v1 no necesariamente disponible. P0-01, P1-02, P1-03 se citan con esa limitación; no inventes una reproducción del informe v1. Los hallazgos F-10/F-11/F-12 se observaron bajo MinGW/Wine: requieren verificación propia en Windows/MSVC. Los tests de Qt, xUnit y ejecución dentro de Skyrim no fueron ejecutados en aquella auditoría.

Etiquetas obligatorias: [CONFIRMADO_LOCAL], [DOCUMENTADO], [INFERIDO], [NO_EJECUTADO], [BLOQUEADO]. Un resultado heredado de una auditoría no pasa a [CONFIRMADO_LOCAL] hasta ejecutar y comprobar su prueba.
2. FASE CERO: COMPROBAR EL DIRECTORIO SIN INTERFERIR CON CLAUDE
Trabaja solo en:
D:\.Proyectos Games\SkyrimNpcStudio-ui2
Accede mediante las herramientas del Antigravity Desktop que realmente estén disponibles: explorador de archivos, terminal PowerShell, editores, ejecución de comandos, herramientas de Windows y cualquier MCP previamente conectado y autorizado. Detecta cuáles existen; no inventes acciones o conectores. No es obligatorio instalar Serena, DesktopCommander, Everything, context7 ni otros MCP para empezar si el terminal y editor local ya funcionan.

Inspecciona en solo lectura:
- Existencia y contenido de CMakeLists.txt, CMakePresets.json, SkyrimNpcStudio.slnx, scripts/build.ps1, scripts/local-review.ps1, scripts/smoke-test.ps1, scripts/workbench-check.ps1.
- Versiones efectivas de pwsh/powershell, CMake, Ninja, cl.exe, Qt, dotnet, Python, compilador y Test Explorer sin modificar PATH global.
- Qué rutas pueden estar siendo editadas por Claude y qué carpetas de compilación se encuentran en uso.
- Disponibilidad real de AUDIT_SENIOR_REPORT_v2.md y audit-evidence.zip en el PC. Si no están, deja las reproducciones que dependan de esos archivos como BLOQUEADO_POR_INPUT y desarrolla primero las piezas independientes.
Prohibido ejecutar git status, git diff, git branch, git log, git add, git commit, git push, gh, GitHub MCP o peticiones a la API de GitHub. No uses git indirectamente a través de scripts que lo invoquen sin inspeccionarlos. El directorio es la fuente de verdad de esta sesión.

No instales versiones nuevas de SDKs ni complementos sin revisar antes fuentes, licencias, requisitos y autorización del propietario. No alteres las variables de entorno permanentes del sistema.
3. ACUERDO DE COORDINACIÓN ENTRE AGENTES (OBLIGATORIO)
3.1. Sin consentimiento mutuo no se tocan archivos compartidos
Claude podría estar modificando cualquier archivo porque empezó a trabajar antes de recibir esta orden. No supongas que tiene un módulo libre. El propietario debe enviarle el mensaje del Apéndice A. Hasta recibir esa aceptación —por conversación o por archivo de coordinación de Claude— Antigravity únicamente puede modificar archivos nuevos bajo sus rutas exclusivas, indicadas a continuación.

Crea, si no existen, las siguientes carpetas dentro de la raíz del proyecto (sin modificar carpetas ajenas):
.ai-coordination/
.ai-coordination/messages/
.ai-coordination/claims/
.ai-coordination/locks/
.ai-coordination/status/
tools/antigravity/
scripts/antigravity/
tests/antigravity/
docs/antigravity/
out/antigravity/
.ai-coordination/ es un buzón local de texto, no un sistema de permisos ni una API. Si hay contenido ya existente no lo borres. La copia de respaldo de archivos de Antigravity y todos sus logs van en out/antigravity/, no en las carpetas de Claude.
3.2. Tabla de propiedad explícita
Ámbito	Propietario mientras corrijamos la auditoría	Antigravity puede	Antigravity no puede
engines/nif/**, engines/face/**	Claude	leer; documentar evidencia	editar, formatear, revertir
engines/rendering/**, engines/assets/**, engines/mo2/**	Claude	leer y probar en entorno aislado	editar o cambiar cachés compartidas
services/BethesdaWorker/**	Claude	leer; diseñar pruebas externas	editar worker o cambiar los contratos RPC
apps/desktop/viewmodels/studio_controller.*, scene_viewmodel.*	Claude	leer y documentar	modificar mientras F-10/F-11/F-12 están abiertos
apps/desktop/qml/Main.qml, Theme.qml, CharacterViewport.qml	RESERVADO hasta asignación formal	inspeccionar; diseñar prototipos externos	editar sin permiso de Claude
apps/desktop/CMakeLists.txt, tests/CMakeLists.txt, scripts existentes	RESERVADO	leer y ejecutar cuando sea seguro	editar, formatear, registrar pruebas nuevas en ellos
tools/antigravity/**	Antigravity	crear pruebas/herramientas nuevas	incorporar software no verificado
scripts/antigravity/**	Antigravity	crear ejecutores PowerShell nuevos	editar ejecutores oficiales de Claude
tests/antigravity/**	Antigravity	crear tests y fixtures sintéticos	introducir fixtures corruptos en MO2
docs/antigravity/**, out/antigravity/**	Antigravity	generar reportes y evidencias	declarar PASS sin ejecutarlo
.ai-coordination/status/claude.json	Claude	leer	escribir o borrar
.ai-coordination/status/antigravity.json	Antigravity	escribir de forma atómica	copiar el estado de Claude como propio


Importante: los documentos y el plan previo no autorizan reasignar rutas por implicación. Los agentes deben aceptar el reparto; en caso de desacuerdo, aplica solo lectura sobre la ruta y pide decisión al propietario.
3.3. Mensajería local, sin fingir que existe chat entre agentes
Utiliza estos archivos:
.ai-coordination/messages/antigravity-to-claude.md
.ai-coordination/messages/claude-to-antigravity.md
.ai-coordination/status/antigravity.json
.ai-coordination/status/claude.json
.ai-coordination/claims/ownership.md
Cada agente escribe únicamente su mensaje y estado. El otro lo consulta de forma periódica (por ejemplo, al comienzo y al cierre de cada incremento). Crear un mensaje no demuestra que el destinatario lo leyó. Solicita una confirmación escrita, y no inicies trabajos sobre zonas compartidas hasta recibirla. No se necesita servidor web, Slack ni conexión externa.

Contenido mínimo de cada mensaje: hora local con zona, emisor, objetivo, rutas exactas que solicita reservar, rutas que va a leer, posibles cambios en tests/build y criterio de aceptación. Para conflictos: CONFLICT, WAITING, ACCEPTED, DONE, BLOCKED con detalle, no mensajes vacíos.
3.4. Exclusividad de recursos: compilación, pruebas pesadas y ejecución de la app
Aunque editen archivos diferentes, dos builds completas sobre el mismo árbol pueden competir por CPU/RAM, el worker .NET, salidas generadas o cachés. Adopta estas reglas:
1. Usa otra carpeta de build exclusivamente para Antigravity, por ejemplo out/antigravity/build-qa/, nunca build/debug, build/package, bin o dist si Claude los usa.
2. Para CTest, lanza siempre el ctest de esa carpeta de build; no corras pruebas desde el árbol de Claude.
3. Para pruebas UI, usa un SNS_SETTINGS_DIR, SNS_DIAGNOSTICS_DIR, SNS_PORTRAIT_CACHE_DIR, SNS_LOCAL_TEST_DIR, TEMP y rutas de evidencia distintas si dichas variables son soportadas realmente; inspección previa obligatoria.
4. Cuando un arnés pueda abrir/ejecutar Skyrim, crear o instalar un mod, no lo ejecutes automáticamente; usa mocks/sandbox solo para pruebas del arnés y etiqueta la aceptación dentro del juego como pendiente.
5. Limita compilaciones pesadas a una a la vez por defecto. Crea una reserva exclusiva temporal documentada en .ai-coordination/locks/heavy-build.lock usando creación atómica (FileMode.CreateNew), no if(Test-Path)...New-Item con carrera. Si ya está ocupado, trabaja en otra tarea. Nunca borres un lock de Claude por cuenta propia.
6. Si falla un proceso de prueba con NIF hostiles, inspecciona el proceso real y sus límites de memoria antes de volver a ejecutar. No cargues el corpus hostil en el proceso QML principal.
7. Si una fuente crítica cambia entre inicio y fin de una prueba, marca la prueba como INVALIDATED_BY_CONCURRENT_EDIT, aunque el programa haya devuelto exit code 0.
3.5. Tratamiento de edición concurrente y copias
- Antes de editar un archivo que sí poseas, calcula SHA-256, guarda copia local y anota timestamp.
- Inmediatamente antes de escribir comprueba que SHA-256 sigue igual; si cambió, vuelve a leer y reevalúa el parche. No sobrescribas.
- Al escribir reportes, utiliza archivo temporal y reemplazo atómico cuando sea posible; nunca trunques un archivo ajeno.
- No uses búsqueda y reemplazo global sobre el repositorio, autoformateo del proyecto, cmake --build --clean-first sobre directorios de Claude o tareas de limpieza de carpetas compartidas.
- Un «hice backup» no permite tocar archivos ajenos. Una prueba que falla no autoriza revertir los cambios de Claude.
3.6. Protocolo de transferencia de archivos (handoff)
Si Antigravity necesita modificar Main.qml, CMakeLists.txt o un viewmodel común:
1. Crear solicitud con nombres exactos de archivos y motivo en antigravity-to-claude.md.
2. Claude debe responder ACCEPTED y declarar que ha terminado de escribir esos archivos durante la ventana acordada.
3. Registrar baseline SHA-256 y copiar a respaldo de Antigravity.
4. Antigravity modifica y prueba únicamente las rutas autorizadas.
5. Antigravity entrega lista de archivos, hashes finales y resultados.
6. Claude reconoce la entrega y se libera la reserva.
Si Claude no puede aceptar porque está integrando F0, Antigravity mantiene su tarea local y NO intenta forzar la integración.
4. OBJETIVO PARALELO INMEDIATO — ANTIGRAVITY_QA_LAB
4.1. Qué construirás realmente
Implementa un conjunto funcional de herramientas locales de control de calidad independientes del código productivo, instalables por simple copia, que inspeccionen resultados de compilaciones y tests, recojan evidencias verificables, detecten cambios concurrentes e informen qué gates F0/S0-S3 se cumplen.

Archivos nuevos sugeridos (si un archivo ya existe, inspección y coordinación antes de tocarlo):
scripts/antigravity/Invoke-SnsQaBaseline.ps1
scripts/antigravity/Invoke-SnsRegression.ps1
scripts/antigravity/Test-SnsEvidence.ps1
scripts/antigravity/Write-SnsQaReport.ps1
scripts/antigravity/Get-SnsSourceFingerprint.ps1
scripts/antigravity/Request-SnsBuildSlot.ps1
scripts/antigravity/Release-SnsBuildSlot.ps1
scripts/antigravity/Invoke-SnsSafeCorpusTest.ps1
tools/antigravity/qa_report.py       # opcional: Python stdlib para resultados JSON/HTML
tests/antigravity/test_qa_manifest.ps1
tests/antigravity/test_concurrent_change.ps1
tests/antigravity/test_invalid_evidence.ps1
docs/antigravity/QA_RUNBOOK.md
docs/antigravity/KNOWN_BLOCKERS.md
out/antigravity/runs/<RUN_ID>/...
Los nombres anteriores son archivos a crear, no funciones existentes. Si no puedes implementar alguna pieza de manera real, explica el bloqueo y no la marques como completada.
4.2. Invoke-SnsQaBaseline.ps1: línea base concreta
Debe:
- Identificar herramientas locales, versiones, rutas de CMake/dotnet y arquitectura x64.
- Leer de forma no destructiva los manifests y configuración para descubrir comandos reales.
- Calcular hashes SHA-256 del subconjunto de fuentes de cada prueba antes y después.
- Ejecutar una prueba corta no destructiva en su propia carpeta si existen build y dependencias; en caso contrario preparar su carpeta de build sin tocar la de Claude.
- Registrar command line, working directory, exit code, inicio/fin, stdout/stderr y versión de toolchain.
- Informar PASS, FAIL, SKIP, BLOCKED, INVALIDATED_BY_CONCURRENT_EDIT, no un booleano genérico.
- Evitar copiar a logs datos privados del perfil MO2, tokens, nombres reales de usuario o rutas personales completas cuando no sean imprescindibles; redacción de rutas en reporte compartible.
Criterio: dos corridas consecutivas dejan dos manifest JSON distintos con hashes y comandos correctos; un fallo intencional hace fallar el runner, no produce un falso PASS.
4.3. Invoke-SnsRegression.ps1: pruebas existentes, sin reescribirlas
Descubre las suites instaladas y argumentos reales antes de invocarlas; considera ctest --preset ... con -C donde corresponda y dotnet test si la solución existe. No asumas que build.ps1 no llama Git: inspección previa. Si corre sobre una build existente creada por Claude, exige que no se esté escribiendo en ella y registra el hash del binario antes/después.

Reutilizar sin duplicar:
- Tests Catch2/CTest de motores y aplicación.
- Suites xUnit del worker, exportadores y consultas.
- scripts/smoke-test.ps1 y scripts/workbench-check.ps1 solo cuando haya aislamiento garantizado.
- Pruebas de lectura NIF y FaceGen que Claude añada durante F0.
- Generación de release solo para verificar evidencia ya creada, sin escribir dist ni ejecutar instaladores.
La función no inventará resultados de CI ni afirmará que el juego fue ejecutado por un código que solo corre CTest.
4.4. Get-SnsSourceFingerprint.ps1: detectar contaminación de una prueba
A partir de una lista acotada de archivos conocidos (por ejemplo loaders NIF, worker y conjunto de pruebas), calcula SHA-256 al inicio y final. Registra paths relativos, size, last-write-time y hashes. No intentes hashear todo MO2: puede ser enorme y sus archivos no son propiedad del proyecto.

Si durante las pruebas Claude modifica cualquiera de sus entradas esenciales:
result = INVALIDATED_BY_CONCURRENT_EDIT
reason = "Input source changed during test run"
Esto protege contra el error de declarar «verde» una build que no corresponde al estado actual de los fuentes.
4.5. Test-SnsEvidence.ps1: auditoría independiente de la evidencia
La herramienta debe comprobar que un reporte de PASS realmente tenga:
- Fecha/hora, comando, carpeta, exit code = 0, log existente, hash válido.
- Identificador de fuente o build verificable y coherente en todas sus pruebas.
- Pruebas omitidas contadas explícitamente (SKIP != PASS).
- Para ZIP o binarios, hash del artefacto que coincide con el del manifest.
- Para «verificado en Skyrim», captura humana y acta vinculada al hash del build y perfil; si no existe, exigir PENDIENTE_SKYRIM.
- Para «53 NPC revisados», decisiones individuales efectivamente presentes y vinculadas a sus entradas; no escribir ni aprobar decisiones por el usuario.
Nunca permita promover estado PASS_STATIC a PASS_IN_GAME automáticamente.
4.6. Write-SnsQaReport.ps1: informe offline legible
Genera al menos:
out/antigravity/runs/<RUN_ID>/summary.json
out/antigravity/runs/<RUN_ID>/summary.md
out/antigravity/runs/<RUN_ID>/index.html   (opcional, offline, sin CDN)
Contenido: hallazgo, severidad, responsable del módulo, prueba ejecutada, resultado, razones de SKIP/BLOCKED, evidencia, cambios concurrentes, hallazgos reabiertos, gate actual y próximo paso. No incluyas logos falsos, gráficas ficticias ni «salud 100%» sin fórmula explícita.
4.7. Corpus deliberadamente corrupto: reproduce SIN afectar el sistema
audit-evidence.zip incluye en audit-evidence/repro-inputs/:
- oom_238b.nif
- bad_lengtherror.nif
- regex_overflow.nif
- facegen_lengtherror.nif
- jslot_float_overflow.jslot
- jslot_type_error.jslot
Son archivos hostiles de prueba, jamás recursos para Skyrim. Antigravity no debe copiarlos dentro de Data, MO2, perfiles del usuario ni sus partidas.

Antes de extraer un ZIP, inspecciona entradas (rutas absolutas, .., enlaces) para evitar escritura fuera de out/antigravity/fixtures/. No ejecutes scripts contenidos en el ZIP como confiables. Calcula hashes de cada fixture.

oom_238b.nif llegó a provocar aproximadamente 4,2 GB en la auditoría con Wine; no lo ejecutes sin un proceso separado y una cuota de memoria real comprobada (Windows Job Object/limitador equivalente) más timeout. Si no puedes imponer límites, marca la prueba BLOCKED_UNSAFE_TO_RUN y realiza solo una inspección estática del encabezado. Un try/catch no detiene consumo de memoria ni stack overflow.

Criterio de cierre F-10: con los fixes reales de Claude, corpus rechazado a tiempo, sin que el proceso padre/QML se cierre, y errores individuales registrados. No eleves resultados MinGW a evidencia MSVC sin ejecución en este toolchain.
4.8. Auditoría P0-01/F-13 solo con directorios ficticios
Crea fixtures sintéticos exclusivamente dentro de out/antigravity/fixtures/mock-game/, por ejemplo:
mock-game/
  Skyrim Special Edition/
    Data/
  Mod Organizer 2/
    mods/
      ExistingMod/
        existing.bin
    overwrite/
    profiles/
  safe-output/
Nunca usas MO2 real para demostrar que una ruta debe rechazarse. Un test de seguridad correcto comprueba no modificación de existing.bin (hash anterior = posterior), incluso cuando la exportación se rechaza. Si no existe una interfaz de prueba segura en el software para invocar la ruta de instalación/exportación, solicita a Claude agregarla dentro de su área de propiedad, y por mientras marca el test BLOCKED_MISSING_SAFE_ENTRYPOINT.

Incluye rutas como: Data/Exports, mods/, mods/ExistingMod, overwrite/X, .. y junctions NTFS solo cuando se creen y limpien dentro de ese árbol sintético y existan permisos/soporte adecuados. No atribuyas seguridad general a tres rutas probadas.
4.9. Control F-11/F-12, sin editar scene.cpp
- Prepara escenarios y fixtures para que Claude pruebe su safe_load y el catch del hilo de escaneo.
- Integra resultados de pruebas nuevas del motor y del arnés al reporte independiente.
- Si el fichero malo destruye toda la escena en vez de registrar MeshUnreadable, devuélvelo como FAIL con entrada y comando reproducibles.
- Si el escaneo de un NPC lanza excepción sin controlar, reporta FAIL en vez de añadir silenciosamente un catch a código de Claude.
- Si el proceso termina inesperadamente, adjunta exit code y logs disponibles, no reinicies agresivamente ni modifiques el perfil real para «probar otra vez».
4.10. Definición de terminado para ANTIGRAVITY_QA_LAB
Debe haber herramientas ejecutables y pruebas de esas herramientas, no solo Markdown:
- [ ] Runner local funciona sin Git/GitHub.
- [ ] Salidas en directorio aislado y reproducibles.
- [ ] Detecta automáticamente exit code no cero.
- [ ] Distingue PASS/FAIL/SKIP/BLOCKED.
- [ ] Detecta cambios concurrentes de fuente y invalida el resultado.
- [ ] Verifica integridad de hashes de los artefactos.
- [ ] No promueve pruebas locales a «Skyrim verificado».
- [ ] No extrae fixtures fuera de su sandbox ni ejecuta corpus OOM sin límites.
- [ ] Puede registrar una corrida real de CTest o xUnit sin tocar las carpetas de build de Claude.
- [ ] Reporte de run incluye logs, versión de herramientas y fuente comprobable.
5. PAQUETE PARALELO SECUNDARIO — ESPECIFICACIONES DE QA PARA CLAUDE
Al completar la base del laboratorio, no esperes ociosamente a Claude. Trabaja en tareas complementarias que no requieren editar su código:
Paquete	Entregable de Antigravity	Qué debe entregar Claude	Qué NO hacer
AG-P0-INSTALL	Matriz de pruebas con rutas MO2 sintéticas, junction, overwrite y hash de datos originales	bloquear instalación insegura y reparar rutas de salida	cambiar instalador que Claude está editando
AG-F10-CORPUS	corpus verificado, runner protegido, contrato de error, límites efectivos y timing	prevalidación NIF/FaceGen y confinamiento nifly	llamar al corpus hostil desde QML
AG-F11-SCAN	escenario controlado y reporte de excepción/continuación	try/catch/resultado por NPC en escaneo	modificar studio_controller.cpp
AG-F12-SCENE	prueba de resiliencia de escena y visualización de errores	recuperación por mesh, sin perder cuerpo/cabeza	cambiar scene.cpp
AG-F13-PATHS	matriz de targets ilegales + checks no-write	validación unificada en worker	editar ModPackager.cs
AG-F14-LOCK	benchmark con consultas concurrentes durante exportación sintética	snapshot inmutable / lock mínimo	falsear latencias en UI
AG-F15-F20	fixtures y regresión para metadata, flags, caché y strings	fixes en workers/motores	generalizar sin evidencia
AG-MO2-SKYRIM	validador de actas, hashes y completitud de kits	generar kits verificables para perfil desechable	iniciar Skyrim o aprobar el acta del usuario


En todos los casos, Antigravity es consumidor de la API/artefacto real, no dueño de la implementación productiva.
6. PAQUETE FUTURO TRAS LOS GATES — ANTIGRAVITY_SETTINGS_CENTER
Esta fase NO puede sustituir las correcciones críticas. Solo comenzarla cuando Claude haya informado que S0/S1 están estabilizadas, el laboratorio confirme que las pruebas fundamentales pasan y se acepte el traspaso de los archivos QML/C++ implicados.
6.1. Valor real para la app
Centro integral de Configuración (interfaz y comportamiento real), inspirado funcionalmente en preferencias de VS Code/Blender, sin copiar sus assets. Debe ser accesible desde el menú Vista o Herramientas, la paleta de comandos y la barra de estado. Mejorar el estado actual de ScalePreferencesDialog.qml y WorkbenchState sin duplicar preferencias.
6.2. Matriz de opciones configurables
Página	Preferencias que deben funcionar	Dependencia real
Apariencia	tema oscuro/claro/alto contraste, acento, fuentes instaladas, escala global, densidad, iconos	Theme.qml, WorkbenchState y QSettings
Layout	explorador/inspector/consola, ancho por workspace, maximización del visor, layouts nombrados, reset granular	WorkbenchSplit.qml, WorkspaceLayout
Texto por zona	fuente/escala para explorador, visor, inspector, consola, accesibilidad	ZoneAttached, WorkbenchState::setZoneFont
Visor 3D	FOV, sensibilidad orbital, iluminación, fondo, grid, mostrar huesos y calidad de texturas	SceneViewModel/QML; exponer ajustes solo si motor los respeta
Retratos NPC	calidad/tamaño, capacidad de caché, regeneración, prioridad de render, cancelación	PortraitRenderer, PortraitCache; preservar fuente studio-render
Skyrim/MO2	rutas visibles, lectura de perfil, selector de perfil, test de conectividad de solo lectura	MO2 VFS; no activar instalación directa mientras P0-01 abierto
Proyectos	carpeta de trabajo, guardado seguro, snapshots, conflictos de autosave	.snps, .sncf; nunca cambiar proyectos al alternar tema
Rendimiento	límites efectivos de cache, workers, texturas, operaciones en segundo plano	motores existentes y límites medidos; no presets simulados
Accesibilidad	teclado, focus rings, tamaños mínimos, alto contraste, ayudas	QML/Qt; pruebas de teclado
Atajos	buscador y gestor de atajos, detección de conflictos, restablecimiento	centralizar Action y shortcut commands
Diagnósticos	nivel de log, exportar bundle sin secretos, pantalla de estado	diag::*, out/antigravity para QA, rutas de logs reales
IA/extensiones	proveedores opcionales, permisos y estado REAL de integración	no generar API keys, no crear interruptores para IA inexistente


No agregues «activar SMP real» si solo se interpreta XML y se muestra pose estática. En ese caso debe etiquetarse «Inspección SMP (sin simulación)».
6.3. Fase de implementación de Settings (tras handoff)
1. Inspecciona de nuevo en el PC apps/desktop/viewmodels/workbench_state.*, apps/desktop/qml/Main.qml, Theme.qml, ScalePreferencesDialog.qml, apps/desktop/qml/components/, zone_scale.* y tests/app/test_workbench_state.cpp.
2. Documenta qué claves QSettings existen, scope por perfil, defaults, límites y comportamiento real. Evita introducir un segundo registro de preferencias para lo mismo.
3. Solicita propiedad de un conjunto mínimo de archivos. Primer incremento puede vivir en apps/desktop/qml/settings/** y apps/desktop/viewmodels/settings_center_* con un único punto de integración en Main/CMake cedido por Claude.
4. Implementa SettingsCenter navegable y buscable; cada control llama a un setter real, muestra su valor actual y guarda configuración. Si un setter no existe aún, no mostrar el control como activo: deja feature en backlog o implementa backend dentro del área cedida.
5. Añade importar/exportar preferencias mediante esquema versionado sin credenciales y con validación, previsualización de cambios y restauración segura.
6. Implementa temas y tokens reactivos de forma incremental; verifica contraste en estados disabled/hover/focus, evita doble factor HiDPI y que el usuario no pueda leer las pestañas a escala máxima.
7. Añade gestión de atajos centralizada solo cuando exista una definición de comandos reales y pruebas de colisiones. No uses hotkeys globales que entren en conflicto con Skyrim o MO2.
8. Agrega tests propios y después integra a CTest/CMake únicamente por handoff. Ejecuta casos 1100×700, 1366×768, 1920×1080, DPI lógico y Qt escala real; si no dispones de monitor DPI físico, decláralo pendiente.
9. Ejecuta UI real, captura ventana visible y verifica persistencia tras reinicio. No confundir capturas de menú con prueba de backend.
10. Entrega reporte al laboratorio QA independiente y confirma que F0 no sufrió regresiones.
6.4. Archivos reservables tras autorización
apps/desktop/qml/settings/**              # nuevos
apps/desktop/viewmodels/settings_center_*  # nuevos
apps/desktop/qml/components/Settings*.qml # nuevos
apps/desktop/viewmodels/workbench_state.*  # existentes: requieren cesión
apps/desktop/qml/Theme.qml                # existente: requiere cesión
apps/desktop/qml/Main.qml                 # existente: requiere cesión
apps/desktop/CMakeLists.txt               # existente: requiere cesión
No modifiques de forma automática estas rutas mientras Claude las use. Si no concede propiedad, conserva el trabajo como módulo externo probado y documenta que falta su integración.
6.5. Criterios de aceptación Settings
- Cambiar opción produce efecto real sin reiniciar si la arquitectura lo permite.
- Persistencia tras cerrar/abrir y compatibilidad hacia adelante de claves.
- Reset granular no borra proyecto ni perfil MO2.
- Controles no soportados no aparecen como funcionales.
- Datos sensibles no se exportan con preferencias.
- No hay bindings cíclicos, warnings QML, recorte de textos ni pérdida de foco.
- Build Windows y pruebas pertinentes pasan; evidencia en el laboratorio QA.
7. OTROS MÓDULOS FUTUROS QUE ANTIGRAVITY PUEDE TOMAR, UNO A LA VEZ
Solo después del acuerdo de propiedad y los gates del Plan Maestro. La intención no es multiplicar tareas abiertas, sino dejar clara la especialización de cada agente.
Prioridad posterior	Área para Antigravity	Interfaz de frontera con Claude	Verificación
U1	Settings, accesibilidad, temas y layouts	contratos de preferencias	persistencia + UI + DPI
U2-A	Catálogo visual SMP con diagnóstico correcto	RPC de metadatos del worker diseñado por Claude	caso con XML SMP sin palabras en nombre/ruta
U2-B	Visualización de shapes ocultos y motivos	RenderPart definido por Claude	comparativas de mallas visibles y colisionadores
D17	Comparador de retratos editor vs captura real	caché y origen studio-render	fuente de cada imagen verificable
D22	Centro visual de evidencia de QA	JSON de ANTIGRAVITY_QA_LAB	no confundir PASS Windows con PASS Skyrim
D33	Localización y accesibilidad	keys QML y comandos de UI	teclado, contraste, texto desbordado


Exclusiones mientras no exista prueba de concepto aprobada: motor real SMP/FSMP, reescritura del renderer NIF, cambios invasivos de shaders, nueva arquitectura de worker, IA autónoma conectada a sistema de archivos sin política de permisos.
8. PLAN DE EJECUCIÓN PARA LAS DOS IAs EN CONJUNTO
Etapa P0 — Inmediata (ambos activos)
Claude: arregla P0-01, F-13, F-10/F-11/F-12 con implementación real y tests en sus archivos.

Antigravity: crea laboratorio QA independiente, recoge evidencia de la compilación y verifica automáticamente cuándo los tests de seguridad pueden ejecutarse. Desarrolla y prueba el propio runner con fixtures sintéticos benignos incluso si Claude todavía está modificando fuentes.

Intercambio: Claude expone comandos y artefactos de prueba; Antigravity devuelve fallos concretos, logs y hashes. Ninguno «aprueba» el trabajo del otro por cortesía.
Etapa P1 — Resiliencia y aceptación
Claude: corrige F-14…F-20, produce los candidatos locales de release; preserva el control humano y evita Git.

Antigravity: ejecuta suites separadas contra builds inmutables, prueba carreras de estado por evidencia, crea reporte consolidado de seguridad, comprueba hashes de paquetes y prepara formularios de prueba MO2/Skyrim sin ejecutarlos sobre perfil real.

Gate: usuario valida Skyrim cuando corresponda; CI/GitHub queda pendiente por política del propietario.
Etapa P2 — Funcionalidad visual
Claude: estabilidad de motor, exportación Multi-NPC segura, contratos JSON-RPC.

Antigravity: recibe propiedad temporal del Settings Center y lo implementa con pruebas de QML y Settings; después catálogo visual SMP y mejoras del inspector, siempre con contrato estable entregado por Claude.

Gate: no se integra un cambio visual que degrade F0, exportación o lectura de NIF.
Etapa P3 — Evolución 2027–2029
Mantener D1–D36, pero con fronteras iguales: Claude es dueño de motores/worker/formatos; Antigravity de experiencia de escritorio/QA/instrumentación; todas las transferencias de archivos compartidos son explícitas. Decisión 27D (agentes autónomos) solo en sandboxes y nunca elimina las aprobaciones ni valida in-game por inferencia.
9. PRIMER CICLO OBLIGATORIO DE ANTIGRAVITY: ACCIÓN, NO DISCURSO
Ciclo 1 — Inspeccionar y reservar
1. Comprueba la carpeta exacta.
2. Lee el plan maestro y audit v2 recibidos; si faltan en Windows, registra bloqueo de evidencia adjunta.
3. Localiza scripts, CMake y pruebas actuales.
4. Crea únicamente las carpetas de Antigravity y su mensaje a Claude.
5. Espera su confirmación solo para archivos compartidos; el QA en carpetas exclusivas puede avanzar de inmediato.
6. Registra herramientas disponibles, versión y estado de baseline.
Entregable mínimo: directorios propios, QA_RUNBOOK.md, inventario verificado y antigravity-to-claude.md con solicitud de fronteras.
Ciclo 2 — Primera implementación ejecutable
1. Programa Get-SnsSourceFingerprint.ps1 y sus tests.
2. Programa Invoke-SnsQaBaseline.ps1 (sin Git).
3. Introduce un fallo intencional en un fixture de test de Antigravity, no en el proyecto.
4. Demuestra que el runner lo detecta como FAIL.
5. Ejecuta una prueba legítima disponible o documenta por qué está bloqueada.
6. Registra hashes, salida y resultados exactos.
Entregable mínimo: script real + suite propia + manifest JSON + evidencia de un PASS y un FAIL controlado.
Ciclo 3 — Evidencias de la corrección de Claude
1. Lee claude-to-antigravity.md o la confirmación expresa del propietario.
2. Consume artefactos estables y resultados de build de Claude.
3. Ejecuta regresión externa de F-10/F-11/F-12 según seguridad del harness.
4. Compara hashes de fuentes antes/después.
5. Reporta claramente fallos, invalidaciones y gates pendientes.
6. Envía a Claude los bugs precisos, sin editar su código.
Entregable mínimo: summary.json, summary.md y registro de evidencia trazable.
Ciclo 4 — Settings Center tras autorización
No comiences antes de superar S0/S1 + handoff. Mientras esperas, puedes mejorar scripts QA y cobertura o preparar un prototipo QML separado en tools/antigravity/, etiquetado como prototipo no integrado, no como función ya disponible.
10. POLÍTICA DE SEGURIDAD Y CRITERIOS DE ACEPTACIÓN
- Nunca tocar originales Skyrim/Data, MO2 real mods/, overwrite/, perfiles habituales ni saves personales.
- Nada de instalación automática, publicación ni merge. No usar Git/GitHub.
- Nunca ejecutar audit-evidence/repro-inputs/*.nif hostiles sin aislamiento real.
- Mantener la revisión individual histórica de 53 NPC ambiguos; no asumir que la cifra de otro perfil coincide con 53; no aprobar ninguno.
- F0 gate: no debe quedar P0-01 abierto ni NIF hostiles capaces de tumbar el proceso en casos reproducibles; deben existir pruebas MSVC/Windows donde aplique.
- Cada etapa debe presentar comandos reales, status, versión de build, hashes, salida y rutas de los archivos modificados.
- No generar pantallas decorativas, selectores sin backend, scripts con PASS hard-coded ni reportes que omiten errores.
- Si una función requiere cambios sobre archivos de Claude, detente en esa función concreta, solicita cesión y continúa en un módulo que controles.
- Respeta identidad de origen de los retratos (studio-render != captura de Skyrim), y limita el alcance oficialmente a Skyrim AE 1.6.1170 y MO2.
- Prioriza costo de GPU/RAM en paralelo; no lances dos stress-tests gráficos o fuzzers a la vez sin un motivo y capacidad comprobada.
Definition of Done — cualquier entrega Antigravity
Condición	Evidencia requerida
Código real	archivo + entrada ejecutable/invocación + comportamiento visible en consola/UI
Prueba real	comando, exit code, logs y fecha
Aislamiento	ruta de build/fixture propia, lista de archivos no tocados
Integridad	hashes de source inputs y artefactos; detecta cambios concurrentes
Resultado	PASS/FAIL/SKIP/BLOCKED por caso
Riesgo	casos pendientes, permisos, si hace falta intervención de Claude
Estado Git	NO EJECUTADO POR ORDEN DEL PROPIETARIO
Estado Skyrim	PENDIENTE_SKYRIM salvo acta auténtica del usuario


11. FORMATO OBLIGATORIO DEL REPORTE A MI CHAT
Usa un informe concreto, breve y verificable, cada vez que completes un ciclo:
ANTIGRAVITY — CICLO N
Ruta real verificada: D:\.Proyectos Games\SkyrimNpcStudio-ui2
Coordinación Claude: WAITING / ACCEPTED / BLOCKED

OBJETIVO: ...
IMPLEMENTADO REALMENTE: ...
ARCHIVOS NUEVOS/MODIFICADOS: [ruta exacta + dueño]
COMANDOS EJECUTADOS: [comando + cwd + exit code]
PRUEBAS: PASS=n / FAIL=n / SKIP=n / BLOCKED=n
EVIDENCIAS: [rutas locales + SHA-256]
ARCHIVOS DE CLAUDE MODIFICADOS: NINGUNO (o detalle de handoff aprobado)
RIESGO O BLOQUEO: ...
PRÓXIMO INCREMENTO: ...
ESTADO SKYRIM: PENDIENTE DE VALIDACIÓN HUMANA
GIT/GITHUB: NO UTILIZADO
Si no tienes acceso al PC, dilo y no inventes que creaste archivos allí. Si estás trabajando en una sesión de Antigravity que sí accede al PC, demuestra ese acceso mediante Test-Path/listado real y resultados de código.
12. MENSAJE LISTO PARA CLAUDE (EL PROPIETARIO DEBE ENVIÁRSELO)
Claude, a partir de ahora Antigravity va a trabajar simultáneamente en D:\.Proyectos Games\SkyrimNpcStudio-ui2. No detengas las correcciones de seguridad que ya realizas. Te asigno en exclusividad el código productivo F0/S0-S2: loaders NIF/FaceGen, scene, escaneo, MO2/instalador, worker .NET, exportaciones y sus tests internos. Antigravity creará el laboratorio QA independiente en tools/antigravity/, scripts/antigravity/, tests/antigravity/, docs/antigravity/ y out/antigravity/; no modifiques esos directorios. Lee el archivo de coordinación que deje en .ai-coordination/messages/antigravity-to-claude.md y confirma aceptación escribiendo .ai-coordination/messages/claude-to-antigravity.md. No toques los archivos propiedad del otro agente; no compartan carpetas de build, tests pesados ni cachés. Reserva Main.qml, Theme.qml, CMakeLists.txt y archivos comunes para cesión explícita. Cuando superemos F0, te pediré transferir el módulo Settings Center a Antigravity. Ninguno usará Git/GitHub hasta mi autorización. No cambies recursos Skyrim/MO2 reales ni apruebes los 53 NPC ambiguos. Si hay conflicto, pausa solo la ruta compartida y sigue con tu propio módulo.

Recordatorio: este mensaje es necesario para que el reparto sea real. Si no se entrega, Claude no tiene por qué conocer los límites que este archivo le asigna.
13. ORDEN FINAL A ANTIGRAVITY — EJECUTA YA
COMIENZA AHORA.
1. Inspecciona D:\.Proyectos Games\SkyrimNpcStudio-ui2 con tus herramientas locales reales.
2. Confirma que Claude tiene la exclusividad del núcleo crítico y solicita aceptación de coordinación.
3. Construye hoy la primera versión funcional del QA Lab en scripts/antigravity y tests/antigravity, sin tocar código de Claude.
4. Implementa fingerprints SHA-256, runner con exit codes, resultados JSON, bloqueo de recursos pesados y prueba de invalidación por cambio concurrente.
5. Demuestra un test válido y uno fallido intencionado sobre fixtures propios, con logs y hashes.
6. Consume y verifica de manera independiente los resultados que Claude produzca de seguridad F0.
7. No avances al Settings Center integrado hasta superar los gates críticos y recibir handoff; trabaja en QA mientras tanto.
8. Conserva todas las 36 decisiones y el plan revisado, sin reescribir la aplicación ni reconstruir la arquitectura.
9. No uses Git, GitHub, acciones remotas ni recursos del Skyrim/MO2 original.
10. Entrégame cada mejora con archivo real, comando, resultado y evidencia. No entregues solo planes.
RESULTADO ESPERADO: Claude corrige; Antigravity valida, aporta herramientas ejecutables y después construye la interfaz. Ambos trabajan realmente sobre el PC, simultáneamente, con exclusividad por archivos y evidencias cruzadas.
</USER_REQUEST>
<ADDITIONAL_METADATA>
The current local time is: 2026-10-19T10:46:50-03:00.

The user has mentioned some items in the form @[ITEM]. Here is extra information about the items that were mentioned by the user, in the order that they appear:

/using-agent-skills is a [Slash Command]:
<SKILL>The user requested you read and use the "using-agent-skills" skill. The path to the skill file is:
C:\Users\juanc\.gemini\config\plugins\agent-skills\skills\using-agent-skills\SKILL.md</SKILL>
/agentkit-seo is a [Slash Command]:
<SKILL>The user requested you read and use the "agentkit-seo" skill. The path to the skill file is:
C:\Users\juanc\.gemini\config\skills\agentkit-seo\SKILL.md</SKILL>
</ADDITIONAL_METADATA>