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
  Partes: forma de la cara (FaceMorph, FaceParts), piezas de cabeza (HeadParts), textura de cara
  (HeadTexture), tono de piel (TextureLighting, TintLayers), color de pelo (HairColor), raza y sexo,
  altura y peso, atuendo (DefaultOutfit, SleepingOutfit, WornArmor).
- **Datos faciales** (lo que define la geometría FaceGen): la versión que fija por última vez HeadParts,
  FaceMorph, FaceParts, Race o Weight.
- **Hallazgos** (gravedad `ok` / `info` / `warning`):

| Código | Cuándo | Gravedad |
|---|---|---|
| `NoFaceGen` | no hay FaceGen en el perfil | warning |
| `FaceGenRecordMismatch` | los datos faciales y el FaceGen vienen de mods distintos y la desviación medida no es < 0,05 (o no se pudo medir) | warning |
| `FaceGenFromReplacedPluginCopy` | mods distintos, pero el mod del FaceGen trae su propia copia del plugin que fija la cara y gana otra copia del mismo archivo (traducción, copia limpiada) | info |
| `FaceGenFromOtherModMatches` | mods distintos, pero la desviación medida es < 0,05: el FaceGen sale del registro | ok |
| `FaceTintFromOtherMod` | el FaceTint lo aporta otro mod que la geometría | warning |
| `NoFaceTint` | hay geometría pero no tinte | info |
| `ShadowedFaceGen` | un mod trae FaceGen suelto para el NPC pero pierde en MO2 | info |
| `FaceReplaced` | campos faciales de una versión intermedia sustituidos por una versión del mismo mod que aporta el FaceGen efectivo (replacer completo) | info |
| `LostFaceEdits` | campos faciales de una versión intermedia que el ganador no conserva y que tomó una versión de otro mod (p. ej. un parche hecho sobre una copia antigua del registro) | warning |
| `FaceSourcesConsistent` | sin avisos y datos faciales + FaceGen del mismo mod | ok |

- **Sugerencias de `FaceGenRecordMismatch`**, por orden: si el mod del FaceGen tiene un plugin en la
  cadena (anulado por el que fija la cara) → cargarlo después o usar un parche con sus campos faciales;
  si el mod de los datos faciales trae FaceGen suelto que pierde en MO2 → subirlo; si no → comprobar o
  regenerar el FaceGen con el estudio. Nunca se aplican solas.

## Límites conocidos

- No se ejecuta Skyrim: «encaja» significa que los archivos son coherentes (mismo mod o desviación
  medida pequeña), no que se haya visto en el juego. Pendiente de validación en Skyrim.
- La desviación solo existe cuando el FaceGen del perfil tiene la misma topología que el que produce el
  registro; con cabezas High Poly, esculpidas o de RaceMenu suele ser «no comparable» y el aviso se basa
  solo en la procedencia (mods distintos ≠ incompatibles con certeza; el texto lo dice).
- Las versiones del registro se comparan por los campos que rastrea el worker; un cambio en un campo no
  rastreado no mueve el control.
- Los BSA se atribuyen al mod que aporta el archivo `.bsa`; entre BSA el orden lo decide el
  `AssetResolver` (orden de plugins / INI), igual que el resto del estudio.
- Plugins sin proveedor en MO2 (fuera del load order) aparecen con mod vacío.
- La pestaña «Protección facial» conserva su aviso de procedencia anterior (registro ganador frente al
  mod del FaceGen) porque alimenta el Easy Mod Creator; el mapa de control es más preciso porque usa los
  datos faciales y la medición.
