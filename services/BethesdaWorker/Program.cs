using System.Diagnostics;
using System.Text.RegularExpressions;
using SkyrimNpcStudio.Worker.Rpc;

namespace SkyrimNpcStudio.Worker;

internal static partial class Program
{
    private const int ExitUsage = 2;
    private const int ExitFailure = 3;

    [GeneratedRegex("^[A-Za-z0-9._-]{1,200}$")]
    private static partial Regex PipeNamePattern();

    public static async Task<int> Main(string[] args)
    {
        string? pipe = null;
        int? parentPid = null;
        for (var i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--pipe" when i + 1 < args.Length:
                    pipe = args[++i];
                    break;
                case "--parent-pid" when i + 1 < args.Length && int.TryParse(args[i + 1], out var pid):
                    parentPid = pid;
                    i++;
                    break;
                case "--version":
                    Console.WriteLine(PatchBuilder.ModPackager.WorkerVersion);
                    return 0;
                default:
                    return Usage($"unknown argument '{args[i]}'");
            }
        }

        if (pipe is null || !PipeNamePattern().IsMatch(pipe))
        {
            return Usage("--pipe <name> is required ([A-Za-z0-9._-], max 200 chars)");
        }

        using var cts = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            cts.Cancel();
        };

        if (parentPid is { } ppid)
        {
            WatchParent(ppid, cts);
        }

        try
        {
            return await RpcHost.RunAsync(pipe, cts.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            return 0;
        }
        catch (IOException ex)
        {
            Log.Error($"pipe failure: {ex.Message}");
            return ExitFailure;
        }
    }

    /// <summary>Exits when the studio dies without saying goodbye, so no orphan worker keeps files open.</summary>
    private static void WatchParent(int pid, CancellationTokenSource cts)
    {
        Process parent;
        try
        {
            parent = Process.GetProcessById(pid);
        }
        catch (ArgumentException)
        {
            Log.Error($"parent process {pid} is not running");
            cts.Cancel();
            return;
        }

        parent.EnableRaisingEvents = true;
        parent.Exited += (_, _) =>
        {
            Log.Warn($"parent process {pid} exited; stopping");
            cts.Cancel();
        };
    }

    private static int Usage(string error)
    {
        Console.Error.WriteLine($"error: {error}");
        Console.Error.WriteLine("usage: SkyrimNpcStudio.BethesdaWorker --pipe <name> [--parent-pid <pid>]");
        return ExitUsage;
    }
}
