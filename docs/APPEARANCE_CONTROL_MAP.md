# Mapa de control de la apariencia

Pestaña **Origen** del Character Creator (y avisos en la ficha del NPC). Para el NPC seleccionado
responde: qué plugin y qué mod de MO2 controla cada parte de su apariencia, a quién anula, y si el
registro y los archivos FaceGen encajan. Solo lee: no cambia el orden de carga, las prioridades de MO2
ni ningún archivo.

Motor: `engines/appearance` → `sns::appearance::analyze_appearance_control` (puro, sin E/S; pruebas
`tests/unit/engines/test_control_map.cpp`). Integración: `StudioController::appearanceControl`.
Panel: `qml/charactercreator/AppearanceControlPanel.qml`. Arnés: escenario `appearance-control`.

## Entradas

| Entrada | Origen |
|---|---|
| Cadena de versiones del registro (origen → ganador) con los campos que cambia cada una | worker Mutagen (`getNpc.overrides[].changedFields`) |
| Mod de MO2 de cada plugin | `StudioController::providerOf` |
| Copia efectiva del FaceGen y del FaceTint (suelto o BSA, y qué mod la aporta) | `AssetResolver::locate` (sueltos ganan a BSA, como el juego) |
| Todas las copias sueltas, en prioridad ascendente | `VirtualFileSystem::providers_of` |
| Desviación entre el FaceGen del perfil y el que produce el registro | análisis facial (`diagnose_face_compatibility`, misma topología) |
| Campos que una versión intermedia cambió y el ganador no conserva | worker (`getNpc.conflicts[].field`) |

## Reglas

- **Quién controla una parte**: la última versión de la cadena cuyo valor del campo difiere del de la
  versión anterior; si ninguna lo cambió, el origen. Por eso un parche que solo toca el atuendo gana el
  registro pero no controla la cara.
- **Valores copiados**: si ese cambio deja el campo con un valor idéntico al de una versión anterior (un
  parche que reenvía la cara de otro mod, o un plugin hecho sobre el registro vanilla que la devuelve al
  original), el valor se atribuye a la versión que lo introdujo y la que lo copia aparece como «copiado
  por» (`via`). El worker lo calcula por huella de cada campo (`overrides[].copiedFrom`: campo → plugin
  más antiguo con ese valor).
  Partes: forma de la cara (FaceMorph, FaceParts), piezas de cabeza (HeadParts), textura de cara
  (HeadTexture), tono de piel (TextureLighting, TintLayers), color de pelo (HairColor), raza y sexo,
  altura y peso, atuendo (DefaultOutfit, SleepingOutfit, WornArmor).
- **Datos faciales** (lo que define la geometría FaceGen): la versión que fija por última vez HeadParts,
  FaceMorph, FaceParts, Race o Weight.
- **Hallazgos** (gravedad `ok` / `info` / `warning`):

| Código | Cuándo | Gravedad |
|---|---|---|
| `NoFaceGen` | no hay FaceGen en el perfil y el NPC debería tenerlo | warning |
| `FaceGenNotUsed` | no hay FaceGen y no se espera: rasgos de plantilla, o raza que ningún NPC del perfil usa con FaceGen (criaturas) | ok |
| `FaceGenRecordMismatch` | los datos faciales y el FaceGen vienen de mods distintos y la desviación medida no es < 0,05 (o no se pudo medir) | warning |
| `FaceGenFromReplacedPluginCopy` | mods distintos, pero el mod del FaceGen trae su propia copia del plugin que fija la cara y gana otra copia del mismo archivo (traducción, copia limpiada) | info |
| `FaceGenHasRecordHeadParts` | mods distintos, pero el otro mod solo fija HeadParts y cada pieza del registro tiene su forma (mismo nombre de EditorID) en el FaceGen efectivo | info |
| `FaceGenFromOtherModMatches` | mods distintos, pero la desviación medida es < 0,05: el FaceGen sale del registro | ok |
| `FaceTintFromOtherMod` | el FaceTint lo aporta otro mod que la geometría | warning |
| `NoFaceTint` | hay geometría pero no tinte | info |
| `ShadowedFaceGen` | un mod trae FaceGen suelto para el NPC pero pierde en MO2 | info |
| `FaceReplaced` | campos faciales de una versión intermedia sustituidos por una versión del mismo mod que aporta el FaceGen efectivo (replacer completo) | info |
| `LostFaceEdits` | campos faciales de una versión intermedia que el ganador no conserva y que tomó una versión de otro mod (p. ej. un parche hecho sobre una copia antigua del registro) | warning |
| `FaceSourcesConsistent` | sin avisos y datos faciales + FaceGen del mismo mod | ok |

- **Sugerencias de `FaceGenRecordMismatch`**, por orden: si el mod del FaceGen tiene un plugin en la
  cadena (anulado por el que fija la cara) → el botón **Usar el rostro de «X»** (abajo) o cargarlo
  después; si el mod de los datos faciales trae FaceGen suelto que pierde en MO2 → subirlo; si no →
  comprobar o regenerar el FaceGen con el estudio. Nunca se aplican solas.

## Resolución asistida: «Usar el rostro de X»

Para el caso clásico de cara oscura: el FaceGen que gana en MO2 es de un mod que **también** tiene su
versión del registro en la cadena, pero un plugin posterior fija otros datos faciales (en el perfil de
prueba, «AI Overhaul 1.8» sobre «NITHI NPC Enhancement - Riften», 28 NPC).

Motor: `ControlReport::resolution` (`sns::appearance::FaceResolution`), solo junto a
`FaceGenRecordMismatch`. Condiciones:

- X = la última versión de la cadena cuyo mod aporta el FaceGen efectivo, y no es el ganador;
- raza y sexo del ganador iguales a los de X (nunca se copian: el FaceGen se hizo para ellos);
- al menos un campo facial distinto entre X y el ganador. Campos posibles: HeadParts, FaceMorph,
  FaceParts, TintLayers, HairColor, HeadTexture, TextureLighting y Weight; se copian **solo los que
  difieren** (Weight, por ejemplo, solo si el ganador lo cambió). La altura nunca.

El botón (pestaña Origen) llama a `StudioController::prepareFaceResolution` y abre el asistente de
Easy Mod en el modo `face-resolution`:

- **No se mezcla con las ediciones**: con cambios pendientes en el editor (rostro, color de cabello,
  altura, atuendo) la creación queda bloqueada; el color de cabello sigue su exportación con Hair Color
  Safe Mode. Mientras se analiza el FaceGen, espera.
- **FaceGen**: el mod no trae ninguno (ni copiado ni regenerado); el arnés comprueba que el FaceGen del
  perfil queda idéntico byte a byte. La protección facial (High Poly) sigue bloqueando editar y
  regenerar; el asistente explica que este parche no hace ninguna de las dos cosas.
- **Worker** (`FaceAssignment.copyFaceFrom` + `onlyFields` con PNAM, NAM9, NAMA, TINT, HCLF, FTST, QNAM,
  NAM7; `faceGen.mode = "source-facegen"`, `faceGen.effectiveFaceGenMod`): copia esos subregistros de
  la versión de X tal cual sobre el override del ganador y rechaza la exportación si X no está cargado,
  es el ganador, es de otra raza o sexo, si llega algún FaceGen, si el mod del FaceGen no es el que
  aporta X o si se pide otro campo. Tras escribir, relee el plugin con Mutagen: los campos copiados
  idénticos a X, ningún otro campo del NPC distinto del ganador (Safe Patch, motivo «Copiado de X») y
  X y el ganador como **masters** (se carga después de ambos; `loadAfter` en el manifiesto y el README).
  `installAbove` queda vacío: el mod no tiene archivos que compitan en MO2.
- **Comprobación final**: el worker devuelve la cadena del NPC con el plugin releído como última versión
  (`projectedChains`), el estudio la pasa por el mapa de control con los FaceGen del perfil y la
  exportación solo se da por buena si ya no hay `FaceGenRecordMismatch` y los datos faciales son del mod
  de X (`FaceSourcesConsistent`). `readyForGame: false`, `inGameVerified: false`.

Pruebas: `test_control_map.cpp` (`[resolution]`), `test_easy_mod.cpp` (`[resolution]`),
`FaceResolutionExportTests.cs` (plugins sintéticos) y el escenario de arnés `face-resolution`.

## Todo el perfil

Botón **Analizar NPC únicos** (pestaña Origen): el mismo análisis para cada NPC único, sin la medición
facial. Los NPC de una sola versión se analizan sin consultar al worker; los de varias piden su cadena.
La lista muestra primero los avisos y abre el NPC al pulsarlo. En el perfil de prueba (1308 NPC únicos)
tarda unos 38 s en Debug; ver `docs/plans/next-generation-development.md`.

## Resolución asistida: evidencia y decisión

- La «copia» de un valor (`copiedFrom`) se infiere por igualdad con una versión anterior de la cadena
  cargada; la interfaz lo muestra como «inferido».
- Hay desajuste si cualquier campo de geometría del ganador tiene otro valor que la versión del mod del
  FaceGen, aunque la versión más reciente siga siendo de ese mod (reversión parcial a vanilla).
- Una resolución es **clara** si los valores que descarta son copias (reversión accidental) y **ambigua** si
  descarta ediciones propias de un mod: se ofrece con «Decisión necesaria» y nunca se aplica sola.
- Antes de exportar se explica: rostro elegido, campos que cambian, valores que se conservan (por versión),
  valores que dejan de aplicarse, pendientes y otros rostros disponibles.
- Al exportar: el mod del FaceGen se localiza de nuevo en MO2 y debe ser el del plugin de origen; sin
  proveedor conocido se rechaza; el parche se rechaza si, comparado sobre los registros releídos, le falta
  algún campo facial del origen.

## Límites conocidos

- No se ejecuta Skyrim: «encaja» significa que los archivos son coherentes (mismo mod o desviación
  medida pequeña), no que se haya visto en el juego. Pendiente de validación en Skyrim.
- La desviación solo existe cuando el FaceGen del perfil tiene la misma topología que el que produce el
  registro; con cabezas High Poly, esculpidas o de RaceMenu suele ser «no comparable» y el aviso se basa
  solo en la procedencia (mods distintos ≠ incompatibles con certeza; el texto lo dice).
- Las versiones del registro se comparan por los campos que rastrea el worker; un cambio en un campo no
  rastreado no mueve el control. Hasta la versión 7 del extractor los colores (QNAM, colores de TINC)
  tenían todos la misma huella, así que un cambio solo de tono de piel o de color de tinte no se veía;
  corregido (el catálogo se reconstruye solo).
- «Copiado» significa valor idéntico por huella, no que el autor lo copiara: dos mods que fijan por
  separado el mismo valor también cuentan como copia del primero.
- Si quien escribe el valor es el mod del FaceGen efectivo, el valor es suyo aunque coincida con el de
  una versión anterior (un replacer que conserva morphs vanilla hizo su FaceGen con ellos); solo se
  atribuye por copia cuando lo escribe otro mod.
- Cuando el FaceGen efectivo es el del juego (BSA), la resolución puede ser «Usar el rostro de
  «Skyrim.esm»»: devuelve la cara al registro vanilla, que es la que dibuja ese FaceGen, y deshace los
  cambios faciales del otro mod (p. ej. las piezas de cabeza que corrige USSEP). Revísalo antes de usarlo.
- La resolución asistida no comprueba que la cara de X sea la que quieres, solo que registro y FaceGen
  vuelvan a ser del mismo mod; lo que AI Overhaul (u otro) cambiaba en esos campos se pierde (aparece
  como `FaceReplaced`). Un plugin posterior que vuelva a cambiar el NPC ganará al parche.
- Los BSA se atribuyen al mod que aporta el archivo `.bsa`; entre BSA el orden lo decide el
  `AssetResolver` (orden de plugins / INI), igual que el resto del estudio.
- Plugins sin proveedor en MO2 (fuera del load order) aparecen con mod vacío.
- «No se espera FaceGen» es heurístico: plantilla con rasgos (en el análisis del perfil, cualquier
  plantilla de un NPC de una versión, porque sus flags no se cargan) o raza sin ningún FaceGen entre los
  NPC únicos del perfil. Antes del primer análisis del perfil, un NPC sin piezas de cabeza de una raza
  de criatura aún puede salir como `NoFaceGen`.
- La comprobación de piezas compara nombres (EditorID = nombre de la forma en el FaceGen); la misma
  pieza con otra malla no se detecta.
- La pestaña «Protección facial» conserva su aviso de procedencia anterior (registro ganador frente al
  mod del FaceGen) porque alimenta el Easy Mod Creator; el mapa de control es más preciso porque usa los
  datos faciales y la medición.
