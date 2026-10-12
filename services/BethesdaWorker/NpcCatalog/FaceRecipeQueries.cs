using System.Text.RegularExpressions;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.NpcCatalog;

/// <summary>
/// Resolves what the Creation Kit uses to build an NPC's FaceGen (see docs/FACEGEN.md): the race's
/// default head parts for the NPC's sex replaced type by type by the NPC's own (PNAM), extra parts
/// appended after their owner, each part's mesh/TRI/texture set, the morph race, the race tint
/// masks and the record's face values.
/// </summary>
internal static partial class FaceRecipeQueries
{
    private static readonly HashSet<string> BethesdaMasters = new(StringComparer.OrdinalIgnoreCase)
    {
        "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm",
    };

    [GeneratedRegex(@"^cc[A-Za-z]{3}SSE\d{3}-", RegexOptions.CultureInvariant)]
    private static partial Regex CreationClub();

    /// <summary>Bethesda masters and Creation Club content: assets every Skyrim SE install has.</summary>
    public static bool IsOfficial(ModKey key) => BethesdaMasters.Contains(key.FileName) || CreationClub().IsMatch(key.FileName);

    /// <summary>
    /// The five base-game masters vanilla FaceGen was built from. Creation Club plugins are newer and
    /// some override races without the DLC head-part changes, so they are not part of this set.
    /// </summary>
    public static bool IsBaseGameMaster(ModKey key) => BethesdaMasters.Contains(key.FileName);

    public static FaceRecipeResponse Get(LoadOrderSession session, CatalogQueries queries, FaceRecipeRequest request)
    {
        if (!queries.TryParseAnyFormKey(request.NpcFormKey, out var key))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"'{request.NpcFormKey}' is not a FormKey.");
        }

        INpcGetter npc;
        string source;
        if (!string.IsNullOrWhiteSpace(request.SourcePlugin))
        {
            var plugin = session.Find(ModKey.FromFileName(request.SourcePlugin))
                ?? throw new WorkerException(ErrorCodes.RecordNotFound, $"Plugin '{request.SourcePlugin}' is not in the active load order.");
            var version = plugin.Mod.Npcs.RecordCache.TryGetValue(key);
            if (version is null)
            {
                throw new WorkerException(ErrorCodes.RecordNotFound, $"'{request.SourcePlugin}' does not contain NPC {key}.");
            }

            npc = version;
            source = plugin.ModKey.FileName;
        }
        else
        {
            if (!session.LinkCache.TryResolve<INpcGetter>(key, out var winner))
            {
                throw new WorkerException(ErrorCodes.RecordNotFound, "NPC not found: " + request.NpcFormKey);
            }

            npc = winner;
            source = queries.WinningPluginOf(key);
        }

        if (npc.Template.FormKeyNullable is not null && npc.Configuration.TemplateFlags.HasFlag(NpcConfiguration.TemplateFlag.Traits))
        {
            throw new WorkerException(ErrorCodes.ValidationFailed,
                "This NPC takes its face from a template (Use Traits); edit the template NPC instead.");
        }

        var cache = request.OfficialOnly ? session.OfficialLinkCache : session.LinkCache;
        if (!cache.TryResolve<IRaceGetter>(npc.Race.FormKey, out var race))
        {
            throw new WorkerException(ErrorCodes.RecordNotFound, "NPC race is unresolved.");
        }

        var issues = new List<Issue>();
        var female = npc.Configuration.Flags.HasFlag(NpcConfiguration.Flag.Female);
        var morphRace = race.MorphRace.FormKeyNullable is { } morphKey
            && cache.TryResolve<IRaceGetter>(morphKey, out var morph) ? morph.EditorID ?? "" : "";
        var vampire = race.Keywords?.Any(k => cache.TryResolve<IKeywordGetter>(k.FormKey, out var kw)
            && string.Equals(kw.EditorID, "Vampire", StringComparison.OrdinalIgnoreCase)) ?? false;
        var headData = female ? race.HeadData?.Female : race.HeadData?.Male;

        // One part per type: race defaults first, then the NPC's own parts replace them.
        var slots = new List<IHeadPartGetter>();
        void Put(IHeadPartGetter part)
        {
            var index = slots.FindIndex(p => p.Type == part.Type);
            if (index >= 0)
            {
                slots[index] = part;
            }
            else
            {
                slots.Add(part);
            }
        }

        foreach (var reference in headData?.HeadParts ?? [])
        {
            if (reference.Head.FormKeyNullable is { } hk && cache.TryResolve<IHeadPartGetter>(hk, out var part))
            {
                Put(part);
            }
        }

        var npcParts = request.HeadPartFormKeys?.ToList() ?? npc.HeadParts.Select(h => h.FormKey.ToString()).ToList();
        foreach (var text in npcParts)
        {
            if (queries.TryParseAnyFormKey(text, out var hk) && cache.TryResolve<IHeadPartGetter>(hk, out var part))
            {
                Put(part);
            }
            else
            {
                issues.Add(Issue.Warning("HeadPartUnresolved", $"Head part '{text}' does not resolve in the active load order.", text));
            }
        }

        var parts = new List<FaceRecipeHeadPart>();
        var seen = new HashSet<FormKey>();
        void Add(IHeadPartGetter part, bool extra)
        {
            if (!seen.Add(part.FormKey))
            {
                return;
            }

            parts.Add(ToDto(cache, part, extra, female, race));
            foreach (var link in part.ExtraParts)
            {
                if (cache.TryResolve<IHeadPartGetter>(link.FormKey, out var child))
                {
                    Add(child, true);
                }
                else
                {
                    issues.Add(Issue.Warning("ExtraPartUnresolved", $"Extra part {link.FormKey} of {part.EditorID} does not resolve.", part.FormKey.ToString()));
                }
            }
        }

        foreach (var part in slots)
        {
            Add(part, false);
        }

        IReadOnlyList<string>? faceTextures = null;
        string? faceTexturesKey = null;
        FormKey? npcTexture = npc.HeadTexture.FormKeyNullable;
        if (request.HeadTextureFormKey is { } requestedTexture)
        {
            npcTexture = requestedTexture.Length == 0 ? null
                : queries.TryParseAnyFormKey(requestedTexture, out var rt) ? rt
                : throw new WorkerException(ErrorCodes.InvalidArgument, $"'{requestedTexture}' is not a FormKey.");
        }

        var faceTexture = npcTexture ?? headData?.DefaultFaceTexture.FormKeyNullable;
        if (faceTexture is { } tk && cache.TryResolve<ITextureSetGetter>(tk, out var txst))
        {
            faceTextures = TextureSlots(txst);
            faceTexturesKey = txst.FormKey.ToString();
        }

        var masks = new List<FaceRecipeTintMask>();
        foreach (var mask in headData?.TintMasks ?? [])
        {
            var path = mask.FileName?.GivenPath;
            if (mask.Index is not { } index || string.IsNullOrWhiteSpace(path))
            {
                continue;
            }

            masks.Add(new FaceRecipeTintMask(index, TexturePath(path), (int)(mask.MaskType ?? 0)));
        }

        AppearanceColor? hairColor = null;
        string? hairColorKey = null;
        if (npc.HairColor.FormKeyNullable is { } colorKey && cache.TryResolve<IColorRecordGetter>(colorKey, out var color))
        {
            hairColor = new AppearanceColor(color.Color.R, color.Color.G, color.Color.B);
            hairColorKey = color.FormKey.ToString();
        }

        var skin = npc.TextureLighting is { } lighting ? new AppearanceColor(lighting.R, lighting.G, lighting.B) : new AppearanceColor(255, 255, 255);
        if (npc.TextureLighting is null)
        {
            issues.Add(Issue.Info("NoTextureLighting", "The record has no skin tone (QNAM); white is assumed for the face tint."));
        }

        var tints = new List<FaceRecipeTint>();
        foreach (var layer in npc.TintLayers)
        {
            if (layer.Index is not { } index)
            {
                continue;
            }

            var c = layer.Color ?? System.Drawing.Color.White;
            tints.Add(new FaceRecipeTint(index, new AppearanceColor(c.R, c.G, c.B), TintValue(layer.InterpolationValue), layer.Preset ?? -1));
        }

        var faceGen = CatalogQueries.FaceGenFor(key);
        return new FaceRecipeResponse
        {
            NpcFormKey = key.ToString(),
            SourcePlugin = source,
            FaceGenMeshPath = faceGen.GeometryPath,
            FaceGenTintPath = faceGen.TintPath,
            Female = female,
            Height = npc.Height,
            Race = new FaceRecipeRace(race.FormKey.ToString(), race.EditorID ?? "", morphRace, vampire),
            HeadParts = parts,
            NpcHeadParts = npcParts,
            FaceTextureSet = faceTextures,
            FaceTextureSetFormKey = faceTexturesKey,
            NpcHeadTexture = npcTexture?.ToString() ?? "",
            TintMasks = masks,
            Values = new FaceRecipeValues
            {
                Sliders = Sliders(npc.FaceMorph),
                Presets = npc.FaceParts is { } fp
                    ? [unchecked((int)fp.Nose), unchecked((int)fp.Unknown), unchecked((int)fp.Eyes), unchecked((int)fp.Mouth)]
                    : [-1, -1, -1, -1],
                Weight = npc.Weight,
                SkinTone = skin,
                HairColor = hairColor,
                HairColorFormKey = hairColorKey,
                Tints = tints,
            },
            Issues = issues,
        };
    }

    /// <summary>NAM9 in record order (the 19th float is unused and not returned).</summary>
    public static IReadOnlyList<float> Sliders(INpcFaceMorphGetter? fm) => fm is null
        ? new float[18]
        :
        [
            fm.NoseLongVsShort, fm.NoseUpVsDown, fm.JawUpVsDown, fm.JawNarrowVsWide, fm.JawForwardVsBack,
            fm.CheeksUpVsDown, fm.CheeksForwardVsBack, fm.EyesUpVsDown, fm.EyesInVsOut, fm.BrowsUpVsDown,
            fm.BrowsInVsOut, fm.BrowsForwardVsBack, fm.LipsUpVsDown, fm.LipsInVsOut, fm.ChinNarrowVsWide,
            fm.ChinUpVsDown, fm.ChinUnderbiteVsOverbite, fm.EyesForwardVsBack,
        ];

    /// <summary>TINV is a 0..100 percentage on disk.</summary>
    private static float TintValue(float? raw) => raw is not { } v ? 1.0f : v > 1.0001f ? v / 100.0f : v;

    private static FaceRecipeHeadPart ToDto(Mutagen.Bethesda.Plugins.Cache.ILinkCache<ISkyrimMod, ISkyrimModGetter> cache, IHeadPartGetter part, bool extra,
        bool female, IRaceGetter race)
    {
        var male = part.Flags.HasFlag(HeadPart.Flag.Male);
        var femaleFlag = part.Flags.HasFlag(HeadPart.Flag.Female);
        var validSex = male == femaleFlag ? "any" : male ? "male" : "female";
        var validForRace = true;
        if (part.ValidRaces.FormKeyNullable is { } listKey && cache.TryResolve<IFormListGetter>(listKey, out var list))
        {
            validForRace = list.Items.Any(i => i.FormKey == race.FormKey || i.FormKey == race.MorphRace.FormKeyNullable);
        }

        var tris = new List<FaceRecipeTri>();
        foreach (var p in part.Parts)
        {
            var file = p.FileName?.GivenPath;
            if (p.PartType is { } type && !string.IsNullOrWhiteSpace(file))
            {
                tris.Add(new FaceRecipeTri((int)type, MeshPath(file)));
            }
        }

        IReadOnlyList<string>? textures = null;
        if (part.TextureSet.FormKeyNullable is { } tk && cache.TryResolve<ITextureSetGetter>(tk, out var txst))
        {
            textures = TextureSlots(txst);
        }

        return new FaceRecipeHeadPart
        {
            FormKey = part.FormKey.ToString(),
            EditorId = part.EditorID ?? part.FormKey.ToString(),
            Type = part.Type?.ToString() ?? "Misc",
            Model = MeshPath(part.Model?.File.GivenPath ?? ""),
            Tris = tris,
            TextureSet = textures,
            Extra = extra,
            Plugin = part.FormKey.ModKey.FileName,
            Official = IsOfficial(part.FormKey.ModKey),
            ValidSex = validSex,
            ValidForRace = validForRace,
        };
    }

    /// <summary>
    /// TXST fields in BSShaderTextureSet slot order, as the Creation Kit writes them into FaceGen
    /// (measured on vanilla eyes and heads: TX02 goes to slot 5, TX03 to 2, TX04 to 3, TX05 to 4).
    /// </summary>
    public static IReadOnlyList<string> TextureSlots(ITextureSetGetter txst) =>
    [
        TexturePath(txst.Diffuse?.GivenPath), TexturePath(txst.NormalOrGloss?.GivenPath),
        TexturePath(txst.GlowOrDetailMap?.GivenPath), TexturePath(txst.Height?.GivenPath),
        TexturePath(txst.Environment?.GivenPath), TexturePath(txst.EnvironmentMaskOrSubsurfaceTint?.GivenPath),
        TexturePath(txst.Multilayer?.GivenPath), TexturePath(txst.BacklightMaskOrSpecular?.GivenPath), "",
    ];

    public static string MeshPath(string path) => Prefixed(path, "meshes\\");

    public static string TexturePath(string? path) => Prefixed(path, "textures\\");

    private static string Prefixed(string? path, string prefix)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return "";
        }

        var p = path.Replace('/', '\\').TrimStart('\\');
        return p.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ? p : prefix + p;
    }
}
