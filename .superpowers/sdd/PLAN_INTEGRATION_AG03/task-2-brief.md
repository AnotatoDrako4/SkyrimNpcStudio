# Task 2 Brief: Worker .NET Protocol & DTOs

## Goal
Update `BethesdaWorker` contracts and builders to support Protocol Minor 6, `SpellFormKeys`, `PerkFormKeys`, and the Behavior Intelligence DTOs.

## File Requirements
1. `services/BethesdaWorker/Contracts/Protocol.cs`:
   - Set `public const int Minor = 6;`
   - In `public static class Methods`, add:
     - `CreationAnalyzeVoice = "creation.analyzeVoice"`
     - `CreationGetBehaviorCatalog = "creation.getBehaviorCatalog"`
     - `CreationApplyArchetype = "creation.applyArchetype"`
     - `CreationUndoArchetype = "creation.undoArchetype"`
     - `CreationValidateBehavior = "creation.validateBehavior"`
2. `services/BethesdaWorker/Contracts/CreationDtos.cs`:
   - Add `SpellFormKeys` and `PerkFormKeys` (`IReadOnlyList<string>`) to `NativeNpcDraft`.
   - Add request/response contracts for behavior intelligence.
3. `services/BethesdaWorker/PatchBuilder/NativeNpcBuilder.cs`:
   - Support populating `ActorEffect` (`ISpellRecordGetter`) from `d.SpellFormKeys`.
   - Support populating `Perks` (`IPerkGetter`) with `Rank = 1` from `d.PerkFormKeys`.
4. `services/BethesdaWorker/Validation/NativeNpcValidator.cs`:
   - Null-check and collection bounds (<= 64) for `SpellFormKeys` and `PerkFormKeys`.
   - Resolve `ISpellGetter` and `IPerkGetter` for each entry.

## Verification
- `dotnet build services/BethesdaWorker/BethesdaWorker.csproj` compiles with 0 errors.
- Unit and export test suites pass (`BethesdaWorker.Tests`, `SkyrimNpcStudio.ExportTests`).
