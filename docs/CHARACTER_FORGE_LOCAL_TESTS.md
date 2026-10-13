# Character Forge — verificación local pendiente

La nube preparó .NET y comprobó pruebas sintéticas y sintaxis. **No se ha compilado ni ejecutado la aplicación Windows desde esta sesión.** Ningún resultado certifica el renderizado, Blender, MO2 o Skyrim. Los paneles con motores pendientes muestran acciones inactivas.

## Capacidades y límites

| Área | Implementado | Pendiente |
| --- | --- | --- |
| NPC original | DTO/RPC 1.3, validación, builder Mutagen, NPC_/OTFT/ACHR interior opcional y relectura | Assets reales y validación dentro de Skyrim |
| Proyecto | .sncf v2, migración v1 en memoria, guardado atómico, undo/redo propio, workspace contextual | Ejecución de Catch2 y UI Windows |
| Viewport | Contenedor para escena de creación, toolbar y estado vacío explícito | Backend de geometría original; no usa la escena del NPC seleccionado |
| Modelos | Archivos GLB/glTF/FBX/OBJ/BLEND; nombre/tamaño, metadatos glTF 2.0 y cabecera BLEND; biblioteca de referencias | Importación de buffers/geometría, previsualización y adaptación NIF |
| Blender/MCP | Selector, detección en PATH, configuración no verificada, interfaces C++ tipadas | Bridge, versión real y conexión MCP |
| IA | Descripción, proveedor futuro e imágenes de referencia verificadas; persistencia local | Adaptadores, costos confirmados y generación; no se usan APIs/claves |
| Equipo | Catálogo ARMO/ARMA, slots/raza/sexo, inventario y OTFT propio | Disponibilidad de assets reales y física en juego |
| Materiales/esqueleto | Referencias y opacidad como borrador; estado de rigging sin huesos ficticios | Escritura DDS/NIF, análisis del rig y skinning |
| Exportación | Wizard de cinco pasos y paquete técnico opt-in con SHA-256/manifest/ZIP | FaceGen NIF + FaceTint DDS certificados; exportación final bloqueada |

Los archivos externos se guardan como rutas, sin copiar contenido. Un proyecto creado en otra máquina puede requerir seleccionar nuevamente modelos, imágenes y texturas. No se modifican .snps ni Skyrim/MO2.

## Compilación y registros en Windows

Desde la raíz del checkout de `feature/character-forge-ui`, con los requisitos del README:

```powershell
New-Item -ItemType Directory -Force out/forge-review | Out-Null
.\scripts\build.ps1 *>&1 | Tee-Object out/forge-review/build.log
# Si la compilación termina bien, repetir los checks individualmente para aislar fallos:
dotnet test --solution SkyrimNpcStudio.slnx *>&1 | Tee-Object out/forge-review/dotnet.log
ctest --preset debug -j 4 --output-on-failure *>&1 | Tee-Object out/forge-review/ctest.log
. .\scripts\dev-env.ps1
.\build\debug\bin\SkyrimNpcStudio.exe --smoke-test --screenshot out/forge-review/smoke.png 2> out/forge-review/qml-smoke.log
```

No usar `-SkipTests` como evidencia de pruebas aprobadas. Revisar cada código de salida y resúmenes; los tests `[local]` requieren SNS_LOCAL_* y deben contabilizarse como omitidos si no hay configuración. En caso de error, conservar los logs, el commit, la versión Qt/.NET y el paso exacto que falló.

## Checklist manual (todos pendientes)

- [ ] Inicio sin errores QML; tercer workspace sin seleccionar un NPC existente.
- [ ] Navegación completa, selección visible y panel derecho contextual; redimensionar 1280×720, 1440×900 y 1920×1080. El viewport debe conservar el protagonismo.
- [ ] Crear .sncf, editar identidad/estadísticas/rostro/ropa/inventario; cambiar de sección y comprobar valores; guardar y reabrir.
- [ ] Abrir .sncf v1: migración en memoria sin escribir; guardar en v2 deliberadamente. Rechazar versiones futuras/no enteras, JSON corrupto y opciones de tipo incorrecto sin sobrescribir.
- [ ] Undo/redo de NPC, materiales, preparación IA y navegación; abrir otro proyecto y restaurar su sección activa. Mantener Character Creator y Outfit Studio independientes.
- [ ] Cargar perfil MO2 e indexar. Seleccionar raza/voz/clase/HDPT/armaduras reales; errores por FormKeys incorrectos, slots y ARMA incompatibles.
- [ ] Cambiar perfil después de validar: búsqueda/validación/exportación deshabilitadas hasta finalizar el nuevo índice; no reutilizar respuesta antigua.
- [ ] Seleccionar GLB/glTF: confirmar tamaño y counts contra el archivo; rechazar cabeceras/truncamientos inválidos. FBX/OBJ solo selección de archivo; BLEND cabecera básica, sin promesa de conversión.
- [ ] Biblioteca real: seleccionar nuevamente un archivo, moverlo y confirmar estado ausente; nunca mostrar modelo genérico como preview.
- [ ] Blender: detectar PATH/elegir executable; estado configurado no verificado. Abrir/sincronizar/importar/exportar/MCP deben permanecer inactivos hasta implementar bridge.
- [ ] IA: conservar descripción/proveedor/imágenes front/side/back; rechazar archivos no legibles; Generar inactivo y sin consumo de API.
- [ ] Materiales: referencias y opacidad se guardan, pero no cambian DDS/NIF. Esqueleto vacío sin datos reales; pruebas de NIF/rig quedan para el motor futuro.
- [ ] Wizard: resumen, dependencias, validación, destino y exportación técnica; advertencia explícita. Comprobar ESP nuevo y manifest `readyForGame:false`, `inGameVerified:false`, FaceGen pendiente.
- [ ] Cancelar operación, caída del worker, ruta inválida y nombre ya existente: mensajes claros, sin sobrescritura ni staging parcial.
- [ ] Regresión completa de Character Creator, Outfit Studio, .snps, MO2 y visor existente usando datos locales.
- [ ] FaceGen: implementar receta original → NIF/DDS, verificar geometría/rutas/morphs/tintes y solo entonces considerar habilitar exportación final. No instalar los paquetes técnicos actuales.
- [ ] Después de completar FaceGen y los controles anteriores, verificar en un perfil de ensayo de Skyrim. Mantener `inGameVerified:false` hasta documentar esa prueba real.

## Evidencia Cloud

SDK 10.0.401, runtime 10.0.12. Restore `--locked-mode`, build .NET sin warnings/errors, 35 unit/RPC y 30 export pasan. En Linux se ejecutan DLL administradas con `libe_sqlite3.so` del paquete fijado 2.1.12 en salidas ignoradas. No se cambian lockfiles ni RuntimeIdentifier win-x64.

Los 23 QML Forge pasan parseo con `qmlformat` 6.11.2 y están registrados en CMake. Syntax-only C++20 de fuentes de creación y pruebas portables de serialización/esquema pasan. Estas comprobaciones no sustituyen MOC, compilación MSVC/Qt, CTest o prueba visual Windows. La revisión de componentes con ViewModel simulado se usó solo como análisis estructural, nunca como evidencia de integración real.
