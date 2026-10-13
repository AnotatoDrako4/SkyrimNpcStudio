# Native NPC Creator — implementación de Fase 2

**Goal:** crear NPC originales dentro de Character Forge, guardar .sncf y exportar plugins técnicos verificables sin suplantar FaceGen.
**Spec:** solicitud Fase 2 aportada por el usuario y `2026-10-12-character-forge.md`.
**Architecture:** NativeNpcDraft independiente en C++/JSON, UI Qt propia, DTO/RPC creation y NativeNpcBuilder Mutagen separado de los builders de overrides. El exportador comparte staging y empaquetado existentes.
**Tech Stack:** C++20, Qt 6.11.2, .NET 10.0.401, Mutagen 0.54.4, xUnit 4.0.1.

## Restricciones globales
- No compartir borradores ni modificar .snps; migrar .sncf v1 a v2 al cargar.
- Mantener main reciente, sin push a main ni merge remoto.
- Toda NPC_/OTFT nueva pertenece al plugin generado; dependencias retienen sus FormKeys.
- FaceGen nativo no está certificado: bloquear exportación final, permitir solo exportación técnica explícita con manifiesto y README inequívocos.
- Windows requerido para C++/Qt/CTest/UI; ejecutar pruebas .NET sintéticas en Linux con SQLite nativo del paquete fijado.

## Secuencia y contratos
1. Estabilización: rama desde Fase 1, integrar main, verificar 49 pruebas existentes. La API GitHub 403 limita consulta de estado y creación de PR; Git HTTPS funciona.
2. Worker: `NativeNpcDraft`, `CreationValidateRequest/Response`, `CreationExportRequest/Response`; RPC `creation.validateNpc`, `creation.exportNpc`, `creation.searchRecords`. Validar tipos de referencias, identidad, raza/sexo, estadísticas, slots/ARMA, headparts. Tests primero para nueva NPC, OTFT, inventario, masters, ESL, cancelación, rechazo y rollback. Build/relectura con Mutagen y empaquetado atómico.
3. Motor: NativeNpcDraft JSON versionado con estadísticas, apariencia, equipo y dependencias; `ForgeDraft::set_native_npc` conserva undo/redo. Persistencia v2 y migración v1 sin escribir durante carga. Catch2 de roundtrip, migración, límites e independencia.
4. Interfaz: conectar WorkerClient al ViewModel propio; formularios compactos por secciones y selectores de registros mediante RPC; invalidar resultados al editar y evitar operaciones simultáneas. Mostrar FaceGen pendiente y botón final bloqueado; exportación técnica deliberada.
5. Entrega: pruebas RPC y suites .NET, revisión de diff, documentación de evidencia/limitaciones, commits y PR hacia Fase 1 si la API permite hacerlo.

## Review Focus
- JSON corrupto o versión futura: rechazar sin sobrescribir; v1 carga con defaults.
- Valores no finitos, FormKeys de tipo incorrecto y EditorID duplicado: rechazo estructurado.
- Exportación cancelada, repetida o destino existente: no dejar archivos parciales ni sobrescribir.
- Respuesta RPC después de editar/cambiar proyecto/perfil: no reutilizar validación obsoleta.
- FaceGen ausente: ninguna ruta debe anunciar un paquete listo para Skyrim.

## Ledger
- Preparación: SDK verificado SHA-512, restore locked y build 0 warnings/errors, 34 unit + 15 export verdes en main.
- Decisión: seguir la ejecución autónoma expresamente solicitada, sin nueva aprobación de diseño.
- Base: c35a9f8 (Fase 1), main e3c770b incorporado mediante merge local.
