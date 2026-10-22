# Task 4 Brief: Character Forge QML Panels Integration

## Goal
Integrate the Behavior Studio QML controls into `StatsEditor.qml` and `ForgeValidationPanel.qml` to expose archetypes, spells, perks, and follower voice diagnostics.

## File Requirements
1. `apps/desktop/qml/characterforge/StatsEditor.qml`:
   - Replace with `tools/antigravity/follower-behavior-lab/DropIn/qml/StatsEditor.integrated.qml`.
   - Adds the archetype template dropdown, preview and apply buttons, word-wrap warning on preservation of appearance/identity, and Repeaters + Selectors for `spellFormKeys` and `perkFormKeys`.
2. `apps/desktop/qml/characterforge/ForgeValidationPanel.qml`:
   - Replace with `tools/antigravity/follower-behavior-lab/DropIn/qml/ForgeValidationPanel.integrated.qml`.
   - Adds diagnostic rows for follower voice classification, marriage evidence, and the explicit `REQUIRES_IN_GAME_TEST` warning for behavior packages.
   - Retains `NativeFaceGenUnavailable` export lock styled with `Theme.error`.

## Verification
- Run UI smoke test with `-Size 1366x768 -ShowWindow` to ensure QML engine loads and parses without syntax errors or warnings.
- Exit code 0.
