using System.Globalization;
using System.Text;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;
using LibreHardwareMonitor.Hardware;

namespace FrameLedger.CaptureHost.Telemetry;

/// <summary>
/// §M5's instrument: does LibreHardwareMonitor return GPU sensors to an UNELEVATED process
/// with no PawnIO?
/// </summary>
/// <remarks>
/// <para>
/// Prints three things, in this order, so the raw evidence precedes the interpretation:
/// the process facts (elevation, PawnIO), the raw sensor tree of every GPU node for
/// <c>--seconds</c> ticks with the field each sensor would feed, and finally the
/// <see cref="LhmTelemetrySource"/>'s own <c>Capabilities</c> read through the port. The
/// verdict line names the row of the decision table pre-committed in
/// <c>20_OPEN_QUESTIONS</c> §M5 — written before this code ran, which is the only reason a
/// verdict printed by the instrument itself is admissible.
/// </para>
/// <para>
/// <b>Deliberately GPU-only.</b> There is no <c>--cpu</c>: the CPU and board groups need
/// PawnIO by LHM's own design, and a run that enabled them would measure that instead of
/// §M5. Deliberately no game either — this verb injects nothing and resolves no target.
/// </para>
/// </remarks>
internal static class LhmProbe
{
    public const int DefaultSeconds = 5;

    /// <summary>
    /// The five fields the decision table is decided on — the ones the Tier-2 sentence
    /// would name if it named any.
    /// </summary>
    public const GpuCapabilities DecidingFields =
        GpuCapabilities.TempCore | GpuCapabilities.Load | GpuCapabilities.Power
        | GpuCapabilities.VramAdapter | GpuCapabilities.CoreClock;

    public static int Run(int seconds)
    {
        HostConsole.Line($"probe-lhm: LibreHardwareMonitorLib {typeof(Computer).Assembly.GetName().Version}, GPU group only, {seconds} tick(s)");
        HostConsole.Line($"  os: {Environment.OSVersion.VersionString}   process elevated: {LhmEnvironment.IsElevated}   " +
                         $"PawnIO installed: {LhmEnvironment.IsPawnIoInstalled?.ToString() ?? "unknown"} (never opened: only the CPU and board groups use it, and this run enables the GPU group alone)");

        string? fault = WalkRawTree(seconds, out bool sawGpu, out GpuCapabilities union);
        bool disabledViaPort = ReadThroughThePort();

        string row;
        if (fault is not null || disabledViaPort)
        {
            row = "R4 — the library threw or hung";
        }
        else if (!sawGpu)
        {
            row = "OUTSIDE THE TABLE — LHM opened and enumerated no GPU node; record it, do not fold it into R3";
        }
        else if (union == GpuCapabilities.None)
        {
            row = "R3 — a GPU node exists and every sensor read null";
        }
        else if ((union & DecidingFields) == DecidingFields)
        {
            row = "R1 — all five deciding fields carried a value";
        }
        else
        {
            row = $"R2 — a subset carried a value; missing: {DecidingFields & ~union}";
        }

        HostConsole.Line($"  fields seen over the run: {union}");
        HostConsole.Line($"  VERDICT (20_OPEN_QUESTIONS §M5, elevated={LhmEnvironment.IsElevated}): {row}");
        return 0;
    }

    /// <summary>The raw evidence: every GPU sensor, every tick. Returns the fault text if the library failed.</summary>
    private static string? WalkRawTree(int seconds, out bool sawGpu, out GpuCapabilities union)
    {
        sawGpu = false;
        union = GpuCapabilities.None;
        var raw = new LhmComputerAdapter(enableCpuAndMemory: false);
        try
        {
            raw.Open();
            for (int tick = 1; tick <= seconds; tick++)
            {
                raw.Update();
                HostConsole.Line($"  tick {tick}:");
                foreach (IHardware hardware in raw.Hardware)
                {
                    if (!SensorMap.IsGpu(hardware))
                    {
                        continue;
                    }

                    sawGpu = true;
                    HostConsole.Line($"    {hardware.HardwareType}  \"{hardware.Name}\"  ({hardware.Identifier})");
                    foreach (ISensor sensor in SensorMap.AllSensors(hardware))
                    {
                        HostConsole.Line(DescribeSensor(sensor));
                    }

                    GpuSample sample = SensorMap.Build(hardware, DateTimeOffset.UtcNow);
                    union |= sample.PresentFields;
                    HostConsole.Line("      mapped: " + Describe(sample));
                }

                if (tick < seconds)
                {
                    Thread.Sleep(1000);
                }
            }

            raw.Close();
            return null;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            string fault = $"{ex.GetType().Name}: {ex.Message}";
            HostConsole.Problem($"  FAULT (raw walk): {fault}");
            return fault;
        }
    }

    /// <summary>Through <see cref="IGpuTelemetrySource"/>, so what the Agent would see is what gets recorded. True if the layer disabled itself.</summary>
    private static bool ReadThroughThePort()
    {
        using var source = new LhmTelemetrySource(new LhmComputerAdapter(enableCpuAndMemory: false),
            new LhmTelemetryOptions(), TimeProvider.System);
        source.Start();
        Thread.Sleep(2500);
        bool read = source.TryRead(out GpuSample? viaPort);
        HostConsole.Line($"  via IGpuTelemetrySource: TryRead={read}  Capabilities={source.Capabilities}  " +
                         $"faults={source.Faults}  disabled={source.IsDisabled}  lastFault={source.LastFault ?? "-"}");
        if (viaPort is not null)
        {
            HostConsole.Line("    sample: " + Describe(viaPort));
        }

        return source.IsDisabled;
    }

    private static string DescribeSensor(ISensor sensor)
    {
        string value = sensor.Value is float v ? v.ToString("0.##", CultureInfo.InvariantCulture) : "null";
        string target = SensorMap.Classify(sensor) is (GpuCapabilities field, int rank) ? $"{field} (rank {rank})" : "-";
        return $"      {sensor.SensorType,-12} #{sensor.Index,-2} {sensor.Name,-32} {value,10}   -> {target}";
    }

    private static string Describe(GpuSample s)
    {
        var sb = new StringBuilder();
        sb.Append("core=").Append(Num(s.TempCoreC)).Append("C hot=").Append(Num(s.TempHotspotC)).Append("C mem=").Append(Num(s.TempMemoryC))
          .Append("C load=").Append(Num(s.LoadPct)).Append("% vram=").Append(Num(s.VramAdapterMb)).Append("MB clk=").Append(Num(s.CoreClockMhz))
          .Append('/').Append(Num(s.MemClockMhz)).Append("MHz power=").Append(Num(s.PowerW)).Append("W fan=").Append(Num(s.FanRpm))
          .Append("rpm  present=").Append(s.PresentFields);
        return sb.ToString();
    }

    private static string Num(double? value) => value?.ToString("0.#", CultureInfo.InvariantCulture) ?? "N/A";
}
