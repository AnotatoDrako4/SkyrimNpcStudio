# ADR 0005 — Códigos de error de aplicación fuera del rango reservado

**Estado:** aceptada (2026-10-11)

## Contexto

La primera versión usaba −32001..−32009, dentro del rango "server error" que JSON-RPC deja a la implementación. Un test de protocolo falló: el cliente StreamJsonRpc interpretó nuestro `RecordNotFound = −32003` como `ResponseSerializationFailure` y lanzó `RemoteSerializationException`. StreamJsonRpc ya usa −32000..−32004.

## Decisión

Códigos de aplicación positivos **1001–1099** (`ProtocolMismatch` 1001 … `InvalidArgument` 1009). Se mantienen los estándar (−32700, −32600, −32601, −32602, −32603) y −32800 (cancelado, como LSP). El cliente C++ usa −1 (timeout) y −2 (desconectado), que nunca viajan por el cable.

## Consecuencia

Cualquier cliente JSON-RPC distingue sin ambigüedad errores de dominio de errores de la biblioteca.
