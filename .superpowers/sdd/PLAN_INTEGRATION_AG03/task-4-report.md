# Task 4 Report: Character Forge QML Panels Integration

## Status: DONE
- **Files Modified:**
  - `apps/desktop/qml/characterforge/StatsEditor.qml`: integrated archetype selection bar (preview dry-run, apply), word-wrap warning on preservation of appearance/identity, and Repeaters + Selectors for `spellFormKeys` and `perkFormKeys`.
  - `apps/desktop/qml/characterforge/ForgeValidationPanel.qml`: integrated follower voice and marriage diagnostics, explicit `REQUIRES_IN_GAME_TEST` warning for behavior packages, retaining `NativeFaceGenUnavailable` export lock.
- **Compilation:**
  - `SkyrimNpcStudio.exe` built cleanly with C++ AOT-compiled QML cache (`qmlcachegen`).
- **Tests:**
  - `smoke-test.ps1 -Binary build/refint-app/bin/SkyrimNpcStudio.exe -Size 1366x768 -ShowWindow`: Exit Code 0, 0 QML errors, 0 warnings.
