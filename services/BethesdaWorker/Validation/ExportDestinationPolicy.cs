using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.NpcCatalog;
using SkyrimNpcStudio.Worker.PluginReader;

namespace SkyrimNpcStudio.Worker.Validation;

/// <summary>
/// The one rule every export applies to its output folder (Modes A/B/C and native NPC; audit F-13): an
/// existing absolute folder that, with every symbolic link and junction resolved, is not inside
/// <list type="bullet">
/// <item>the folder of an active plugin (a game Data folder or an installed mod), nor its subfolders;</item>
/// <item>MO2's mods folder (the parent of an active plugin's folder that MO2 manages: it holds a meta.ini);</item>
/// <item>the game folder (the parent of the Data folder of a base-game master, when it holds the game's executable);</item>
/// <item>an MO2 overwrite folder or any folder of an MO2 instance (a folder with ModOrganizer.ini).</item>
/// </list>
/// Returns the resolved folder, which the export then writes into (never the path as written), so a link
/// swapped after this check does not redirect the write.
/// </summary>
internal static class ExportDestinationPolicy
{
    private static readonly StringComparison PathComparison =
        OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;

    private static readonly string[] GameExecutables = ["SkyrimSE.exe", "SkyrimSELauncher.exe", "TESV.exe", "SkyrimVR.exe"];

    public static string Validate(LoadOrderSession session, string path)
    {
        if (string.IsNullOrWhiteSpace(path) || !Path.IsPathFullyQualified(path) || !Directory.Exists(path))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, "Choose an existing absolute output directory outside Skyrim and MO2.");
        }

        var output = RealDirectory(path);
        foreach (var plugin in session.Plugins)
        {
            var folder = RealDirectory(Path.GetDirectoryName(Path.GetFullPath(plugin.Path))!);
            Refuse(output, folder, "Export cannot write inside a game Data directory or an active mod, including subdirectories and links.");

            var parent = Directory.GetParent(folder)?.FullName;
            if (parent is null)
            {
                continue;
            }

            if (File.Exists(Path.Combine(folder, "meta.ini")))
            {
                Refuse(output, parent, "Export cannot write inside MO2's mods directory.");
            }

            if (FaceRecipeQueries.IsBaseGameMaster(plugin.ModKey) && GameExecutables.Any(exe => File.Exists(Path.Combine(parent, exe))))
            {
                Refuse(output, parent, "Export cannot write inside the game directory.");
            }
        }

        for (var directory = new DirectoryInfo(output); directory is not null; directory = directory.Parent)
        {
            if (directory.Name.Equals("overwrite", StringComparison.OrdinalIgnoreCase)
                || File.Exists(Path.Combine(directory.FullName, "ModOrganizer.ini")))
            {
                throw new WorkerException(ErrorCodes.InvalidArgument, "Export cannot write inside MO2 or its overwrite directory.");
            }
        }

        return output;
    }

    private static void Refuse(string output, string forbidden, string message)
    {
        if (output.Equals(forbidden, PathComparison) || output.StartsWith(forbidden + Path.DirectorySeparatorChar, PathComparison))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, message);
        }
    }

    /// <summary>The folder with each component's symbolic link or junction resolved to its final target.</summary>
    internal static string RealDirectory(string value)
    {
        var full = Path.GetFullPath(value);
        var resolved = Path.GetPathRoot(full)!;
        foreach (var part in full[resolved.Length..].Split([Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar], StringSplitOptions.RemoveEmptyEntries))
        {
            resolved = Path.Combine(resolved, part);
            resolved = new DirectoryInfo(resolved).ResolveLinkTarget(returnFinalTarget: true)?.FullName ?? resolved;
        }

        return Path.TrimEndingDirectorySeparator(resolved);
    }
}
