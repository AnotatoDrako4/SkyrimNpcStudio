# Character Forge 3.0 — SkinnedMesh pipeline (incremento 3, primer vertical)

Checkout `D:\.Proyectos Games\SkyrimNpcStudio-ui2`, `main` @ f5b87b0, **sin commit**. Build aislada `build/claude-forge`.
Evidencia: `out/local-implementation/forge-skinned-20261103/`. Estado: `TESTS_UNITARIOS_OK` · `INTEGRACION_WINDOWS_OK` · `PENDIENTE_SKYRIM`.

## Implementado
- `tools/blender/forge_skyrim.py`: `profile` static|skinned; `armatures[].bonesDetail` (bind en espacio NIF con la función de
  PyNifly), `meshes[].skin` (pesos, sin peso, >4 influencias, SBP, muestras de pesos). Exporta con `rotate_bones_pretty` de la armature.
- Defecto corregido: en segundo plano PyNifly 29.1 usa `rotate_bones_pretty=False` y escribía el skin-to-bone girado 90° en X
  (medido con su propia API). Ahora el job pasa el valor de `PYN_ROTATE_BONES_PRETTY`.
- `engines/creation/skeleton_mapping`: verified / suggested (solo textual, requiere humano) / unrecognized / incompatible.
- `skyrim_conversion`: `classify` skinned, `verify_nif` skinned (skin, huesos, influencias, particiones, bind vs Blender y
  vs referencia, pesos), `skinned_states` (cadena estricta PREVIEW_ONLY → PENDIENTE_SKYRIM).
- App: `runSkyrimJob(..., profile)`, `attach_skeleton_mapping` (referencia solo lectura: `SNS_FORGE_REFERENCE_SKELETON` o
  `skeletons/Skyrim/skeleton.nif` de PyNifly), `package_export` por perfil; mod STAT rechazado para skinned. Viewmodel:
  `skyrimProfile`, `skyrimConversion.{profile, skeletonMapping, states}`. QML sin cambios.
- Límite verificado en nifly: SSE limita vértices y triángulos de BSTriShape a 65 535 y trunca sin error. Particionado: no implementado.

## Pruebas
`sns_engine_tests "[skyrim]"` 9 casos / 161 aserciones PASS · `sns_forge_asset_tests "[skinned]"` (Blender real) 2 / 33 PASS ·
regresión completa tras skinned: NO EJECUTADA. NIF verificado: 130 v, 216 tri, 4 huesos, 16/16 checks, SHA-256
`F07A2C4E82E6073A56C55F3DD197843CC3CAB0A19D7BC4747D7DB0FF33404EAE`.

## Goblin (`G:\Body_RETOPO_glutes_v1.blend`, SHA-256 F539FD99…6DF51, intacto)
`RIG_INSPECTED`, no compatible: mallas de 89 184 y 89 745 v; `Guide_Breasts` sin armature; 20 huesos con nombres Blender
(ninguno de Skyrim); solo 5 con pesos (sin brazos ni pierna derecha); ~53 000 vértices sin normalizar; sin SBP; metros;
texturas ausentes. Requiere re-rig sobre el esqueleto de Skyrim.

## Contrato de preview (Antigravity)
Pedir: `convertSelectedModelWithBlender()` · estado: `blenderBusy`, `scene.state` · GLB: `workspace.model.path` / `scene.filePath` ·
cancelar: `cancelOperation()` · errores: `lastError`, `scene.message`. **Pendiente**: progreso (`forge_bridge.py` no emite
`SNS_FORGE_PROGRESS`) e invalidación (falta `sourceSha256` del .blend en el modelo GLB).
