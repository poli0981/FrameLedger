using FluentAssertions;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>
/// <c>18_GPU_VENDOR_APIS</c> §Abstraction: <i>"merges them with a fixed precedence per field
/// (L3 &gt; L2 &gt; L1) and records which layer supplied each value"</i>, and the descriptor
/// <c>sessions.telemetry_source</c> stores.
/// </summary>
public sealed class CompositeTelemetrySourceTests
{
    private static readonly DateTimeOffset _t0 = new(2026, 9, 9, 12, 0, 0, TimeSpan.Zero);

    [Fact]
    public void AHigherLayerWinsAFieldAndALowerOneFillsWhatItLeftNull()
    {
        using var l1 = new FakeLayer(TelemetryLayer.Baseline);
        using var l2 = new FakeLayer(TelemetryLayer.Lhm);
        using var l3 = new FakeLayer(TelemetryLayer.Nvapi);
        l1.Publish(l1.Sample(_t0, vram: 3_000, adapter: "dxgi name"));
        l2.Publish(l2.Sample(_t0.AddMilliseconds(-300), load: 55, vram: 2_990, temp: 61, power: 210, adapter: "lhm name"));
        l3.Publish(l3.Sample(_t0.AddMilliseconds(-100), load: 57, temp: 62));
        using var composite = new CompositeTelemetrySource([l1, l3, l2]);

        composite.TryRead(out GpuSample? merged).Should().BeTrue();

        merged!.LoadPct.Should().Be(57, "L3 over L2");
        merged.TempCoreC.Should().Be(62);
        merged.PowerW.Should().Be(210, "L3 had none; L2 fills");
        merged.VramAdapterMb.Should().Be(2_990, "L2 over L1");
        merged.AdapterName.Should().Be("lhm name", "identity follows the same precedence");
        merged.Layer.Should().Be(TelemetryLayer.Nvapi, "the highest layer that contributed");
        merged.TakenAt.Should().Be(_t0.AddMilliseconds(-100));

        composite.LayerOf(GpuCapabilities.Load).Should().Be(TelemetryLayer.Nvapi);
        composite.LayerOf(GpuCapabilities.Power).Should().Be(TelemetryLayer.Lhm);
        composite.LayerOf(GpuCapabilities.VramAdapter).Should().Be(TelemetryLayer.Lhm);
        composite.LayerOf(GpuCapabilities.Fan).Should().Be(TelemetryLayer.None, "nobody supplied it");
        composite.Capabilities.Should().Be(GpuCapabilities.Load | GpuCapabilities.VramAdapter | GpuCapabilities.TempCore | GpuCapabilities.Power);
    }

    [Fact]
    public void TheDescriptorListsStandingLayersLowestFirstWhateverOrderTheyWereGivenIn()
    {
        using var l1 = new FakeLayer(TelemetryLayer.Baseline);
        using var l2 = new FakeLayer(TelemetryLayer.Lhm);
        using var composite = new CompositeTelemetrySource([l2, l1]);

        composite.Descriptor.Should().Be("l1+lhm");
        composite.Layer.Should().Be(TelemetryLayer.None, "the composite is not a layer; the descriptor is its identity");

        l2.IsDisabled = true;
        composite.Descriptor.Should().Be("l1", "a layer disabled mid-session drops out");
        composite.IsDisabled.Should().BeFalse();

        l1.IsDisabled = true;
        composite.Descriptor.Should().BeEmpty();
        composite.IsDisabled.Should().BeTrue();
        composite.TryRead(out _).Should().BeFalse();
    }

    [Fact]
    public void ALayerWhoseTryReadThrowsIsExcludedOnTheSecondThrowAndTheOthersAreUnaffected()
    {
        using var l1 = new FakeLayer(TelemetryLayer.Baseline).Publish(null);
        l1.Publish(l1.Sample(_t0, vram: 100));
        using var l2 = new FakeLayer(TelemetryLayer.Lhm).Script(() => throw new InvalidOperationException("driver said no"));
        using var composite = new CompositeTelemetrySource([l1, l2]);

        composite.TryRead(out GpuSample? first).Should().BeTrue();
        first!.VramAdapterMb.Should().Be(100, "L1 still answers while L2 throws");
        composite.Descriptor.Should().Be("l1+lhm", "one throw is tolerated");
        composite.LastFaultOf(TelemetryLayer.Lhm).Should().Contain("driver said no");

        composite.TryRead(out _).Should().BeTrue();
        composite.Descriptor.Should().Be("l1", "the second throw excludes the layer for the session");
        l2.Reads.Should().Be(CompositeTelemetrySource.MaxFaults);

        composite.TryRead(out _).Should().BeTrue();
        l2.Reads.Should().Be(CompositeTelemetrySource.MaxFaults, "never retried");
        composite.LastFaultOf(TelemetryLayer.Nvapi).Should().BeNull("no such layer here");
    }

    [Fact]
    public void ADisabledLayersCapabilitiesAreNotTheCompositesAndItsSampleIsNotRead()
    {
        using var l1 = new FakeLayer(TelemetryLayer.Baseline);
        l1.Publish(l1.Sample(_t0, vram: 100));
        using var l2 = new FakeLayer(TelemetryLayer.Lhm);
        l2.Publish(l2.Sample(_t0, load: 50));
        l2.IsDisabled = true;
        l2.Capabilities = GpuCapabilities.None;
        using var composite = new CompositeTelemetrySource([l1, l2]);

        composite.TryRead(out GpuSample? merged).Should().BeTrue();

        merged!.LoadPct.Should().BeNull();
        l2.Reads.Should().Be(0);
        composite.Capabilities.Should().Be(GpuCapabilities.VramAdapter);
    }

    [Fact]
    public void TwoSourcesClaimingOneLayerOrNoLayerAreRefused()
    {
        using var a = new FakeLayer(TelemetryLayer.Lhm);
        using var b = new FakeLayer(TelemetryLayer.Lhm);
        using var unnamed = new FakeLayer(TelemetryLayer.None);
        Action twice = () => new CompositeTelemetrySource([a, b]).Dispose();
        Action none = () => new CompositeTelemetrySource([unnamed]).Dispose();

        twice.Should().Throw<ArgumentException>();
        none.Should().Throw<ArgumentException>();
    }

    [Fact]
    public void LayerOfTakesExactlyOneBit()
    {
        using var l1 = new FakeLayer(TelemetryLayer.Baseline);
        using var composite = new CompositeTelemetrySource([l1]);

        Action two = () => composite.LayerOf(GpuCapabilities.Load | GpuCapabilities.Fan);
        Action zero = () => composite.LayerOf(GpuCapabilities.None);

        two.Should().Throw<ArgumentOutOfRangeException>();
        zero.Should().Throw<ArgumentOutOfRangeException>();
    }

    [Fact]
    public void DisposingTheCompositeDisposesItsLayers()
    {
        using var l1 = new FakeLayer(TelemetryLayer.Baseline);
        using var l2 = new FakeLayer(TelemetryLayer.Lhm);
        var composite = new CompositeTelemetrySource([l1, l2]);

        composite.Dispose();
        composite.Dispose();

        l1.Disposed.Should().BeTrue();
        l2.Disposed.Should().BeTrue();
    }

    [Fact]
    public void TheNamesAreTheOnesTheSchemaExampleUses()
    {
        TelemetryLayerNames.Describe([TelemetryLayer.Nvapi, TelemetryLayer.Baseline, TelemetryLayer.Lhm, TelemetryLayer.Lhm])
            .Should().Be("l1+lhm+nvapi");
        Action none = () => TelemetryLayerNames.Of(TelemetryLayer.None);
        none.Should().Throw<ArgumentOutOfRangeException>();
    }
}
