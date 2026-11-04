# Skyrim NPC Studio — Visual QA Lab (`AG03`)

## 1. Arquitectura General

El **Visual QA Lab** (`tools/visual-qa-lab/`) es un sistema independiente en C++20 diseñado para validar geométrica y visualmente las escenas 3D generadas por `Skyrim NPC Studio` (`sns::rendering::CharacterScene`), sin volver a parsear mallas ya cargadas ni modificar los motores compartidos de `engines/body/` o `engines/rendering/`.

### Componentes principales

| Componente | Cabecera | Implementación | Responsabilidad |
|---|---|---|---|
| **Tipos y zonas anatómicas** | `include/sns/visual_qa/visual_qa_types.hpp` | `src/clipping_analyzer.cpp` | Veredictos (`PASS`, `FAIL`, `SKIP`, `INCONCLUSIVE`), 20 zonas anatómicas en espacio de vista Y-up, roles de superficie (`SurfaceRole`) y trazabilidad de incidencias (`VisualQaIssue`). |
| **Motor de detección de clipping y ajuste** | `include/sns/visual_qa/clipping_analyzer.hpp` | `src/clipping_analyzer.cpp` | Broad-phase AABB, árbol espacial BVH por forma, intersección exacta 3D triángulo-triángulo, protrusión hacia el exterior según normales, análisis cilíndrico de holgura radial (`TOO_SMALL_CLIPPING`, `TOO_LARGE_FLOATING`, `WELL_FITTED`) y exclusión de falsos positivos (slots Biped cubiertos, formas ocultas NIF, proxies de colisión SMP `VirtualGround`/`TopC`/`PantsC`). |
| **Detector de costuras anatómicas** | `include/sns/visual_qa/seam_analyzer.hpp` | `src/seam_analyzer.cpp` | Extracción de lazos de borde abierto reales (soldando costuras UV internas por posición 3D) y medición punto-a-segmento bidireccional en `NeckHead`, `WristHandLeft`, `WristHandRight`, `AnkleFootLeft` y `AnkleFootRight`. Calibra tolerancias según `scene.scale` y distingue `Aligned`, `AbnormalGap`, `Intersection`, `Misalignment`, `ScaleMismatch` y `CoveredByGarment`. |
| **Validador de morphs BodySlide** | `include/sns/visual_qa/morph_analyzer.hpp` | `src/morph_analyzer.cpp` | Parsers binarios independientes para archivos `PIRT` (`.tri`) y `OSD\0` (`.osd`). Compara Estado A (sin preset) vs Estado B (con preset) y clasifica en `ExactBodySlideMorph`, `ApproximateGeometricMorph`, `LegitimateZeroChange`, `FalseNoChange`, `OutfitMorphNotApplied` y `AbnormalDeformation`. |
| **Capturas reproducibles y comparador visual** | `include/sns/visual_qa/image_comparator.hpp` | `src/image_comparator.cpp` | 8 encuadres controlados (`01_front_full` .. `08_full_outfit`), codificación/decodificación PNG vía WIC, mapa de calor de diferencias antes/después, filtrado 3x3 de ruido de GPU/antialiasing y alerta de geometría desaparecida o zonas vacías. |
| **Inspector de integridad de escena** | `include/sns/visual_qa/scene_inspector.hpp` | `src/scene_inspector.cpp` | Detecta mallas ausentes/ilegibles, piezas ocultas inesperadamente, defectos de interpolación de peso `_0.nif`/`_1.nif`, triángulos con picos o `NaN`/`Inf`, fugas de cuerpos incrustados (`CoveredBodyVisibleLeak`) e incompatibilidades entre familias corporales (`CBBE 3BA` vs `UBE`). |
| **SNS Visual Regression Suite** | `include/sns/visual_qa/regression_suite.hpp` | `src/regression_suite.cpp` | Ejecuta los 10 escenarios automatizados `VQA-01` .. `VQA-10` combinando fixtures deterministas y datos reales del perfil MO2 (`Serana 002B6C:Dawnguard.esm` y `Lydia 0A2C8E:Skyrim.esm`). |
| **Generador de informes y hashes** | `include/sns/visual_qa/report_writer.hpp` | `src/report_writer.cpp` | Genera `report.json`, `summary.md`, `geometry-diff.json`, `issues.json` y `artifact-hashes.json` (SHA-256) dentro de `out/visual-qa/<run-id>/`. |

---

## 2. Compilación y Ejecución en Windows

Desde PowerShell en la raíz del repositorio:

```powershell
. .\scripts\dev-env.ps1
cmake -S tools/visual-qa-lab -B build/ag03-vqa -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ag03-vqa --parallel 8

# Ejecutar la suite de pruebas Catch2 (incluye VQA-01..VQA-10):
.\build\ag03-vqa\bin\sns_visual_qa_tests.exe

# Ejecutar el CLI independiente y generar un informe en out/visual-qa/<run-id>/:
.\build\ag03-vqa\bin\sns-visual-qa.exe --mo2-ini "D:/Apps/ModOrganizer.ini" --profile "Default" --run-id "serana-qa-01"
```
