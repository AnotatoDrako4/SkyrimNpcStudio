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

## Texturas

- Se elige el **mip almacenado** más grande que cabe (2048 px color, 1024 px normales); sin mips, filtro de caja. `TexturesReduced` lo indica.
- Presupuesto de 512 MiB de RGBA8 por escena; si se agota, se baja la resolución a la mitad (hasta 256 px) en vez de dejar una pieza en blanco. Primero se cargan las texturas de la cabeza.
- Los mapas de normales en espacio de modelo (`_msn`) no se reproducen: se usan las normales de vértice y la pieza lo avisa.

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
