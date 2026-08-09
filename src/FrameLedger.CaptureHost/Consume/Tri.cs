namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// CLAUDE.md rule 7's tri-state. Zero is N/A, so a value nobody set is not a claim.
/// </summary>
internal enum Tri
{
    NotApplicable = 0,
    No,
    Yes,
}
