# Character Forge v5 — plataforma de preparación de assets

Estado: `EN_DESARROLLO`  
Rama objetivo: `main`  
Regla: cada incremento debe ser reproducible, reversible y no debe escribir en `Skyrim/Data`, MO2 ni partidas.

## Objetivo de v5

Character Forge v5 deja de ser únicamente un conversor Blender → NIF y pasa a ser una plataforma que inspecciona, diagnostica, prepara, verifica y empaqueta assets para Skyrim.

La aplicación no debe afirmar compatibilidad dentro del juego basándose solo en una build o una prueba local. El estado `PENDIENTE_SKYRIM` permanece hasta que exista evidencia humana dentro de Skyrim.

## Perfiles funcionales

| Perfil | Alcance | Estado |
|---|---|---|
| `static` | Props sin huesos, NIF, DDS, STAT y ZIP | `INTEGRACION_WINDOWS_OK` |
| `skinned` | Armature, huesos, pesos, bind pose, particiones y morphs | `EN_DESARROLLO` |
| `outfit` | Slots, Armor Addons, `_0/_1`, BodySlide y TRI | `NO_INICIADO` |
| `npc` | FaceGen, FaceTint, Head Parts y FormKey | `BLOQUEADO` hasta cerrar dependencias de seguridad |

Cada perfil debe declarar sus entradas, validaciones, reparaciones permitidas, artefactos y limitaciones.

## Incrementos

### V5.0 — Fiabilidad y contrato común

- Runner de fiabilidad local.
- Matriz de cobertura.
- Estados deterministas `PASS`, `FAIL`, `SKIP`, `PENDIENTE_SKYRIM`.
- Hashes de entradas y salidas.
- Staging atómico y limpieza tras error.
- Relectura de NIF, DDS, manifiesto y ZIP.
- Diagnóstico CTest/Unicode sin renombrado masivo.

### V5.1 — Inspector de assets

- Conteo de vértices, triángulos, UV, normales, tangentes y bounds.
- Detección de texturas ausentes, duplicadas o fuera de la carpeta permitida.
- Detección de transformaciones sospechosas.
- Detección de geometría degenerada.
- Informe técnico JSON y HTML.

### V5.2 — Perfil skinned

- Mapeo contra skeleton de referencia.
- Huesos desconocidos o ausentes.
- Vértices sin pesos.
- Pesos no normalizados.
- Bind pose inconsistente.
- Particiones incompatibles.
- Verificación de pesos y triángulos contra Blender.

### V5.3 — Perfil outfit/armor

- Variantes `_0` y `_1`.
- Slots y Armor Addons.
- Integración controlada con BodySlide/Outfit Studio.
- TRI y morphs.
- Diagnóstico de cobertura y conflictos.

### V5.4 — Herramientas y automatización

- Detección de Blender, PyNifly, BodySlide, Outfit Studio y NifSkope.
- Versiones visibles y comprobadas.
- Timeouts, cancelación y logs por herramienta.
- CLI de validación por manifiesto.
- Procesamiento por lotes en staging.
- Modo simulación sin escritura final.

### V5.5 — Flujo de usuario

Importar → Inspeccionar → Diagnosticar → Preparar → Previsualizar → Exportar → Verificar → Empaquetar.

Cada hallazgo debe mostrar severidad, causa, consecuencia, reparación disponible y evidencia.

## Contrato de herramienta externa

Toda herramienta externa debe tener:

1. Detección de disponibilidad.
2. Versión y ruta verificadas.
3. Invocación sin shell concatenado.
4. Timeout explícito.
5. Cancelación cooperativa.
6. Captura de stdout/stderr sin secretos.
7. Limpieza de procesos y staging.
8. Resultado verificable por archivos, hashes o readback.

Una herramienta ausente produce diagnóstico accionable; no se convierte silenciosamente en una simulación.

## Gates de entrega

- `G1`: contrato y fixtures revisados.
- `G2`: pruebas unitarias y de integridad pasan.
- `G3`: prueba Windows con logs y hashes.
- `G4`: regresión de Character Creator y Outfit Studio.
- `G5`: paquete aislado reproducible.
- `G6`: aceptación humana dentro de Skyrim, si aplica.

Ningún incremento puede saltar de `G3` a `VERIFICADO_EN_SKYRIM` sin `G6`.

## Próximo incremento autorizado

Implementar el inspector skinned como módulo aislado de diagnóstico y sus fixtures, sin modificar el conversor, el ViewModel ni el contrato que Claude esté editando. El primer resultado será un informe determinista de armature, pesos, huesos y bind pose; la exportación skinned seguirá bloqueada hasta superar sus gates.
