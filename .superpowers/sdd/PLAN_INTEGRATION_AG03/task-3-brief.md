# Task 3 Brief: Worker .NET Behavior Service & RPC Target

## Goal
Register the Follower Behavior Intelligence RPC endpoints into `BethesdaWorker` via `WorkerRpcTarget.cs` and link `FollowerBehaviorLab`.

## File Requirements
1. `services/BethesdaWorker/BethesdaWorker.csproj`:
   - Add `<ProjectReference Include="..\..\tools\antigravity\follower-behavior-lab\FollowerBehaviorLab.csproj" />`.
2. `services/BethesdaWorker/Rpc/WorkerRpcTarget.cs`:
   - Add using `SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;`.
   - Add `Methods.CreationAnalyzeVoice`, `Methods.CreationGetBehaviorCatalog`, `Methods.CreationApplyArchetype`, `Methods.CreationUndoArchetype`, `Methods.CreationValidateBehavior` to `Capabilities` in `Handshake()`.
   - Add the 5 `[JsonRpcMethod]` handlers: `AnalyzeVoice`, `GetBehaviorCatalog`, `ApplyArchetype`, `UndoArchetype`, `ValidateBehavior`.

## Verification
- `dotnet build services/BethesdaWorker/BethesdaWorker.csproj` compiles with 0 errors and 0 warnings.
- `dotnet test tests/unit/BethesdaWorker.Tests/BethesdaWorker.Tests.csproj` passes 63/63.
- `dotnet test tests/export/SkyrimNpcStudio.ExportTests/SkyrimNpcStudio.ExportTests.csproj` passes 95/95.
- `dotnet test tests/antigravity/follower-behavior-lab/FollowerBehaviorLab.Tests.csproj` passes 28/28.
