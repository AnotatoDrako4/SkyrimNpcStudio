# Third-party notices

Skyrim NPC Studio is licensed under GPL-3.0-only (see `LICENSE`). It uses:

| Component | License | Use |
|---|---|---|
| Qt 6.11 (Qt Base, Declarative, Quick Controls; Quick 3D from Phase 2) | LGPL-3.0 / GPL-3.0 (Qt Quick 3D: GPL-3.0 or commercial) | Desktop application |
| Mutagen.Bethesda.Skyrim 0.54.4 and its dependencies (Loqui, Noggog.CSharpExt) | GPL-3.0-only | Plugin reading and writing in the worker |
| StreamJsonRpc 2.25.29 (and Nerdbank.Streams, Microsoft.VisualStudio.Threading) | MIT | JSON-RPC server in the worker |
| Microsoft.Data.Sqlite 10.0.12 + SQLitePCLRaw / SQLite | MIT / Public Domain | Catalog cache |
| nlohmann/json 3.12.0 | MIT | JSON in the C++ engines |
| nifly cca0a770094bb962fb28ea1fec5ea903e68fda8e | GPL-3.0 | Real NIF geometry and skin reading |
| LZ4 1.10.0 | BSD-2-Clause | BSA 105 decompression |
| DirectXTex May 2026 (2.1.1) | MIT | CPU DDS BC1-BC7 decompression |
| Catch2 3.16.0 | BSL-1.0 | C++ tests (not distributed) |
| xunit.v3 4.0.1 | Apache-2.0 | .NET tests (not distributed) |

The Elder Scrolls V: Skyrim is © Bethesda Softworks / ZeniMax Media. This project contains no game data:
all test plugins are generated synthetically at test time. Mod Organizer 2 is a separate GPL-3.0 project;
this studio only reads its configuration files.
