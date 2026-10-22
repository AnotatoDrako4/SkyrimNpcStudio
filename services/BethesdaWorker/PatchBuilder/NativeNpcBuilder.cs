using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;
using Noggog;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PluginReader;
using SkyrimNpcStudio.Worker.Validation;

namespace SkyrimNpcStudio.Worker.PatchBuilder;

/// <summary>Creates original records. No existing NPC is copied or overridden.</summary>
internal static class NativeNpcBuilder
{
    public static CreationValidateResponse Validate(LoadOrderSession session, CatalogQueries queries, CreationValidateRequest request) =>
        NativeNpcValidator.Validate(session, queries, request);

    public static CreationExportResponse Export(LoadOrderSession session, CatalogQueries queries, CreationExportRequest request, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        ExportDestinationPolicy.Validate(session, request.OutputDirectory);
        var validation = Validate(session, queries, new CreationValidateRequest { Npc = request.Npc, PluginName = request.PluginName });
        if (!validation.TechnicallyExportable || !request.TechnicalOnly)
            throw new WorkerException(ErrorCodes.ValidationFailed, "Native NPC validation failed; final export requires verified native FaceGen.", validation.Issues);
        var package = ModPackager.ExportNativeNpc(session, queries, request, validation, ct);
        return new CreationExportResponse { Package = package, Validation = validation, ReadBackVerified = true };
    }

    internal static BuiltPatch Build(LoadOrderSession session, CatalogQueries queries, CreationExportRequest request,
        CreationValidateResponse validation, string path, CancellationToken ct)
    {
        var d = request.Npc;
        var key = ModKey.FromFileName(request.PluginName);
        var mod = new SkyrimMod(key, SkyrimRelease.SkyrimSE);
        mod.ModHeader.Author = request.Author.Length > 255 ? request.Author[..255] : request.Author;
        mod.ModHeader.Description = "TECHNICAL TEST ONLY - native FaceGen pending; do not install in Skyrim";
        if (request.EslFlag) mod.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Small;
        FormKey K(string text) => queries.TryParseAnyFormKey(text, out var value) ? value : throw new WorkerException(ErrorCodes.InvalidArgument, $"Invalid FormKey {text}");
        var npc = mod.Npcs.AddNew(d.EditorId);
        npc.Name = d.Name;
        npc.Height = d.Height; npc.Weight = d.Weight;
        npc.Race.SetTo(K(d.RaceFormKey));
        npc.Voice.SetTo(K(d.VoiceFormKey)); npc.Class.SetTo(K(d.ClassFormKey));
        if (d.Female) npc.Configuration.Flags |= NpcConfiguration.Flag.Female;
        npc.Configuration.Flags |= NpcConfiguration.Flag.Unique;
        npc.Configuration.Level = d.LevelMultiplier is { } mult ? new PcLevelMult { LevelMult = mult } : new NpcLevel { Level = (short)d.Level };
        npc.Configuration.CalcMinLevel = (short)d.MinLevel; npc.Configuration.CalcMaxLevel = (short)d.MaxLevel;
        npc.Configuration.SpeedMultiplier = 100;
        npc.PlayerSkills = new PlayerSkills { Health = (ushort)d.Health, Magicka = (ushort)d.Magicka, Stamina = (ushort)d.Stamina };
        foreach (var (skill, value) in d.Skills) npc.PlayerSkills.SkillValues[Enum.Parse<Skill>(skill)] = (byte)value;
        if (d.CombatStyleFormKey.Length > 0) npc.CombatStyle.SetTo(K(d.CombatStyleFormKey));
        if (d.HairColorFormKey.Length > 0) npc.HairColor.SetTo(K(d.HairColorFormKey));
        if (d.HeadTextureFormKey.Length > 0) npc.HeadTexture.SetTo(K(d.HeadTextureFormKey));
        foreach (var head in d.HeadPartFormKeys) npc.HeadParts.Add(new FormLink<IHeadPartGetter>(K(head)));
        foreach (var f in d.Factions) npc.Factions.Add(new RankPlacement { Faction = new FormLink<IFactionGetter>(K(f.FormKey)), Rank = (sbyte)f.Rank });
        npc.Items = new ExtendedList<ContainerEntry>();
        foreach (var item in d.Inventory) npc.Items.Add(new ContainerEntry { Item = new ContainerItem { Item = new FormLink<IItemGetter>(K(item.FormKey)), Count = item.Count } });
        foreach (var p in d.PackageFormKeys) npc.Packages.Add(new FormLink<IPackageGetter>(K(p)));
        if (d.SpellFormKeys.Count > 0)
        {
            npc.ActorEffect ??= new ExtendedList<IFormLinkGetter<ISpellRecordGetter>>();
            foreach (var s in d.SpellFormKeys) npc.ActorEffect.Add(new FormLink<ISpellRecordGetter>(K(s)));
        }
        if (d.PerkFormKeys.Count > 0)
        {
            npc.Perks ??= new ExtendedList<PerkPlacement>();
            foreach (var pk in d.PerkFormKeys) npc.Perks.Add(new PerkPlacement { Perk = new FormLink<IPerkGetter>(K(pk)), Rank = 1 });
        }
        if (d.Face is { } face) ApplyFace(npc, face);
        var records = new List<ExportedRecord> { Record(npc, "NPC_") };
        if (d.OutfitArmorFormKeys.Count > 0)
        {
            var outfit = mod.Outfits.AddNew(d.EditorId + "_Outfit");
            outfit.Items = new ExtendedList<IFormLinkGetter<IOutfitTargetGetter>>(d.OutfitArmorFormKeys.Select(x => (IFormLinkGetter<IOutfitTargetGetter>)new FormLink<IOutfitTargetGetter>(K(x))));
            npc.DefaultOutfit.SetTo(outfit.FormKey); records.Add(Record(outfit, "OTFT"));
        }
        if (d.Placement is { } placement)
        {
            // An explicitly requested interior CELL context is necessary to contain the new ACHR.
            // Do not clone the cell's existing references or regenerate them as new records.
            var source = session.LinkCache.Resolve<ICellGetter>(K(placement.CellFormKey));
            var cell = source.DeepCopy();
            cell.Persistent.Clear(); cell.Temporary.Clear(); cell.NavigationMeshes.Clear(); cell.Landscape = null;
            var placed = new PlacedNpc(mod.GetNextFormKey(), SkyrimRelease.SkyrimSE) { EditorID = d.EditorId + "_Ref",
                Placement = new Placement { Position = new P3Float(placement.X, placement.Y, placement.Z), Rotation = new P3Float(0, 0, placement.RotationZ) } };
            placed.Base.SetTo(npc.FormKey); cell.Temporary.Add(placed);
            mod.Cells.AddInteriorCell(cell);
            records.Add(Record(placed, "ACHR"));
            records.Add(new ExportedRecord { FormKey = cell.FormKey.ToString(), EditorId = cell.EditorID, RecordType = "CELL", Kind = "override" });
        }
        ct.ThrowIfCancellationRequested();
        if (request.EslFlag && mod.EnumerateMajorRecords().Any(r => r.FormKey.ModKey == key && r.FormKey.ID is < 0x800 or > 0xFFF))
            throw new WorkerException(ErrorCodes.ValidationFailed, "Generated FormKeys exceed the supported ESL range.");
        mod.WriteToBinary(path, new BinaryWriteParameters { MastersListOrdering = new MastersListOrderingByLoadOrder(session.Plugins.Select(p => p.ModKey).ToArray()), MasterFlagsLookup = session.MasterStyles });
        ct.ThrowIfCancellationRequested();
        using var written = SkyrimMod.CreateFromBinaryOverlay(new ModPath(key, path), SkyrimRelease.SkyrimSE, new BinaryReadParameters { MasterFlagsLookup = session.MasterStyles });
        Verify(session, mod, written, records, request.EslFlag);
        return new BuiltPatch(path, written.ModHeader.MasterReferences.Select(m => m.Master).ToArray(), request.EslFlag, records, [], validation.Issues);
    }

    private static ExportedRecord Record(ISkyrimMajorRecordGetter r, string type) =>
        new() { FormKey = r.FormKey.ToString(), EditorId = r.EditorID, RecordType = type, Kind = "new" };

    private static void Verify(LoadOrderSession session, SkyrimMod expected, ISkyrimModGetter written, IReadOnlyList<ExportedRecord> records, bool esl)
    {
        void Require(bool condition, string message)
        { if (!condition) throw new WorkerException(ErrorCodes.ValidationFailed, "Native plugin read-back failed: " + message); }
        var actual = written.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
        Require(actual.Count == records.Count, "record count mismatch");
        foreach (var r in expected.EnumerateMajorRecords())
        {
            Require(actual.ContainsKey(r.FormKey), "record missing");
            Require(r.EditorID == actual[r.FormKey].EditorID, "EditorID mismatch");
        }
        Require(written.Npcs.Count == 1 && written.Npcs.Single().FormKey.ModKey == expected.ModKey, "NPC must be new and owned by output plugin");
        var e = expected.Npcs.Single(); var a = written.Npcs.Single();
        Require(e.Name?.String == a.Name?.String && e.Race.FormKey == a.Race.FormKey && e.Voice.FormKey == a.Voice.FormKey && e.Class.FormKey == a.Class.FormKey, "identity links mismatch");
        Require(Math.Abs(e.Height - a.Height) < 1e-5 && Math.Abs(e.Weight - a.Weight) < 1e-5 && e.Configuration.Flags == a.Configuration.Flags, "physical traits mismatch");
        Require(e.Configuration.Level.Equals(a.Configuration.Level) && e.Configuration.CalcMinLevel == a.Configuration.CalcMinLevel && e.Configuration.CalcMaxLevel == a.Configuration.CalcMaxLevel, "level mismatch");
        Require(e.PlayerSkills!.Health == a.PlayerSkills!.Health && e.PlayerSkills.Magicka == a.PlayerSkills.Magicka && e.PlayerSkills.Stamina == a.PlayerSkills.Stamina && e.PlayerSkills.SkillValues.All(x => a.PlayerSkills.SkillValues[x.Key] == x.Value), "statistics mismatch");
        Require(e.HeadParts.Select(x => x.FormKey).SequenceEqual(a.HeadParts.Select(x => x.FormKey)) && e.HairColor.FormKey == a.HairColor.FormKey && e.HeadTexture.FormKey == a.HeadTexture.FormKey && e.CombatStyle.FormKey == a.CombatStyle.FormKey, "appearance links mismatch");
        Require(e.DefaultOutfit.FormKey == a.DefaultOutfit.FormKey && (e.Items ?? []).Select(x => (x.Item.Item.FormKey, x.Item.Count)).SequenceEqual((a.Items ?? []).Select(x => (x.Item.Item.FormKey, x.Item.Count))), "equipment mismatch");
        Require(e.Factions.Select(x => (x.Faction.FormKey, x.Rank)).SequenceEqual(a.Factions.Select(x => (x.Faction.FormKey, x.Rank))) && e.Packages.Select(x => x.FormKey).SequenceEqual(a.Packages.Select(x => x.FormKey)), "behavior links mismatch");
        Require(FaceRecipeQueries.Sliders(e.FaceMorph).SequenceEqual(FaceRecipeQueries.Sliders(a.FaceMorph)), "face morph mismatch");
        if (e.FaceParts is { } parts) Require(a.FaceParts is not null && parts.Nose == a.FaceParts.Nose && parts.Unknown == a.FaceParts.Unknown && parts.Eyes == a.FaceParts.Eyes && parts.Mouth == a.FaceParts.Mouth, "face preset mismatch");
        if (e.TextureLighting is { } color) Require(a.TextureLighting is { } ac && color.R == ac.R && color.G == ac.G && color.B == ac.B, "skin color mismatch");
        Require(e.TintLayers.Select(t => (t.Index, t.Color?.R, t.Color?.G, t.Color?.B, t.InterpolationValue, t.Preset)).SequenceEqual(a.TintLayers.Select(t => (t.Index, t.Color?.R, t.Color?.G, t.Color?.B, t.InterpolationValue, t.Preset))), "tint mismatch");
        foreach (var outfit in expected.Outfits)
            Require((outfit.Items ?? []).Select(x => x.FormKey).SequenceEqual((written.Outfits.Single(x => x.FormKey == outfit.FormKey).Items ?? []).Select(x => x.FormKey)), "outfit mismatch");
        foreach (var placed in expected.EnumerateMajorRecords<IPlacedNpcGetter>())
        {
            var read = (IPlacedNpcGetter)actual[placed.FormKey];
            Require(placed.Base.FormKey == read.Base.FormKey && placed.Placement!.Position == read.Placement!.Position && placed.Placement.Rotation == read.Placement.Rotation, "placement mismatch");
        }
        Require(esl == written.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small), "ESL flag mismatch");
        foreach (var record in actual.Values)
        {
            if (record.FormKey.ModKey == expected.ModKey && esl) Require(record.FormKey.ID is >= 0x800 and <= 0xFFF, "ESL range mismatch");
            foreach (var link in record.EnumerateFormLinks().Where(l => !l.IsNull))
                Require(link.FormKey.ModKey == expected.ModKey ? actual.ContainsKey(link.FormKey) : session.LinkCache.TryResolve(link.FormKey, link.Type, out _), $"unresolved reference {link.FormKey}");
        }
        var masterSet = written.ModHeader.MasterReferences.Select(m => m.Master).ToHashSet();
        var last = -1;
        foreach (var master in written.ModHeader.MasterReferences)
        {
            var plugin = session.Find(master.Master);
            Require(plugin is not null && plugin.LoadIndex > last, "missing or unordered master");
            last = plugin!.LoadIndex;
        }
        foreach (var record in actual.Values)
        {
            if (record.FormKey.ModKey != expected.ModKey) Require(masterSet.Contains(record.FormKey.ModKey), "missing CELL master");
            foreach (var link in record.EnumerateFormLinks().Where(l => !l.IsNull && l.FormKey.ModKey != expected.ModKey))
                Require(masterSet.Contains(link.FormKey.ModKey), "missing reference master");
        }
    }

    private static void ApplyFace(Npc npc, FaceValuesDto v)
    {
        var s = v.Sliders;
        npc.FaceMorph = new NpcFaceMorph { NoseLongVsShort = s[0], NoseUpVsDown = s[1], JawUpVsDown = s[2], JawNarrowVsWide = s[3], JawForwardVsBack = s[4], CheeksUpVsDown = s[5], CheeksForwardVsBack = s[6], EyesUpVsDown = s[7], EyesInVsOut = s[8], BrowsUpVsDown = s[9], BrowsInVsOut = s[10], BrowsForwardVsBack = s[11], LipsUpVsDown = s[12], LipsInVsOut = s[13], ChinNarrowVsWide = s[14], ChinUpVsDown = s[15], ChinUnderbiteVsOverbite = s[16], EyesForwardVsBack = s[17] };
        npc.FaceParts = new NpcFaceParts { Nose = unchecked((uint)v.Presets[0]), Unknown = unchecked((uint)v.Presets[1]), Eyes = unchecked((uint)v.Presets[2]), Mouth = unchecked((uint)v.Presets[3]) };
        npc.TextureLighting = System.Drawing.Color.FromArgb(255, v.SkinTone.R, v.SkinTone.G, v.SkinTone.B);
        foreach (var t in v.Tints) npc.TintLayers.Add(new TintLayer { Index = (ushort)t.Index, Preset = (short)t.Preset, InterpolationValue = t.Value, Color = System.Drawing.Color.FromArgb(0, t.Color.R, t.Color.G, t.Color.B) });
    }
}
