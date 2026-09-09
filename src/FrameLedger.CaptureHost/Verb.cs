namespace FrameLedger.CaptureHost;

/// <summary>What the host was asked to do.</summary>
internal enum Verb
{
    None = 0,
    ConsentList,
    ConsentGrant,
    ConsentRevoke,
    Capture,

    /// <summary>
    /// Startup's first act, run by hand (P2 PR-D): every <c>.partial</c> under the host's <c>tmp\</c> becomes an
    /// <c>interrupted</c> session or is dropped for a stated reason. The Agent runs the same code before any capture.
    /// </summary>
    Recover,

    /// <summary>
    /// Launch mode (P1 item 2): start the consented executable, hold it, and let the guard inject the
    /// moment it has mapped a presentation runtime. The same consent record, the same gate, the same
    /// payload; only WHEN the full scan runs differs.
    /// </summary>
    Launch,

    /// <summary>
    /// §M5's instrument: open LibreHardwareMonitor GPU-only, print the raw sensor tree
    /// and the mapped sample for a few seconds, and name the pre-committed row it landed on.
    /// Injects nothing, needs no consent record, and takes no <c>--exe</c>.
    /// </summary>
    ProbeLhm,
}
