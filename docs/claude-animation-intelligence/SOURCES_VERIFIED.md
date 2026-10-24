# Verified sources

Mission `CLAUDE-B-ANIM-01`, checked 2026-10-22. A URL alone proves nothing, so each row
below names the file and symbol that was actually read, and how. The Fallout 4 port
`DCCStudios/F4-OpenAnimationReplacer` was deliberately **not** consulted: its schema may
diverge and nothing here is derived from it.

Source copies were downloaded to a scratch directory and grepped locally; the commands are
reproducible.

```bash
curl -sSL -o Parsing.cpp    https://raw.githubusercontent.com/ersh1/OpenAnimationReplacer/main/src/Parsing.cpp
curl -sSL -o Conditions.cpp https://raw.githubusercontent.com/ersh1/OpenAnimationReplacer/main/src/Conditions.cpp
curl -sSL -o Conditions.h   https://raw.githubusercontent.com/ersh1/OpenAnimationReplacer/main/src/Conditions.h
curl -sSL -o OpenAnimationReplacer.cpp \
  https://raw.githubusercontent.com/ersh1/OpenAnimationReplacer/main/src/OpenAnimationReplacer.cpp
```

## VERIFIED

| # | claim | source and how it was checked |
|---|-------|-------------------------------|
| 1 | `modlist.txt`: first line is the HIGHEST priority; `+` enabled, `-` disabled, `*` foreign, `#` comment, `*_separator` is a separator and not a data provider | `engines/mo2/src/profile.cpp::parse_modlist` in this repository. It reverses the top-down list so `index == priority`, and sets `ModKind::Separator` for a name ending in `_separator`. Our `mo2_profile.parse_modlist` mirrors it exactly. |
| 2 | OAR submod `config.json` fields | `Parsing.cpp::DeserializeSubMod`. `name` is **required** (`kInfoOnly`/`kFull`); `description` optional; `priority` is **required and must be an int** (`doc.FindMember("priority") ... IsInt()`), otherwise the submod is rejected; `disabled` optional bool; plus `overrideAnimationsFolder`, `replacementAnimDatas` (with `projectName`, `path`, `disabled`, `variants[].{filename,disabled,weight,playOnce}`), and the deprecated `disabledAnimations`, `keepRandomResultsOnLoop`, `shareRandomResults`. |
| 3 | `user.json` precedence is a whole-section replacement, not a per-field merge | `Parsing.cpp::ParseModSubdirectory` lines 1025-1066. With both files present it reads `config.json` in `DeserializeMode::kInfoOnly` (name + description only) and `user.json` in `kDataOnly` (everything else). With only `config.json` it uses `kFull`. A legacy submod reads `user.json` alone. Our `oar_discovery` implements precisely this and test `O04` pins it. |
| 4 | mod-group `config.json` fields | `Parsing.cpp::DeserializeMod`: `name` required, `author` and `description` optional, `conditionPresets[]` with `{name, description, conditions[]}`. No `priority` at group level. |
| 5 | 124 built-in condition names | `OpenAnimationReplacer.cpp`, `_conditionFactories.emplace("<Name>", ...)`. Extracted with `grep -oE '_conditionFactories\.emplace\("[A-Za-z0-9]+"'`. Cross-checked against `Conditions.h` `GetName()` returns. |
| 6 | 23 legacy-only condition names that are valid but hidden from the OAR menu | `OpenAnimationReplacer.cpp`, `_hiddenConditionFactories.emplace(...)`: `IsEquippedRightType`, `IsEquippedLeftType`, `IsEquippedRight`, `IsEquippedLeft`, `IsEquippedRightHasKeyword`, `IsEquippedLeftHasKeyword`, `IsActorValueEqualTo/LessThan`, `IsActorValueBaseEqualTo/LessThan`, `IsActorValueMaxEqualTo/LessThan`, `IsActorValuePercentageEqualTo/LessThan`, `IsFactionRankEqualTo/LessThan`, `IsLevelLessThan`, `CurrentGameTimeLessThan`, `CurrentTarget`, `CurrentTargetFactionRank`, `CurrentTargetHasKeyword`, `ValueEqualTo`, `ValueLessThan`. **This row corrected a real false positive**: before it existed the scan flagged 10 legitimate DAR rule sets as `CONFIG_ERROR` at `high` severity. |
| 7 | legacy DAR `_conditions.txt` grammar | `Conditions.cpp::CreateConditionFromString`: a line starting with `;` is a comment; a leading `NOT` negates (`substr(3)` then trim); the function name runs to the first space or `(`; the argument is the text between `(` and the first `)`; a line with neither space nor `(` is a bare condition name; an unregistered name becomes an `InvalidCondition`. `Parsing.cpp::ConditionsTxtFile::GetConditions`: a line ending in `OR` opens an OR block, otherwise lines are AND-joined. |
| 8 | legacy condition aliases | `Conditions.cpp::CorrectLegacyConditionName` maps `IsEquippedShout` to `IsEquippedPower`; `CorrectConditionName` maps `CurrentPackageProcedureType` to `CurrentPackageType`. |
| 9 | `_CustomConditions` is the reserved DAR directory and its numeric subdirectory is the priority | `Parsing.cpp` lines 895 and 1874 compare the directory stem to `"_CustomConditions"`. The second DAR layout, `<pluginName>/<formID>/`, was observed in the user's own installation. |
| 10 | a condition may declare `requiredPlugin` | `Conditions.cpp::CreateConditionFromJson` reads `"requiredPlugin"` alongside `"condition"` and `requiredVersion`. |
| 11 | Havok container header magic `57 E0 E0 57 10 C0 C0 10` | Empirical, not documentation: `xxd -l 32` over installed animation files (`EVG Conditional Idles/.../NPC Cold/female/mt_idle.hkx`, `mt_runbackward.hkx`) and a behaviour file (`00 - Normal Version/meshes/actors/character/behaviors/FNIS_HalosPoser2_Behavior.hkx`). All begin with that magic. |
| 12 | MO2 mod priority decides loose-file providers; plugin order is a different thing | <https://github.com/ModOrganizer2/modorganizer/wiki/Mod-information-window>, corroborated by `engines/mo2/src/vfs.cpp` in this repository, whose `resolve()` takes `providers.back()` after pushing providers in ascending priority. |
| 13 | Pandora patches behaviour graphs, not OAR rules | <https://github.com/Monitor221hz/Pandora-Behaviour-Engine-Plus>. Its priorities are unrelated to MO2 mod order and to OAR submod priority; the tool never equates them. |
| 15 | which plugins the game loads regardless of `plugins.txt` | `ModOrganizer2/modorganizer-game_skyrimse` `src/gameskyrimse.cpp`: `primaryPlugins()` returns `skyrim.esm, update.esm, dawnguard.esm, hearthfires.esm, dragonborn.esm` plus `CCPlugins()`, which reads every line of `gameDirectory()/Skyrim.ccc`. Checked against the user's install: `Skyrim.ccc` has 74 lines and lists `ccQDRSSE001-SurvivalMode.esl` on line 17, which `plugins.txt` carries unstarred. `loadorder.txt` is therefore not evidence of loading. |
| 16 | Skyrim BSA directory layout (36-byte header; 24-byte folder records in v105, 16 in v104; bstring folder names; 16-byte file records; null-terminated name table) | UESP "Skyrim Mod:Archive File Format", then verified **empirically** on 2026-10-23: for all 110 archives of the enabled mods and the official `Skyrim - Animations.bsa` (1 691 folders, 8 979 files, 7 699 `.hkx`), the parsed end offset equals `36 + folders*24 + folders + totalFolderNameLength + 16*files + totalFileNameLength`. Version 104 is accepted but was not present to verify. |
| 17 | `replacementAnimDatas` are per-animation settings, not targets; `"disabled": true` disables that animation | `ReplacerMods.cpp`: replacement animations are created from `animFile.fullPath`; `_replacementAnimDatas` is searched by `(projectName, path)` and applied with `LoadAnimData`; `ReplacementAnimation.cpp::LoadAnimData` calls `SetDisabled(bDisabled)` and `IsDisabled()` returns `_bDisabled || parent disabled`. Real paths look like `data\\meshes\\...\\female\\_variants_mt_runforward`. |
| 18 | a `_variants_X` folder is one replacement for `X.hkx` (OAR, not legacy DAR) | `Parsing.cpp::ConvertVariantsPath` strips `_variants_` and appends `.hkx`; `CacheAnimationFilesInDirectory` treats such a folder as one animation whose `.hkx` files are variants, only when `!a_bIsLegacy`. |
| 19 | `PRESET` conditions reference a group's `conditionPresets` by name | `Conditions.h` `PRESETCondition` adds a `ConditionPresetComponent` named `"Preset"`; validity depends on that component. Presets are read by `Parsing.cpp::DeserializeMod` from the group `config.json`. Real profile: 2 groups, 36 references. |
| 20 | load order is `plugins.txt` order after the primary plugins | `gameskyrimse.cpp` `loadOrderMechanism()` returns `LoadOrderMechanism::PluginsTxt`; primary plugins are the base masters (listed in that order) plus `Skyrim.ccc`. The engine's archive mounting order is inferred from it, not verified, so archive winners are `likely`. |
| 14 | the real OAR submod layout `.../animations/OpenAnimationReplacer/<group>/<submod>/config.json` | Observed in the user's installation: 21 `OpenAnimationReplacer` directories, e.g. `EVG Conditional Idles/.../OpenAnimationReplacer/EVGCI/NPC Cold/config.json` carrying `priority: 6001925` and a nested `conditions` tree with `OR`/`AND`/`negated` and `pluginName`/`formID` operands. |

## NOT VERIFIED — recorded and marked, never inferred

| area | state | what the tool does instead |
|------|-------|----------------------------|
| installed OAR version on this machine | `UNVERIFIED_FORMAT` | The local install is the source of truth for the user; this tool validates against `main` and preserves unknown fields as `UNSUPPORTED_FIELD` rather than assuming a version. The Nexus page (mod 92109) is not treated as evidence of what is installed. |
| runtime evaluation of `AND`/`OR`/`XOR`/`negated`, presets, variants, `Random`, state scopes | not evaluated | Conditions are flattened and recorded with name, depth, `negated`, `requiredVersion`, `requiredPlugin` and referenced plugins. No truth value is computed. Overlaps report `runtimeWinner: UNKNOWN`. |
| conditions supplied by other SKSE plugins through the OAR condition API | `UNKNOWN_EXTENSION`, confidence `unverifiable` | Any name outside the 124 + 23 verified sets is reported as needing an extension, not as an error. Reference: `src/API/OpenAnimationReplacerAPI-Conditions.h`. |
| whether a referenced plugin is really loaded | confidence `likely` | Compared against `plugins.txt` and `loadorder.txt`, which can lag reality. Reported as `MISSING_REFERENCE`, never as proof the submod is dead. |
| BSA contents | names read, data never read | Since revision 4 the archive directory is read (row 16) so archive-only animations are reported; file data is never extracted. Archive-vs-archive precedence follows plugin load order and is not resolved. BA2 and other formats are refused as `UNVERIFIED_FORMAT`. |
| the game `Data` folder | out of scope | Not walked, per the mission's read limits, so a vanilla or DLC loose file is not counted as a provider. This is stated in every report's limitations. |
| HKX structural validity | out of scope | `hkx_probe` reads the size and 16 bytes. A recognised header is reported with the note that the file was not deserialised. An unrecognised header is explicitly *not* called broken. |
| whether a patcher run succeeded | `OUTPUT_PRESENT_UNVERIFIED` | Generated files and existing logs are read as data. Tool attribution requires a filename pattern, an owned directory or a log line; a mod merely *named* after a patcher is not evidence (test `DP06b`). |
| anything about in-game behaviour | `NOT_VERIFIED_IN_GAME` | The top-level field is set on every report and cannot be overridden by any finding. |
