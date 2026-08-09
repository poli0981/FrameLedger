namespace FrameLedger.Application.Consent;

/// <summary>
/// What a write to the consent store did. Every value is reportable to a human.
/// </summary>
/// <remarks>
/// The rule <c>RulesSeedOutcome</c> established: the failure mode of a store is
/// silence, so "nothing happened" has to be a value someone can print rather than
/// an absence someone has to notice.
/// </remarks>
public enum ConsentWriteOutcome
{
    /// <summary>The record is on disk.</summary>
    Written = 0,

    /// <summary>There is no record for that executable to update.</summary>
    NotFound = 1,

    /// <summary>
    /// The executable on disk is not the one the record was written for, so the
    /// write was refused rather than silently re-pointed at a different binary.
    /// </summary>
    StaleFingerprint = 2,

    /// <summary>The write did not happen. Never treat this as "probably fine".</summary>
    Failed = 3,
}
