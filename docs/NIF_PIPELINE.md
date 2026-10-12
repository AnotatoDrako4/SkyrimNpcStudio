# Pipeline NIF / DDS / visor 3D (Fase 2 — diseño, sin implementar)

Nada de este documento está implementado todavía. El visor muestra un aviso explícito en su lugar.

## Objetivo

Mostrar al NPC seleccionado con **recursos reales** del perfil: cuerpo, cabeza (FaceGen), pelo y piezas del atuendo en edición, con texturas DDS, normales/tangentes, transparencias y *skinning*, indicando siempre el nivel de fidelidad:

1. Vista preliminar (geometría sin todas las texturas).
2. Vista con materiales y geometrías reales cargadas.
3. Vista con recursos validados para exportación.

Nunca se afirmará que la vista es idéntica al juego (shaders, físicas SMP/FSMP y animaciones no se reproducen).

## Cadena prevista

```
VFS (sueltos)  ─┐
BSA (nuevo)    ─┼─> resolver ruta Data\… ─> bytes ─┬─> nifly: NiTriShape/BSTriShape, BSDismemberSkinInstance,
                │                                  │   BSLightingShaderProperty, BSShaderTextureSet
                │                                  └─> DDS ─> decodificador ─> QImage/QQuick3DTextureData
                └─> geometría ─> QQuick3DGeometry (posiciones, normales, tangentes, UV, índices, joints/weights)
                                  └─> Model en View3D con PrincipledMaterial/CustomMaterial aproximado
```

- **nifly** (GPL-3.0, C++17, CMake): ya verificado (último commit 2026-09-14). Se integrará como dependencia fijada por commit en `engines/nif`, compilado como C++17 dentro del proyecto C++20.
- **BSA**: lector propio o biblioteca compatible GPL en `engines/assets`; requisito previo para encontrar mallas/texturas vanilla.
- **DDS**: evaluar DirectXTex (MIT) frente a decodificación propia de BC1/BC3/BC5/BC7; requisito: BC7 (habitual en SE).
- **Skinning**: `Skin` + `Joint` de Qt Quick 3D con el esqueleto `skeleton.nif`/`skeleton_female.nif` del perfil.
- **Picking**: `View3D.pick()` → pieza → selección en el editor de atuendos.

## Pruebas previstas

- Unitarias con NIF/DDS sintéticos generados en los tests (nunca assets del juego en el repositorio).
- Integración local `[local]`: cargar la cabeza FaceGen y el cuerpo del NPC seleccionado del perfil real.
- Capturas automáticas con `--screenshot` para regresiones visuales.
