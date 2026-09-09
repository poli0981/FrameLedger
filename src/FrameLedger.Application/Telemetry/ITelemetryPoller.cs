namespace FrameLedger.Application.Telemetry;

/// <summary>
/// The 1 Hz telemetry thread (<c>18_GPU_VENDOR_APIS</c> §Runtime policy: <i>"Poll at 1 Hz on a
/// dedicated Agent thread. Never from the game process, never from the Overlay."</i>).
/// </summary>
/// <remarks>
/// <para>
/// One producer, one consumer. The poller's own thread reads the composite source once per
/// interval and queues a <see cref="TelemetrySample"/>; the session loop — the only other
/// party — takes the queue's contents each drain tick through <see cref="Drain"/>. Nothing
/// here touches the ring, and nothing in the session loop waits on a vendor call.
/// </para>
/// <para>
/// <see cref="Descriptor"/> is what <c>sessions.telemetry_source</c> stores: the layers still
/// standing, lowest first, joined with <c>+</c> (<c>l1+lhm+nvapi</c>). It is read at finalize
/// time, so a layer disabled mid-session drops out of the descriptor the session is stored
/// under — the descriptor names what the aggregates could have come from, not what was
/// constructed.
/// </para>
/// </remarks>
public interface ITelemetryPoller : IDisposable
{
    string Descriptor { get; }

    /// <summary>Samples queued and never drained because the queue was full. A stall figure, like the ring's.</summary>
    long Dropped { get; }

    /// <summary>Start the thread. Idempotent.</summary>
    void Start();

    /// <summary>Move every queued sample into <paramref name="into"/>, oldest first. Returns how many.</summary>
    int Drain(ICollection<TelemetrySample> into);
}
