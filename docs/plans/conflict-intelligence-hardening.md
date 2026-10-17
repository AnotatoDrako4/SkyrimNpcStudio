# Endurecimiento del NPC Conflict Intelligence Engine

Rama `feature/conflict-intelligence-hardening`, creada desde `release/v1.1` (que ya integra #8 → #19 y
#20). Plataforma: Windows 11, MSVC 14.44, Qt 6.11.2, .NET 10; perfil MO2 `Default` en solo lectura. Nada
de este registro se ha comprobado dentro de Skyrim.

## Estado inicial

- Árbol limpio, sin cambios sin publicar. PR #18 → #19 apilados y #20 contra `main`, todos abiertos y
  fusionables; #21 (`release/v1.1`) los integra.
- GitHub Actions: todas las ejecuciones terminan en `startup_failure` antes de ejecutar ningún trabajo
  (problema de cuenta/flujo documentado en #13), no en errores de compilación. No se tocan pruebas por ello.
- Ningún ECC/Ralph loop activo.

## P0 — MessagePack / NU1902

Causa: StreamJsonRpc 2.25.29 (última estable) pide MessagePack 2.5.302, afectado por
GHSA-qhrr-8q5h-9q3h; la auditoría de NuGet lo marca (NU1902) y `TreatWarningsAsErrors` lo convierte en
error de restauración. #20 fija la transitiva en `Directory.Packages.props` (pinning central, solo afecta a
los paquetes listados) a 2.5.305. Diff revisado: solo el pin, la ADR y los `packages.lock.json`.

Comprobado en la rama: `dotnet restore --locked-mode --force-evaluate` correcto en el worker y en los dos
proyectos de prueba (lock files coherentes); auditoría activa (`NuGetAudit` no desactivado,
`TreatWarningsAsErrors` sí); `dotnet list package --vulnerable --include-transitive`: ninguno; MessagePack
resuelve a 2.5.305. JSON-RPC y Mutagen: worker 37/37, exportación 75/75 y la app indexando el perfil real
(verificación de 1.1.0).

## Iteración 1 — P1: condiciones de seguridad de «Usar el rostro de X»

### Defectos encontrados (lectura del código de #19)

1. **Fallo abierto en el worker** (`FacePatchBuilder.ApplySourceFace`): la comprobación «el FaceGen que usa
   el juego es del mod del plugin de origen» solo se hacía si `PluginProviders` contenía el plugin de origen;
   si faltaba —o si el nombre difería en mayúsculas, porque el diccionario venía de JSON con comparación
   ordinal— la condición se aprobaba sin evaluarse.
2. **Comprobación tautológica en el estudio** (`AppearanceEditor::exportFaceResolution`): enviaba como
   `effectiveFaceGenMod` el mod del plugin de origen (`r.source_mod`), no el del FaceGen localizado; el
   worker comparaba el mod de origen consigo mismo.
3. **Punto ciego de las huellas**: los campos a copiar y la comprobación posterior del mapa proyectado salen
   de huellas del catálogo; si una huella confunde dos valores distintos (como el fallo de `Color` que #19
   ya corrigió), un campo facial distinto no se copia y nadie lo detecta.

### Atribución: qué es inferencia

`copiedFrom` («valor cambiado idéntico al de una versión anterior → copia de esa versión») es una
**inferencia por igualdad de valores**, no una prueba de autoría. Se usa para (a) explicar quién introdujo
un valor y (b) decidir qué campos copiar. Para (b) el error es benigno (copiar un campo ya igual no cambia
nada y el worker relee que lo copiado es idéntico al origen); el riesgo real era el punto ciego 3.

### Correcciones

- Worker: sin proveedor del plugin de origen → `FaceSourceProviderUnknown` (se rechaza); búsqueda de
  proveedor sin distinguir mayúsculas.
- Estudio: `effectiveFaceGenMod` = mod del FaceGen localizado de nuevo en MO2 al exportar (suelto sobre
  BSA; para BSA, el mod que aporta el archivo); vacío si no se localiza (el worker lo rechaza).
- Worker: `ProjectedChain.RemainingFaceDifferences` = campos faciales del registro escrito que siguen
  distintos de la versión de origen, **comparados sobre los registros** (no sobre huellas). El estudio
  rechaza la verificación del flujo de resolución si queda alguno. La API conserva la copia parcial.
- Pruebas: proveedor ausente y diccionario vacío rechazados; proveedor con otras mayúsculas aceptado y, si
  apunta a otro mod, rechazado; copia completa sin diferencias restantes; copia parcial con
  `NAM7, NAMA, PNAM, QNAM, TINT` restantes. Exportación 77/77.

### Fragilidad cerrada de paso

La exportación reenviaba la carpeta de salida tal cual; con una ruta relativa el worker la rechazaba
(«outputDirectory must be an absolute path»). En la app la carpeta viene de un diálogo (absoluta); el
arnés la pasó relativa. Ahora el estudio la convierte en absoluta antes de enviarla (Easy Mod y
resolución).

## Iteración 2 — P2 y P4: validación y consecuencias antes de exportar

### P2: qué valida ya el flujo

| Verificación | Dónde |
|---|---|
| Plugin releído con Mutagen | worker `Verify` |
| Campos copiados idénticos al origen | worker (releído) + `RemainingFaceDifferences` (registros) |
| Campos no pedidos = ganador | Safe Patch Engine (máscara de igualdad completa) |
| Masters (origen y ganador) | worker + estudio |
| Sin FaceGen en el mod | worker (`SourceFaceGenNoAssets`) + estudio (archivos del mod) |
| FaceGen efectivo intacto | el mod no trae FaceGen y nada se escribe en el perfil; el arnés compara el FaceGen del perfil antes y después |
| Mapa proyectado sin el desajuste | estudio (`control_projector_` con la cadena releída) |
| Manifiesto con cambios, masters y `loadAfter` | worker |
| No se declara verificado en el juego | `readyForGame: false`, `inGameVerified: false` |

Límite: el mapa proyectado usa huellas del catálogo; `RemainingFaceDifferences` cubre la copia sobre los
registros reales. Ninguna de las dos comprueba lo que Skyrim dibuja.

### P4: explicación de consecuencias

`FaceResolution` añade, calculado con la cadena real: `explanation` (rostro elegido y registro que se
conserva, con los cambios propios del ganador), `keeps` (valores no faciales que se conservan, por la
versión que los escribió), `replaced` (valores faciales que dejan de aplicarse y quién los escribió;
cuando la autoría se basa en una copia lo dice como «inferido: el mismo valor que…»), `pending` (avisos
del mapa que el parche no resuelve; las ediciones faciales perdidas que el parche recupera no cuentan) y
`alternatives` (otros mods con FaceGen y versión del NPC que pierden en MO2: el usuario decide). Se
muestra en la tarjeta de Origen y en el asistente existente (sin pantallas nuevas). Una prueba unitaria
lo cubrió y cazó un error propio: el aviso `LostFaceEdits` salía como pendiente aunque el parche es
justo lo que recupera esas ediciones.

Bolli en el perfil real (asistente, página 2): «Se usará el rostro de «NITHI NPC Enhancement - Riften»
… y se conservará el resto del registro de «AI Overhaul.esp», incluidos sus cambios de DefaultOutfit,
AIData, Packages»; deja de aplicarse cada valor facial de AI Overhaul, «inferido: el mismo valor que
«Skyrim.esm»» (AI Overhaul se hizo sobre el registro vanilla). Manifiesto: `effectiveFaceGenMod` = mod
localizado, tres masters, sin assets, `readyForGame: false`.

## Iteración 3 — P5: rendimiento del análisis del perfil (medido, Debug)

Instrumentación nueva en `appearanceScan`: bloqueo más largo del hilo de la interfaz (`maxBlockMs`, con la
fase), tiempo de análisis en el estudio (`analysisMs`) y el resto, espera al worker.

| | Antes | Después |
|---|---|---|
| Escaneo completo (1308 NPC únicos) | 37,0 s | 24–29 s (tres pasadas) |
| Bloqueo más largo del hilo de la interfaz | 798 ms (listado + 462 NPC de una versión de una vez) | 260–572 ms: una respuesta de getNpc que lee un FaceGen de un BSA (con páginas de 1000 era el listado, 440 ms) |
| Análisis en el estudio | 11,5 s | 11,6–15,4 s, ahora en paralelo con el worker |
| Llamadas al worker | 848 | 852 (páginas de listado de 250) |
| Resultados | — | idénticos: 683 filas, mismos códigos y resoluciones (comparadas fila a fila) |

Cambios: los NPC de una versión se analizan de 40 en 40 (una porción por vuelta del bucle de eventos) mientras
el worker ya atiende a los de varias versiones; hasta 4 `getNpc` en vuelo (antes uno, ~30 ms cada uno, esperando
al estudio). Sin cachés nuevas. La cancelación y el cambio de perfil siguen invalidando el escaneo (el puntero
del escaneo cambia y las respuestas viejas se descartan).

Lecturas redundantes: el FaceGen se lee una vez por NPC y solo si las piezas de cabeza las fija otro mod que
el del FaceGen; la ubicación (`locate`) se hace dos veces por NPC de varias versiones (listado y análisis),
coste por debajo de la medición. No se optimiza sin medición que lo justifique.

## Propuesta (no implementada): Multi-NPC Conflict Resolution Engine

Basada en el flujo individual validado. Objetivo: elegir varios NPC con conflictos parecidos, decidir el
rostro de cada uno y generar un único ESP/ESL.

- **Selección**: desde la lista «Todo el perfil» de Origen, filas con resolución; agrupar por pareja
  origen → ganador (en el perfil de prueba: 27 «NITHI The Rift - Men → AI Overhaul», 9 «Skyrim.esm → USSEP
  en castellano», 7 «BeastHHBB → AI Overhaul», …). Las ambiguas (`discards_own_edits` no vacío) nunca se
  seleccionan por grupo: una a una y con la decisión explícita.
- **Contrato**: el worker ya acepta varias asignaciones en `ExportFaceOverhaulRequest.Assignments`, y
  `FacePatchBuilder.Build` ya itera por NPC, declara masters por unión (`loadAfter`) y devuelve una
  `ProjectedChain` por NPC con `RemainingFaceDifferences`. No hace falta otro exportador: falta que el estudio
  envíe N asignaciones `copyFaceFrom` (cada una con su FaceGen localizado) y verifique cada cadena proyectada.
- **Seguridad por NPC**: las mismas condiciones del flujo individual por cada NPC; un NPC que falle no debe
  colarse: o se rechaza el lote entero (comportamiento actual del worker ante un error) o el estudio lo
  excluye antes de enviar y lo informa.
- **Límites a vigilar**: el plugin ESL admite un número limitado de registros nuevos (los overrides no
  cuentan); masters acumulados (cada grupo añade origen y ganador); el tiempo de verificación crece con N
  (relectura y Safe Patch por NPC). Medir con el grupo de 27 antes de prometer lotes mayores.
- **Reutiliza**: Appearance Control Map (selección y explicación por NPC), Face Resolution (asignaciones),
  Mutagen, Safe Patch Engine, Easy Mod Creator (un modo «resolución de varios NPC» en el mismo asistente) y la
  exportación A/B/C existente.

Pendiente medido: leer el FaceGen (comprobación de piezas) sigue en el hilo de la interfaz; con un BSA
grande es el bloqueo restante (hasta ~0,6 s). Moverlo a un hilo de trabajo es la siguiente optimización si
hace falta.

## Iteración 4 — P3: perfil real y muestra de resoluciones

| NPC | Comprobado | Resultado |
|---|---|---|
| Bolli (`01334E`) | resolución de extremo a extremo (44 comprobaciones): parche escrito en carpeta de trabajo, releído, Safe Patch, masters, sin FaceGen, mapa proyectado coherente, FaceGen del perfil intacto, manifiesto | PASSED; explicación: rostro de NITHI, se conservan AIData, Packages, DefaultOutfit de AI Overhaul |
| Brand-Shei (`01334F`) | igual | PASSED (44) |
| Lydia | replacer completo | sin resolución (coherente: FaceReplaced + FaceSourcesConsistent) |
| Serana | cabeza High Poly propia, Hair Color Safe Mode | coherente; hair-safe 350 comprobaciones PASSED (FaceGen idéntico salvo HairTint) |
| Shady Sam | FaceGen en BSA, plugin sustituido por traducción | nota FaceGenFromReplacedPluginCopy, sin resolución |
| Ingrid | vanilla | FaceSourcesConsistent |

### Defectos del motor encontrados con datos reales

1. **Falso negativo (reversión parcial a vanilla)**: la «autoría» de la cara se tomaba como la versión más
   reciente entre los campos de geometría; si un mod posterior devolvía FaceMorph/FaceParts a vanilla pero
   conservaba las HeadParts del mod del FaceGen, el resultado era «coherente». Ahora, si el mod del FaceGen
   tiene versión en la cadena, cualquier campo de geometría del ganador con otro valor que esa versión es un
   desajuste (`diverged_geometry`). En el perfil: Sybille Stentor e Ysolda (un mod de ropa hecho sobre una
   copia antigua del registro) pasan a `FaceGenRecordMismatch` con resolución clara: 105 → 107 desajustes.
2. **Clasificación errónea derivada**: en ese caso la nota de «copia sustituida del plugin» se activaba
   sola (el plugin de la cara es el del propio mod del FaceGen); ahora solo aplica cuando otro mod controla la
   cara. Prueba unitaria con la forma de Sybille/Ysolda.

### Muestra de las resoluciones ofrecidas (84)

Clasificación nueva por evidencia: **clara** si los valores faciales que se descartan son copias de una
versión anterior (un mod posterior hecho sobre una copia antigua devuelve la cara — reversión accidental);
**ambigua** si descarta valores que el mod introdujo él mismo (puede ser una corrección intencionada sin
FaceGen): se sigue ofreciendo, pero con «Decisión necesaria» explicando qué ediciones propias se pierden.

- 31 claras, 53 ambiguas. Ejemplos ambiguos: «Skyrim.esm → USSEP en castellano» (9), «Dawnguard.esm →
  USSEP» (3), «HearthFires.esm → USSEP» (2): el parche descartaría correcciones propias de USSEP (HeadParts,
  FaceMorph, Weight) solo para que el registro coincida con el FaceGen vanilla, que es el que el juego ya
  muestra. No se presentan como conflicto confirmado.
- «NITHI The Rift - Men → AI Overhaul» (27): 13 claras (valores de AI Overhaul inferidos como copias de
  Skyrim.esm) y 14 ambiguas (AI Overhaul trae valores propios en algún campo, p. ej. HeadParts de Bolli).
- La copia se infiere por igualdad de valores en la cadena cargada; si la versión copiada no está en el perfil
  (p. ej. un parche hecho sobre USSEP sin USSEP instalado), el valor cuenta como propio y la resolución sale
  ambigua: el error va hacia pedir decisión, no hacia ocultarla.

Falsos positivos registrados: los de las pasadas anteriores (replacers completos, traducciones, plantillas,
piezas sin malla) siguen corregidos; ninguno nuevo en la muestra. Falso negativo corregido: el punto 1.
Nada de esto se ha comprobado dentro de Skyrim: preparar la verificación humana con los parches de Bolli y
Brand-Shei de `out\hardening6` siguiendo `docs/SKYRIM_ACCEPTANCE_TEST.md` (perfil y partida aislados).

### Sin cambios (comprobado)

El Safe Patch Engine compara todas las propiedades NPC_ con la máscara de igualdad de Mutagen y falla
cerrado ante veredictos ilegibles: IA, paquetes, facciones, voz, inventario, scripts y estadísticas del
ganador se conservan por construcción y por verificación.
