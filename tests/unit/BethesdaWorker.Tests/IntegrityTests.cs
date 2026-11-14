using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PatchBuilder;
using SkyrimNpcStudio.Worker.Validation;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

public sealed class IntegrityTests : IClassFixture<CatalogFixture>
{
    private readonly CatalogFixture _fixture;
    public IntegrityTests(CatalogFixture fixture) => _fixture = fixture;
    [Fact]
    public void RecordCapturesEffectiveRecipeAndStableIdentity()
    {
        var result = _fixture.State.Read(c => IntegrityQueries.Get(c.Session, c.Queries,
            new(SyntheticLoadOrder.Lydia.ToString()), CancellationToken.None));
        Assert.Equal(SyntheticLoadOrder.Lydia.ToString(), result.FormKey);
        Assert.Equal("Skyrim.esm", result.DefiningPlugin);
        Assert.NotEmpty(result.HeadParts);
        Assert.Contains(result.Assets, p => p.EndsWith("000A2C8E.nif", StringComparison.OrdinalIgnoreCase));
        Assert.Contains("record:" + result.FormKey, result.Dependencies);
        Assert.Equal(64, result.AppearanceSignature.Length);
    }
    [Fact]
    public void TraitsUseTemplateIdentityAndAssets()
    {
        var result = _fixture.State.Read(c => IntegrityQueries.Get(c.Session, c.Queries,
            new(SyntheticLoadOrder.TemplatedGuard.ToString()), CancellationToken.None));
        Assert.Equal(SyntheticLoadOrder.GuardTemplate.ToString(), result.TraitsSource);
        Assert.Contains("record:" + SyntheticLoadOrder.GuardTemplate, result.Dependencies);
        Assert.Contains(result.Assets, p => p.EndsWith("000A0002.nif", StringComparison.OrdinalIgnoreCase));
    }
    [Fact]
    public void MissingNpcIsConfirmedDependencyAndCancellationIsNotSuccess()
    {
        var result = _fixture.State.Read(c => IntegrityQueries.Get(c.Session, c.Queries, new("FFFFFF:Skyrim.esm"), CancellationToken.None));
        Assert.NotEmpty(result.Unresolved);
        Assert.Throws<OperationCanceledException>(() => _fixture.State.Read(c => IntegrityQueries.Get(c.Session, c.Queries,
            new(SyntheticLoadOrder.Lydia.ToString()), new CancellationToken(true))));
    }
    [Fact]
    public void RepairReadbackRejectsRecordsOutsideDeclaredPackage()
    {
        var path = _fixture.LoadOrder.LoadOrder[0].Path;
        var built = new BuiltPatch(path, [], false, [], [], []);
        Assert.Throws<WorkerException>(() => _fixture.State.Read(c =>
        {
            RepairIntegrityGate.Check(c.Session, built, Path.GetDirectoryName(path)!, CancellationToken.None);
            return true;
        }));
    }
    [Fact]
    public void MalformedStagingAndCancelledValidationAreRejected()
    {
        var dir = Path.Combine(Path.GetTempPath(), "sns-integrity-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            var allow = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "repair.esp" };
            File.WriteAllText(Path.Combine(dir, "repair.esp"), "fixture");
            RepairIntegrityGate.CheckFiles(dir, allow, CancellationToken.None);
            File.WriteAllText(Path.Combine(dir, "unexpected.dll"), "fixture");
            Assert.Throws<WorkerException>(() => RepairIntegrityGate.CheckFiles(dir, allow, CancellationToken.None));
            Assert.Throws<OperationCanceledException>(() => RepairIntegrityGate.CheckFiles(dir, allow, new CancellationToken(true)));
        }
        finally { Directory.Delete(dir, true); }
    }
}
