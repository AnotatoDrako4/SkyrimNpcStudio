# Loop de fidelidad del visor 3D (fase 2, desde 2026-10-16)

Base: PR #16 (`feature/face-body-color`, SkinTint ya validado). Rama: `feature/render-fidelity`.
Plataforma: Windows 11, MSVC 14.44, Qt 6.11.2, perfil MO2 `Default` (solo lectura). Referencia de shaders:
Community Shaders (`package/Shaders/Lighting.hlsl`, `src/ShaderCache.h`). Nada se compara con Skyrim en
ejecución: el visor sigue siendo una aproximación (ver `docs/NIF_PIPELINE.md`).

## Iteración 1 — normal maps `_msn`

- **Investigación.** Skyrim: `n = rgb.xzy · 2 − 1`; con skinning, `normalize(boneRS · n)` (técnica
  `MODELSPACENORMALS` + `SKINNED`). Qt Quick 3D: `normalMap` solo tangente, `mat3(T, B, N) · n_ts`.
  Solución viable sin renderizador nuevo: hornear por CPU el `_msn` a un mapa tangente por malla con la
  base de la pose de reposo; Qt aplica la base deformada, `R [T B N] n_ts = R n`.
- **Fixtures sintéticos** (`test_assets_rendering.cpp`): base identidad (resultado = normal decodificada
  con `xzy`), base girada 90° (la base por el valor horneado devuelve la normal), concordancia y relleno.
- **Primera prueba con datos reales: ninguna malla concordaba** (−0,53…0,34): el horneado no se aplicó
  (salvaguarda < 0,5). Sonda temporal con las 48 combinaciones de canales/signos: ninguna > 0,53 contra
  las normales del NIF; **contra las normales geométricas, la decodificación `xzy` gana en todas
  (0,82–0,99)**. Conclusión: las normales de vértice del NIF en formas `_msn` no son fiables (Skyrim no
  las lee) y el visor iluminaba con ellas.
- **Corrección.** Formas `_msn`: normales reconstruidas desde la geometría (por área, soldadas por
  posición), tangentes desde las UV, horneado con esa base; aviso/diagnóstico con la concordancia.
- **Resultado (perfil real):** todas horneadas — Ingrid cuerpo 0,99 / pies 0,94 / manos 0,99 / cabeza
  0,82; Lydia 3BA 0,96, manos 0,99, cabeza Helen 0,88; Serana 3BA 0,97, manos 0,99, cabeza Priscilla 0,86.
  Capturas `out\msn-after2\` frente a `out\skin-after\`: el cuerpo gana relieve (músculos, clavícula,
  pecho); la cara deja de verse plana. Serana conserva su cabeza (solo cambia la iluminación del visor).
- **Escena sintética:** forma `_msn` con normales falsas (+X) → normal geométrica, mapa plano horneado,
  concordancia > 0,99, `ModelSpaceNormalsBaked`.

## Iteración 2 — pelo y cejas

- **Investigación.** Skyrim `Hair`: `difusa · lerp(1, TintColor, color de vértice.g)`. Visor: `difusa ·
  tinte · color de vértice (rgb)`. Sonda temporal en el perfil real: pelo vanilla de Ingrid con 4,3 % de
  vértices de verde < 0,9 (mínimo 0), R = G = B; cejas y línea del pelo verde 1; pelo de Lydia/Serana sin
  colores de vértice.
- **Corrección.** Piezas de pelo con colores de vértice: el factor `lerp(1, tinte lineal, g)` se escribe por
  vértice al cambiar el color (material blanco); el resto no cambia. Hair Color Safe Mode no se toca (solo
  visor).
- **Resultado.** Captura de pelo de Ingrid idéntica píxel a píxel en la zona visible (solo cambian 21
  píxeles de la barra de estado): el tinte completo no cambia. Arnés (`check_hair_view`): Ingrid pelo 611
  vértices, 585 con tinte completo, **26 sin tinte (verde 0, antes negros)**, 0 fuera de
  `lerp(1, tinte, verde)`; cejas 102/102 y línea del pelo 216/216 con tinte completo; Lydia sin colores de
  vértice (sin cambio).

## Comparación reproducible

`outender-compare\<npc>-<toma>.png`: misma cámara (`-CaptureSet`, 1600×900) en tres etapas — antes,
SkinTint, SkinTint + `_msn` — para Ingrid, Lydia y Serana (cuerpo y rostro). Coste del horneado: Ingrid
4,8 → 6,4 s, Lydia 3,0 → 5,2 s de carga.

## Materiales de piel (prioridad 2)

No se cambió: el visor usa rugosidad 0,8 y metalness 0 con la iluminación de Qt; la especular (`_s`), la
dispersión subsuperficial y la retroiluminación de Skyrim no tienen equivalente demostrable en
`PrincipledMaterial` sin un material propio, y la fase pedía no inventar equivalencias. El modo «Sin
iluminación» sigue disponible para separar textura de sombreado. Documentado en `NIF_PIPELINE.md`.

## Regresiones

ctest 140, 0 fallos; Serana `hair-safe, switch-npc-rapid, creator-layout` 361 comprobaciones; Ingrid
`skin-export` (FaceGen y FaceTint exportados idénticos a la vista previa), `hair-single, hair-sequence`
110; Lydia `hair-single`. SkinTint y FaceTint (pruebas unitarias) sin cambios.
