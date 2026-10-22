using System.Text.Json;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;

/// <summary>
/// Self-contained JSON-RPC 2.0 dispatcher implementing the Character Forge behavior intelligence methods.
/// Ready to be wired directly into WorkerRpcTarget.cs once file ownership handoff is authorized.
/// </summary>
public static class FollowerBehaviorRpcDispatcher
{
    public static JsonRpcResponseEnvelope DispatchJson(string rawRequestJson, string? defaultFixtureDir = null)
    {
        JsonRpcRequestEnvelope request;
        try
        {
            request = SandboxGuard.ParseSafeJson<JsonRpcRequestEnvelope>(rawRequestJson, "jsonrpc-request");
        }
        catch (LabUsageException ex)
        {
            return new JsonRpcResponseEnvelope
            {
                Id = null,
                Error = new JsonRpcErrorObject(-32700, $"Parse error ({ex.Code}): {ex.Message}"),
            };
        }

        try
        {
            object result = request.Method switch
            {
                CreationBehaviorRpcMethods.AnalyzeVoice =>
                    HandleAnalyzeVoice(DeserializeParams<CreationAnalyzeVoiceRequest>(request.Params), defaultFixtureDir),
                CreationBehaviorRpcMethods.GetBehaviorCatalog =>
                    HandleGetCatalog(DeserializeParams<CreationBehaviorCatalogRequest>(request.Params), defaultFixtureDir),
                CreationBehaviorRpcMethods.ApplyArchetype =>
                    HandleApplyArchetype(DeserializeParams<CreationApplyArchetypeRequest>(request.Params), defaultFixtureDir),
                CreationBehaviorRpcMethods.UndoArchetype =>
                    HandleUndoArchetype(DeserializeParams<CreationUndoArchetypeRequest>(request.Params)),
                CreationBehaviorRpcMethods.ValidateBehavior =>
                    HandleValidateBehavior(DeserializeParams<CreationValidateBehaviorRequest>(request.Params), defaultFixtureDir),
                _ => throw new LabUsageException("MethodNotFound", $"Unknown JSON-RPC method '{request.Method}'.", -32601),
            };

            return new JsonRpcResponseEnvelope
            {
                Id = request.Id,
                Result = result,
            };
        }
        catch (LabUsageException ex)
        {
            int rpcCode = ex.Code switch
            {
                "MethodNotFound" => -32601,
                "MalformedJson" or "MissingArgument" or "InvalidArgument" or "UnauthorizedCategory" => WorkerCompatibleErrorCodes.InvalidArgument,
                "ArchetypeNotFound" or "FixtureNotFound" => WorkerCompatibleErrorCodes.RecordNotFound,
                "UndoIntegrityMismatch" => WorkerCompatibleErrorCodes.ValidationFailed,
                _ => WorkerCompatibleErrorCodes.InvalidArgument,
            };

            return new JsonRpcResponseEnvelope
            {
                Id = request.Id,
                Error = new JsonRpcErrorObject(rpcCode, ex.Message, new { code = ex.Code }),
            };
        }
    }

    public static CreationAnalyzeVoiceResponse HandleAnalyzeVoice(CreationAnalyzeVoiceRequest req, string? defaultFixtureDir = null)
    {
        if (string.IsNullOrWhiteSpace(req.VoiceFormKey))
        {
            throw new LabUsageException("InvalidArgument", "voiceFormKey is required.");
        }

        using var ctx = OpenContext(req.FixtureDirectory, defaultFixtureDir, req.ProfileName);
        var analysis = VoiceDialogueAnalyzer.AnalyzeVoice(ctx, req.VoiceFormKey, req.Female, req.Factions);

        return new CreationAnalyzeVoiceResponse
        {
            LoadOrderFingerprint = ctx.LoadOrderFingerprint,
            VoiceAnalysis = analysis,
            WorkerIssues = analysis.Issues.Select(MapToWorkerIssue).ToArray(),
        };
    }

    public static CreationBehaviorCatalogResponse HandleGetCatalog(CreationBehaviorCatalogRequest req, string? defaultFixtureDir = null)
    {
        if (req.FilterText.Length > 160)
        {
            throw new LabUsageException("InvalidArgument", "FilterText exceeds maximum length of 160 characters.");
        }

        int limit = Math.Clamp(req.Limit <= 0 ? 200 : req.Limit, 1, 500);
        using var ctx = OpenContext(req.FixtureDirectory, defaultFixtureDir, req.ProfileName);

        var classes = CombatClassCatalog.EnumerateActiveClasses(ctx)
            .Where(c => c.EditorId.Contains(req.FilterText, StringComparison.OrdinalIgnoreCase)
                     || c.Name.Contains(req.FilterText, StringComparison.OrdinalIgnoreCase))
            .Take(limit)
            .ToArray();

        var styles = CombatClassCatalog.EnumerateActiveCombatStyles(ctx)
            .Where(s => s.EditorId.Contains(req.FilterText, StringComparison.OrdinalIgnoreCase))
            .Take(limit)
            .ToArray();

        var voices = VoiceDialogueAnalyzer.EnumerateActiveVoices(ctx)
            .Where(v => v.EditorId.Contains(req.FilterText, StringComparison.OrdinalIgnoreCase))
            .Take(limit)
            .ToArray();

        var archetypes = ArchetypePlanner.GetAllArchetypes()
            .Select(a =>
            {
                var val = ArchetypePlanner.ValidateArchetype(ctx, a);
                return new ArchetypeCatalogDescriptor(
                    a.Id,
                    a.DisplayName,
                    a.RoleSummary,
                    a.ClassFormKey,
                    a.CombatStyleFormKey,
                    val.AllLinksResolved,
                    val.RequiredMasters,
                    val.Issues);
            })
            .ToArray();

        return new CreationBehaviorCatalogResponse
        {
            LoadOrderFingerprint = ctx.LoadOrderFingerprint,
            Classes = classes,
            CombatStyles = styles,
            Voices = voices,
            Archetypes = archetypes,
        };
    }

    public static CreationApplyArchetypeResponse HandleApplyArchetype(CreationApplyArchetypeRequest req, string? defaultFixtureDir = null)
    {
        ArgumentNullException.ThrowIfNull(req.Npc);
        var template = ArchetypePlanner.GetById(req.ArchetypeId);

        using var ctx = OpenContext(req.FixtureDirectory, defaultFixtureDir, req.ProfileName);
        var report = BulkArchetypeEngine.ApplyArchetypeDryRun(ctx, [req.Npc], template, req.Categories);
        var entry = report.Entries[0];

        return new CreationApplyArchetypeResponse
        {
            DryRun = req.DryRun,
            ArchetypeId = template.Id,
            LoadOrderFingerprint = report.LoadOrderFingerprint,
            SourceSha256Before = entry.SourceSha256Before,
            SourceSha256After = entry.SourceSha256After,
            SourceUntouched = entry.SourceUntouched,
            ProposedSha256 = entry.ProposedSha256,
            AppliedCategories = entry.AppliedCategories,
            Changes = entry.Changes,
            ProposedNpc = entry.ProposedDraft,
            UndoRestoreToken = entry.UndoRestoreToken,
            ArchetypeValidation = report.ArchetypeValidation,
        };
    }

    public static CreationUndoArchetypeResponse HandleUndoArchetype(CreationUndoArchetypeRequest req)
    {
        ArgumentNullException.ThrowIfNull(req.UndoRestoreToken);
        if (string.IsNullOrWhiteSpace(req.ExpectedOriginalSha256))
        {
            throw new LabUsageException("InvalidArgument", "expectedOriginalSha256 is required for undo verification.");
        }

        var json = JsonSerializer.Serialize(req.UndoRestoreToken, SandboxGuard.CanonicalJsonOptions);
        var restored = JsonSerializer.Deserialize<FollowerDraftDto>(json, SandboxGuard.SafeJsonOptions)!;
        var actualHash = SandboxGuard.ComputeObjectCanonicalSha256(restored);

        if (!string.Equals(actualHash, req.ExpectedOriginalSha256, StringComparison.Ordinal))
        {
            throw new LabUsageException(
                "UndoIntegrityMismatch",
                $"Restored draft SHA-256 ({actualHash}) does not match expected pre-archetype SHA-256 ({req.ExpectedOriginalSha256}).");
        }

        return new CreationUndoArchetypeResponse
        {
            Restored = true,
            RestoredSha256 = actualHash,
            RestoredNpc = restored,
        };
    }

    public static CreationValidateBehaviorResponse HandleValidateBehavior(CreationValidateBehaviorRequest req, string? defaultFixtureDir = null)
    {
        ArgumentNullException.ThrowIfNull(req.Npc);
        using var ctx = OpenContext(req.FixtureDirectory, defaultFixtureDir, req.ProfileName);
        var verdict = FollowerValidationEngine.ValidatePlan(ctx, req.Npc);

        var workerIssues = verdict.Issues
            .Select(MapToWorkerIssue)
            .ToList();

        // Always include the core NativeFaceGenUnavailable error in WorkerIssues so CreationValidateResponse
        // contract consumers see the exact same block as NativeNpcValidator.cs line 148.
        if (!workerIssues.Any(i => i.Code == "NativeFaceGenUnavailable"))
        {
            workerIssues.Add(new WorkerCompatibleIssue(
                "NativeFaceGenUnavailable",
                "error",
                "Native FaceGen NIF and FaceTint DDS are not generated/verified. Final playable export is blocked; technical plugins must not be installed."));
        }
        else
        {
            workerIssues = workerIssues
                .Select(i => i.Code == "NativeFaceGenUnavailable"
                    ? i with { Severity = "error", Message = "Native FaceGen NIF and FaceTint DDS are not generated/verified. Final playable export is blocked; technical plugins must not be installed." }
                    : i)
                .ToList();
        }

        var missingRefs = verdict.ResolvedLinks
            .Where(l => !l.Resolved)
            .Select(l => l.FormKey)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();

        return new CreationValidateBehaviorResponse
        {
            TechnicallyExportable = verdict.StructurallyValid,
            LoadOrderFingerprint = verdict.LoadOrderFingerprint,
            DraftSha256 = verdict.DraftSha256,
            Issues = workerIssues,
            BehaviorIssues = verdict.Issues,
            MissingReferences = missingRefs,
            Masters = verdict.RequiredMasters,
            VoiceAnalysis = verdict.VoiceAnalysis,
            ResolvedClass = verdict.ResolvedClass,
            ResolvedCombatStyle = verdict.ResolvedCombatStyle,
            ResolvedLinks = verdict.ResolvedLinks,
        };
    }

    private static T DeserializeParams<T>(JsonElement element)
    {
        if (element.ValueKind is JsonValueKind.Undefined or JsonValueKind.Null)
        {
            throw new LabUsageException("InvalidArgument", $"Missing 'params' object for {typeof(T).Name}.");
        }
        return SandboxGuard.ParseSafeJson<T>(element.GetRawText(), typeof(T).Name);
    }

    private static PluginContext OpenContext(string requestedDir, string? fallbackDir, string? profileName)
    {
        var dir = !string.IsNullOrWhiteSpace(requestedDir) ? requestedDir : (fallbackDir ?? "");
        if (string.IsNullOrWhiteSpace(dir))
        {
            var root = SandboxGuard.FindRepoRoot() ?? Directory.GetCurrentDirectory();
            dir = Path.Combine(root, "tests", "antigravity", "follower-behavior-lab", "fixtures", "basic");
        }
        if (!Directory.Exists(dir) || !File.Exists(Path.Combine(dir, "Skyrim.esm")))
        {
            LabSyntheticFixture.EnsureBasicFixture(dir);
        }
        return PluginContext.OpenDirectory(dir, profileName);
    }

    private static WorkerCompatibleIssue MapToWorkerIssue(BehaviorIssue issue)
    {
        var sev = issue.Severity switch
        {
            ValidationSeverity.ERROR => "error",
            ValidationSeverity.WARNING or ValidationSeverity.UNKNOWN or ValidationSeverity.REQUIRES_IN_GAME_TEST => "warning",
            _ => "info",
        };
        return new WorkerCompatibleIssue(issue.Code, sev, issue.Message, issue.TargetFormKey);
    }
}
