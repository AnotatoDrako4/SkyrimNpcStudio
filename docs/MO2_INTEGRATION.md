# Integración con Mod Organizer 2

Todo es **solo lectura**: el estudio nunca escribe en `modlist.txt`, `plugins.txt`, `loadorder.txt`, la carpeta `overwrite/`, `mods/` ni en `Data/` del juego. Funciona con MO2 cerrado y no depende de USVFS.

Probado contra una instancia real: MO2 2.5.2 portable, perfil con 660 entradas de mods (585 proveedores activos, 84 619 archivos sueltos), 415 plugins activos, SkyrimSE.exe 1.6.1170.

## Instancias

| Tipo | Detección |
|---|---|
| Global | `%LOCALAPPDATA%\ModOrganizer\<nombre>\ModOrganizer.ini` (`discover_global_instances`) |
| Portable | Selección manual del `ModOrganizer.ini` o su carpeta; se considera portable si junto al ini hay `ModOrganizer.exe` o `portable.txt` |

`ModOrganizer.ini` usa el dialecto INI de **QSettings** ([`ini.cpp`](../engines/mo2/src/ini.cpp)):

- claves con *percent-encoding* (`Python%20Proxy`) y `\` como separador de subgrupo (`1\binary` → `1/binary`);
- valores entre comillas con escapes C (`\\`, `\"`, `\xHHHH`), listas separadas por comas fuera de comillas;
- `@ByteArray(...)` contiene texto (p. ej. `gamePath=@ByteArray(D:\\Games\\...)`), `@Variant(...)` contiene binario y se ignora.

Rutas: `base_directory` (por defecto la carpeta de la instancia) y `mod_directory`, `profiles_directory`, `overwrite_directory`, `download_directory` con el marcador `%BASE_DIR%`. También se leen `skip_file_suffixes` (por defecto `.mohidden`) y `skip_directories`.

## Perfil

- `modlist.txt` está **invertido**: la primera línea es la de *mayor* prioridad. `+` activo, `-` inactivo, `*` mod externo (`*DLC: …`, `*Creation Club: …`, `*Unmanaged: …`, sus archivos viven en `Data/`). Los nombres que terminan en `_separator` son separadores sin archivos.
- `plugins.txt`: `*` = activo. No lista los masters implícitos.
- `loadorder.txt`: orden completo según MO2; se usa solo para diagnosticar discrepancias (`LoadOrderTxtMismatch`).
- `settings.ini`: `LocalSaves`, `LocalSettings`, `AutomaticArchiveInvalidation`.

## Dos ejes distintos

| Eje | Decide | Fuente | Implementación |
|---|---|---|---|
| Prioridad de mods | qué **archivo** gana | `modlist.txt` | `VirtualFileSystem` |
| Orden de carga | qué **registro** gana | implícitos + `Skyrim.ccc` + `plugins.txt` | `resolve_load_order` + índice del worker |

## Sistema de archivos virtual

`VirtualFileSystem::build` reproduce la superposición: `Data/` del juego (prioridad mínima) → mods activos en orden de prioridad → `overwrite/` (máxima). Para cada ruta (clave insensible a mayúsculas, separador `\`) guarda los proveedores en orden ascendente; el último gana. Se excluyen `meta.ini` en la raíz de cada mod, los sufijos y directorios configurados.

Consultas: `resolve`, `providers_of`, `conflicts(mod)` (archivos que gana/pierde), `root_plugins`.

Rendimiento medido: 84 619 archivos en 22,8 s con disco frío y 4,6 s en caliente. Corre en segundo plano con progreso y cancelación.

### Limitaciones actuales

- **Contenido de BSA no indexado.** Un recurso que no aparece como archivo suelto puede estar dentro de un BSA; por eso las mallas no encontradas se reportan como *warning* (`MeshNotLoose`), nunca como error. Lector de BSA: Fase 2.
- No se replica el comportamiento de USVFS en tiempo de ejecución (p. ej. archivos creados por herramientas lanzadas desde MO2): se refleja el estado en disco, incluida `overwrite/`.

## Orden de carga efectivo

1. Masters oficiales presentes: `Skyrim.esm`, `Update.esm`, `Dawnguard.esm`, `HearthFires.esm`, `Dragonborn.esm`.
2. Creation Club según `<juego>/Skyrim.ccc`, si el archivo existe en el VFS.
3. Entradas activas de `plugins.txt` en su orden.
4. *Partición de masters*: el motor carga los plugins con flag ESM (o extensión `.esm`/`.esl`) antes que el resto; si `plugins.txt` los lista después, se reordenan y se informa `MasterHoisted`.

Las cabeceras TES4 se leen en C++ (solo los primeros bytes) para conocer flags ESM/ESL y masters. Diagnósticos: `PluginMissing`, `PluginHeaderUnreadable`, `MissingMaster`, `MasterAfterDependent`, `TooManyPlugins` (254 completos), `TooManyLightPlugins` (4096), `UnlistedPlugin`, `LoadOrderTxtMismatch`.

## Idioma de los nombres

Los plugins localizados (como `Skyrim.esm`) guardan los nombres en archivos `.STRINGS`. `detect_game_language` lee `sLanguage` de `Skyrim.ini`/`SkyrimCustom.ini`: los del perfil si `LocalSettings=true`; si no, los de `Documentos\My Games\Skyrim Special Edition*`; si no existen, los del perfil. El worker lo pasa a Mutagen (`StringsReadParameters.TargetLanguage`) y forma parte de la firma de caché. Si un nombre no puede leerse, el registro se indexa sin nombre y el plugin reporta un único `StringsUnavailable`.

> Caso real detectado: `ccBGSSSE001-Fish.bsa` tenía su cabecera a ceros (no es un BSA válido), por lo que 213 nombres de ese CC no se pudieron resolver. Steam → *Verificar integridad de los archivos* lo repara.

## Instalación del mod exportado

Si Skyrim fue movido, **Carpeta de Skyrim** y CLI `--game` permiten una ruta propia del estudio guardada por instancia en QSettings. Se exige `SkyrimSE.exe` y `Data`; `ModOrganizer.ini` permanece intacto. No se elige automáticamente entre instalaciones distintas.

Fase actual: exportar a carpeta y ZIP; el usuario lo instala con *Instalar mod desde archivo* en MO2. No se modifica la instancia automáticamente. La importación directa asistida (crear la carpeta del mod en `mods/` con MO2 cerrado y confirmación del usuario) está planificada para una fase posterior.
