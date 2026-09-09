namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// The one counter L1 polls: an adapter's dedicated memory in use, machine-wide.
/// <see cref="PdhAdapterMemoryCounter"/> is the real one; the seam exists so
/// <see cref="BaselineTelemetrySource"/> is tested on a machine with no GPU.
/// </summary>
public interface IAdapterMemoryCounter : IDisposable
{
    /// <summary>
    /// Bind to the adapter with this LUID. False when the counter does not exist here — an
    /// answer (the field is N/A), not a fault. Rebinding closes the previous one.
    /// </summary>
    bool TryOpen(ulong luid);

    /// <summary>Bytes of dedicated adapter memory in use. Throws on failure — the source counts it.</summary>
    ulong ReadDedicatedUsageBytes();
}
