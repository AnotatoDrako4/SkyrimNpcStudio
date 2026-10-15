namespace SkyrimNpcStudio.Worker.Contracts;

/// <summary>One NPC_ field whose value in the exported override differs from the winning record.</summary>
public sealed record NpcFieldChange
{
    /// <summary>Mutagen property name (e.g. "HairColor").</summary>
    public required string Field { get; init; }

    /// <summary>Record subrecord signature when known (e.g. "HCLF"); empty for fields without a mapping.</summary>
    public required string Subrecord { get; init; }

    public required string Original { get; init; }
    public required string Exported { get; init; }

    /// <summary>Why the export changes it (the operation that asked for it).</summary>
    public required string Reason { get; init; }

    /// <summary>FormKeys the exported value points to; "(nuevo en este mod)" marks records the patch creates.</summary>
    public required IReadOnlyList<string> Dependencies { get; init; }
}

/// <summary>Semantic comparison of one NPC override, re-read from the written plugin, against its winning record.</summary>
public sealed record NpcSemanticDiff
{
    public required string Npc { get; init; }
    public string? NpcEditorId { get; init; }
    public required IReadOnlyList<NpcFieldChange> Changes { get; init; }
}
