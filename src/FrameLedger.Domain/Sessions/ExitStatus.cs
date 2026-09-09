namespace FrameLedger.Domain.Sessions;

/// <summary><c>04_CAPTURE</c> §Crash &amp; exit classification, and <c>sessions.exit_status</c>'s CHECK list.</summary>
public enum ExitStatus
{
    /// <summary>The presenting process exited 0 and no Application Error / WER record names it.</summary>
    Normal = 0,

    /// <summary>Non-zero exit code, or an event 1000 / 1001 naming the exe within <c>[start, end + 30 s]</c>.</summary>
    Crashed,

    /// <summary>The guard fired mid-session.</summary>
    UnhookedSafety,

    /// <summary>The Overlay self-disabled after faults, stopped itself, or the supervision was lost.</summary>
    Degraded,

    /// <summary>The Agent died mid-session; recovered from the <c>.partial</c> on the next start.</summary>
    Interrupted,
}
