# Pipeline NIF / DDS / visor 3D

El visor muestra al NPC seleccionado con **recursos reales** del perfil de MO2 (cuerpo, cabeza FaceGen, pelo y piezas del atuendo), en pose de referencia. Nunca afirma ser idéntico al juego: los shaders de Skyrim, las animaciones y la física SMP/FSMP no se reproducen (la escena emite siempre el aviso `PreviewFidelity`).

## Cadena

```
VFS de MO2 (sueltos) ─┐
BSA cargados          ─┼─> AssetResolver (Data\…; suelto > último BSA) ─> bytes
                       │
   NIF ─> nifly ─> sns::nif::Shape (espacio raíz del NIF) ─> skinning CPU (esqueleto de la raza)
   DDS ─> parse_dds (mip almacenado que cabe) ─> DirectXTex (BCn → RGBA8) ─> measure_alpha
                       │
   build_character_scene ─> RenderPart (vértices intercalados, materiales traducidos, datos del inspector)
                       │
   SceneViewModel ─> QQuick3DGeometry / QQuick3DTextureData ─> CharacterViewport.qml (View3D)
```

| Módulo | Responsabilidad |
|---|---|
| `engines/assets` | VFS + BSA 103/104/105; `diagnose_archive` explica un BSA ilegible (cabecera, tamaño, % de ceros) y recomienda reparar fuera del estudio |
| `engines/nif` | Lectura con nifly: geometría, UV, normales/tangentes/bitangentes, colores de vértice, *skin*, shader (tipo, flags, alpha de material, UV offset/scale), `NiAlphaProperty`, bandera *Hidden*, nodo padre, transformación aplicada, XML SMP de la raíz |
| `engines/textures` | DDS (BC1–BC7, DX10, RGBA sin comprimir) y estadísticas reales de alfa |
| `engines/rendering` | Escena: reglas del juego (slots, orden de equipamiento, pelo bajo casco), skinning, FaceGen, traducción de transparencia, avisos por pieza |

## Carga segura (auditoría v2, F-10/F-11/F-12)

Todo NIF llega a nifly por **una sola puerta**: `sns::nif::load_nifly` (`engines/nif/include/sns/nif/safe_load.hpp`), que
usan `load_mesh`/`load_skeleton` y el `parse_nif` de FaceGen. Antes de llamar a nifly, `prevalidate_nif` recorre la cabecera
con lectura acotada y rechaza con `sns::Error(Parse)`: más de 64 MiB, cabecera que no es de Skyrim (20.2.0.7, user 12),
cadenas o tablas que salen del archivo, índices de tipo fuera de rango, tamaños de bloque que no caben (nifly reservaba lo
que dijera la cabecera: 4,1 GB con un archivo de 238 bytes) y rutas de `BSShaderTextureSet` fuera de su bloque o de más de
1024 bytes (nifly pasa cada ruta por `std::regex`). Cualquier excepción de nifly se convierte en `sns::Error`; un FaceGen
ilegible es «no legible», nunca una excepción que salga de un hilo. En la escena, una malla que falle por cualquier motivo
solo pierde esa pieza (`MeshUnreadable`).

Concordancia con nifly sobre datos reales (solo lectura): 4000 NIF de los mods del perfil, 0 rechazos falsos
(`sns_nif_load_probe scan`). Riesgo residual: las rutas propias de `BSEffectShaderProperty` no se validan campo a campo
(su disposición depende de la versión); quedan acotadas por el límite de 64 MiB y por el `catch` de `load_nifly`. Medido en
MSVC Debug antes de la corrección: la ruta de 589 824 bytes del caso de la auditoría no desbordó la pila, pero tardó 5,6 s.

## Transformaciones (NIF → nifly → Qt)

1. Formas sin *skin*: transformación global del NIF (cadena de padres compuesta una sola vez).
2. Formas con *skin*: `globalToSkin⁻¹` lleva los vértices al espacio raíz; el skinning CPU aplica `boneToGlobal · skinToBone · globalToSkin` por influencia (máx. 4). Huesos que no están en el esqueleto de la raza pero sí en el propio NIF (huesos SMP `A-S-…`) usan el nodo del NIF (`ExtraBonesFromMesh`, informativo). Huesos que no están en ningún sitio conservan la pose de *bind* (`BoneMissing`), nunca colapsan al origen; no se inventan ni se borran huesos.
3. Vista: Skyrim (Z arriba) → Qt (Y arriba): `(x, y, z) → (−x, z, y)`, rotación propia que conserva el *winding*.
4. Tangentes: algunos NIF guardan tangente y bitangente al revés; se comparan con el gradiente UV (dP/du) y se intercambian si hace falta (`tangentsSwapped` en el inspector).
5. UV: se aplica `uv · uvScale + uvOffset` del `BSShaderProperty`, como el shader del juego.

## Transparencia: traducción explícita

`translate_alpha` decide el modo a partir de la `NiAlphaProperty` **y** del alfa que de verdad llega al material (medido en la textura decodificada, en los colores de vértice o en el *Alpha* del shader). No hay `alphaCutoff` fijo ni un `Mask` global para el pelo.

| NIF | Alfa real presente | Modo en Qt Quick 3D |
|---|---|---|
| sin blend ni test | — | `Opaque` |
| blend y/o test | ningún texel < 250 | `Opaque` (motivo: "no texel or vertex carries alpha") |
| solo test (umbral *t*) | sí | `Mask`, `alphaCutoff = t/255` |
| solo blend (ojos, cejas, cristal) | sí | `Blend` |
| blend + test (pelo, cuero cabelludo) | sí | `Blend` + `Material.OpaquePrePassDepthDraw` (`blendPrepass`): profundidad de los texeles opacos, bordes mezclados |

Las prendas semitransparentes conservan su modo; nada se vuelve opaco para "arreglar" la imagen.

## Formas que el juego no dibuja

Se listan en el panel de geometrías (cursiva) y en el inspector, pero empiezan ocultas y no cuentan para el encuadre (`ShapesHiddenByDefault`):

- sin propiedad de shader (p. ej. `VirtualGround`, plano de 160×160 de las faldas SMP);
- con la bandera *Hidden* en la forma o en un nodo padre;
- formas de colisión del XML SMP (`<per-vertex-shape>` / `<per-triangle-shape>`) **sin ninguna textura** (p. ej. `CPhy`). Estar en el XML no basta: la propia falda también figura y sí se ve;
- pelo bajo un casco (slots 31/41).

## FaceGen

La cabeza usa la geometría `facegeom\<plugin de origen>\<FormID>.nif`. El tinte `facetint\…dds` se **compone** sobre la difusa (no la sustituye), con la fórmula del shader FaceTint reimplementada por Community Shaders: `(b² + 2·t·b·(1−b)) · 3.984375 · (detalle + (1/255, 0, 1/255))`, usando el mapa de detalle del slot 3. Un tinte neutro (0.5) y un detalle neutro (64/255) dejan la difusa igual. Sin tinte: aviso `FaceTintMissing`.

### Piel del cuerpo (SkinTint) y coherencia con la cara

Las formas con shader SkinTint (tipo 5; técnica `FacegenRGBTint` en Community Shaders, `ShaderCache.h`)
aplican el tono de piel del NPC (QNAM) con la **misma mezcla** que la cara, no como multiplicación:
`(b² + 2·t·b·(1−b)) · (1.01171875, 0.99609375, 1.01171875)` sobre los valores en gamma de la difusa
(`GetFacegenRGBTintBaseColor`, `Lighting.hlsl`). `rendering::apply_skin_tint` compone una copia de la
difusa en memoria (clave `…|skintint:r,g,b`, origen `composite` en el inspector) y el material ya no
lleva el tono como `baseColor`. El NIF, las texturas del perfil y lo exportado no cambian.

Defecto corregido el 2026-10-16: el visor multiplicaba la difusa por el tono (≈0.8), así que el cuerpo
salía más oscuro que la cara y se veía un corte en el cuello. Medido en la captura de cuerpo entero
(`-CaptureSet`, perfil real), luminancia pecho/mejilla antes → después: Ingrid 0,65 → 1,12; Lydia
0,69 → 1,14; Serana 0,50 → 0,90 (la cara no cambia; la diferencia restante es de iluminación: el pecho
mira a la luz). Prueba unitaria: `SkinTint applies the skin tone as a soft-light blend, not a multiply`.

El pelo (HairTint, técnica `Hair`) multiplica por `lerp(1, TintColor, color de vértice.g)`. Desde el
2026-10-16 el visor lo reproduce: en las piezas de pelo **con** colores de vértice el factor se escribe
por vértice (`SceneViewModel::apply_hair_vertex_tint`, tinte linealizado porque Qt usa los colores de
vértice tal cual) y el material queda blanco; antes se multiplicaba por el color de vértice completo y los
vértices con verde 0 salían negros en lugar de sin tinte. Medido en el pelo vanilla de Ingrid: 4,3 % de
los vértices con verde < 0,9 (mínimo 0), R = G = B; el pelo de Lydia y Serana no trae colores de vértice
(sin cambio). Las zonas de tinte completo no cambian (captura de pelo idéntica píxel a píxel).

### Mapas de normales en espacio de modelo (`_msn`)

Skyrim decodifica `n = rgb.xzy · 2 − 1` en el espacio de la forma y, con skinning, lo gira con la matriz
de hueso mezclada (`Lighting.hlsl`, `MODELSPACENORMALS`). Qt solo acepta normal maps tangentes, así que
`rendering::bake_model_space_normals` hornea cada `_msn` por malla: para cada texel cubierto por un
triángulo, `n_ts = [T B N]⁻¹ · n` con la base interpolada de la pose de reposo (antes del skinning);
Qt ilumina con la base ya deformada, `R [T B N] n_ts = R n`, el resultado del juego. Los bordes de las
islas UV se rellenan (4 pasadas) y lo no cubierto queda plano.

Hallazgo con datos reales: en las formas `_msn` de cuerpos, manos y cabezas, las **normales de vértice
del NIF no concuerdan con la geometría** (producto escalar medio 0,1–0,4; Skyrim nunca las lee), mientras
que el `_msn` concuerda con las normales geométricas (0,82–0,99) con la decodificación `xzy` (la mejor de
las 48 combinaciones de canales y signos en todas las mallas). El visor iluminaba esas formas con las
normales del NIF; ahora las reconstruye desde la geometría (por área, soldadas por posición para no crear
costuras en los cortes de UV), regenera las tangentes desde las UV y hornea con esa base.

Salvaguarda: si la concordancia media normal-de-vértice/`_msn` de una malla es < 0,5, no se hornea
(`ModelSpaceNormalsMismatch`) y se usan las normales de vértice; si se hornea, `ModelSpaceNormalsBaked`
con la concordancia. Perfil real: Ingrid cuerpo 0,99, pies 0,94, manos 0,99, cabeza 0,82; Lydia y Serana
cuerpo 3BA 0,96–0,97, manos 0,99, cabezas Helen/Priscilla 0,86–0,88. Pruebas: `Model-space normal maps
bake to the tangent space of the mesh` (identidad, base girada, convención, relleno) y la escena sintética
(normales falsas → geométricas, mapa plano horneado). Coste medido: el horneado suma 1,5–2,3 s a la
carga de una escena con cuerpo, manos y cabeza `_msn` (Ingrid 4,8 → 6,4 s; Lydia 3,0 → 5,2 s).

### Lo que sigue siendo aproximado frente a Skyrim

- Iluminación: luz direccional + ambiente de Qt Quick 3D con rugosidad fija 0,8 y sin especular de
  Skyrim, sin dispersión subsuperficial (`skin`) ni retroiluminación; un mismo color puede verse algo
  distinto en zonas que miran a la luz y en las que no.
- `_msn`: la base tangente se interpola por texel; en mallas muy deformadas por FaceGen (cabezas) el
  mapa base y la geometría concuerdan menos (0,82–0,88) y el relieve fino puede diferir algo.
- Color: las texturas se muestran como sRGB y la mezcla se hace en gamma, como el modo sin iluminación
  lineal del juego; con la iluminación lineal de Community Shaders el resultado final del juego varía.
- Pelo: el verde del color de vértice se reproduce; la especular anisótropa del pelo de Skyrim no.

## Texturas

- Se elige el **mip almacenado** más grande que cabe (2048 px color, 1024 px normales); sin mips, filtro de caja. `TexturesReduced` lo indica.
- Presupuesto de 512 MiB de RGBA8 por escena; si se agota, se baja la resolución a la mitad (hasta 256 px) en vez de dejar una pieza en blanco. Primero se cargan las texturas de la cabeza.
- Los mapas de normales en espacio de modelo (`_msn`) se hornean a espacio tangente por malla (ver «Mapas de normales en espacio de modelo»); si no concuerdan con la malla, se usan las normales de vértice con aviso.

## Equipamiento

El juego equipa los objetos del atuendo en orden y cada uno desplaza a los anteriores que comparten slot: queda el **último** (`PieceUnequipped`). Las piezas de piel (cuerpo, manos, pies) se ocultan bajo cualquier pieza del atuendo que comparta slot.

## Inspector y diagnóstico (solo visor)

El botón ⓘ muestra, para la geometría seleccionada: forma, pieza, NIF y proveedor MO2, nodo, registro (ARMO/ARMA o FaceGen del NPC), slots, shader y flags, `NiAlphaProperty`, modo alfa efectivo y su motivo, alfa del material, transformación UV, cada textura (ruta, origen suelto/BSA, tamaño, mip, formato, estadísticas de alfa, composición), vértices/triángulos, límites, transformación, skinning, huesos del NIF y ausentes, colores de vértice, tangentes, XML SMP, motivo de ocultación y advertencias.

Diagnósticos temporales: aislar, ocultar, malla de alambre, normales, tangentes, solo difusa, canal alfa (shader `qml/shaders/alpha_channel.*`), sin iluminación, UV y **Restaurar**. No modifican recursos, el proyecto `.snps` ni el mod exportado.

Implementación: cada pieza es un `Node` con modelos fijos (normal, canal alfa, UV) que solo cambian `visible`, y la visibilidad vive en `SceneViewModel.visibility`, separada de `parts`. Qt Quick 3D libera un recurso (geometría, textura) cuando pierde su última referencia y **no reenvía sus datos** si vuelve a referenciarse; recrear delegados o intercambiar `geometry` producía modelos vacíos o texturas en blanco (antes, tras la vista UV el visor quedaba vacío).

## Verificación

- Unitarias (`tests/unit/engines/test_assets_rendering.cpp`) con NIF generados con nifly, DDS BC1/BC3/BC7/RGBA hechos a mano y un VFS sintético de dos mods: traducción de alfa, mips, alfa real BC3/BC7, FaceTint, orden de equipamiento, tangentes, huesos SMP, XML SMP, formas no dibujadas, jerarquía de nodos, colores de vértice, UV degeneradas, resolución MO2 y BSA dañado.
- Local `[local][diag]`: volcado completo de la escena real de un NPC (`SNS_LOCAL_NPC`, por defecto Lydia `0A2C8E:Skyrim.esm`).
- `scripts/smoke-test.ps1 -VerifyViewport [-ShowWindow] [-CaptureSet <dir>]`: D3D11 nativo; comprueba píxeles reales, selección, inspector, cámara, ocultación, UV, diagnósticos, restaurar y diseño compacto; falla con avisos QML o errores de compilación de shaders. `-CaptureSet` guarda 5 capturas con cámara absoluta fija (rostro, con pelo, cuerpo, armadura de acero vanilla, atuendo propio) y mide píxeles cubiertos y casi blancos. La selección (`View3D.pick`) solo se puede probar con la ventana expuesta (`-ShowWindow`); en modo oculto se informa como *SKIPPED*.
