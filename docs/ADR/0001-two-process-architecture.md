# ADR 0001 — Estudio C++/Qt + worker .NET por JSON-RPC sobre named pipe

**Estado:** aceptada (2026-10-11)

## Contexto

La edición fiable de plugins de Skyrim SE requiere Mutagen (.NET). La aplicación de escritorio y el futuro visor 3D requieren C++/Qt. El prompt prohíbe microservicios web y dependencias cloud.

## Decisión

- Dos procesos locales. El estudio lanza el worker y se comunica por JSON-RPC 2.0 sobre un named pipe con framing `Content-Length` (LSP). Servidor: StreamJsonRpc; cliente: implementación propia en `engines/ipc`.
- El worker **no conoce MO2**: recibe la lista de plugins ya resuelta (ruta del archivo ganador). La lógica de MO2 vive solo en `engines/mo2`.
- El worker posee el índice SQLite (Mutagen está allí) y toda escritura de plugins.

## Consecuencias

- Fallos de Mutagen o de un plugin corrupto no tumban la interfaz; el worker se puede reiniciar.
- La frontera de proceso obliga a contratos explícitos y versionados (DTOs en `Contracts/` y `protocol.hpp`).
- Coste: serialización JSON (despreciable frente a E/S de plugins; una búsqueda típica tarda milisegundos).
