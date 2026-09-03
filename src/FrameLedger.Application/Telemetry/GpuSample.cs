namespace FrameLedger.Application.Telemetry;

/// <summary>
/// One 1 Hz reading of adapter-wide GPU telemetry. Every field nullable; null is
/// <c>N/A</c>, never zero (<c>03_METRICS</c> §Sensor aggregates).
/// </summary>
/// <remarks>
/// <para>
/// The field list is <c>18_GPU_VENDOR_APIS</c> §Abstraction's, verbatim, plus
/// <see cref="AdapterName"/> so a report can say which adapter a sample describes on a
/// machine with more than one. <b>There is deliberately no latency field</b>: Reflex / PC
/// latency is per frame and travels in the ring, not in a 1 Hz sample
/// (<c>20_OPEN_QUESTIONS</c> §M8).
/// </para>
/// <para>
/// <b>Per-process VRAM is not here either.</b> It comes from the Overlay inside the game
/// and is a different series with a different label; <see cref="VramAdapterMb"/> is the
/// adapter-wide figure.
/// </para>
/// </remarks>
public sealed record GpuSample
{
    public required DateTimeOffset TakenAt { get; init; }

    public required TelemetryLayer Layer { get; init; }

    public string? AdapterName { get; init; }

    public double? TempCoreC { get; init; }

    public double? TempHotspotC { get; init; }

    public double? TempMemoryC { get; init; }

    public double? LoadPct { get; init; }

    /// <summary>Adapter-wide dedicated memory in use, MiB.</summary>
    public double? VramAdapterMb { get; init; }

    public double? CoreClockMhz { get; init; }

    public double? MemClockMhz { get; init; }

    public double? PowerW { get; init; }

    public double? FanRpm { get; init; }

    /// <summary>L3 only. Vendor bit set; null everywhere else.</summary>
    public uint? ThrottleReasons { get; init; }

    /// <summary>L3 only.</summary>
    public int? PcieGen { get; init; }

    /// <summary>L3 only.</summary>
    public int? PcieWidth { get; init; }

    /// <summary>
    /// The fields this sample actually carries. What a source's <c>Capabilities</c> may be
    /// built from, and the only thing it may be built from.
    /// </summary>
    public GpuCapabilities PresentFields
    {
        get
        {
            GpuCapabilities present = GpuCapabilities.None;
            if (TempCoreC is not null)
            {
                present |= GpuCapabilities.TempCore;
            }

            if (TempHotspotC is not null)
            {
                present |= GpuCapabilities.TempHotspot;
            }

            if (TempMemoryC is not null)
            {
                present |= GpuCapabilities.TempMemory;
            }

            if (LoadPct is not null)
            {
                present |= GpuCapabilities.Load;
            }

            if (VramAdapterMb is not null)
            {
                present |= GpuCapabilities.VramAdapter;
            }

            if (CoreClockMhz is not null)
            {
                present |= GpuCapabilities.CoreClock;
            }

            if (MemClockMhz is not null)
            {
                present |= GpuCapabilities.MemClock;
            }

            if (PowerW is not null)
            {
                present |= GpuCapabilities.Power;
            }

            if (FanRpm is not null)
            {
                present |= GpuCapabilities.Fan;
            }

            if (ThrottleReasons is not null)
            {
                present |= GpuCapabilities.ThrottleReasons;
            }

            if (PcieGen is not null || PcieWidth is not null)
            {
                present |= GpuCapabilities.Pcie;
            }

            return present;
        }
    }
}
