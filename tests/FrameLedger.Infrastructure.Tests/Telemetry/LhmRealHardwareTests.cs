using FluentAssertions;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>
/// The real library, on whatever this machine is. Asserts the CONTRACT, never the
/// hardware: CI has no GPU and a dev box may have any vendor's.
/// </summary>
/// <remarks>
/// <c>Category=Integration</c> because it opens vendor driver paths; <c>build.ps1
/// -SkipIntegration</c> excludes it on CI. The §M5 measurement itself is
/// <c>FrameLedger.CaptureHost probe-lhm</c> and its result is recorded in
/// <c>spike-notes</c> §10 — this case only proves the port behaves under the real library.
/// </remarks>
[Trait("Category", "Integration")]
public sealed class LhmRealHardwareTests
{
    [Fact]
    public void TheRealLibraryEitherSamplesOrDisablesAndNeverClaimsAFieldItDidNotRead()
    {
        using var source = new LhmTelemetrySource(new LhmComputerAdapter(enableCpuAndMemory: false),
            new LhmTelemetryOptions(), TimeProvider.System);

        source.Start();
        SpinWait.SpinUntil(() => source.IsDisabled || source.TryRead(out _), TimeSpan.FromSeconds(8));

        if (source.IsDisabled)
        {
            source.Capabilities.Should().Be(GpuCapabilities.None);
            source.LastFault.Should().NotBeNull();
            return;
        }

        if (source.TryRead(out GpuSample? sample))
        {
            // A capability bit is a claim that the field has carried a value; the sample in
            // hand may legitimately have fewer (a null tick), never more.
            (sample.PresentFields & ~source.Capabilities).Should().Be(GpuCapabilities.None);
            sample.Layer.Should().Be(TelemetryLayer.Lhm);
        }
        else
        {
            // No GPU node LHM understands. An answer, not a fault.
            source.Faults.Should().Be(0);
            source.Capabilities.Should().Be(GpuCapabilities.None);
        }
    }
}
