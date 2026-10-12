namespace SkyrimNpcStudio.Worker.Contracts;

public sealed record HandshakeRequest
{
    public required string ClientName { get; init; }
    public required string ClientVersion { get; init; }
    public required int ProtocolMajor { get; init; }
    public int ProtocolMinor { get; init; }
}

public sealed record HandshakeResponse
{
    public required string WorkerVersion { get; init; }
    public required int ProtocolMajor { get; init; }
    public required int ProtocolMinor { get; init; }
    public required string MutagenVersion { get; init; }
    public required int ProcessId { get; init; }
    public required IReadOnlyList<string> Capabilities { get; init; }
}

public sealed record PingResponse
{
    public required long UnixTimeMs { get; init; }
}

/// <summary>A problem found while processing data. Severity is "error", "warning" or "info".</summary>
public sealed record Issue
{
    public required string Severity { get; init; }
    public required string Code { get; init; }
    public required string Message { get; init; }

    /// <summary>What the issue is about: a plugin file name, a FormKey, an asset path…</summary>
    public string? Subject { get; init; }

    public static Issue Error(string code, string message, string? subject = null) =>
        new() { Severity = "error", Code = code, Message = message, Subject = subject };

    public static Issue Warning(string code, string message, string? subject = null) =>
        new() { Severity = "warning", Code = code, Message = message, Subject = subject };

    public static Issue Info(string code, string message, string? subject = null) =>
        new() { Severity = "info", Code = code, Message = message, Subject = subject };
}
