using System.IO.Pipes;
using System.Text.Json;
using System.Text.Json.Serialization;
using StreamJsonRpc;

namespace SkyrimNpcStudio.Worker.Rpc;

/// <summary>Serves exactly one client over a named pipe until it disconnects or shutdown is requested.</summary>
internal static class RpcHost
{
    public static void ConfigureSerializer(JsonSerializerOptions options)
    {
        options.PropertyNamingPolicy = JsonNamingPolicy.CamelCase;
        options.DictionaryKeyPolicy = null;
        options.DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull;
        options.PropertyNameCaseInsensitive = false;
        options.Converters.Add(new JsonStringEnumConverter(JsonNamingPolicy.CamelCase));
    }

    /// <summary>Wires a JSON-RPC endpoint over any duplex stream (named pipe in production, in-memory in tests).</summary>
    public static JsonRpc Attach(Stream stream, WorkerState state, Action requestShutdown)
    {
        var formatter = new SystemTextJsonFormatter();
        ConfigureSerializer(formatter.JsonSerializerOptions);
        var handler = new HeaderDelimitedMessageHandler(stream, stream, formatter);
        var rpc = new JsonRpc(handler);
        var target = new WorkerRpcTarget(state, requestShutdown, (method, payload) =>
        {
            // Progress is best effort: a slow or gone client must not fail the operation.
            _ = rpc.NotifyWithParameterObjectAsync(method, payload);
        });
        rpc.AddLocalRpcTarget(target, new JsonRpcTargetOptions { UseSingleObjectParameterDeserialization = true });
        rpc.StartListening();
        return rpc;
    }

    public static async Task<int> RunAsync(string pipeName, CancellationToken ct)
    {
        await using var pipe = new NamedPipeServerStream(
            pipeName, PipeDirection.InOut, maxNumberOfServerInstances: 1, PipeTransmissionMode.Byte,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        Log.Info($"listening on \\\\.\\pipe\\{pipeName}");
        await pipe.WaitForConnectionAsync(ct).ConfigureAwait(false);
        Log.Info("client connected");

        using var state = new WorkerState();
        using var shutdown = CancellationTokenSource.CreateLinkedTokenSource(ct);
        using var rpc = Attach(pipe, state, () => shutdown.CancelAfter(TimeSpan.FromMilliseconds(200)));
        try
        {
            await rpc.Completion.WaitAsync(shutdown.Token).ConfigureAwait(false);
            Log.Info("client disconnected");
        }
        catch (OperationCanceledException)
        {
            Log.Info("shutting down");
        }

        return 0;
    }
}
