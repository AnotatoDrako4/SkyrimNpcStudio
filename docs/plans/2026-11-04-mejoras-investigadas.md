# Plan: mejoras investigadas (noviembre 2026)

- **Fecha:** 2026-11-04
- **Base:** `main` @ `66096f5` con cambios locales sin commit (Character Forge, easy mode, etc.).
- **Estado:** M1 `INTEGRACION_WINDOWS_OK`; M2 `EN_DESARROLLO` (motor y worker hechos, falta la app; ver [`docs/qa/MEJORAS_2026-11_ESTADO.md`](../qa/MEJORAS_2026-11_ESTADO.md)); M3–M7 `NO_INICIADO`.
- **Origen:** investigación web del 2026-11-04 contrastada con el código. Cada fase cita la fuente externa
  y el punto del repositorio que la justifica. Lo que solo tiene fuente comunitaria está marcado así.

## 0. Reglas que este plan no cambia

- Se trabaja en worktree y rama aislados (`rc/mejoras-2026-11`) creados **después** de que el dueño haga
  commit del trabajo local actual. El plan no hace commit, merge ni reset sobre `main`.
- Nada escribe en `Skyrim/Data`, en `mods/` u `overwrite` de MO2 ni en el perfil habitual. La única fase
  que llega a MO2 (M3) lo hace a través del instalador de MO2, en un perfil de prueba marcado por el
  usuario y con autorización humana específica.
- Estas fases no sustituyen a F0. M1, M2, M5 y M6 son herramientas y compatibilidad sobre funciones ya
  existentes; M3 y M4 añaden rutas de escritura nuevas y por eso esperan a que F0 supere sus gates.
- Ninguna fase marca `VERIFICADO_EN_SKYRIM`: como mucho deja preparado el kit y el acta.

## Orden y dependencias

| Fase | Qué | Prioridad | Tamaño | Depende de | Permiso extra |
|---|---|---|---|---|---|
| M1 | Compatibilidad PyNifly 30.x — `INTEGRACION_WINDOWS_OK` | P0 | M | — | Blender + PyNifly locales |
| M2 | Kit de prueba en juego (`bat`) — `EN_DESARROLLO` | P1 | M | — | — |
| M3 | Plugin MO2 de instalación supervisada | P1 | L | M2, F0 | Perfil MO2 de prueba, aprobación del dueño |
| M4 | Exportación de atuendo vía SkyPatcher | P2 | M | F0, readme oficial de SkyPatcher | — |
| M5 | Snapshots legibles de los ESP exportados | P2 | S | — | — |
| M6 | BodySlide oficial como oráculo de pruebas | P2 | M | — | BodySlide local |
| M7 | MorphTarget en el visor | P3 | M | Medición previa | — |

Descartado tras la investigación (ver §9): actualizar Mutagen o nifly, ampliar el rango ESL y escribir
un generador propio de FaceGen.

---

## M1 — Compatibilidad con PyNifly 30.x (P0)

**Problema confirmado.**
- `tools/blender/forge_skyrim.py:35` solo da por probado `PYNIFLY_TESTED_VERSIONS = {(29, 1, 0)}`.
- PyNifly 30.0.0 (27 oct 2026) quitó hkxcmd y lee/escribe HKX de forma nativa; 30.3.0 salió el 4 nov 2026
  ([releases](https://github.com/BadDogSkyrim/PyNifly/releases)).
- En la rama `main` de PyNifly, `get_bone_global_xf` tiene ahora la firma
  `get_bone_global_xf(arma, bone_name, game, use_pose, rotations=None)` y `game_rotations` es un alias de
  `game_rotations_pretty` (`io_scene_nifly/blender_defs.py`).
- ~~En su `__init__.py`, la preferencia `rotate_bones_pretty` se describe ahora como *"Display only…"*.~~
  **Corrección (2026-11-04, al implementar):** ese texto ya estaba en 29.1.0, y `export_nif.execute` elige la tabla
  igual en 29.1.0 y 30.3.0. El único cambio de API es `rotations=`, compatible hacia atrás.

> **Estado M1: `INTEGRACION_WINDOWS_OK`** (2026-11-04). 30.3.0 es ahora versión probada; matriz real 29.1.0/30.3.0 con
> 5 PASS / 1 SKIP cada una. Informe: [`docs/qa/PYNIFLY_30_COMPATIBILITY.md`](../qa/PYNIFLY_30_COMPATIBILITY.md).
> Decisión aplicada en el paso 5: aviso + readback, que era la política ya existente en `skyrim_conversion.cpp`.

**Riesgo concreto (antes de implementar).** `nif_bone_matrix()` (`forge_skyrim.py`, ~línea 310) calcula la matriz esperada
**sustituyendo el global** `blender_defs.game_rotations`, y `rotate_bones_pretty()` asume que en 29.1 la
bandera `PYN_ROTATE_BONES_PRETTY` gira 90° los skin-to-bone. Si en 30.x la rotación ya no depende de esa
bandera, o la función ya no lee el global, la comprobación de readback compararía contra una matriz
equivocada: daría un falso FAIL o, peor, un falso PASS.

**Cambios.**
1. `pynifly_facts()`: además de la versión, registrar si `get_bone_global_xf` acepta `rotations`
   (`inspect.signature`) y qué tablas de rotación existen. Es un dato que se puede observar, no algo que se
   deduce del número de versión.
2. `nif_bone_matrix()`: si existe el parámetro `rotations`, pasarlo explícitamente y no tocar el global.
   La ruta que sustituye el global solo queda para 29.x.
3. `rotate_bones_pretty()`: decidir la convención según el comportamiento medido, no según la bandera. Hay
   que exportar el modelo de prueba con la bandera activada y desactivada, releerlo con nifly y comparar
   los skin-to-bone (paso 4).
4. Nueva matriz de pruebas locales en `tests/app/test_forge_skyrim.cpp` (etiqueta `[local]`): el mismo
   modelo de `make_test_models.py` exportado con 29.1.0 y con 30.3.0. Se comparan huesos (traslación y
   rotación con tolerancia de 1e-4), pesos muestreados, número de triángulos y particiones.
5. Política para versiones no probadas (**decisión del dueño**): ¿bloquear, avisar o aceptar con aviso si
   pasa el readback? La recomendación es aceptar con aviso solo si el readback completo pasa, porque el
   readback es la prueba real.
6. Añadir `(30, 3, 0)` a `PYNIFLY_TESTED_VERSIONS` **solo** con la matriz en PASS, y dejar el log de esa
   ejecución en `docs/qa/`.

**Aceptación.** Las pruebas `[forge][skyrim][blender][local]` pasan con 29.1.0 y con 30.3.0, y el informe
muestra qué ruta se usó (`rotations=` o global). Sin Blender, esas pruebas se cuentan como SKIP, nunca
como PASS.

**Valor añadido.** Con HKX nativo se abre la exportación de animaciones sin hkxcmd. Queda fuera de esta
fase; se anota como incremento posterior.

---

## M2 — Kit de prueba en juego con `bat` (P1)

**Problema.** Hoy el README del mod pide al usuario `help <EditorID> 4` y después `player.placeatme
<FormID>` a mano (`ModPackager.cs:416-419`). Es lento y propenso a errores, y la puerta
`PENDIENTE_SKYRIM → VERIFICADO_EN_SKYRIM` no deja un acta ligada al build.

**Hecho externo.** Según UESP ([Console](https://en.uesp.net/wiki/Skyrim:Console), sección *Batch Files*),
`bat nombre` ejecuta `Data\nombre.txt` en orden, sin lógica y sin pararse si una línea falla. Según la misma
página, `coc` dentro de un `bat` cierra el juego (la página lo marca como pendiente de verificar), así que el
kit **nunca** incluye `coc`.

**Diseño.** El FormID en tiempo de ejecución depende del orden de carga, que no se conoce al crear el ZIP.
Por eso el kit es un **segundo ZIP pequeño, generado para un perfil concreto** después de instalar el mod:

1. Motor Qt-free (`engines/export`, función nueva `runtime_form_id(const EffectiveLoadOrder&, plugin,
   local_id)`), a partir de `mo2::resolve_load_order` (`engines/mo2/include/sns/mo2/load_order.hpp`):
   - Plugin completo: `índice(2 hex) + id local(6 hex)`, contando solo los no-light activos.
   - Plugin light (`header->is_light`): `FE + índice light(3 hex) + id local(3 hex)`.
   - Error explícito si el plugin no está activo en ese perfil o si el id light queda fuera de 0x000–0xFFF.
2. Viewmodel: acción «Preparar prueba en Skyrim» en el diálogo de exportación. El usuario elige un perfil
   de prueba, se lee el manifiesto del mod exportado (FormKeys de los registros nuevos o modificados) y se
   genera `SNS_Test_<Mod>.zip`, que contiene:
   - `SNS_Test_<Mod>.txt`: `player.placeatme <FormID> 1` por cada NPC o STAT, más líneas de comentario
     que Skyrim ignora como comando fallido. Se comprueba en la primera aceptación.
   - `ACTA_PRUEBA.md`: SHA-256 del ESP y del ZIP del mod, nombre del perfil, FormIDs calculados, casillas
     para capturas y la línea final `inGameVerified: false` hasta que la rellene un humano.
3. El kit se instala con «Instalar mod desde archivo» de MO2, igual que el mod. El estudio no escribe en
   `mods/`.

**Pruebas.**
- Catch2: `runtime_form_id` con un orden de carga sintético (masters, ESP, ESL con extensión `.esl` y con
  flag 0x200, plugin inactivo, plugin ausente) y casos límite (índice 0xFD, light 0xFFF).
- xUnit o export: el manifiesto expone los FormKeys que necesita el kit.
- Manual, en la primera aceptación real: `bat SNS_Test_<Mod>` coloca el NPC. Hasta entonces el formato `FE`
  queda como inferencia de la comunidad pendiente de observar.

**Aceptación.** El kit se genera de forma reproducible (mismos inputs → mismo SHA-256), rechaza perfiles
donde el mod no está activo y nunca contiene `coc`.

---

## M3 — Plugin de MO2 para instalación supervisada (P1, requiere aprobación)

**Contexto.** `StudioController::installModToMo2` (`studio_controller.cpp:664`) está desactivado por la
auditoría P0-01. Su comentario fija las condiciones para reabrirlo: flujo supervisado, destino canónico,
no sobrescribir, rollback, pruebas propias y aprobación del dueño.

**Hecho externo.** La API Python de MO2 ([mobase](https://www.modorganizer.org/python-plugins-doc/autoapi/mobase/index.html))
ofrece:
- `IOrganizer.installMod(filename, name_suggestion='') -> IModInterface`, que devuelve `None` si se
  cancela o falla;
- `IModList.setActive(name, active) -> bool`;
- `IOrganizer.refresh(save_changes=True)`, que es asíncrono.

Instalar con `installMod` usa el instalador del propio MO2, con su diálogo de conflicto de nombres, así que
el estudio sigue sin escribir en `mods/`.

**Diseño (traspaso por archivos, sin IPC con MO2).**
1. El estudio deja en su staging `install-requests/<sha256>.json` con: ruta del ZIP, SHA-256, nombre
   sugerido, perfil de destino y versión del esquema. Es idempotente por hash.
2. `tools/mo2/sns_installer/` es un plugin `IPluginTool` («Instalar exportaciones de Skyrim NPC Studio»)
   que el usuario copia a `plugins/` de MO2. Al abrirlo:
   - lista las peticiones pendientes y verifica el SHA-256 del ZIP;
   - **rechaza** la instalación si el perfil activo de MO2 no contiene el marcador `sns-test-profile.txt`,
     que crea el usuario a mano: el plugin nunca lo crea;
   - llama a `installMod`; si devuelve `None`, anota la cancelación; si no, activa el mod solo si el
     usuario marca la casilla y después llama a `refresh()`;
   - escribe `install-results/<sha256>.json` con el resultado, el nombre final del mod y la marca de
     tiempo.
3. El estudio lee el resultado y ofrece M2 (kit de prueba) para ese perfil.
4. Rollback: eliminar el mod desde MO2. El plan no automatiza el borrado.

**Pruebas.**
- Python: `pytest` con un `mobase` falso. Cubre hash incorrecto, perfil sin marcador, `installMod` que
  devuelve `None`, petición repetida (no reinstala) y JSON malformado.
- C++: el viewmodel genera peticiones válidas y no escribe nada fuera del staging.
- Integración manual con una instancia portable de MO2 en staging. Hasta entonces el estado máximo es
  `INTEGRACION_WINDOWS_OK`.

**Permisos.** El código y las pruebas con un `mobase` falso se pueden hacer en la rama aislada. Probarlo
contra un perfil MO2 real requiere autorización humana específica (AGENTS.md, *Permisos y gates*).

---

## M4 — Atuendo vía SkyPatcher en lugar de override de NPC_ (P2)

**Motivo.** El Modo A crea un override del registro NPC_ ganador. Ese override compite con los mods de
apariencia: es el conflicto clásico que resuelven EasyNPC y NPC Plugin Chooser 2. Un INI de SkyPatcher
asigna el atuendo sin tocar el registro NPC_.

**Fuente (solo comunitaria).** Las guías indican
`filterByNpcs=<Plugin>|<FormID>:outfitDefault=<Plugin>|<FormID>` en `SKSE\Plugins\SkyPatcher\npc\`,
cargadas en orden alfanumérico
([guía](https://themoddingbordello.vercel.app/guides/sos-outfit-distribution-guide)). **No se pudo leer el
readme oficial.** Gate de entrada: el dueño descarga SkyPatcher y se toma la sintaxis del readme oficial,
en especial el formato del FormID (con o sin `0x`, ceros a la izquierda, plugins ESL). Sin eso, la fase
queda `BLOQUEADO`.

**Diseño.**
- Nuevo modo de exportación `A-SkyPatcher`. El ESP solo contiene el OTFT nuevo (sin override NPC_) y se
  añade un INI `SNS_<Mod>.ini`. Si el atuendo elegido ya existe, no hay ESP.
- `manifest.json` declara las dependencias SkyPatcher + Address Library, y el README avisa de que sin
  SkyPatcher el cambio no se aplica.
- El Safe Patch sigue aplicándose al ESP. El INI se valida releyéndolo con un parser propio
  (estructura, FormKeys existentes en el orden de carga, ninguna línea desconocida).

**Pruebas.** INI dorado, ida y vuelta del parser, rechazo de FormKeys inexistentes y comparación de que el
Modo A y el Modo A-SkyPatcher apuntan al mismo OTFT. La verificación en juego sigue en `PENDIENTE_SKYRIM`.

---

## M5 — Snapshots legibles de los ESP exportados (P2)

**Objetivo.** Pruebas de regresión en las que el diff de un PR muestre qué cambió en el ESP generado,
además del Safe Patch campo a campo que ya existe (`NpcSafePatch.cs`, `Fingerprint.cs`).

**Opción elegida.** Un volcado propio en el worker con Mutagen: JSON ordenado por FormKey, sin
`VersionControl`/`FormVersion`/`Version2`, igual que la lista de exclusión de `NpcSafePatch.cs:37`. Se
guarda en `tests/export/.../Snapshots/*.json` y se compara en xUnit.

**Por qué no Spriggit de entrada.** [Spriggit](https://github.com/Mutagen-Modding/Spriggit) hace esto mismo
en YAML/JSON, pero un agregador fecha su último push en mayo de 2024 y añadiría una herramienta externa a
las pruebas. Queda como alternativa si el volcado propio crece demasiado.

**Aceptación.** Cada exportador (A, B, C, Forge STAT) tiene un snapshot. Un cambio no intencionado rompe la
prueba con un diff legible.

---

## M6 — BodySlide oficial como oráculo (P2)

**Motivo.** `Compilar-Cuerpos.bat` usa la reimplementación propia (`sns-probe bodyslide-batch`) y no hay
contra qué compararla.

**Hecho externo.** La línea de comandos de BodySlide admite `--groupbuild <grupos> --targetdir <dir>
--preset <preset> --trimorphs`. **Si se omite `--targetdir`, escribe en la carpeta Data del juego**
([wiki](https://github-wiki-see.page/m/ousnius/BodySlide-and-Outfit-Studio/wiki/Command%E2%80%90line-arguments)).

**Diseño.** Script de prueba local (`tests/tools/bodyslide_oracle.ps1`) que:
- se niega a ejecutarse sin `--targetdir` dentro del staging, y comprueba la ruta antes de lanzar nada;
- compila el mismo grupo y preset con BodySlide y con `sns-probe`;
- compara por forma (nifly) el número de vértices, las posiciones (tolerancia 1e-3) y los TRI.

**Aceptación.** El informe de diferencias queda en `docs/qa/`. Sin BodySlide instalado se cuenta como SKIP.
Las diferencias encontradas se convierten en pruebas unitarias en `test_bodyslide_morphs.cpp`.

---

## M7 — MorphTarget en el visor (P3, con medición previa)

**Contexto.** El skinning de la CPU se ejecuta una vez por build, no en cada frame
(`scene.hpp:207`, `apply_skinning`). Pasarlo a la GPU no aporta nada sin animación. Qt Quick 3D tiene
`MorphTarget` con `weight` entre 0 y 1 ([doc Qt 6.12](https://doc.qt.io/qt-6/qml-qtquick3d-morphtarget.html)),
lo que permitiría mover los sliders de BodySlide y TRI sin reconstruir la geometría.

**Gate.** Primero hay que medir la latencia p95 de un cambio de slider con el perfil real. Solo si supera
~100 ms se implementa, después de confirmar que `MorphTarget` existe igual en Qt 6.11. Si no, la fase se
cierra sin código.

---

## 9. Descartado, con su motivo

| Idea | Motivo |
|---|---|
| Actualizar Mutagen | 0.54.4 es la última estable (8 ago 2026); la 0.55 está en alfa ([NuGet](https://www.nuget.org/packages/Mutagen.Bethesda.Skyrim)). |
| Actualizar nifly | Fijado en `cca0a77` (14 sep 2026); los commits posteriores solo afectan a Starfield ([commits](https://github.com/ousnius/nifly/commits/main)). |
| Rango ESL 0x000–0x7FF | Exige HEDR 1.71, y esos plugins se ignoran o pueden cerrar el juego en ejecutables antiguos, GOG y VR (fuente comunitaria). El rango actual 0x800–0xFFF es el compatible. |
| Generador propio de FaceGen | No hay ninguna herramienta verificada que sustituya a Ctrl+F4 del Creation Kit. Se mantiene el kit CK. |
| WAL en SQLite | Ya está activo (`CatalogStore.cs:54`). |

## 10. Informe por fase

Al cerrar cada fase se informa según AGENTS.md: worktree y SHA, fase, cambio funcional, archivos y
métodos, comandos y exit codes, PASS/FAIL/SKIP por separado, riesgos, artefactos con hash, permisos usados
y siguiente incremento.
