namespace FrameLedger.Application.Capture;

/// <summary>Knobs, so a test can run the loop without waiting 30 s for a scan.</summary>
public sealed record CaptureOptions
{
    /// <summary><c>04_CAPTURE</c> §Ring draining: every 100 ms.</summary>
    public TimeSpan DrainInterval { get; init; } = TimeSpan.FromMilliseconds(100);

    /// <summary><c>19_SAFETY</c> §During a session: every 30 s.</summary>
    public TimeSpan ScanInterval { get; init; } = TimeSpan.FromSeconds(30);

    /// <summary>How long the Overlay gets to publish a usable handshake.</summary>
    public TimeSpan AttachBudget { get; init; } = TimeSpan.FromSeconds(5);

    /// <summary>Zero means "until the target exits".</summary>
    public TimeSpan MaxDuration { get; init; } = TimeSpan.Zero;

    /// <summary>
    /// Launch mode: how long the guard may wait for the launched target to map a presentation runtime
    /// before it gives up with <c>LaunchNoPresentationRuntime</c>. A launcher that sits on its window
    /// past this is a launcher, and the descendant election is the Agent's (P2).
    /// </summary>
    public TimeSpan LaunchWaitBudget { get; init; } = TimeSpan.FromSeconds(60);

    /// <summary>
    /// How long the loop waits after asking the Overlay to flush its native log at session end — the
    /// watchdog acts within one 1 s tick, so a little more than that.
    /// </summary>
    public TimeSpan LogFlushGrace { get; init; } = TimeSpan.FromMilliseconds(1300);
}
