using System.Text.Json;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;
using Xunit;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Tests;

public sealed class PhaseEIntegrationRpcTests : IDisposable
{
    private readonly string _fixtureDir;

    public PhaseEIntegrationRpcTests()
    {
        _fixtureDir = Path.Combine(Path.GetTempPath(), "sns-ag-fbl-e-" + Guid.NewGuid().ToString("N"));
        LabSyntheticFixture.EnsureBasicFixture(_fixtureDir);
    }

    [Fact]
    public void RPC01_AnalyzeVoice_ReturnsVoiceEvidenceAndRequiresInGameTest()
    {
        var rawReq = $$"""
        {
          "jsonrpc": "2.0",
          "id": 101,
          "method": "creation.analyzeVoice",
          "params": {
            "fixtureDirectory": "{{_fixtureDir.Replace('\\', '/')}}",
            "voiceFormKey": "013AD2:Skyrim.esm",
            "female": true
          }
        }
        """;

        var response = FollowerBehaviorRpcDispatcher.DispatchJson(rawReq);

        Assert.Null(response.Error);
        Assert.NotNull(response.Result);
        var resultJson = JsonSerializer.Serialize(response.Result, SandboxGuard.SafeJsonOptions);
        var res = JsonSerializer.Deserialize<CreationAnalyzeVoiceResponse>(resultJson, SandboxGuard.SafeJsonOptions)!;

        Assert.Equal(EvidenceClassification.STATIC_EVIDENCE, res.VoiceAnalysis.FollowerDialogueClassification);
        Assert.Equal(EvidenceClassification.STATIC_EVIDENCE, res.VoiceAnalysis.MarriageDialogueClassification);
        Assert.False(res.RecruitableGuaranteed);
        Assert.False(res.MarriageGuaranteed);
        Assert.Equal("REQUIRES_IN_GAME_TEST", res.RuntimeVerification);
    }

    [Fact]
    public void RPC02_GetBehaviorCatalog_ReturnsClassesCombatStylesVoicesAndSixArchetypes()
    {
        var rawReq = $$"""
        {
          "jsonrpc": "2.0",
          "id": 102,
          "method": "creation.getBehaviorCatalog",
          "params": {
            "fixtureDirectory": "{{_fixtureDir.Replace('\\', '/')}}",
            "filterText": "",
            "limit": 100
          }
        }
        """;

        var response = FollowerBehaviorRpcDispatcher.DispatchJson(rawReq);

        Assert.Null(response.Error);
        Assert.NotNull(response.Result);
        var resultJson = JsonSerializer.Serialize(response.Result, SandboxGuard.SafeJsonOptions);
        var res = JsonSerializer.Deserialize<CreationBehaviorCatalogResponse>(resultJson, SandboxGuard.SafeJsonOptions)!;

        Assert.NotEmpty(res.Classes);
        Assert.NotEmpty(res.CombatStyles);
        Assert.NotEmpty(res.Voices);
        Assert.Equal(6, res.Archetypes.Count);
        Assert.All(res.Archetypes, a => Assert.True(a.AllLinksResolved));
    }

    [Fact]
    public void RPC03_ApplyAndUndoArchetype_PreservesIdentityAndRestoresExactBitMatch()
    {
        var draft = LabSyntheticFixture.CreateSampleDraft(
            editorId: "SNS_RpcApplyTest",
            name: "RPC Applicant") with
        {
            Height = 1.08f,
            Weight = 77.0f,
            HeadPartFormKeys = ["051623:Skyrim.esm"],
            HairColorFormKey = "0A0434:Skyrim.esm",
            HeadTextureFormKey = "03B522:Skyrim.esm"
        };

        var initialSha = SandboxGuard.ComputeObjectCanonicalSha256(draft);

        var applyReq = $$"""
        {
          "jsonrpc": "2.0",
          "id": 103,
          "method": "creation.applyArchetype",
          "params": {
            "fixtureDirectory": "{{_fixtureDir.Replace('\\', '/')}}",
            "archetypeId": "assassin",
            "dryRun": true,
            "npc": {{JsonSerializer.Serialize(draft, SandboxGuard.SafeJsonOptions)}}
          }
        }
        """;

        var applyResp = FollowerBehaviorRpcDispatcher.DispatchJson(applyReq);
        Assert.Null(applyResp.Error);
        var applyResultJson = JsonSerializer.Serialize(applyResp.Result, SandboxGuard.SafeJsonOptions);
        var applyResult = JsonSerializer.Deserialize<CreationApplyArchetypeResponse>(applyResultJson, SandboxGuard.SafeJsonOptions)!;

        Assert.True(applyResult.SourceUntouched);
        Assert.Equal(initialSha, applyResult.SourceSha256Before);
        Assert.Equal(initialSha, applyResult.SourceSha256After);

        // Verification of protected fields:
        Assert.Equal(draft.Name, applyResult.ProposedNpc.Name);
        Assert.Equal(draft.EditorId, applyResult.ProposedNpc.EditorId);
        Assert.Equal(draft.Height, applyResult.ProposedNpc.Height);
        Assert.Equal(draft.Weight, applyResult.ProposedNpc.Weight);
        Assert.Equal(draft.HeadPartFormKeys, applyResult.ProposedNpc.HeadPartFormKeys);
        Assert.Equal(draft.HairColorFormKey, applyResult.ProposedNpc.HairColorFormKey);
        Assert.Equal(draft.HeadTextureFormKey, applyResult.ProposedNpc.HeadTextureFormKey);

        // Undo invocation via RPC
        var undoReq = $$"""
        {
          "jsonrpc": "2.0",
          "id": 104,
          "method": "creation.undoArchetype",
          "params": {
            "expectedOriginalSha256": "{{initialSha}}",
            "undoRestoreToken": {{JsonSerializer.Serialize(applyResult.UndoRestoreToken, SandboxGuard.SafeJsonOptions)}}
          }
        }
        """;

        var undoResp = FollowerBehaviorRpcDispatcher.DispatchJson(undoReq);
        Assert.Null(undoResp.Error);
        var undoResultJson = JsonSerializer.Serialize(undoResp.Result, SandboxGuard.SafeJsonOptions);
        var undoResult = JsonSerializer.Deserialize<CreationUndoArchetypeResponse>(undoResultJson, SandboxGuard.SafeJsonOptions)!;

        Assert.True(undoResult.Restored);
        Assert.Equal(initialSha, undoResult.RestoredSha256);
        Assert.Equal(draft.EditorId, undoResult.RestoredNpc.EditorId);
        Assert.Equal(draft.Health, undoResult.RestoredNpc.Health);
    }

    [Fact]
    public void RPC04_ValidateBehavior_LocksPlayableExportAndReportsIssues()
    {
        var draft = LabSyntheticFixture.CreateSampleDraft(
            editorId: "SNS_RpcValTest",
            name: "RPC Validation Test",
            voiceKey: LabSyntheticFixture.VoiceFemaleUniqueSilent);

        var valReq = $$"""
        {
          "jsonrpc": "2.0",
          "id": 105,
          "method": "creation.validateBehavior",
          "params": {
            "fixtureDirectory": "{{_fixtureDir.Replace('\\', '/')}}",
            "pluginName": "FollowerTest.esp",
            "npc": {{JsonSerializer.Serialize(draft, SandboxGuard.SafeJsonOptions)}}
          }
        }
        """;

        var valResp = FollowerBehaviorRpcDispatcher.DispatchJson(valReq);
        Assert.Null(valResp.Error);
        var valResultJson = JsonSerializer.Serialize(valResp.Result, SandboxGuard.SafeJsonOptions);
        var valResult = JsonSerializer.Deserialize<CreationValidateBehaviorResponse>(valResultJson, SandboxGuard.SafeJsonOptions)!;

        Assert.False(valResult.Exportable);
        Assert.Equal("pending", valResult.FaceGenStatus);
        Assert.Equal("REQUIRES_IN_GAME_TEST", valResult.RuntimeStatus);
        Assert.Contains(valResult.Issues, i => i.Code == "NativeFaceGenUnavailable" && i.Severity == "error");
        Assert.Contains(valResult.Issues, i => i.Code == "VoiceNotInFollowerList");
    }

    [Fact]
    public void RPC05_UnknownMethod_ReturnsJsonRpcMethodNotFoundCode()
    {
        var badReq = """
        {
          "jsonrpc": "2.0",
          "id": 999,
          "method": "creation.unknownMethod",
          "params": {}
        }
        """;

        var resp = FollowerBehaviorRpcDispatcher.DispatchJson(badReq);
        Assert.NotNull(resp.Error);
        Assert.Equal(-32601, resp.Error!.Code);
    }

    public void Dispose()
    {
        try
        {
            if (Directory.Exists(_fixtureDir)) Directory.Delete(_fixtureDir, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
