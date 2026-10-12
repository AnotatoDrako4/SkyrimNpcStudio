# ADR 0004 — Licencia GPL-3.0-only

**Estado:** aceptada (2026-10-11)

- **Qt Quick 3D** se distribuye solo bajo GPLv3 o licencia comercial (no LGPL) — comprobado en la documentación oficial de Qt.
- **Mutagen** es GPL-3.0-*only*; **nifly** es GPL-3.0.

El estudio enlaza Qt Quick 3D (Fase 2) y el worker enlaza Mutagen, así que el repositorio completo se publica como **GPL-3.0-only** (texto en `LICENSE`).

Distribución:

- Se debe ofrecer el código fuente correspondiente, incluidas las versiones exactas de dependencias (ver ADR 0003).
- Los mods exportados por el usuario **no** son obra derivada del estudio; el estudio no copia assets de terceros (modo dependiente).
- No se incluyen assets de Bethesda ni de otros autores en el repositorio: los tests usan plugins sintéticos generados en tiempo de ejecución.
