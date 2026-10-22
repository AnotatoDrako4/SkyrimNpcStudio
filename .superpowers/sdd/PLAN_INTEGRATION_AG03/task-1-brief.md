# Task 1 Brief: Wire Protocol & C++ Engine Contracts

## Goal
Extend the C++ wire protocol declarations and NativeNpcDraft model to support Behavior Studio integration without breaking backwards compatibility.

## File Requirements
1. `engines/ipc/include/sns/ipc/protocol.hpp`:
   - Set `inline constexpr int kMinor = 6;`
   - In `namespace sns::ipc::protocol::method`, declare:
     ```cpp
     inline constexpr const char* kCreationAnalyzeVoice = "creation.analyzeVoice";
     inline constexpr const char* kCreationGetBehaviorCatalog = "creation.getBehaviorCatalog";
     inline constexpr const char* kCreationApplyArchetype = "creation.applyArchetype";
     inline constexpr const char* kCreationUndoArchetype = "creation.undoArchetype";
     inline constexpr const char* kCreationValidateBehavior = "creation.validateBehavior";
     ```
2. `engines/creation/include/sns/creation/native_npc_draft.hpp`:
   - In `struct NativeNpcDraft`, add:
     ```cpp
     std::vector<std::string> spell_form_keys, perk_form_keys;
     ```
3. `engines/creation/src/native_npc_draft.cpp`:
   - In `validate_native_draft`: enforce `d.spell_form_keys.size() > 64 || d.perk_form_keys.size() > 64` bounds check.
   - In `native_npc_to_json`: serialize `j["spellFormKeys"] = d.spell_form_keys;` and `j["perkFormKeys"] = d.perk_form_keys;`.
   - In `native_npc_from_json`: deserialize `d.spell_form_keys = j.value("spellFormKeys", d.spell_form_keys);` and `d.perk_form_keys = j.value("perkFormKeys", d.perk_form_keys);`.

## Verification
- C++ headers compile cleanly.
- Existing C++ test suites pass.
- Backwards compatibility: drafts without `spellFormKeys` or `perkFormKeys` parse without error and default to empty vectors.
