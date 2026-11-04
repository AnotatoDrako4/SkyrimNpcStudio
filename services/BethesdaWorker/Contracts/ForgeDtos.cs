namespace SkyrimNpcStudio.Worker.Contracts;

// Character Forge (export.forgeStaticResource): a static resource the studio already exported and verified (NIF
// re-read by nifly, DDS encoded, manifest hashed) becomes an MO2 mod with a new ESL-flagged plugin holding one STAT
// that points at the NIF. The worker only copies the listed files, checking their hashes; it never converts them.

public sealed record ExportForgeResourceRequest
{
    /// <summary>New plugin file name ("CF_Goblet.esp"); same rules as every other export.</summary>
    public required string PluginName { get; init; }

    public required string OutputDirectory { get; init; }

    public bool CreateZip { get; init; } = true;

    public bool EslFlag { get; init; } = true;

    public string Author { get; init; } = "Skyrim NPC Studio";

    public string? Description { get; init; }

    /// <summary>EditorID of the new STAT: letters, digits and underscore, 1..64, starting with a letter.</summary>
    public required string EditorId { get; init; }

    /// <summary>The exported package folder: the files are read from its "Data" subfolder.</summary>
    public required string SourceDirectory { get; init; }

    /// <summary>Files under Data, "meshes/..." (.nif) or "textures/..." (.dds), with the SHA-256 the studio recorded.</summary>
    public required IReadOnlyList<ForgeResourceFile> Files { get; init; }

    /// <summary>The STAT model: one of the listed .nif files, relative to "meshes" ("CharacterForge\x\x.nif").</summary>
    public required string ModelPath { get; init; }

    /// <summary>OBND in game units, from the NIF the studio re-read: [x, y, z] lower and upper corners.</summary>
    public required IReadOnlyList<short> BoundsMin { get; init; }

    public required IReadOnlyList<short> BoundsMax { get; init; }
}

public sealed record ForgeResourceFile
{
    public required string RelativePath { get; init; }

    public required string Sha256 { get; init; }
}
