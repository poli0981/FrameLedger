using FrameLedger.Application.Telemetry;
using LibreHardwareMonitor.Hardware;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// Which LibreHardwareMonitor sensor feeds which <see cref="GpuSample"/> field.
/// </summary>
/// <remarks>
/// <para>
/// <c>18_GPU_VENDOR_APIS</c> §L2: <i>"sensor mapping by SensorType + name heuristics per
/// vendor, kept in SensorMap.cs with unit tests against captured sensor-tree fixtures."</i>
/// The names below are LHM's own (<c>GPU Core</c>, <c>GPU Hot Spot</c>, <c>GPU Memory
/// Junction</c>, <c>GPU Package</c>, <c>GPU Memory Used</c>, …), and each rule carries a rank
/// so that when a vendor exposes two candidates the more specific one wins deterministically
/// rather than by tree order.
/// </para>
/// <para>
/// <b>Two traps the rules are written around.</b> NVIDIA publishes a <c>Load</c> sensor named
/// <c>GPU Memory</c> that is the percentage of VRAM in use, not a load — only <c>GPU Core</c>
/// is the load figure. And <c>Control</c> sensors (fan duty cycle, %) are not fans: the RPM
/// is the <c>Fan</c> sensor, and a duty cycle published as RPM would be off by two orders of
/// magnitude.
/// </para>
/// <para>
/// <b>A null value maps to nothing.</b> The field stays null and the capability bit stays
/// clear; there is no "sensor exists so the field is 0" path anywhere in this class.
/// </para>
/// </remarks>
public static class SensorMap
{
    /// <summary>The three GPU hardware types LHM knows. Everything else is ignored by this layer.</summary>
    public static bool IsGpu(IHardware hardware)
    {
        ArgumentNullException.ThrowIfNull(hardware);
        return hardware.HardwareType is HardwareType.GpuNvidia or HardwareType.GpuAmd or HardwareType.GpuIntel;
    }

    /// <summary>Every sensor under a node, including its sub-hardware, in tree order.</summary>
    public static IEnumerable<ISensor> AllSensors(IHardware hardware)
    {
        ArgumentNullException.ThrowIfNull(hardware);
        foreach (ISensor sensor in hardware.Sensors)
        {
            yield return sensor;
        }

        foreach (IHardware sub in hardware.SubHardware)
        {
            foreach (ISensor sensor in AllSensors(sub))
            {
                yield return sensor;
            }
        }
    }

    /// <summary>
    /// The field a sensor feeds and how preferred it is for that field (lower wins), or
    /// null when this layer does not read it.
    /// </summary>
    public static (GpuCapabilities Field, int Rank)? Classify(ISensor sensor)
    {
        ArgumentNullException.ThrowIfNull(sensor);
        string name = sensor.Name ?? string.Empty;

        return sensor.SensorType switch
        {
            SensorType.Temperature => ClassifyTemperature(name),
            SensorType.Load => ClassifyLoad(name),
            SensorType.SmallData => ClassifyMemory(name),
            SensorType.Clock => ClassifyClock(name),
            SensorType.Power => ClassifyPower(name),
            SensorType.Fan => name.StartsWith("GPU Fan", StringComparison.OrdinalIgnoreCase)
                ? (GpuCapabilities.Fan, 0)
                : (GpuCapabilities.Fan, 1),
            _ => null,
        };
    }

    /// <summary>Build a sample from one GPU node. Fields with no reporting sensor stay null.</summary>
    public static GpuSample Build(IHardware gpu, DateTimeOffset takenAt)
    {
        ArgumentNullException.ThrowIfNull(gpu);

        var best = new Dictionary<GpuCapabilities, (int Rank, int Index, double Value)>();
        foreach (ISensor sensor in AllSensors(gpu))
        {
            if (Classify(sensor) is not (GpuCapabilities field, int rank) || sensor.Value is not float value)
            {
                continue;
            }

            if (!best.TryGetValue(field, out (int Rank, int Index, double Value) current)
                || rank < current.Rank
                || (rank == current.Rank && sensor.Index < current.Index))
            {
                best[field] = (rank, sensor.Index, value);
            }
        }

        double? Get(GpuCapabilities field) => best.TryGetValue(field, out (int Rank, int Index, double Value) v) ? v.Value : null;

        return new GpuSample
        {
            TakenAt = takenAt,
            Layer = TelemetryLayer.Lhm,
            AdapterName = gpu.Name,
            TempCoreC = Get(GpuCapabilities.TempCore),
            TempHotspotC = Get(GpuCapabilities.TempHotspot),
            TempMemoryC = Get(GpuCapabilities.TempMemory),
            LoadPct = Get(GpuCapabilities.Load),
            VramAdapterMb = Get(GpuCapabilities.VramAdapter),
            CoreClockMhz = Get(GpuCapabilities.CoreClock),
            MemClockMhz = Get(GpuCapabilities.MemClock),
            PowerW = Get(GpuCapabilities.Power),
            FanRpm = Get(GpuCapabilities.Fan),
        };
    }

    private static (GpuCapabilities, int)? ClassifyTemperature(string name)
    {
        if (Has(name, "Hot Spot") || Has(name, "Hotspot"))
        {
            return (GpuCapabilities.TempHotspot, 0);
        }

        if (Has(name, "Memory"))
        {
            return (GpuCapabilities.TempMemory, 0);
        }

        if (Is(name, "GPU Core"))
        {
            return (GpuCapabilities.TempCore, 0);
        }

        return Has(name, "Core") ? (GpuCapabilities.TempCore, 1) : null;
    }

    private static (GpuCapabilities, int)? ClassifyLoad(string name)
    {
        // NVIDIA's `GPU Memory` load is VRAM-in-use as a percentage. It is not a load.
        if (Is(name, "GPU Core"))
        {
            return (GpuCapabilities.Load, 0);
        }

        return Is(name, "D3D 3D") ? (GpuCapabilities.Load, 1) : null;
    }

    private static (GpuCapabilities, int)? ClassifyMemory(string name)
    {
        if (Is(name, "GPU Memory Used"))
        {
            return (GpuCapabilities.VramAdapter, 0);
        }

        if (Is(name, "D3D Dedicated Memory Used"))
        {
            return (GpuCapabilities.VramAdapter, 1);
        }

        // `D3D Shared Memory Used` is system RAM the adapter has mapped, not VRAM. Measured on
        // the dev box 2026-09-03: it sat beside `D3D Dedicated Memory Used` and the fragment
        // rule took both.
        return Has(name, "Memory Used") && !Has(name, "Shared") ? (GpuCapabilities.VramAdapter, 2) : null;
    }

    private static (GpuCapabilities, int)? ClassifyClock(string name)
    {
        if (Is(name, "GPU Core"))
        {
            return (GpuCapabilities.CoreClock, 0);
        }

        if (Is(name, "GPU Memory"))
        {
            return (GpuCapabilities.MemClock, 0);
        }

        if (Has(name, "Memory"))
        {
            return (GpuCapabilities.MemClock, 1);
        }

        return Has(name, "Core") ? (GpuCapabilities.CoreClock, 1) : null;
    }

    private static (GpuCapabilities, int) ClassifyPower(string name)
    {
        if (Is(name, "GPU Package"))
        {
            return (GpuCapabilities.Power, 0);
        }

        if (Is(name, "GPU Power"))
        {
            return (GpuCapabilities.Power, 1);
        }

        if (Has(name, "Package"))
        {
            return (GpuCapabilities.Power, 2);
        }

        return Is(name, "GPU Core") ? (GpuCapabilities.Power, 3) : (GpuCapabilities.Power, 4);
    }

    private static bool Is(string name, string expected) => string.Equals(name, expected, StringComparison.OrdinalIgnoreCase);

    private static bool Has(string name, string fragment) => name.Contains(fragment, StringComparison.OrdinalIgnoreCase);
}
