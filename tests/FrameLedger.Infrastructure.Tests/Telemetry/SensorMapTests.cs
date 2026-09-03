using FluentAssertions;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;
using LibreHardwareMonitor.Hardware;
using NSubstitute;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>
/// The name heuristics, against a tree shaped like LibreHardwareMonitor's NVIDIA node.
/// </summary>
/// <remarks>
/// The shape here is what LHM's <c>NvidiaGpu.cs</c> publishes by name; the real tree from
/// the dev box is recorded in <c>spike-notes</c> §10 once §M5 has run, and any name that
/// differs there is corrected here in the same PR. Until then these pin the RULES — the
/// two traps in <see cref="SensorMap"/>'s remarks and the null rule — not a vendor.
/// </remarks>
public sealed class SensorMapTests
{
    private static ISensor Sensor(SensorType type, string name, float? value, int index = 0)
    {
        ISensor s = Substitute.For<ISensor>();
        s.SensorType.Returns(type);
        s.Name.Returns(name);
        s.Value.Returns(value);
        s.Index.Returns(index);
        return s;
    }

    private static IHardware Gpu(HardwareType type, params ISensor[] sensors)
    {
        IHardware h = Substitute.For<IHardware>();
        h.HardwareType.Returns(type);
        h.Name.Returns("NVIDIA GeForce RTX 5080");
        h.Sensors.Returns(sensors);
        h.SubHardware.Returns([]);
        return h;
    }

    [Fact]
    public void AnNvidiaShapedTreeFillsEveryFieldThisLayerOwns()
    {
        IHardware gpu = Gpu(HardwareType.GpuNvidia,
            Sensor(SensorType.Temperature, "GPU Core", 61.5f),
            Sensor(SensorType.Temperature, "GPU Hot Spot", 74f, 1),
            Sensor(SensorType.Temperature, "GPU Memory Junction", 68f, 2),
            Sensor(SensorType.Load, "GPU Core", 97f),
            Sensor(SensorType.Load, "GPU Memory", 42f, 1),
            Sensor(SensorType.SmallData, "GPU Memory Used", 6144f),
            Sensor(SensorType.SmallData, "GPU Memory Total", 16384f, 1),
            Sensor(SensorType.Clock, "GPU Core", 2610f),
            Sensor(SensorType.Clock, "GPU Memory", 15001f, 1),
            Sensor(SensorType.Power, "GPU Package", 312.4f),
            Sensor(SensorType.Fan, "GPU Fan 1", 1450f),
            Sensor(SensorType.Control, "GPU Fan 1", 55f));

        GpuSample s = SensorMap.Build(gpu, DateTimeOffset.UnixEpoch);

        s.AdapterName.Should().Be("NVIDIA GeForce RTX 5080");
        s.TempCoreC.Should().Be(61.5);
        s.TempHotspotC.Should().Be(74);
        s.TempMemoryC.Should().Be(68);
        s.LoadPct.Should().Be(97, "the `GPU Memory` load is VRAM-in-use, not a load");
        s.VramAdapterMb.Should().Be(6144, "used, not total");
        s.CoreClockMhz.Should().Be(2610);
        s.MemClockMhz.Should().Be(15001);
        s.PowerW.Should().Be(312.4f);
        s.FanRpm.Should().Be(1450, "the Control sensor is a duty cycle in %, not an RPM");
        s.PresentFields.Should().Be(GpuCapabilities.TempCore | GpuCapabilities.TempHotspot | GpuCapabilities.TempMemory
            | GpuCapabilities.Load | GpuCapabilities.VramAdapter | GpuCapabilities.CoreClock | GpuCapabilities.MemClock
            | GpuCapabilities.Power | GpuCapabilities.Fan);
    }

    [Fact]
    public void SharedMemoryIsNotVram()
    {
        // MEASURED 2026-09-03 on the dev box: NVIDIA publishes `D3D Shared Memory Used` (system
        // RAM mapped for the adapter) beside `D3D Dedicated Memory Used`, and the first draft of
        // the fragment rule took both. With no `GPU Memory Used` in the tree the shared figure
        // would have been published as adapter VRAM.
        IHardware gpu = Gpu(HardwareType.GpuNvidia,
            Sensor(SensorType.SmallData, "D3D Shared Memory Used", 279f, 4));

        SensorMap.Build(gpu, DateTimeOffset.UnixEpoch).VramAdapterMb.Should().BeNull();

        gpu = Gpu(HardwareType.GpuNvidia,
            Sensor(SensorType.SmallData, "D3D Shared Memory Used", 279f, 4),
            Sensor(SensorType.SmallData, "D3D Dedicated Memory Used", 2763f, 3));

        SensorMap.Build(gpu, DateTimeOffset.UnixEpoch).VramAdapterMb.Should().Be(2763);
    }

    [Fact]
    public void ANullValueLeavesTheFieldNullAndTheBitClear()
    {
        // THE ONE DIRECTION THAT MUST NEVER BE WRONG. A sensor that exists in the tree and has
        // not reported is exactly the §M5 R3 shape, and mapping it to 0 would turn "nothing
        // was measured" into a 0 °C GPU with a capability bit behind it.
        IHardware gpu = Gpu(HardwareType.GpuNvidia,
            Sensor(SensorType.Temperature, "GPU Core", null),
            Sensor(SensorType.Load, "GPU Core", null),
            Sensor(SensorType.Power, "GPU Package", null));

        GpuSample s = SensorMap.Build(gpu, DateTimeOffset.UnixEpoch);

        s.TempCoreC.Should().BeNull();
        s.LoadPct.Should().BeNull();
        s.PowerW.Should().BeNull();
        s.PresentFields.Should().Be(GpuCapabilities.None);
    }

    [Fact]
    public void TheMoreSpecificNameWinsRegardlessOfTreeOrder()
    {
        // `GPU Core` power (AMD publishes it) must not displace `GPU Package` when both exist,
        // even when it comes first.
        IHardware gpu = Gpu(HardwareType.GpuAmd,
            Sensor(SensorType.Power, "GPU Core", 150f, 0),
            Sensor(SensorType.Power, "GPU Package", 220f, 1));

        SensorMap.Build(gpu, DateTimeOffset.UnixEpoch).PowerW.Should().Be(220);
    }

    [Fact]
    public void AHotSpotSensorDoesNotStealTheCoreTemperature()
    {
        IHardware gpu = Gpu(HardwareType.GpuNvidia,
            Sensor(SensorType.Temperature, "GPU Hot Spot", 80f, 0),
            Sensor(SensorType.Temperature, "GPU Core", 60f, 1));

        GpuSample s = SensorMap.Build(gpu, DateTimeOffset.UnixEpoch);

        s.TempCoreC.Should().Be(60);
        s.TempHotspotC.Should().Be(80);
    }

    [Fact]
    public void SubHardwareSensorsAreWalkedToo()
    {
        ISensor core = Sensor(SensorType.Temperature, "GPU Core", 55f);
        IHardware sub = Substitute.For<IHardware>();
        sub.Sensors.Returns([core]);
        sub.SubHardware.Returns([]);
        IHardware gpu = Gpu(HardwareType.GpuIntel);
        gpu.SubHardware.Returns([sub]);

        SensorMap.AllSensors(gpu).Should().ContainSingle().Which.Should().BeSameAs(core);
        SensorMap.Build(gpu, DateTimeOffset.UnixEpoch).TempCoreC.Should().Be(55);
    }

    [Fact]
    public void OnlyTheThreeGpuTypesAreGpus()
    {
        foreach (HardwareType type in Enum.GetValues<HardwareType>())
        {
            IHardware h = Substitute.For<IHardware>();
            h.HardwareType.Returns(type);
            bool expected = type is HardwareType.GpuNvidia or HardwareType.GpuAmd or HardwareType.GpuIntel;
            SensorMap.IsGpu(h).Should().Be(expected, $"{type}");
        }
    }

    [Fact]
    public void SensorTypesThisLayerDoesNotReadMapToNothing()
    {
        foreach (SensorType type in new[] { SensorType.Voltage, SensorType.Current, SensorType.Control, SensorType.Level,
                     SensorType.Data, SensorType.Throughput, SensorType.Energy, SensorType.Frequency })
        {
            SensorMap.Classify(Sensor(type, "GPU Core", 1f)).Should().BeNull($"{type}");
        }
    }
}
