# Beta candidata 1 (versión 0.1.0)

Fecha: 2026-10-15. Objetivo: beta estable el 2026-12-15. Esta candidata es para probar, no para
publicar: nada se ha comprobado todavía dentro de Skyrim.

El ZIP se genera con `.\scripts\release.ps1` desde el commit de esta rama
(`dist\SkyrimNpcStudio-0.1.0-<commit>-win64.zip` + `.sha256`); el hash del ZIP de la candidata
figura en el PR que la propone, porque cambiaría al escribirlo aquí.

## Estado por función

| Función | Estado | Evidencia |
|---|---|---|
| Lectura de MO2 (instancias, perfiles, VFS, load order, conflictos), solo lectura | VERIFICADO EN WINDOWS | perfil real: 665 mods, 414 plugins, 8.686 NPC |
| Modo A — atuendo | VERIFICADO EN WINDOWS | Lydia, Shady Sam, Serana; diff semántico `DefaultOutfit` |
| Modo B — rostro (Hair Color Safe Mode, sliders, tono, Head Parts, donante, altura, peso) | VERIFICADO EN WINDOWS | Serana, Lydia, Ingrid, Aslfur; FaceGen = vista previa / = ganador salvo HairTint |
| Modo C — rostro + atuendo | VERIFICADO EN WINDOWS | Lydia, Shady Sam, Serana; diff `DefaultOutfit, HairColor` |
| NPC Safe Patch Engine (campos pedidos, máscara conservadora) | VERIFICADO EN WINDOWS | .NET 105/105; `UnauthorizedNpcChange` con 14 tipos de cambio ajeno |
| Easy Mod Creator (5 pasos, dependencias, bloqueos) | VERIFICADO EN WINDOWS (arnés) | 1100×700 – 1920×1080 medidos; revisión con la ventana visible PENDIENTE |
| Estabilidad (cambio rápido de NPC, deshacer/rehacer, proyectos) | VERIFICADO EN WINDOWS | Debug y ASan sin informes; GPU con ventana visible PENDIENTE |
| Character Forge: proyectos `.sncf`, visor GLB/glTF, selección, puente Blender | VERIFICADO EN WINDOWS | `-ForgePreview` 63/0 con ventana visible; Blender 5.2.2 18 aserciones |
| Character Forge: NIF y FaceGen de NPC nuevos | PENDIENTE | exportación final bloqueada (`readyForGame: false`, `inGameVerified: false`) |
| Release reproducible (ZIP + manifiesto + verificación sin herramientas de desarrollo) | VERIFICADO EN WINDOWS | dos reconstrucciones desde cero, mismo ZIP byte a byte |
| Mods exportados dentro de Skyrim | PENDIENTE | paquetes y lista en [SKYRIM_ACCEPTANCE_TEST.md](SKYRIM_ACCEPTANCE_TEST.md) |
| CI en GitHub Actions | BLOQUEADO | `startup_failure` por la cuenta (README, «CI en GitHub Actions») |
| Multi-NPC Pack (modo D), adaptación entre cuerpos (CBBE/3BA/UNP) | PENDIENTE | fuera de esta candidata |

Ninguna fila está en VERIFICADO EN SKYRIM.

## Para pasar de candidata a beta

1. Prueba de aceptación en Skyrim completada por una persona con capturas
   ([SKYRIM_ACCEPTANCE_TEST.md](SKYRIM_ACCEPTANCE_TEST.md)): Serana, Lydia A/C, Shady Sam A/C sin
   caras deformadas ni cambios no pedidos.
2. Revisión del Easy Mod Creator y del visor con la ventana visible (incluida la órbita con ratón).
3. Actions en marcha (acción de cuenta) o, mientras tanto, `local-review.ps1` y `release.ps1` sin fallos
   en el commit candidato.
4. Fusionar la cadena de PR #8–#13 (y el de esta candidata) en `main` tras revisarla; ningún merge es
   automático.

## Probar la candidata

1. Descomprime el ZIP en una carpeta propia (fuera de Skyrim y de MO2) y comprueba el `.sha256`.
2. Abre `SkyrimNpcStudio.exe` con MO2 cerrado. El estudio solo lee tu instancia; los mods se exportan
   a la carpeta que elijas.
3. Instala los mods generados en un perfil de MO2 de prueba, nunca en tu perfil principal.
