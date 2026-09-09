namespace FrameLedger.Application.Capture;

/// <summary>
/// Whether the process we injected into is still there — §S29(e)'s answer, and it
/// deliberately does not come from the shared memory.
/// </summary>
/// <remarks>
/// <c>ShmRingReader</c> holds the section open, so an exited game leaves
/// <c>writeIndex</c> frozen and <c>status</c> <c>READY</c>: byte-for-byte identical
/// to a loading screen. §S26 made it strictly worse by dropping
/// <c>DXGI_PRESENT_TEST</c>, which removed the accidental heartbeat an occluded
/// title used to emit. The signal has to come from the OS.
/// </remarks>
public interface ITargetLiveness : IDisposable
{
    bool HasExited { get; }

    /// <summary>
    /// Does the target own the foreground window at this instant?
    /// </summary>
    /// <remarks>
    /// Sampled once per drain tick so the report can say when the operator switched away —
    /// frame generation stops while a title is unfocused, and the resulting window mixes two
    /// states. <b>False on every tick does NOT mean "unfocused"</b>: a process owning no
    /// top-level window at all answers false forever, which is exactly what
    /// <c>hook-harness</c> does. The loop counts the trues and lets the report tell the two
    /// apart; <c>ForegroundWindowProbe</c> carries the reasoning.
    /// </remarks>
    bool IsForeground { get; }
}
