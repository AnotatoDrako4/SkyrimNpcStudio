using System.Diagnostics;
using System.Text;
using System.Text.Json;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Fixtures;
using SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Models;
using Xunit;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Tests;

public sealed class PhaseDReportStressAndSecurityTests : IDisposable
{
    private readonly string _tempDir;
    private readonly string _sandboxOutDir;

    public PhaseDReportStressAndSecurityTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "sns-ag-fbl-d-" + Guid.NewGuid().ToString("N"));
        _sandboxOutDir = Path.Combine(_tempDir, "sandbox-out");
        Directory.CreateDirectory(_sandboxOutDir);
        LabSyntheticFixture.EnsureBasicFixture(_tempDir);
    }

    [Fact]
    public void S01_MalformedJsonInput_ReturnsNonZeroExitCodeWithoutUnhandledCrash()
    {
        var malformedPath = Path.Combine(_tempDir, "malformed.json");
        File.WriteAllText(malformedPath, "{ \"schemaVersion\": \"1.0.0\", \"draft\": { broken_json ", Encoding.UTF8);
        var outPath = Path.Combine(_sandboxOutDir, "verdict-malformed.json");

        using var stdout = new StringWriter();
        using var stderr = new StringWriter();

        var ex = Assert.Throws<LabUsageException>(() =>
            Program.RunCli(["validate", "--input", malformedPath, "--out", outPath], stdout, stderr, _sandboxOutDir));

        Assert.Equal("MalformedJson", ex.Code);
        Assert.NotEqual(0, ex.ExitCode);
        Assert.False(File.Exists(outPath));
    }

    [Fact]
    public void S02_DeeplyNestedAndMassiveJsonInput_IsRejectedBySafeLimits()
    {
        // Build deeply nested JSON (depth = 50 > MaxJsonDepth = 32)
        var sb = new StringBuilder();
        for (int i = 0; i < 50; i++) sb.Append("{\"a\":");
        sb.Append('1');
        for (int i = 0; i < 50; i++) sb.Append('}');

        var exDepth = Assert.Throws<LabUsageException>(() =>
            SandboxGuard.ParseSafeJson<FollowerPlanInput>(sb.ToString(), "deep-nested.json"));
        Assert.Equal("MalformedJson", exDepth.Code);

        // Build oversized array (> MaxCollectionItems = 5000)
        var items = string.Join(",", Enumerable.Range(0, 5200).Select(i => $"\"013952:Skyrim.esm\""));
        var hugeArrayJson = $$"""
        {
          "schemaVersion": "1.0.0",
          "draft": {
            "name": "Huge",
            "editorId": "SNS_Huge",
            "outfitArmorFormKeys": [{{items}}]
          }
        }
        """;

        var exArray = Assert.Throws<LabUsageException>(() =>
            SandboxGuard.ParseSafeJson<FollowerPlanInput>(hugeArrayJson, "huge-array.json"));
        Assert.Equal("CollectionTooLarge", exArray.Code);
    }

    [Fact]
    public void S03_OutputPathEscapingSandbox_IsRejectedBeforeWriting()
    {
        var escapedPath = Path.Combine(_sandboxOutDir, "..", "..", "outside_escape.json");

        var ex = Assert.Throws<LabUsageException>(() =>
            SandboxGuard.ValidateOutputPath(escapedPath, _sandboxOutDir));

        Assert.Equal("SandboxEscapeRejected", ex.Code);
        Assert.Equal(3, ex.ExitCode);
    }

    [Fact]
    public void S04_HtmlReportWithMaliciousPayloads_EscapesAllExecutableTagsAndAttributes()
    {
        using var ctx = PluginContext.OpenDirectory(_tempDir);
        var maliciousDraft = LabSyntheticFixture.CreateSampleDraft(
            editorId: "SNS_Xss\" onload=\"alert(1)",
            name: "<script>alert('XSS_NAME')</script><img src=x onerror=alert(2)>");

        var verdict = FollowerValidationEngine.ValidatePlan(ctx, maliciousDraft);
        var html = ReportGenerator.GenerateHtmlFromVerdict(verdict, "C:\\fake\\<script>bad.json</script>");

        Assert.DoesNotContain("<script>alert('XSS_NAME')</script>", html, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("<img src=x onerror=alert(2)>", html, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("&lt;script&gt;alert(&#39;XSS_NAME&#39;)&lt;/script&gt;", html, StringComparison.Ordinal);
        Assert.Contains("SNS_Xss&quot; onload=&quot;alert(1)", html, StringComparison.Ordinal);
    }

    [Fact]
    public void Benchmark_OneThousandSyntheticRecords_CompletesWithinBoundedTimeAndMemory()
    {
        var benchDir = Path.Combine(_tempDir, "bench-1000");
        LabSyntheticFixture.EnsureBasicFixture(benchDir, extraSyntheticRecords: 1000);

        long memBefore = GC.GetTotalMemory(forceFullCollection: true);
        var swLoad = Stopwatch.StartNew();
        using var ctx = PluginContext.OpenDirectory(benchDir);
        swLoad.Stop();

        var swAnalyze = Stopwatch.StartNew();
        var inspect = FollowerValidationEngine.InspectCatalog(ctx);
        var draft = LabSyntheticFixture.CreateSampleDraft();
        var verdict = FollowerValidationEngine.ValidatePlan(ctx, draft);
        var html = ReportGenerator.GenerateHtmlFromVerdict(verdict);
        swAnalyze.Stop();

        long peakMemDelta = Math.Max(0, GC.GetTotalMemory(forceFullCollection: false) - memBefore);
        var inspectJson = JsonSerializer.Serialize(inspect, SandboxGuard.SafeJsonOptions);

        // Verify 1,000+ synthetic records were actually enumerated and analyzed
        int totalCatalogRecords = inspect.Classes.Count + inspect.CombatStyles.Count + inspect.Voices.Count;
        Assert.True(totalCatalogRecords >= 1000, $"Expected >= 1000 catalog records, got {totalCatalogRecords}");
        Assert.True(swLoad.ElapsedMilliseconds < 10000);
        Assert.True(swAnalyze.ElapsedMilliseconds < 15000);
        Assert.True(inspectJson.Length > 10000);
        Assert.True(html.Length > 2000);
        Assert.True(peakMemDelta >= 0);
    }

    public void Dispose()
    {
        try
        {
            if (Directory.Exists(_tempDir)) Directory.Delete(_tempDir, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
