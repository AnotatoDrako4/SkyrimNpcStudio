# INFORME DE COMPATIBILIDAD: CUERPOS Y PRENDAS (CBBE / 3BA / UBE / UNP)

**Identificador:** `SNS-DOC-BODY-OUTFIT-COMPATIBILITY-2026-10`  
**Fecha:** 2026-10-24  
**Objetivo:** Especificación e implementación del motor de detección de cuerpos y análisis de compatibilidad cruzada entre prendas y anatomías.  
**Estado:** `IMPLEMENTADO_Y_VERIFICADO_OK`

---

## 1. ANTECEDENTES Y REQUERIMIENTO

En el ecosistema de modding de Skyrim SE/AE coexisten múltiples estándares anatómicos femeninos y masculinos:
1. **CBBE / CBBE 3BA (3BBB):** El estándar más extendido con soporte de física SMP/CBPC y huesos extendidos.
2. **UBE (Ultimate Body Enhancer):** Estándar de alta definición con topología propietaria, incompatible a nivel de vértices con CBBE.
3. **UNP / BHUNP:** Familia basada en UNP con topología diferente a CBBE.
4. **Vanilla / Default:** Mallas originales de Bethesda.

Cuando un NPC utiliza un cuerpo específico (por ejemplo, UBE o 3BA) y se le equipa una prenda diseñada para otra familia sin soporte de deslizadores (sliders/BodySlide), se producen deformaciones severas, costuras visibles en cuello, muñecas y tobillos (*neck/wrist/ankle seams*) y colapso de vértices al aplicar morphs de peso (`Weight`).

---

## 2. ARQUITECTURA DE DETECCIÓN Y CLASIFICACIÓN

### 2.1. Extensión del Guardián de Compatibilidad (`CompatibilityGuardian`)
Se extendió el enum `DetectedBodyType` en `engines/appearance/include/sns/appearance/compatibility_guardian.hpp`:
```cpp
enum class DetectedBodyType {
    Vanilla,
    CBBE,
    TBD,
    UNP,
    SevenB,
    BHUNP,
    HighPoly,
    Cbbe3BA,
    UBE,       // <-- Nuevo estándar añadido
    Unknown
};
```

En `engines/appearance/src/compatibility_guardian.cpp`, la función `inspect_body_system` analiza los archivos de malla y carpetas de textura del VFS de MO2:
- Reconoce firmas UBE en rutas como `meshes/actors/character/ube/`, nombres de archivo que contienen `ube` o geometrías con patrones de vértices propios de UBE.
- Clasifica con precisión entre Vanilla, CBBE clásica, CBBE 3BA y UBE.

---

## 3. MOTOR DE ANÁLISIS DE PRENDAS (`assess_outfit_mesh_compatibility`)

Se implementó el analizador estricto de mallas de atuendo en `engines/body/src/body_analysis.cpp` y su cabecera `engines/body/include/sns/body/body_analysis.hpp`.

### 3.1. Criterios de Evaluación:
Para cada archivo de malla de atuendo:
1. **Existencia en VFS (`mesh_exists`):** Confirma que el archivo `.nif` exista físicamente en el disco o dentro de un archivo BSA montado.
2. **Variantes de Peso (`has_weight_variants`, `paired_mesh_exists`):**
   - Comprueba si la malla termina en `_0.nif` o `_1.nif`.
   - Busca automáticamente su contraparte (si tiene `_1.nif`, verifica que exista `_0.nif` y viceversa).
   - Si falta una de las variantes, emite una advertencia de peso fijo (*single-weight variant*), informando que la prenda no se deformará con el deslizador de peso del NPC.
3. **Morfología TRI (`has_tri_morphs`):**
   - Comprueba si existe un archivo `.tri` asociado con el mismo nombre base o en la carpeta `BODYTRI`.
   - Verifica si la malla incluye morphs de conformación para cambios dinámicos.
4. **Clasificación de Familia de Malla (`mesh_family`):**
   - Determina si la malla pertenece a `BodyFamily::Cbbe3ba`, `BodyFamily::Cbbe`, `BodyFamily::Ube`, u `BodyFamily::Other` examinando nombres de shapes internos (`3BA`, `CBBE`, `UBE`, etc.) y rutas.
5. **Evaluación de Compatibilidad Cruzada (`compatible`, `warning_level`):**
   - **CBBE 3BA NPC + CBBE 3BA Prenda:** `compatible = true`, `warning_level = None`.
   - **CBBE 3BA NPC + CBBE Clásica:** `compatible = true`, `warning_level = Info` (compatible, sin físicas avanzadas de senos/glúteos).
   - **CBBE 3BA NPC + UBE Prenda:** `compatible = false`, `warning_level = Warning` ("La prenda pertenece a la familia UBE pero el NPC utiliza un cuerpo CBBE/3BA. Puede provocar roturas de malla y costuras visibles").
   - **UBE NPC + CBBE/3BA Prenda:** `compatible = false`, `warning_level = Warning` ("La prenda pertenece a la familia CBBE pero el NPC utiliza UBE").

---

## 4. INTEGRACIÓN Y EXPOSICIÓN AL USUARIO

El guardián de compatibilidad integra estos resultados en el informe de compatibilidad general:
- Emite `CompatibilityIssue` con severidad `Warning` o `Info`.
- Sugiere acciones correctivas deterministas:
  - Generar la prenda en BodySlide para el preset del cuerpo actual.
  - Seleccionar una variante de prenda compatible con la anatomía instalada.

---

## 5. COBERTURA DE PRUEBAS AUTOMATIZADAS

Se desarrollaron y verificaron pruebas unitarias dedicadas:
1. `tests/unit/engines/test_compatibility_guardian.cpp`:
   - Detección de cuerpos UBE y CBBE 3BA.
   - Emisión de advertencias cruzadas ante discordancia de familia anatómica.
2. `tests/unit/engines/test_body_analysis.cpp`:
   - Detección de mallas pares `_0.nif` y `_1.nif`.
   - Detección de mallas unitarias y reporte de variantes ausentes.
   - Detección de archivos TRI de morfología.
   - Matriz de compatibilidad familia vs cuerpo.

Todas las pruebas se ejecutaron y pasaron al 100% (cero fallos).
