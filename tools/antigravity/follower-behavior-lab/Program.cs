using System.Text;
using System.Text.Json;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Integration;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab;

public static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            return RunCli(args, Console.Out, Console.Error);
        }
        catch (LabUsageException ex)
        {
            Console.Error.WriteLine($"[ERROR:{ex.Code}] {ex.Message}");
            return ex.ExitCode;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[FATAL:UnhandledError] {ex.Message}");
            return 1;
        }
    }

    public static int RunCli(string[] args, TextWriter stdout, TextWriter stderr, string? customSandboxRoot = null)
    {
        if (args.Length == 0 || IsHelpCommand(args[0]))
        {
            PrintHelp(stdout);
            return 0;
        }

        var command = args[0].ToLowerInvariant();
        var options = ParseOptions(args.Skip(1).ToArray());

        return command switch
        {
            "inspect" => ExecuteInspect(options, stdout, customSandboxRoot),
            "validate" => ExecuteValidate(options, stdout, customSandboxRoot),
            "apply-archetype" => ExecuteApplyArchetype(options, stdout, customSandboxRoot),
            "report" => ExecuteReport(options, stdout, customSandboxRoot),
            "benchmark" => ExecuteBenchmark(options, stdout, customSandboxRoot),
            "rpc-call" => ExecuteRpcCall(options, stdout, customSandboxRoot),
            _ => throw new LabUsageException("UnknownCommand", $"Unknown command '{args[0]}'. Use 'help' to list supported commands.", 2),
        };
    }

    private static int ExecuteInspect(Dictionary<string, string> options, TextWriter stdout, string? customSandboxRoot)
    {
        if (!options.TryGetValue("--fixture", out var fixtureDir) || string.IsNullOrWhiteSpace(fixtureDir))
        {
            throw new LabUsageException("MissingArgument", "Command 'inspect' requires '--fixture <dir>'.", 2);
        }
        if (!options.TryGetValue("--out", out var outPath) || string.IsNullOrWhiteSpace(outPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'inspect' requires '--out <file.json>'.", 2);
        }

        var safeOut = SandboxGuard.ValidateOutputPath(outPath, customSandboxRoot);
        options.TryGetValue("--profile", out var profile);

        if (!Directory.Exists(fixtureDir) || !File.Exists(Path.Combine(fixtureDir, "Skyrim.esm")))
        {
            LabSyntheticFixture.EnsureBasicFixture(fixtureDir);
        }

        using var ctx = PluginContext.OpenDirectory(fixtureDir, profile);
        var report = FollowerValidationEngine.InspectCatalog(ctx);
        var json = JsonSerializer.Serialize(report, SandboxGuard.SafeJsonOptions);
        File.WriteAllText(safeOut, json, Encoding.UTF8);
        stdout.WriteLine($"[OK] Inspect report written to {safeOut} (classes={report.Classes.Count}, combatStyles={report.CombatStyles.Count}, voices={report.Voices.Count}).");
        return 0;
    }

    private static int ExecuteValidate(Dictionary<string, string> options, TextWriter stdout, string? customSandboxRoot)
    {
        if (!options.TryGetValue("--input", out var inputPath) || string.IsNullOrWhiteSpace(inputPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'validate' requires '--input <plan.json>'.", 2);
        }
        if (!options.TryGetValue("--out", out var outPath) || string.IsNullOrWhiteSpace(outPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'validate' requires '--out <verdict.json>'.", 2);
        }

        var safeOut = SandboxGuard.ValidateOutputPath(outPath, customSandboxRoot);
        var plan = SandboxGuard.ReadSafeJsonFile<FollowerPlanInput>(inputPath);
        if (plan.Draft is null)
        {
            throw new LabUsageException("MalformedPlan", "Plan JSON is missing required 'draft' object.", 2);
        }

        var fixtureDir = !string.IsNullOrWhiteSpace(plan.FixtureDirectory)
            ? plan.FixtureDirectory
            : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(inputPath)) ?? ".", "basic");

        if (!Directory.Exists(fixtureDir) || !File.Exists(Path.Combine(fixtureDir, "Skyrim.esm")))
        {
            LabSyntheticFixture.EnsureBasicFixture(fixtureDir);
        }

        using var ctx = PluginContext.OpenDirectory(fixtureDir, plan.ProfileName);
        var verdict = FollowerValidationEngine.ValidatePlan(ctx, plan.Draft);
        var json = JsonSerializer.Serialize(verdict, SandboxGuard.SafeJsonOptions);
        File.WriteAllText(safeOut, json, Encoding.UTF8);
        stdout.WriteLine($"[OK] Verdict written to {safeOut} (structurallyValid={verdict.StructurallyValid}, runtimeStatus={verdict.RuntimeStatus}).");
        return 0;
    }

    private static int ExecuteApplyArchetype(Dictionary<string, string> options, TextWriter stdout, string? customSandboxRoot)
    {
        if (!options.TryGetValue("--input", out var inputPath) || string.IsNullOrWhiteSpace(inputPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'apply-archetype' requires '--input <draft.json>'.", 2);
        }
        if (!options.TryGetValue("--archetype", out var archetypeId) || string.IsNullOrWhiteSpace(archetypeId))
        {
            throw new LabUsageException("MissingArgument", "Command 'apply-archetype' requires '--archetype <id>'.", 2);
        }
        if (!options.TryGetValue("--out", out var outPath) || string.IsNullOrWhiteSpace(outPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'apply-archetype' requires '--out <diff.json>'.", 2);
        }
        if (!options.ContainsKey("--dry-run"))
        {
            throw new LabUsageException("DryRunRequired", "Safety policy requires '--dry-run' for batch archetype operations.", 2);
        }

        var safeOut = SandboxGuard.ValidateOutputPath(outPath, customSandboxRoot);
        var batchInput = SandboxGuard.ReadSafeJsonFile<BatchDraftInput>(inputPath);

        var drafts = new List<FollowerDraftDto>();
        if (batchInput.Drafts is { Count: > 0 })
        {
            drafts.AddRange(batchInput.Drafts);
        }
        else if (batchInput.Draft is not null)
        {
            drafts.Add(batchInput.Draft);
        }
        else
        {
            throw new LabUsageException("EmptyDraftInput", "Input JSON must contain either 'draft' or non-empty 'drafts'.", 2);
        }

        var fixtureDir = !string.IsNullOrWhiteSpace(batchInput.FixtureDirectory)
            ? batchInput.FixtureDirectory
            : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(inputPath)) ?? ".", "basic");

        if (!Directory.Exists(fixtureDir) || !File.Exists(Path.Combine(fixtureDir, "Skyrim.esm")))
        {
            LabSyntheticFixture.EnsureBasicFixture(fixtureDir);
        }

        IReadOnlyCollection<string>? categories = null;
        if (options.TryGetValue("--categories", out var catCsv) && !string.IsNullOrWhiteSpace(catCsv))
        {
            categories = catCsv.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        }

        var template = ArchetypePlanner.GetById(archetypeId);
        using var ctx = PluginContext.OpenDirectory(fixtureDir, batchInput.ProfileName);
        var diffReport = BulkArchetypeEngine.ApplyArchetypeDryRun(ctx, drafts, template, categories);
        var json = JsonSerializer.Serialize(diffReport, SandboxGuard.SafeJsonOptions);
        File.WriteAllText(safeOut, json, Encoding.UTF8);
        stdout.WriteLine($"[OK] Dry-run archetype '{template.Id}' applied to {diffReport.TotalDrafts} draft(s); diff written to {safeOut} (allSourcesUntouched={diffReport.AllSourcesUntouched}).");
        return 0;
    }

    private static int ExecuteReport(Dictionary<string, string> options, TextWriter stdout, string? customSandboxRoot)
    {
        if (!options.TryGetValue("--input", out var inputPath) || string.IsNullOrWhiteSpace(inputPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'report' requires '--input <verdict.json>'.", 2);
        }
        if (!options.TryGetValue("--out", out var outPath) || string.IsNullOrWhiteSpace(outPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'report' requires '--out <report.html>'.", 2);
        }

        var safeOut = SandboxGuard.ValidateOutputPath(outPath, customSandboxRoot);
        var verdict = SandboxGuard.ReadSafeJsonFile<BehaviorVerdictReport>(inputPath);
        var html = ReportGenerator.GenerateHtmlFromVerdict(verdict, inputPath);
        File.WriteAllText(safeOut, html, Encoding.UTF8);

        var manifestPath = Path.Combine(Path.GetDirectoryName(safeOut)!, "manifest.sha256.json");
        var outDir = Path.GetDirectoryName(safeOut)!;
        ReportGenerator.WriteSha256Manifest(manifestPath,
        [
            (Path.Combine(outDir, "inspect.json"), "inspect-json"),
            (Path.GetFullPath(inputPath), "verdict-json"),
            (Path.Combine(outDir, "diff.json"), "diff-json"),
            (safeOut, "report-html"),
            (Path.Combine(outDir, "benchmark.json"), "benchmark-json")
        ]);

        stdout.WriteLine($"[OK] HTML report written to {safeOut} and SHA-256 manifest written to {manifestPath}.");
        return 0;
    }

    private static int ExecuteRpcCall(Dictionary<string, string> options, TextWriter stdout, string? customSandboxRoot)
    {
        if (!options.TryGetValue("--request", out var reqPath) || string.IsNullOrWhiteSpace(reqPath) || !File.Exists(reqPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'rpc-call' requires '--request <rpc-request.json>'.", 2);
        }
        if (!options.TryGetValue("--out", out var outPath) || string.IsNullOrWhiteSpace(outPath))
        {
            throw new LabUsageException("MissingArgument", "Command 'rpc-call' requires '--out <rpc-response.json>'.", 2);
        }

        var safeOut = SandboxGuard.ValidateOutputPath(outPath, customSandboxRoot);
        var rawReq = File.ReadAllText(reqPath, Encoding.UTF8);
        options.TryGetValue("--fixture", out var fixtureDir);
        var response = FollowerBehaviorRpcDispatcher.DispatchJson(rawReq, fixtureDir);
        var json = JsonSerializer.Serialize(response, SandboxGuard.SafeJsonOptions);
        File.WriteAllText(safeOut, json, Encoding.UTF8);
        stdout.WriteLine($"[OK] JSON-RPC response written to {safeOut} (hasError={response.Error is not null}).");
        return response.Error is null ? 0 : 4;
    }

    private static int ExecuteBenchmark(Dictionary<string, string> options, TextWriter stdout, string? customSandboxRoot)
    {
        var outPath = options.GetValueOrDefault("--out", "out/antigravity/follower-behavior-lab/benchmark.json");
        var safeOut = SandboxGuard.ValidateOutputPath(outPath, customSandboxRoot);

        var tempBase = Path.Combine(Path.GetTempPath(), "sns-ag-fbl-bench-" + Guid.NewGuid().ToString("N"));
        var baselineDir = Path.Combine(tempBase, "baseline");
        var scale1000Dir = Path.Combine(tempBase, "scale1000");

        try
        {
            LabSyntheticFixture.EnsureBasicFixture(baselineDir, extraSyntheticRecords: 0);
            LabSyntheticFixture.EnsureBasicFixture(scale1000Dir, extraSyntheticRecords: 1000);

            var baselineMetrics = MeasureFixturePerformance(baselineDir, "baseline-18-records");
            var scale1000Metrics = MeasureFixturePerformance(scale1000Dir, "stress-1018-records");

            var payload = new
            {
                schemaVersion = "1.0.0",
                generatedAtUtc = DateTime.UtcNow.ToString("o"),
                machine = Environment.MachineName,
                osVersion = Environment.OSVersion.ToString(),
                dotnetVersion = Environment.Version.ToString(),
                baseline = baselineMetrics,
                synthetic1000 = scale1000Metrics
            };

            var json = JsonSerializer.Serialize(payload, SandboxGuard.SafeJsonOptions);
            File.WriteAllText(safeOut, json, Encoding.UTF8);
            stdout.WriteLine($"[OK] Benchmark written to {safeOut} (1000-record analyzeMs={scale1000Metrics.AnalysisDurationMs:F2}, readMs={scale1000Metrics.ReadDurationMs:F2}, peakMemBytes={scale1000Metrics.PeakWorkingSetBytes}).");
            return 0;
        }
        finally
        {
            try { if (Directory.Exists(tempBase)) Directory.Delete(tempBase, recursive: true); } catch { }
        }
    }

    private sealed record BenchmarkMeasurementDto(
        string Label,
        int CatalogRecordCount,
        double ReadDurationMs,
        double AnalysisDurationMs,
        long ManagedHeapDeltaBytes,
        long PeakWorkingSetBytes,
        int InspectJsonBytes,
        int VerdictJsonBytes,
        int ReportHtmlBytes);

    private static BenchmarkMeasurementDto MeasureFixturePerformance(string dir, string label)
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        long heapBefore = GC.GetTotalMemory(false);

        var swRead = System.Diagnostics.Stopwatch.StartNew();
        using var ctx = PluginContext.OpenDirectory(dir);
        swRead.Stop();

        var swAnalyze = System.Diagnostics.Stopwatch.StartNew();
        var inspect = FollowerValidationEngine.InspectCatalog(ctx);
        var draft = LabSyntheticFixture.CreateSampleDraft();
        var verdict = FollowerValidationEngine.ValidatePlan(ctx, draft);
        var html = ReportGenerator.GenerateHtmlFromVerdict(verdict);
        swAnalyze.Stop();

        long heapAfter = GC.GetTotalMemory(false);
        long peakWorkingSet = System.Diagnostics.Process.GetCurrentProcess().PeakWorkingSet64;

        var inspectJson = JsonSerializer.Serialize(inspect, SandboxGuard.SafeJsonOptions);
        var verdictJson = JsonSerializer.Serialize(verdict, SandboxGuard.SafeJsonOptions);

        return new BenchmarkMeasurementDto(
            label,
            inspect.Classes.Count + inspect.CombatStyles.Count + inspect.Voices.Count,
            swRead.Elapsed.TotalMilliseconds,
            swAnalyze.Elapsed.TotalMilliseconds,
            Math.Max(0L, heapAfter - heapBefore),
            peakWorkingSet,
            Encoding.UTF8.GetByteCount(inspectJson),
            Encoding.UTF8.GetByteCount(verdictJson),
            Encoding.UTF8.GetByteCount(html));
    }

    private static bool IsHelpCommand(string arg) =>
        arg.Equals("help", StringComparison.OrdinalIgnoreCase) ||
        arg.Equals("--help", StringComparison.OrdinalIgnoreCase) ||
        arg.Equals("-h", StringComparison.OrdinalIgnoreCase);

    private static void PrintHelp(TextWriter stdout)
    {
        stdout.WriteLine("Antigravity Follower Behavior & Voice Intelligence Studio (AG-FOLLOWER-BEHAVIOR-01)");
        stdout.WriteLine("Usage:");
        stdout.WriteLine("  FollowerBehaviorLab help");
        stdout.WriteLine("  FollowerBehaviorLab inspect --fixture <dir> [--profile <name>] --out <inspect.json>");
        stdout.WriteLine("  FollowerBehaviorLab validate --input <plan.json> --out <verdict.json>");
        stdout.WriteLine("  FollowerBehaviorLab apply-archetype --input <draft.json> --archetype <id> [--categories <csv>] --dry-run --out <diff.json>");
        stdout.WriteLine("  FollowerBehaviorLab report --input <verdict.json> --out <report.html>");
    }

    internal static Dictionary<string, string> ParseOptions(string[] tokens)
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < tokens.Length; i++)
        {
            var t = tokens[i];
            if (!t.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }
            if (i + 1 < tokens.Length && !tokens[i + 1].StartsWith("--", StringComparison.Ordinal))
            {
                map[t] = tokens[i + 1];
                i++;
            }
            else
            {
                map[t] = "true";
            }
        }
        return map;
    }
}
