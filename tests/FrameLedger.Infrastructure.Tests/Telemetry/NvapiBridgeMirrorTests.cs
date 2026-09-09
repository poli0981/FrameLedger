using System.Runtime.InteropServices;
using FluentAssertions;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>
/// The managed mirrors of <c>FlNvSample</c> and <c>FlNvNgxState</c> against the DLL's own sizes, and the DLL's
/// build id against the guard's — the same mirror discipline <c>ShmLayoutMirrorTests</c> applies, one binary later.
/// Fails rather than skips when the bridge is not staged: a mirror test that quietly does nothing is a gate that cannot fail.
/// </summary>
public sealed class NvapiBridgeMirrorTests
{
    [Fact]
    public void TheMirrorsAreTheSizesTheBridgeAnswersAndTheAbiVersionIsTheOneWeWroteAgainst()
    {
        NativeNvapiBridge.IsPresent.Should().BeTrue("FrameLedger.NvapiBridge.dll must be staged beside the test binary (FrameLedger.NvapiBridge.targets)");

        NativeNvapiBridge.AbiVersion().Should().Be(1u);
        ((uint)Marshal.SizeOf<NvapiSample>()).Should().Be(NativeNvapiBridge.SampleSize());
        ((uint)Marshal.SizeOf<NvapiNgxWords>()).Should().Be(NativeNvapiBridge.NgxStateSize());
        NativeNvapiBridge.BuildId().Should().NotBeNullOrEmpty();
    }

    [Fact]
    public void TheRealBridgeEitherInitialisesOrSaysWhyAndNeverClaimsAFieldItDidNotRead()
    {
        using var bridge = new NativeNvapiBridge();
        using var source = new NvapiTelemetrySource(bridge, TimeProvider.System);

        source.Start();

        if (source.IsDisabled)
        {
            // A runner without an NVIDIA driver, or no GPU: the normal branch on CI.
            source.Faults.Should().Be(0);
            source.LastFault.Should().NotBeNull();
            return;
        }

        source.DriverVersion.Should().MatchRegex(@"^\d+\.\d{2} \(.+\)$");
        source.TryRead(out Application.Telemetry.GpuSample? s).Should().BeTrue();
        s!.AdapterName.Should().NotBeNullOrWhiteSpace();
        (s.PresentFields & ~source.Capabilities).Should().Be(Application.Telemetry.GpuCapabilities.None);
        if (s.LoadPct is { } load)
        {
            load.Should().BeInRange(0, 100);
        }
    }
}
