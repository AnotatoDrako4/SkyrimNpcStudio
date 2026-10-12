# ADR 0002 — Motores C++ independientes de Qt

**Estado:** aceptada (2026-10-11)

## Decisión

`engines/*` son bibliotecas C++20 que solo dependen de la STL, Win32 y nlohmann/json. Los view models de `apps/desktop` los envuelven y convierten tipos (`std::string` UTF-8 ↔ `QString`, `nlohmann::json` ↔ `QVariant`).

## Motivos

- Se compilan y prueban sin el SDK de Qt (`cmake --preset engines`), lo que acelera CI y permitió verificar los motores antes de tener Qt instalado.
- Separación estricta exigida: la interfaz no puede escribir plugins ni NIF; los motores no saben nada de QML.
- Reutilizables desde herramientas de línea de comandos o tests de integración.

## Coste

Conversión de tipos en la frontera y no poder usar `QLocalSocket`/`QProcess` dentro de los motores (se usan named pipes overlapped y `CreateProcessW` + Job Object directamente).
