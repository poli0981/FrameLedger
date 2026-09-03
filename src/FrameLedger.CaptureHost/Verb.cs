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
    /// §M5's instrument: open LibreHardwareMonitor GPU-only, print the raw sensor tree
    /// and the mapped sample for a few seconds, and name the pre-committed row it landed on.
    /// Injects nothing, needs no consent record, and takes no <c>--exe</c>.
    /// </summary>
    ProbeLhm,
}
