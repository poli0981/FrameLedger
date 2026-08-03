namespace FrameLedger.Domain.Detection;

/// <summary>Where a field's value came from, for FR-1.3's badge.</summary>
/// <remarks>
/// <see cref="Unknown"/> is 0 and reads as <see cref="UserSupplied"/> at the
/// persistence boundary. Of the two ways to be wrong, badging a human's typed
/// value as auto-detected is a lie they cannot see through; failing to badge a
/// detected value merely costs a badge on something they can still edit.
/// </remarks>
public enum DetectionProvenance
{
    /// <summary>Not recorded. Treated as <see cref="UserSupplied"/> — the safe direction.</summary>
    Unknown = 0,

    /// <summary>Written by the rules engine, and therefore safe to overwrite on a re-run.</summary>
    Detected,

    /// <summary>Typed or corrected by a human. Detection must never overwrite this.</summary>
    UserSupplied,
}
