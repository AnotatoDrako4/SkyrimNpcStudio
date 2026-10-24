using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.PluginReader;
using SkyrimNpcStudio.Worker.Validation;

namespace SkyrimNpcStudio.Worker.PatchBuilder;

/// <summary>
/// export.bodyDistribution: packages text files the studio generated (BodyChangeNG distribution JSON, README,
/// reports...) as an MO2-installable mod, with the same guarantees as <see cref="ModPackager"/>: the output
/// folder passes <see cref="ExportDestinationPolicy"/>, nothing existing is overwritten, everything is staged
/// in a hidden folder next to the destination, verified (read back, ZIP re-opened) and only then moved into
/// place; any failure deletes the staging folder. No plugin is produced.
/// </summary>
internal static class BodyDistributionPackager
{
    public const string ManifestFormat = "skyrim-npc-studio/body-distribution";
    public const int ManifestVersion = 1;
    public const int MaxModNameLength = 100;
    public const int MaxRelativePathLength = 240;

    private static readonly UTF8Encoding Utf8NoBom = new(encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true);
    private static readonly JsonSerializerOptions ManifestJson = new(JsonSerializerDefaults.Web) { WriteIndented = true };

    // Files the packager writes itself; the request cannot supply them.
    private static readonly string[] Reserved = ["manifest.json", "meta.ini"];

    private static readonly HashSet<string> DeviceNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "COM¹", "COM²", "COM³",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "LPT¹", "LPT²", "LPT³",
    };

    public static ExportBodyDistributionResponse Export(LoadOrderSession session, ExportBodyDistributionRequest request, CancellationToken ct)
    {
        // The resolved folder (links and junctions followed): the export writes there, never through a link.
        var outputDir = ExportDestinationPolicy.Validate(session, request.OutputDirectory);
        ValidateModName(request.ModName);
        var files = NormalizeFiles(request.Files);

        var modName = request.ModName;
        var finalModDir = Path.Combine(outputDir, modName);
        var finalZip = Path.Combine(outputDir, modName + ".zip");
        if (Directory.Exists(finalModDir) || File.Exists(finalModDir))
        {
            throw new WorkerException(ErrorCodes.OutputExists, $"'{finalModDir}' already exists; choose another name or folder.");
        }

        if (File.Exists(finalZip) || Directory.Exists(finalZip))
        {
            throw new WorkerException(ErrorCodes.OutputExists, $"'{finalZip}' already exists; choose another name or folder.");
        }

        var staging = Path.Combine(outputDir, $".sns-staging-{Guid.NewGuid():N}");
        var stagedModDir = Path.Combine(staging, modName);
        var movedModDir = false;
        try
        {
            Directory.CreateDirectory(stagedModDir);
            var written = new List<ExportedFile>(files.Count);
            foreach (var (relative, content) in files)
            {
                ct.ThrowIfCancellationRequested();
                var bytes = Utf8NoBom.GetBytes(content);
                var target = Path.Combine(stagedModDir, relative);
                EnsureInside(stagedModDir, target);
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                using (var stream = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    stream.Write(bytes);
                }

                written.Add(new ExportedFile
                {
                    Path = relative.Replace(Path.DirectorySeparatorChar, '/'),
                    Bytes = bytes.LongLength,
                    Sha256 = Convert.ToHexString(SHA256.HashData(bytes)),
                });
            }

            // Read back what reached the disk: the manifest promises these exact bytes.
            foreach (var file in written)
            {
                var onDisk = File.ReadAllBytes(Path.Combine(stagedModDir, file.Path.Replace('/', Path.DirectorySeparatorChar)));
                if (onDisk.LongLength != file.Bytes || !string.Equals(Convert.ToHexString(SHA256.HashData(onDisk)), file.Sha256, StringComparison.Ordinal))
                {
                    throw new WorkerException(ErrorCodes.ValidationFailed, $"Read-back of '{file.Path}' differs from what was written.");
                }
            }

            var ordered = written.OrderBy(f => f.Path, StringComparer.OrdinalIgnoreCase).ToArray();
            var verification = new List<string>
            {
                $"{ordered.Length} archivo(s) escritos en UTF-8 sin BOM y releídos: tamaño y SHA-256 idénticos a los registrados en el manifiesto.",
                "Carpeta de destino validada: fuera de Data del juego, de mods activos, de mods/overwrite/perfiles de MO2 (enlaces resueltos).",
                "Sin plugin (ESP/ESM/ESL): el paquete solo contiene archivos de texto generados por el estudio.",
            };
            var manifest = new
            {
                format = ManifestFormat,
                formatVersion = ManifestVersion,
                tool = new { name = "Skyrim NPC Studio", workerVersion = ModPackager.WorkerVersion, protocol = $"{Protocol.Major}.{Protocol.Minor}" },
                createdUtc = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture),
                modName,
                author = request.Author,
                description = request.Description,
                metadata = request.Metadata ?? new Dictionary<string, string>(),
                files = ordered.Select(f => new { path = f.Path, bytes = f.Bytes, sha256 = f.Sha256 }),
                readyForGame = (bool?)null,
                pendingVerification = new[] { "In-game verification has not been performed" },
            };
            File.WriteAllText(Path.Combine(stagedModDir, "manifest.json"), JsonSerializer.Serialize(manifest, ManifestJson), Utf8NoBom);
            File.WriteAllText(Path.Combine(stagedModDir, "meta.ini"), ModPackager.BuildMetaIni(request.Description, modName), Utf8NoBom);

            string? stagedZip = null;
            if (request.CreateZip)
            {
                stagedZip = Path.Combine(staging, modName + ".zip");
                ModPackager.CreateZip(stagedModDir, stagedZip);
                ModPackager.VerifyZip(stagedZip, stagedModDir);
                verification.Add($"ZIP releído: {ordered.Length + 1} entrada(s) con rutas relativas seguras y contenido idéntico al de la carpeta del mod (SHA-256).");
            }

            ct.ThrowIfCancellationRequested();

            // Commit: both renames stay on the output volume, so each one is atomic.
            Directory.Move(stagedModDir, finalModDir);
            movedModDir = true;
            if (stagedZip is not null)
            {
                File.Move(stagedZip, finalZip, overwrite: false);
            }

            Directory.Delete(staging, recursive: true);
            return new ExportBodyDistributionResponse
            {
                ModDirectory = finalModDir,
                ZipPath = stagedZip is null ? null : finalZip,
                ManifestPath = Path.Combine(finalModDir, "manifest.json"),
                Files = ordered,
                Verification = verification,
            };
        }
        catch
        {
            if (movedModDir && Directory.Exists(finalModDir))
            {
                Directory.Move(finalModDir, stagedModDir);
            }

            TryDelete(staging);
            throw;
        }
    }

    /// <summary>A single folder name Windows and MO2 accept as is.</summary>
    internal static void ValidateModName(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, "The mod name is empty.");
        }

        if (name.Length > MaxModNameLength)
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"The mod name is longer than {MaxModNameLength} characters.");
        }

        if (name.Contains('/') || name.Contains('\\') || name.Contains("..", StringComparison.Ordinal) || name.StartsWith('.'))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"'{name}' is not a valid mod name: no path separators, '..' or leading dot.");
        }

        if (!IsSafeSegment(name))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"'{name}' is not a valid folder name (reserved name, invalid character, or leading/trailing space or dot).");
        }
    }

    /// <summary>
    /// The request's files with OS separators, after refusing anything that could leave the mod folder or collide:
    /// rooted or drive paths, "..", empty segments, reserved names, the packager's own files and duplicates
    /// (case-insensitive, also a file that is another file's folder).
    /// </summary>
    internal static IReadOnlyList<(string Relative, string Content)> NormalizeFiles(IReadOnlyList<BodyPackageFile>? files)
    {
        if (files is null || files.Count == 0)
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, "The distribution has no files.");
        }

        var result = new List<(string, string)>(files.Count);
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var folders = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var file in files)
        {
            var raw = file?.RelativePath ?? "";
            var normalized = NormalizeRelativePath(raw);
            if (file!.Content is null)
            {
                throw new WorkerException(ErrorCodes.InvalidArgument, $"'{raw}' has no content.");
            }

            if (!paths.Add(normalized) || folders.Contains(normalized))
            {
                throw new WorkerException(ErrorCodes.InvalidArgument, $"'{raw}' is listed twice or collides with a folder of another file.");
            }

            var parts = normalized.Split(Path.DirectorySeparatorChar);
            for (var i = 1; i < parts.Length; i++)
            {
                var folder = string.Join(Path.DirectorySeparatorChar, parts[..i]);
                if (paths.Contains(folder))
                {
                    throw new WorkerException(ErrorCodes.InvalidArgument, $"'{raw}' needs a folder that is also listed as a file.");
                }

                folders.Add(folder);
            }

            result.Add((normalized, file.Content));
        }

        return result;
    }

    internal static string NormalizeRelativePath(string raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, "A file has an empty path.");
        }

        if (raw.Length > MaxRelativePathLength)
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"'{raw}' is longer than {MaxRelativePathLength} characters.");
        }

        if (raw.Contains(':') || raw.StartsWith('/') || raw.StartsWith('\\') || Path.IsPathRooted(raw))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"'{raw}' must be a relative path inside the mod folder.");
        }

        var segments = raw.Split('/', '\\');
        foreach (var segment in segments)
        {
            if (segment is "" or "." or "..")
            {
                throw new WorkerException(ErrorCodes.InvalidArgument, $"'{raw}' must be a relative path without empty, '.' or '..' segments.");
            }

            if (!IsSafeSegment(segment))
            {
                throw new WorkerException(ErrorCodes.InvalidArgument, $"'{raw}' contains '{segment}', which is not a valid file or folder name.");
            }
        }

        if (segments.Length == 1 && Reserved.Contains(segments[0], StringComparer.OrdinalIgnoreCase))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"'{raw}' is written by the packager itself and cannot be supplied.");
        }

        return string.Join(Path.DirectorySeparatorChar, segments);
    }

    private static readonly char[] InvalidNameChars =
        [.. Path.GetInvalidFileNameChars(), '<', '>', ':', '"', '/', '\\', '|', '?', '*'];

    private static bool IsSafeSegment(string segment)
    {
        if (segment.Length == 0 || segment.IndexOfAny(InvalidNameChars) >= 0 || segment.Any(char.IsControl))
        {
            return false;
        }

        // Windows drops trailing dots and spaces, and a leading space is a common source of lookalike folders.
        if (segment.EndsWith('.') || segment.EndsWith(' ') || segment.StartsWith(' '))
        {
            return false;
        }

        // "NUL", "nul.txt", "COM1.json": device names are reserved with any extension.
        var stem = segment.Split('.')[0].TrimEnd(' ');
        return !DeviceNames.Contains(stem);
    }

    private static void EnsureInside(string root, string target)
    {
        var full = Path.GetFullPath(target);
        var prefix = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) + Path.DirectorySeparatorChar;
        if (!full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new WorkerException(ErrorCodes.InvalidArgument, $"'{target}' would be written outside the mod folder.");
        }
    }

    private static void TryDelete(string directory)
    {
        try
        {
            if (Directory.Exists(directory))
            {
                Directory.Delete(directory, recursive: true);
            }
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }
}
