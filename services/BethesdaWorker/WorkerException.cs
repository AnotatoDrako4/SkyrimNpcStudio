using SkyrimNpcStudio.Worker.Contracts;

namespace SkyrimNpcStudio.Worker;

/// <summary>An expected failure with a protocol error code (see <see cref="ErrorCodes"/>).</summary>
internal sealed class WorkerException(int code, string message, IReadOnlyList<Issue>? issues = null) : Exception(message)
{
    public int Code { get; } = code;

    public IReadOnlyList<Issue> Issues { get; } = issues ?? [];
}
