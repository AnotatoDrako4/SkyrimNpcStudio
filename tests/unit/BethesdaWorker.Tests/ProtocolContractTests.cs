using System.Reflection;
using System.Text.RegularExpressions;
using SkyrimNpcStudio.Worker.Contracts;
using SkyrimNpcStudio.Worker.Rpc;
using StreamJsonRpc;
using Xunit;

namespace SkyrimNpcStudio.Worker.Tests;

/// <summary>
/// AG05: the wire contract between the studio (C++) and the worker (.NET) did not move with AG04. Protocol 1.6, the five behavior endpoints
/// advertised in the handshake, declared as JSON-RPC methods on the target and spelled the same in engines/ipc/protocol.hpp.
/// </summary>
public sealed class ProtocolContractTests
{
    private static readonly string[] BehaviorMethods =
    [
        Methods.CreationAnalyzeVoice, Methods.CreationGetBehaviorCatalog, Methods.CreationApplyArchetype,
        Methods.CreationUndoArchetype, Methods.CreationValidateBehavior,
    ];

    private static string ProtocolHeader()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
        {
            var candidate = Path.Combine(dir.FullName, "engines", "ipc", "include", "sns", "ipc", "protocol.hpp");
            if (File.Exists(candidate)) return File.ReadAllText(candidate);
        }
        throw new FileNotFoundException("engines/ipc/include/sns/ipc/protocol.hpp not found above the test binaries");
    }

    [Fact]
    public void TheWorkerSpeaksProtocol1_6_AndAdvertisesTheFiveBehaviorEndpoints()
    {
        var rpc = new WorkerRpcTarget(new WorkerState(), () => { }, (_, _) => { });
        var hello = rpc.Handshake(new HandshakeRequest { ClientName = "contract-test", ClientVersion = "0", ProtocolMajor = 1, ProtocolMinor = 6 });
        Assert.Equal(1, hello.ProtocolMajor);
        Assert.Equal(6, hello.ProtocolMinor);
        foreach (var method in BehaviorMethods) Assert.Contains(method, hello.Capabilities);
    }

    [Fact]
    public void AClientOfAnotherMajorVersionIsRefusedWithAProtocolMismatch()
    {
        var rpc = new WorkerRpcTarget(new WorkerState(), () => { }, (_, _) => { });
        var ex = Assert.Throws<LocalRpcException>(() => rpc.Handshake(new HandshakeRequest { ClientName = "old", ClientVersion = "0", ProtocolMajor = 2 }));
        Assert.Equal(ErrorCodes.ProtocolMismatch, ex.ErrorCode);
    }

    [Fact]
    public void TheFiveEndpointsAreJsonRpcMethodsOfTheTarget()
    {
        var declared = typeof(WorkerRpcTarget).GetMethods(BindingFlags.Public | BindingFlags.Instance)
            .Select(m => m.GetCustomAttribute<JsonRpcMethodAttribute>()?.Name).Where(n => n is not null).ToHashSet();
        foreach (var method in BehaviorMethods) Assert.Contains(method, declared);
    }

    [Fact]
    public void TheCppHeaderAgreesWithTheWorker_VersionAndMethodNames()
    {
        var header = ProtocolHeader();
        Assert.Equal(Protocol.Major, int.Parse(Regex.Match(header, @"kMajor\s*=\s*(\d+)").Groups[1].Value, System.Globalization.CultureInfo.InvariantCulture));
        Assert.Equal(Protocol.Minor, int.Parse(Regex.Match(header, @"kMinor\s*=\s*(\d+)").Groups[1].Value, System.Globalization.CultureInfo.InvariantCulture));
        foreach (var method in BehaviorMethods) Assert.Contains("\"" + method + "\"", header, StringComparison.Ordinal);
    }
}
