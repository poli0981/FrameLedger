using FluentAssertions;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>L3's contract through a scripted bridge: unavailable is not zero, a clear bit is null, two throws disable.</summary>
public sealed class NvapiTelemetrySourceTests
{
    private sealed class ManualTimeProvider : TimeProvider
    {
        public override DateTimeOffset GetUtcNow() => new(2026, 9, 10, 12, 0, 0, TimeSpan.Zero);
    }

    [Fact]
    public void AFullSampleMapsEveryFieldAndTheLayerIsNvapi()
    {
        using var bridge = new FakeNvapiBridge { Sample = FakeNvapiBridge.Full };
        using var source = new NvapiTelemetrySource(bridge, new ManualTimeProvider());

        source.TryRead(out GpuSample? s).Should().BeTrue();

        s!.Layer.Should().Be(TelemetryLayer.Nvapi);
        s.AdapterName.Should().Be("NVIDIA GeForce RTX 5080");
        s.TempCoreC.Should().Be(62);
        s.TempMemoryC.Should().BeNull("the bit was clear");
        s.LoadPct.Should().Be(57);
        s.VramAdapterMb.Should().BeNull("this build reads no memory info");
        s.CoreClockMhz.Should().Be(2610);
        s.MemClockMhz.Should().Be(15001);
        s.FanRpm.Should().Be(1200);
        s.ThrottleReasons.Should().Be(0x2u);
        s.PcieWidth.Should().Be(16);
        s.PcieGen.Should().BeNull();
        source.Capabilities.Should().Be(GpuCapabilities.TempCore | GpuCapabilities.Load | GpuCapabilities.CoreClock
            | GpuCapabilities.MemClock | GpuCapabilities.Fan | GpuCapabilities.ThrottleReasons | GpuCapabilities.Pcie);
        source.DriverVersion.Should().Be("616.64 (r616_00)");
        bridge.Inits.Should().Be(1);
    }

    [Theory]
    [InlineData(NativeNvapiBridge.Unavailable)]
    [InlineData(-6)]
    [InlineData(NativeNvapiBridge.NoGpu)]
    public void AnInitThatDoesNotAnswerZeroDisablesTheLayerWithoutAFault(int init)
    {
        using var bridge = new FakeNvapiBridge { InitResult = init, Sample = FakeNvapiBridge.Full };
        using var source = new NvapiTelemetrySource(bridge, new ManualTimeProvider());

        source.Start();

        source.IsDisabled.Should().BeTrue();
        source.Capabilities.Should().Be(GpuCapabilities.None, "unavailable is N/A, never zero");
        source.Faults.Should().Be(0, "a machine without the driver is a normal condition");
        source.LastFault.Should().NotBeNull();
        source.TryRead(out _).Should().BeFalse();
        bridge.Reads.Should().Be(0);
    }

    [Fact]
    public void ABridgeThatThrowsTwiceIsDisabledAndAReadTheDriverRefusesIsNot()
    {
        int calls = 0;
        using var bridge = new FakeNvapiBridge
        {
            Sample = () => ++calls is 2 or 4 ? throw new InvalidOperationException("driver went away") : FakeNvapiBridge.Full(),
        };
        using var source = new NvapiTelemetrySource(bridge, new ManualTimeProvider());

        source.TryRead(out _).Should().BeTrue();
        source.TryRead(out _).Should().BeFalse();
        source.Faults.Should().Be(1);
        source.IsDisabled.Should().BeFalse();
        source.TryRead(out _).Should().BeTrue();
        source.TryRead(out _).Should().BeFalse();
        source.IsDisabled.Should().BeTrue();
        source.Capabilities.Should().Be(GpuCapabilities.None);
        source.TryRead(out _).Should().BeFalse("never retried");
    }

    [Fact]
    public void DisposeShutsTheBridgeDownOnceAndOnlyIfItWasInitialised()
    {
        using var live = new FakeNvapiBridge { Sample = FakeNvapiBridge.Full };
        var source = new NvapiTelemetrySource(live, new ManualTimeProvider());
        source.Start();
        source.Dispose();
        source.Dispose();
        live.Shutdowns.Should().Be(1);
        live.Disposed.Should().BeTrue();

        using var dead = new FakeNvapiBridge { InitResult = -6 };
        var disabled = new NvapiTelemetrySource(dead, new ManualTimeProvider());
        disabled.Start();
        disabled.Dispose();
        dead.Shutdowns.Should().Be(0, "nothing was initialised");
        dead.Disposed.Should().BeTrue();
    }
}
