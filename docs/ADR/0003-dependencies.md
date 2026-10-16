# ADR 0003 — Dependencias fijadas

**Estado:** aceptada (2026-10-11). Versiones comprobadas en sus registros oficiales ese día; regla: no usar publicaciones de menos de ~2 semanas.

| Dependencia | Versión | Licencia | Dónde | Verificación |
|---|---|---|---|---|
| Qt (Base, Declarative, Quick3D, ShaderTools…) | 6.11.2 msvc2022_64 | LGPLv3 / **GPLv3 (Quick3D)** / comercial | App | Archivos del repositorio oficial con SHA-1 verificado (`scripts/fetch_qt.py`) |
| Mutagen.Bethesda.Skyrim | 0.54.4 (2026-08-08) | GPL-3.0-only | Worker | NuGet, `listed`; API comprobada por reflexión y compilación |
| StreamJsonRpc | 2.25.29 | MIT | Worker | NuGet |
| MessagePack (transitiva de StreamJsonRpc) | 2.5.305 (2026-09-23) | MIT | Worker | NuGet; fijada el 2026-10-16 por GHSA-qhrr-8q5h-9q3h (NU1902), corregida en 2.5.303 |
| Microsoft.Data.Sqlite | 10.0.12 | MIT | Worker | NuGet |
| nlohmann/json | 3.12.0 | MIT | Motores | `URL_HASH SHA256=42f6e95c…` |
| Catch2 | 3.16.0 | BSL-1.0 | Tests C++ | `URL_HASH SHA256=0957cae5…` |
| xunit.v3 | 4.0.1 | Apache-2.0 | Tests .NET | NuGet, Microsoft.Testing.Platform |
| nifly | cca0a770094bb962fb28ea1fec5ea903e68fda8e | GPL-3.0 | Motores | Fuente/API, hash, fixture NIF y recursos locales comprobados |
| LZ4 | 1.10.0 | BSD-2-Clause | Motores | SHA256 fijado; fixtures y BSA 105 locales |
| DirectXTex | May 2026 (2.1.1) | MIT | Texturas | Release oficial, API comprobada; SHA256 204ae68a…; descompresión CPU |

- Versiones .NET centralizadas en `Directory.Packages.props` con `packages.lock.json` (`RestorePackagesWithLockFile`).
- La auditoría de NuGet sigue activa y sus avisos son errores (`TreatWarningsAsErrors`): un aviso nuevo en una
  dependencia transitiva rompe el restore. Se corrige fijando la transitiva en `Directory.Packages.props`
  (`CentralPackageTransitivePinningEnabled`, que solo afecta a los paquetes listados), nunca desactivando la
  auditoría. MessagePack queda fijada hasta que StreamJsonRpc dependa de una versión corregida.
- aqtinstall 3.3.0 no entiende el nuevo layout del repositorio de Qt (`qt6_6112/qt6_6112_msvc2022_64/`); por eso `scripts/fetch_qt.py` descarga los archivos `.7z` directamente y verifica su `.sha1`.
- Versiones de Mutagen `7.1.0` y `23.4.0` en NuGet son paquetes antiguos *unlisted* con TFM net5.0: no usar.
