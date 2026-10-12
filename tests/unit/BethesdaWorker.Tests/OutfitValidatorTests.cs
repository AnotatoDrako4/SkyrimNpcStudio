using SkyrimNpcStudio.TestFixtures;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.Validation;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

public sealed class OutfitValidatorTests(CatalogFixture fixture) : IClassFixture<CatalogFixture>
{
    private OutfitValidateResponse Validate(string npc, params string[] armors) =>
        fixture.State.Read(c => OutfitValidator.Validate(c.Session, c.Queries, new OutfitValidateRequest
        {
            NpcFormKey = npc,
            ArmorFormKeys = armors,
        }));

    private static readonly string Lydia = SyntheticLoadOrder.Lydia.ToString();

    [Fact]
    public void PiecesFromDifferentPlugins_AreExportable()
    {
        var r = Validate(Lydia,
            fixture.LoadOrder.CrimsonCuirass.ToString(),
            SyntheticLoadOrder.SteelBoots.ToString(),
            SyntheticLoadOrder.SteelGauntlets.ToString(),
            fixture.LoadOrder.CrimsonCloak.ToString());

        Assert.True(r.Exportable, string.Join("; ", r.Issues.Select(i => i.Message)));
        Assert.Equal(4, r.Pieces.Count);
        Assert.Equal((1u << 2) | (1u << 7) | (1u << 3) | (1u << 16), r.CombinedSlotMask);
        // Lydia is female: the female mesh of the matching ARMA is reported for asset checks.
        Assert.Equal(@"Armor\Crimson\TorsoF_1.nif", Assert.Single(r.Pieces[0].Meshes));
        Assert.Contains(r.Issues, i => i.Code == "SaveGamePersistence");
    }

    [Fact]
    public void TwoBodyPieces_AreASlotConflict()
    {
        var r = Validate(Lydia, SyntheticLoadOrder.SteelCuirass.ToString(), SyntheticLoadOrder.ElvenCuirass.ToString());
        Assert.False(r.Exportable);
        var issue = Assert.Single(r.Issues, i => i.Code == "SlotConflict");
        Assert.Contains("32 - Body", issue.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ArmorWithoutAddonForNordArmorRace_IsInvisible()
    {
        // NordRace → armor race DefaultRace; the hood only has an ARMA for KhajiitRace.
        var r = Validate(Lydia, SyntheticLoadOrder.KhajiitOnlyHood.ToString());
        Assert.False(r.Exportable);
        Assert.Contains(r.Issues, i => i.Code == "NoAddonForRace");
    }

    [Fact]
    public void TemplatedNpcWithInventoryFlag_CannotTakeAnOutfit()
    {
        var r = Validate(SyntheticLoadOrder.TemplatedGuard.ToString(), SyntheticLoadOrder.SteelCuirass.ToString());
        Assert.False(r.Exportable);
        Assert.Contains(r.Issues, i => i.Code == "OutfitFromTemplate");
    }

    [Fact]
    public void UnknownRecords_AreErrors_AndDuplicatesAreCollapsed()
    {
        var r = Validate(Lydia, "123456:Nope.esp", SyntheticLoadOrder.SteelBoots.ToString(), SyntheticLoadOrder.SteelBoots.ToString());
        Assert.Contains(r.Issues, i => i.Code == "ArmorNotFound");
        Assert.Contains(r.Issues, i => i.Code == "DuplicatePiece");
        Assert.Single(r.Pieces);

        var missingNpc = Validate("FFFFFF:Skyrim.esm");
        Assert.Contains(missingNpc.Issues, i => i.Code == "NpcNotFound");
    }

    [Fact]
    public void EmptyOutfit_IsAWarningOnly()
    {
        var r = Validate(Lydia);
        Assert.True(r.Exportable);
        Assert.Contains(r.Issues, i => i.Code == "EmptyOutfit");
    }
}
