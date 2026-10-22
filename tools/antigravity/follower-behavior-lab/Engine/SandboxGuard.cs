using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace SkyrimNpcStudio.Antigravity.FollowerBehaviorLab.Engine;

public sealed class LabUsageException(string code, string message, int exitCode = 2) : Exception(message)
{
    public string Code { get; } = code;
    public int ExitCode { get; } = exitCode;
}

public static class SandboxGuard
{
    public const int MaxJsonBytes = 8 * 1024 * 1024; // 8 MB safe ceiling
    public const int MaxJsonDepth = 32;
    public const int MaxCollectionItems = 5000;

    public static readonly JsonSerializerOptions SafeJsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        MaxDepth = MaxJsonDepth,
        WriteIndented = true,
    };

    public static readonly JsonSerializerOptions CanonicalJsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false,
    };

    /// <summary>
    /// Ensures an output path resolves strictly inside the allowed sandbox roots
    /// (by default out/antigravity or tests/antigravity/follower-behavior-lab/fixtures).
    /// </summary>
    public static string ValidateOutputPath(string requestedOutputPath, string? customAllowedRoot = null)
    {
        if (string.IsNullOrWhiteSpace(requestedOutputPath))
        {
            throw new LabUsageException("InvalidOutputPath", "Output path cannot be empty.", 3);
        }

        string fullTarget;
        try
        {
            fullTarget = Path.GetFullPath(requestedOutputPath);
        }
        catch (Exception ex)
        {
            throw new LabUsageException("InvalidOutputPath", $"Malformed output path '{requestedOutputPath}': {ex.Message}", 3);
        }

        var repoRoot = FindRepoRoot();
        var allowedRoots = new List<string>();
        if (!string.IsNullOrWhiteSpace(customAllowedRoot))
        {
            allowedRoots.Add(EnsureTrailingSeparator(Path.GetFullPath(customAllowedRoot)));
        }
        if (repoRoot is not null)
        {
            allowedRoots.Add(EnsureTrailingSeparator(Path.Combine(repoRoot, "out", "antigravity")));
            allowedRoots.Add(EnsureTrailingSeparator(Path.Combine(repoRoot, "tests", "antigravity", "follower-behavior-lab", "fixtures")));
        }

        var comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        var isInside = allowedRoots.Any(root => fullTarget.StartsWith(root, comparison));
        if (!isInside)
        {
            throw new LabUsageException(
                "SandboxEscapeRejected",
                $"Refusing to write outside allowed Antigravity sandbox directories: '{fullTarget}'.",
                3);
        }

        var parentDir = Path.GetDirectoryName(fullTarget);
        if (!string.IsNullOrEmpty(parentDir))
        {
            Directory.CreateDirectory(parentDir);
        }

        return fullTarget;
    }

    public static T ReadSafeJsonFile<T>(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            throw new LabUsageException("InputNotFound", $"Input file not found: '{path}'.", 2);
        }

        var info = new FileInfo(path);
        if (info.Length <= 0)
        {
            throw new LabUsageException("MalformedJson", $"Input file is empty: '{path}'.", 2);
        }
        if (info.Length > MaxJsonBytes)
        {
            throw new LabUsageException("PayloadTooLarge", $"Input file '{path}' ({info.Length} bytes) exceeds safe limit of {MaxJsonBytes} bytes.", 2);
        }

        string raw;
        try
        {
            raw = File.ReadAllText(path, Encoding.UTF8);
        }
        catch (Exception ex)
        {
            throw new LabUsageException("InputReadError", $"Cannot read '{path}': {ex.Message}", 2);
        }

        return ParseSafeJson<T>(raw, path);
    }

    public static T ParseSafeJson<T>(string rawJson, string sourceLabel = "<memory>")
    {
        if (string.IsNullOrWhiteSpace(rawJson))
        {
            throw new LabUsageException("MalformedJson", $"JSON input from '{sourceLabel}' is empty.", 2);
        }

        if (Encoding.UTF8.GetByteCount(rawJson) > MaxJsonBytes)
        {
            throw new LabUsageException("PayloadTooLarge", $"JSON input from '{sourceLabel}' exceeds {MaxJsonBytes} bytes.", 2);
        }

        try
        {
            using var doc = JsonDocument.Parse(rawJson, new JsonDocumentOptions
            {
                MaxDepth = MaxJsonDepth,
                AllowTrailingCommas = false,
                CommentHandling = JsonCommentHandling.Disallow,
            });

            int nodeCount = 0;
            ValidateElementBounds(doc.RootElement, 0, ref nodeCount);

            var result = JsonSerializer.Deserialize<T>(rawJson, SafeJsonOptions);
            if (result is null)
            {
                throw new LabUsageException("MalformedJson", $"JSON deserialized to null from '{sourceLabel}'.", 2);
            }
            return result;
        }
        catch (LabUsageException)
        {
            throw;
        }
        catch (JsonException jex)
        {
            throw new LabUsageException("MalformedJson", $"Invalid or excessively nested JSON in '{sourceLabel}': {jex.Message}", 2);
        }
    }

    private static void ValidateElementBounds(JsonElement element, int depth, ref int nodeCount)
    {
        if (depth > MaxJsonDepth)
        {
            throw new LabUsageException("JsonDepthExceeded", $"JSON nesting depth exceeds maximum allowed ({MaxJsonDepth}).", 2);
        }

        nodeCount++;
        if (nodeCount > MaxCollectionItems * 10)
        {
            throw new LabUsageException("PayloadTooLarge", $"JSON node count ({nodeCount}) exceeds safe processing threshold.", 2);
        }

        if (element.ValueKind == JsonValueKind.Array)
        {
            int len = element.GetArrayLength();
            if (len > MaxCollectionItems)
            {
                throw new LabUsageException("CollectionTooLarge", $"JSON array length ({len}) exceeds safe limit ({MaxCollectionItems}).", 2);
            }
            foreach (var item in element.EnumerateArray())
            {
                ValidateElementBounds(item, depth + 1, ref nodeCount);
            }
        }
        else if (element.ValueKind == JsonValueKind.Object)
        {
            foreach (var prop in element.EnumerateObject())
            {
                ValidateElementBounds(prop.Value, depth + 1, ref nodeCount);
            }
        }
    }

    public static string ComputeSha256Hex(string text)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        var hash = SHA256.HashData(bytes);
        return Convert.ToHexStringLower(hash);
    }

    public static string ComputeFileSha256Hex(string filePath)
    {
        using var stream = File.Open(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        var hash = SHA256.HashData(stream);
        return Convert.ToHexStringLower(hash);
    }

    public static string ComputeObjectCanonicalSha256<T>(T value)
    {
        var json = JsonSerializer.Serialize(value, CanonicalJsonOptions);
        return ComputeSha256Hex(json);
    }

    public static string? FindRepoRoot(string? startDir = null)
    {
        var dir = new DirectoryInfo(startDir ?? AppContext.BaseDirectory);
        while (dir is not null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "SkyrimNpcStudio.slnx")) ||
                File.Exists(Path.Combine(dir.FullName, "Directory.Packages.props")))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }

        var cwd = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (cwd is not null)
        {
            if (File.Exists(Path.Combine(cwd.FullName, "SkyrimNpcStudio.slnx")) ||
                File.Exists(Path.Combine(cwd.FullName, "Directory.Packages.props")))
            {
                return cwd.FullName;
            }
            cwd = cwd.Parent;
        }

        return null;
    }

    private static string EnsureTrailingSeparator(string path) =>
        path.EndsWith(Path.DirectorySeparatorChar) ? path : path + Path.DirectorySeparatorChar;
}
