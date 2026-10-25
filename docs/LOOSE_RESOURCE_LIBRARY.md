# Biblioteca de Packs de Ropa sin ESP y Proyectos BodySlide (RC-A Punto 4)

**Fecha:** 2026-10-24 · **Plan:** SNS-WIN-CUSTOMIZATION-MASTERPLAN v1.0, RC-A punto 4 (RES-01..03, Épica C)
**Carpeta:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2` (sin Git, cambios en local) · **Build:** `build/debug`
**Estado:** `INTEGRACION_WINDOWS_OK` + `TESTS_UNITARIOS_OK` en el perfil real `BodyChangeNG-Test`.

---

## 1. Alcance y Arquitectura

La biblioteca de recursos sueltos (`LooseOutfitIndexer` en C++ y `LooseResourceLibrary` en el viewmodel) permite el descubrimiento, clasificación y navegación visual de contenido de ropa y armadura que no cuenta con un plugin ESP/ESL/ESM en el orden de carga, o que define proyectos de ajuste en BodySlide.

### Reglas Clave:
1. **RES-01 (Sin registros inventados):** Las mallas sueltas (`kind: "loose-mesh"`) no crean registros ARMO ficticios (`armorFormKey: ""`, `equippable: false`). La equipación directa en actores queda bloqueada hasta su posterior conversión o registro formal.
2. **RES-02 (Detección de Replacers):** Cuando una malla suelta coincide con la ruta de una armadura registrada en el orden de carga activo, se clasifica como `replacer-mesh` vinculada al registro ARMO existente (`confidence: "confirmed"`).
3. **RES-03 (Seguridad e Integridad):**
   - Sanitización de rutas (`is_safe_data_path`): rechaza rutas absolutas, unidades de disco (`C:`, `F:`) y secuencias de escape transversal (`..`).
   - Validación de formato NIF en cabecera (`"Gamebryo File Format"`): archivos dañados o con firmas extrañas marcan `previewable: false` con diagnóstico explicativo sin provocar excepciones ni bloqueos.
4. **Proyectos BodySlide (`kind: "bodyslide-project"`):**
   - Indexación de `.osp` y `.xml` bajo `CalienteTools\BodySlide\SliderSets`.
   - Extracción de nombres de set, rutas de salida esperadas y formas objetivo.
   - Estado: `convertible: "requires-bodyslide-build"`, `previewable: false`.

---

## 2. Optimización de Rendimiento en Disco Externo (F:)

En el perfil real `BodyChangeNG-Test`, los mods se ubican en una unidad externa con 819 archivos `.osp` y más de 95,000 archivos virtuales:

1. **Lectura directa de tamaño y búfer de 8 KB:**
   En lugar de invocar `std::filesystem::file_size` y `Load` completo de la malla en memoria (que leía gigabytes de geometría y vértices), se lee un único búfer de 8 KB por malla.
2. **Inferencia de familias sin carga 3D:**
   La tabla de cadenas y nombres de forma del formato SSE NIF se sitúa al inicio del archivo; la extracción léxica de tokens en los primeros 8 KB deduce familias (`CBBE 3BA`, `UBE`, `BHUNP`, `CBBE`, `UNP`) en microsegundos sin parsear geometría con `nifly::NifFile`.
3. **Filtro temprano de candidatos (`is_clothing_mesh_candidate`):**
   Descarta modelos de suelo (`_gnd.nif`, `_go.nif`), vistas en primera persona (`1stperson`) y morphs faciales, reduciendo a la mitad los accesos I/O innecesarios.

---

## 3. Evidencia en el Perfil Real `BodyChangeNG-Test`

Ejecución automatizada en ventana mediante `scripts/smoke-test.ps1`:
```powershell
.\scripts\smoke-test.ps1 -MO2Profile BodyChangeNG-Test -Npc 0A2C8E:Skyrim.esm -StressAppearance loose-resources -StressWork out/local-implementation/sns-win-20261024-loose/run1 -Screenshot out/local-implementation/sns-win-20261024-loose/run1/smoke.png
```

### Resultados (`loose-resources-report.json`):
- **Total de recursos descubiertos:** 7,810
  - Mallas sueltas (`loose-mesh`): 6,300
  - Proyectos BodySlide (`bodyslide-project`): 1,510
  - Replacers (`replacer-mesh`): 0 en la muestra inicial
- **RES-01:** Verificado (`equippable: false`, sin FormKey falso en 6,300 mallas).
- **RES-02:** Verificado.
- **Búsqueda y filtrado:** `boots` devolvió 239 prendas.
- **Selección y deselección:** Verificadas con eventos reales.
- **QML Warnings:** 0 (limpio).
- **Código de salida:** 0.

---

## 4. Pruebas Globales
- **Suite Catch2 motores (`sns_engine_tests`):** 59 aserciones en 4 casos `[loose]` PASS.
- **Suite global `ctest`:** 314/314 pruebas PASS (0 fallos, 10 omitidas por dependencias opcionales como Blender).
