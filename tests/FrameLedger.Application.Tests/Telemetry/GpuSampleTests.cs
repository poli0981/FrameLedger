using FluentAssertions;
using FrameLedger.Application.Telemetry;

namespace FrameLedger.Application.Tests.Telemetry;

/// <summary>
/// <see cref="GpuSample.PresentFields"/> is the only input a source's <c>Capabilities</c>
/// may be built from, so it has to be exactly the non-null set.
/// </summary>
public sealed class GpuSampleTests
{
    private static GpuSample Empty() => new() { TakenAt = DateTimeOffset.UnixEpoch, Layer = TelemetryLayer.Lhm };

    [Fact]
    public void AnEmptySampleHasNoFields()
    {
        Empty().PresentFields.Should().Be(GpuCapabilities.None);
    }

    [Fact]
    public void EveryFieldHasExactlyOneBitAndAZeroIsAValue()
    {
        // A ZERO IS A VALUE. 03_METRICS §Sensor aggregates: "fields with no data are N/A,
        // never 0" — and the converse holds too: a fan at 0 rpm or a load of 0 % is a
        // measurement, and the bit must be set for it.
        (Empty() with { TempCoreC = 0 }).PresentFields.Should().Be(GpuCapabilities.TempCore);
        (Empty() with { TempHotspotC = 0 }).PresentFields.Should().Be(GpuCapabilities.TempHotspot);
        (Empty() with { TempMemoryC = 0 }).PresentFields.Should().Be(GpuCapabilities.TempMemory);
        (Empty() with { LoadPct = 0 }).PresentFields.Should().Be(GpuCapabilities.Load);
        (Empty() with { VramAdapterMb = 0 }).PresentFields.Should().Be(GpuCapabilities.VramAdapter);
        (Empty() with { CoreClockMhz = 0 }).PresentFields.Should().Be(GpuCapabilities.CoreClock);
        (Empty() with { MemClockMhz = 0 }).PresentFields.Should().Be(GpuCapabilities.MemClock);
        (Empty() with { PowerW = 0 }).PresentFields.Should().Be(GpuCapabilities.Power);
        (Empty() with { FanRpm = 0 }).PresentFields.Should().Be(GpuCapabilities.Fan);
        (Empty() with { ThrottleReasons = 0 }).PresentFields.Should().Be(GpuCapabilities.ThrottleReasons);
        (Empty() with { PcieGen = 4 }).PresentFields.Should().Be(GpuCapabilities.Pcie);
        (Empty() with { PcieWidth = 16 }).PresentFields.Should().Be(GpuCapabilities.Pcie);
    }

    [Fact]
    public void TheFieldsCompose()
    {
        GpuSample s = Empty() with { TempCoreC = 61, LoadPct = 97.5, PowerW = 312.4 };

        s.PresentFields.Should().Be(GpuCapabilities.TempCore | GpuCapabilities.Load | GpuCapabilities.Power);
        s.Layer.Should().Be(TelemetryLayer.Lhm);
    }
}
