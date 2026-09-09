using FluentAssertions;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>
/// The real DXGI factory and the real PDH counter, on whatever this machine is. Asserts the
/// CONTRACT, never the hardware: CI's runner has WARP and nothing else, and a dev box may have
/// any vendor's.
/// </summary>
/// <remarks>
/// <c>Category=Integration</c> for the same reason <see cref="LhmRealHardwareTests"/> is:
/// it opens driver paths. The <c>18_GPU_VENDOR_APIS</c> §L1 matrix cells (adapter VRAM,
/// driver version) are filled from this case's output on the dev box, not asserted here.
/// </remarks>
[Trait("Category", "Integration")]
public sealed class BaselineRealHardwareTests
{
    [Fact]
    public void TheRealFactoryEitherListsAdaptersWithHonestIdentitiesOrDisablesAndNeverClaimsAFieldItDidNotRead()
    {
        using var memory = new PdhAdapterMemoryCounter();
        using var source = new BaselineTelemetrySource(new DxgiAdapters(), memory, TimeProvider.System);

        source.Start();

        if (source.IsDisabled)
        {
            source.Capabilities.Should().Be(GpuCapabilities.None);
            source.LastFault.Should().NotBeNull();
            return;
        }

        foreach (GpuAdapterIdentity adapter in source.Adapters)
        {
            adapter.Name.Should().NotBeNullOrWhiteSpace();
            adapter.Luid.Should().NotBe(0UL, "DXGI never hands out a zero LUID; 0 is the handshake's 'not yet known'");
            if (adapter.DriverVersion is not null)
            {
                adapter.DriverVersion.Split('.').Should().HaveCount(4, "the UMD version is four 16-bit fields");
            }
        }

        if (source.Selected is null)
        {
            // Only software adapters, or none: an answer, not a fault.
            source.Faults.Should().Be(0);
            source.TryRead(out _).Should().BeFalse();
            return;
        }

        source.Selected.IsSoftware.Should().BeFalse("a software adapter is never the default");
        source.SelectAdapter(source.Selected.Luid).Should().BeTrue("the LUID DXGI listed is the LUID that selects");

        if (source.TryRead(out GpuSample? sample))
        {
            sample.AdapterName.Should().Be(source.Selected.Name);
            (sample.PresentFields & ~source.Capabilities).Should().Be(GpuCapabilities.None);
            sample.Layer.Should().Be(TelemetryLayer.Baseline);
            if (sample.VramAdapterMb is { } used)
            {
                used.Should().BeGreaterThan(0, "a desktop with a compositor has dedicated memory in use");
                used.Should().BeLessThanOrEqualTo(source.Selected.DedicatedVideoMemoryMb + 1, "in use cannot exceed what the adapter has");
            }
        }
        else
        {
            source.Faults.Should().Be(1, "a read that failed was counted, not swallowed");
        }
    }
}
