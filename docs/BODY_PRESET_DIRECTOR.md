# Body Preset Director — presets de BodySlide por NPC (Body Change NG)

**Fecha:** 2026-10-24 · **Plan:** SNS-WIN-CUSTOMIZATION-MASTERPLAN v1.0, RC-A punto 3 (BOD-01..04)
**Carpeta:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2` (sin Git, por decisión del propietario) · **Build:** `build/sns-win`
**Estado:** `INTEGRACION_WINDOWS_OK` + `PAQUETE_MO2_VERIFICADO` en el perfil real `BodyChangeNG-Test`; **`PENDIENTE_SKYRIM`**.

## Qué hace el usuario

Pestaña **Cuerpo** del creador → sección «Presets de BodySlide (Body Change NG)»:

1. Ve el estado del perfil: Body Change NG instalado o no, choque con OBody NG, RaceMenu (BodyMorph), familia global de
   `settings.json`, presets leídos, nombres repetidos, archivos ilegibles y la distribución actual (proveedor y reglas).
2. Ve el cuerpo efectivo del NPC elegido: malla del cuerpo (pieza de ranura 32 de su piel), mod que la aporta (suelta o BSA),
   TRI de morphs (`BODYTRI`) y si existe, familia detectada con la evidencia en lenguaje llano, e incidencias.
3. **Operación A — asignar un preset:** busca por nombre/archivo/mod, filtra por familia o «solo asignables»; cada preset tiene un
   veredicto para ese NPC (Compatible, Experimental, Requiere recursos, Bloqueada) con sus motivos. «Asignar al NPC» solo se activa
   para Compatible, o Experimental con consentimiento explícito; Bloqueada y Requiere recursos se rechazan también en el C++.
4. **Operación B — cambiar de familia (3BA ↔ UBE):** solo se evalúa (nunca es «Compatible»: cambia malla, manos, pies, cuello y a
   menudo la raza). No se exporta desde aquí.
5. **Exportar:** la regla del NPC (scope `npc`, objetivo `{plugin, formId}` inequívoco, con `file` cuando el nombre del preset se
   repite) se pone **delante** de las reglas que ya tenía el perfil (BCNG usa la primera que coincide) y se exporta como un **mod
   nuevo** en una carpeta elegida fuera de MO2 y del juego, con README, `manifest.json` y ZIP opcional.

## Arquitectura (sin duplicar motores)

| Capa | Archivo | Papel |
|---|---|---|
| Motor | `engines/body/src/body_analysis.cpp` (nuevo) | Implementa lo que `body_analysis.hpp` declaraba: `inspect_environment`, `BodyInspector` (lee la malla vía VFS/BSA), `assess_preset`, `assess_family_change`, JSON del worker. |
| Motor | `bodyslide_presets.cpp`, `bcng_rules.cpp` (existentes) | Biblioteca de presets (ganador MO2), clasificación de familias como BCNG, lectura/escritura estricta schema 8. |
| Worker | `body.inspectNpcs`, `export.bodyDistribution` (existentes) | Hechos del registro ganador y empaquetado seguro (staging, read-back, manifiesto, ZIP). |
| IPC | `engines/ipc/.../protocol.hpp` | Constantes de los dos métodos 1.7 que faltaban en C++. |
| Viewmodel | `apps/desktop/viewmodels/body_director.*` (nuevo) | `studio.body`: entorno, análisis del NPC (hilo del pool, respuestas viejas descartadas), presets con veredicto, borrador y exportación. |
| QML | `qml/charactercreator/BodyPresetSection.qml` (nuevo) dentro de `BodyCustomizer.qml` | La sección descrita arriba. |

## Detección de familia (evidencia, no adivinanza)

Orden: nombres de las formas de la malla → ruta de la malla y del TRI (`!UBE\…`) → EditorID de la raza → (evidencia débil, marcada
`FamilyFromFramework`) el único framework corporal activo. El texto se tokeniza como BCNG clasifica presets. Un cuerpo del grupo CBBE
se marca 3BA si sus formas lo nombran (`3BA`, `3BA_Vagina`) o si tiene huesos de física de 3BA; si no, «CBBE sin 3BA». Sin evidencia la
familia queda **Desconocida** y solo se permiten asignaciones manuales experimentales. Esto es lectura del estudio, no una copia del
algoritmo interno de BCNG en tiempo de ejecución.

Comprobado en el perfil real: Lydia → malla de «Farodadestin» con formas `3BA 3BA_Vagina 3BA_Anus` (CBBE 3BA); Serana → malla de
«…Priscilla…» (CBBE 3BA); Ingun y Aela → `femalebody_1.nif` de «BodyChangeNG - BodySlide Output» con forma `CBBE` y sin huesos de
física (CBBE); Ulfric → `MaleBody_1.NIF` dentro de `Skyrim - Meshes0.bsa`, sin TRI (Desconocida; los 101 presets, todos femeninos,
bloqueados).

## Seguridad

- No se escribe en MO2 ni en el juego: la carpeta pasa `refused_export_directory` en la app y `ExportDestinationPolicy` en el worker
  (probado: exportar a `F:\Mods Skyrim\mods` se rechaza y no se crea nada).
- Si el `BodyChangeNGdistribution.json` actual no se puede leer, **no se exporta** (se perderían sus reglas). Los comentarios del
  original no se copian; el original no se modifica (SHA-256 igual antes y después de la ejecución).
- Para que el archivo exportado gane, el mod nuevo debe quedar con más prioridad que el proveedor actual en MO2 (lo dice el README).

## Pruebas y evidencia

- Catch2 `[body]`: 9 casos / 185 aserciones (4 nuevos en `tests/unit/engines/test_body_analysis.cpp`).
- Escenario real `--stress-appearance body-director` (no está en «all»): `out/local-implementation/sns-win-20261024-body/run*/`.
- BOD-04 (nombre repetido → regla con `file`) está cubierto por pruebas unitarias; el perfil real no tiene nombres repetidos.

## Pendiente

- `PENDIENTE_SKYRIM`: instalar el ZIP en un perfil MO2 aislado, Skyrim cerrado, y comprobar el cuerpo del NPC en el juego.
- Reglas por raza/facción/`all` (automáticas) y la UI de familias masculinas no están en este incremento.
- Miniaturas de presets: no se generan (no hay vista previa fiable del preset sobre el cuerpo); la lista no muestra imágenes falsas.
