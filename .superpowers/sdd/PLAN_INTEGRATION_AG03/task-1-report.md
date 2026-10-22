# Task 1 Report: Wire Protocol & C++ Engine Contracts

## Status: DONE
- **Files Modified:**
  - `engines/ipc/include/sns/ipc/protocol.hpp`: `kMinor` updated to 6, declared 5 new RPC method constants (`kCreationAnalyzeVoice`, `kCreationGetBehaviorCatalog`, `kCreationApplyArchetype`, `kCreationUndoArchetype`, `kCreationValidateBehavior`).
  - `engines/creation/include/sns/creation/native_npc_draft.hpp`: added `spell_form_keys` and `perk_form_keys`.
  - `engines/creation/src/native_npc_draft.cpp`: added bounds validation (<= 64), serialization and deserialization with backwards compatible defaults.
- **Compilation:**
  - `sns_creation.lib` built cleanly in `build/refint` and `build/refint-app`.
- **Tests:**
  - `sns_reference_tests.exe`: 60 cases (59 passed, 1 skipped due to Windows symlink privilege), 335 assertions passed.
  - Zero C++ compiler errors or warnings.
