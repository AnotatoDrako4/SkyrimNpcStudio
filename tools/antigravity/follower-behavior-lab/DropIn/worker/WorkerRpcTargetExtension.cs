// WorkerRpcTargetExtension.cs
// Additive JSON-RPC methods ready to drop into services/BethesdaWorker/Rpc/WorkerRpcTarget.cs
// and services/BethesdaWorker/Contracts/Protocol.cs without breaking existing contracts.

namespace SkyrimNpcStudio.Worker.Rpc;

#if false
// In services/BethesdaWorker/Contracts/Protocol.cs:
public static class Methods
{
    // ... existing methods ...
    public const string CreationAnalyzeVoice = "creation.analyzeVoice";
    public const string CreationGetBehaviorCatalog = "creation.getBehaviorCatalog";
    public const string CreationApplyArchetype = "creation.applyArchetype";
    public const string CreationUndoArchetype = "creation.undoArchetype";
    public const string CreationValidateBehavior = "creation.validateBehavior";
}

// In services/BethesdaWorker/Rpc/WorkerRpcTarget.cs:
internal sealed partial class WorkerRpcTarget
{
    [JsonRpcMethod(Methods.CreationAnalyzeVoice)]
    public CreationAnalyzeVoiceResponse AnalyzeVoice(CreationAnalyzeVoiceRequest request) =>
        Guard(() => state.Read(c => FollowerBehaviorRpcDispatcher.HandleAnalyzeVoice(request)));

    [JsonRpcMethod(Methods.CreationGetBehaviorCatalog)]
    public CreationBehaviorCatalogResponse GetBehaviorCatalog(CreationBehaviorCatalogRequest request) =>
        Guard(() => state.Read(c => FollowerBehaviorRpcDispatcher.HandleGetCatalog(request)));

    [JsonRpcMethod(Methods.CreationApplyArchetype)]
    public CreationApplyArchetypeResponse ApplyArchetype(CreationApplyArchetypeRequest request) =>
        Guard(() => state.Read(c => FollowerBehaviorRpcDispatcher.HandleApplyArchetype(request)));

    [JsonRpcMethod(Methods.CreationUndoArchetype)]
    public CreationUndoArchetypeResponse UndoArchetype(CreationUndoArchetypeRequest request) =>
        Guard(() => FollowerBehaviorRpcDispatcher.HandleUndoArchetype(request));

    [JsonRpcMethod(Methods.CreationValidateBehavior)]
    public CreationValidateBehaviorResponse ValidateBehavior(CreationValidateBehaviorRequest request) =>
        Guard(() => state.Read(c => FollowerBehaviorRpcDispatcher.HandleValidateBehavior(request)));
}
#endif
