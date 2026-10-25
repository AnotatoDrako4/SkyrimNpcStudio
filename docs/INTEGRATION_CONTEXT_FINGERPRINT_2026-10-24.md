# Integración de la nueva app — Context Fingerprint

**Fecha:** 2026-10-24  
**Repositorio documental:** `D:\.Proyectos Games\SkyrimNpcStudio-ui2`  
**Repositorio de implementación inspeccionado:** `D:\.Proyectos Games\SkyrimNpcStudio`  
**Fase:** F0 / B1 — integración de contexto MO2/VFS  
**Estado:** `IMPLEMENTADO_NO_COMPILADO`

## Objetivo

Hacer visible y auditable el contexto efectivo de Mod Organizer 2 utilizado por la aplicación para cargar NPC, resolver recursos, previsualizar y preparar exportaciones.

La huella evita tratar resultados asíncronos de un perfil anterior como si pertenecieran al perfil actualmente seleccionado.

## Cambios realizados

### Snapshot MO2/VFS

Archivo:

`apps/desktop/viewmodels/mo2_snapshot.hpp`

Se añadió:

```cpp
std::string context_fingerprint;
```

La propiedad pertenece al snapshot inmutable del perfil y no habilita escrituras en MO2, Skyrim/Data, Overwrite ni partidas.

### Cálculo determinista

Archivo:

`apps/desktop/viewmodels/studio_controller.cpp`

Durante el escaneo del perfil se construye una identidad canónica con:

- ruta de `ModOrganizer.ini`;
- nombre del perfil;
- ruta del juego;
- idioma detectado;
- mods, estado habilitado y prioridad;
- plugins y estado activo;
- load order;
- plugin ganador y proveedor MO2.

La identidad se codifica con longitudes de campo y se resume mediante SHA-256. No se usan tiempos de modificación del sistema de archivos, por lo que el resultado no cambia solo por `mtime`.

### Integración visual

Archivo:

`apps/desktop/qml/Main.qml`

La barra inferior muestra:

```text
CONTEXTO <primeros 10 caracteres del SHA-256>
```

El tooltip expone la huella completa para diagnóstico. El indicador solo aparece cuando el estudio está listo.

## Evidencia

Confirmado en el checkout de implementación:

- `VirtualFileSystem` en `engines/mo2/`;
- `AssetResolver` en `engines/assets/`;
- `CommandStack` en `engines/projects/`;
- `services/BethesdaWorker/` y sus proyectos .NET;
- `apps/desktop/qml/Main.qml` y `StatusBadge.qml`.

## Validación ejecutada

| Validación | Resultado |
|---|---|
| Revisión de diff | PASS para los archivos de la integración |
| Revisión de referencias (`rg`) | PASS |
| `cmake --preset engines` | SKIP: CMake no disponible |
| Build C++/Qt | SKIP: CMake/Qt no disponibles |
| `dotnet --version` | FAIL/BLOQUEADO: no hay .NET SDK |
| Tests .NET | SKIP/BLOQUEADO: `No .NET SDKs were found` |
| MO2 real | NO VERIFICADO |
| Skyrim in-game | PENDIENTE_SKYRIM |

## Limitaciones

- El cambio está aplicado en `SkyrimNpcStudio`, mientras este repositorio recibe el registro documental.
- El checkout de implementación tenía modificaciones previas; no fueron revertidas.
- La huella identifica el contexto lógico cargado, pero no demuestra que el resultado sea jugable en Skyrim.
- La compilación y las pruebas deben repetirse en un entorno Windows con CMake, Ninja/MSVC, Qt y .NET SDK compatibles.

## Siguiente incremento

1. Confirmar el SHA/base de trabajo y la rama RC.
2. Compilar la aplicación y ejecutar pruebas unitarias.
3. Cargar dos perfiles aislados y comprobar que generan huellas distintas.
4. Cambiar de perfil durante una operación y verificar que el resultado antiguo se descarta.
5. Registrar captura de la UI y hash del binario final.

No se autoriza instalación en MO2 real, escritura en `Skyrim/Data` ni prueba con partidas personales como parte de este cambio.
