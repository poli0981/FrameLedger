using FluentAssertions;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>
/// L1's contract on a machine with no GPU: which adapter it picks, what a sample carries, and
/// the same fault policy L2 is held to (<c>18_GPU_VENDOR_APIS</c> §Runtime policy).
/// </summary>
public sealed class BaselineTelemetrySourceTests
{
    private const ulong _mib = 1024 * 1024;

    private static readonly DateTimeOffset _t0 = new(2026, 9, 9, 12, 0, 0, TimeSpan.Zero);

    private sealed class ManualTimeProvider : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = _t0;

        public override DateTimeOffset GetUtcNow() => Now;
    }

    private static BaselineTelemetrySource Source(IDxgiAdapters dxgi, IAdapterMemoryCounter memory) =>
        new(dxgi, memory, new ManualTimeProvider());

    [Fact]
    public void TheFirstHardwareAdapterIsSelectedAndASoftwareOneIsListedButNeverDefault()
    {
        using var memory = FakeMemoryCounter.Constant(0);
        using BaselineTelemetrySource source = Source(new FakeDxgi(
            FakeDxgi.Identity("Microsoft Basic Render Driver", 0x11, software: true),
            FakeDxgi.Identity("NVIDIA GeForce RTX 5080", 0x2A)), memory);

        source.Start();

        source.Adapters.Select(a => a.Name).Should().Equal("Microsoft Basic Render Driver", "NVIDIA GeForce RTX 5080");
        source.Selected!.Name.Should().Be("NVIDIA GeForce RTX 5080");
        memory.Opened.Should().Equal([0x2AUL], "the counter is bound to the selected adapter's LUID, never a wildcard");
        source.Layer.Should().Be(TelemetryLayer.Baseline);
    }

    [Fact]
    public void ASampleCarriesTheAdapterNameAndItsDedicatedMemoryInUseAndNothingElse()
    {
        using var memory = FakeMemoryCounter.Constant(3_000 * _mib);
        using BaselineTelemetrySource source = Source(new FakeDxgi(FakeDxgi.Identity("RTX", 0x2A)), memory);

        source.TryRead(out GpuSample? sample).Should().BeTrue("TryRead starts the source if nobody did");

        sample!.AdapterName.Should().Be("RTX");
        sample.VramAdapterMb.Should().Be(3_000);
        sample.PresentFields.Should().Be(GpuCapabilities.VramAdapter, "L1 carries one live field; utilisation is L2's (§M10)");
        sample.Layer.Should().Be(TelemetryLayer.Baseline);
        sample.TakenAt.Should().Be(_t0);
        source.Capabilities.Should().Be(GpuCapabilities.VramAdapter);
        memory.Reads.Should().Be(1, "one collect per read, on the caller's thread");
    }

    [Fact]
    public void AMachineWhoseCounterSetLacksTheAdapterStillGetsTheNameAndClaimsNoField()
    {
        using var memory = FakeMemoryCounter.Absent();
        using BaselineTelemetrySource source = Source(new FakeDxgi(FakeDxgi.Identity("RTX", 0x2A)), memory);

        source.TryRead(out GpuSample? sample).Should().BeTrue();

        sample!.AdapterName.Should().Be("RTX");
        sample.VramAdapterMb.Should().BeNull("absent is N/A, never zero");
        sample.PresentFields.Should().Be(GpuCapabilities.None);
        source.Capabilities.Should().Be(GpuCapabilities.None);
        source.Faults.Should().Be(0, "a counter that does not exist here is an answer, not a fault");
    }

    [Fact]
    public void TheHandshakeLuidSwitchesTheAdapterAndAnUnknownOrZeroLuidChangesNothing()
    {
        using var memory = new FakeMemoryCounter(luid => () => luid * _mib);
        using BaselineTelemetrySource source = Source(new FakeDxgi(FakeDxgi.Identity("first", 0x10), FakeDxgi.Identity("second", 0x20)), memory);

        source.SelectAdapter(0x20).Should().BeTrue();
        source.TryRead(out GpuSample? sample).Should().BeTrue();
        sample!.AdapterName.Should().Be("second");
        sample.VramAdapterMb.Should().Be(0x20);

        source.SelectAdapter(0xDEAD).Should().BeFalse("DXGI never listed it");
        source.SelectAdapter(0).Should().BeFalse("0 is the handshake's 'not yet known'");
        source.SelectAdapter(0x20).Should().BeTrue();
        source.Selected!.Name.Should().Be("second", "a refused selection leaves the previous one standing");
        memory.Opened.Should().Equal([0x10UL, 0x20UL], "rebound once per change, not per call");
    }

    [Fact]
    public void OneThrowIsAFaultAndTheSecondDisablesTheLayerForTheSession()
    {
        int calls = 0;
        using var memory = new FakeMemoryCounter(_ => () =>
            ++calls == 2 || calls == 4 ? throw new InvalidOperationException("PDH_INVALID_DATA") : 100 * _mib);
        using BaselineTelemetrySource source = Source(new FakeDxgi(FakeDxgi.Identity("flaky", 0x1)), memory);

        source.TryRead(out _).Should().BeTrue();
        source.TryRead(out _).Should().BeFalse("the throw is this tick's answer");
        source.Faults.Should().Be(1);
        source.IsDisabled.Should().BeFalse("one fault is tolerated");
        source.LastFault.Should().Contain("PDH_INVALID_DATA");
        source.TryRead(out _).Should().BeTrue("still standing");

        source.TryRead(out _).Should().BeFalse();
        source.Faults.Should().Be(BaselineTelemetrySource.MaxFaults);
        source.IsDisabled.Should().BeTrue();
        source.Capabilities.Should().Be(GpuCapabilities.None, "a disabled layer claims nothing");
        source.TryRead(out _).Should().BeFalse("never retried");
        memory.Reads.Should().Be(4, "the fifth read did not reach the counter");
    }

    [Fact]
    public void AnEnumerationThatThrowsDisablesAtStart()
    {
        using var memory = FakeMemoryCounter.Constant(0);
        using BaselineTelemetrySource source = Source(new FakeDxgi(() => throw new InvalidOperationException("no factory")), memory);

        source.Start();

        source.IsDisabled.Should().BeTrue();
        source.Faults.Should().Be(1, "counted where it happened; the disable is immediate because there is nothing to poll");
        source.LastFault.Should().Contain("no factory");
        source.TryRead(out _).Should().BeFalse();
        source.Adapters.Should().BeEmpty();
    }

    [Fact]
    public void NoAdapterAtAllIsAnAnswerNotAFault()
    {
        var dxgi = new FakeDxgi();
        using var memory = FakeMemoryCounter.Constant(0);
        using BaselineTelemetrySource source = Source(dxgi, memory);

        source.TryRead(out _).Should().BeFalse();
        source.TryRead(out _).Should().BeFalse();

        source.IsDisabled.Should().BeFalse();
        source.Faults.Should().Be(0);
        source.Selected.Should().BeNull();
        dxgi.Enumerations.Should().Be(1, "Start is idempotent");
        memory.Opened.Should().BeEmpty("nothing to bind to");
    }

    [Fact]
    public void DisposeClosesTheCounter()
    {
        using var memory = FakeMemoryCounter.Constant(0);
        BaselineTelemetrySource source = Source(new FakeDxgi(FakeDxgi.Identity("a", 0x1)), memory);
        source.Start();

        source.Dispose();
        source.Dispose();

        memory.Disposed.Should().BeTrue();
        source.Selected.Should().BeNull();
    }
}
