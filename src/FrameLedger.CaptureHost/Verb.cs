namespace FrameLedger.CaptureHost;

/// <summary>What the host was asked to do.</summary>
internal enum Verb
{
    None = 0,
    ConsentList,
    ConsentGrant,
    ConsentRevoke,
    Capture,
}
