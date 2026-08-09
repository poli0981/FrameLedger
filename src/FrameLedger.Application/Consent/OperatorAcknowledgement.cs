using FrameLedger.Domain.Consent;

namespace FrameLedger.Application.Consent;

/// <summary>
/// Everything an acknowledgement may say, which is deliberately less than a
/// <see cref="GameConsentRecord"/> holds.
/// </summary>
/// <remarks>
/// <para>
/// <b>It carries no <c>BlockedReason</c> and no <c>PreScanUnverified</c>, and that
/// omission is the mechanism.</b> The first shape of this API took a whole record,
/// which meant granting consent for a title the pre-scan had blocked would write a
/// fresh record with <c>BlockedReason = null</c> — and <c>HookedCaptureGate</c>'s
/// <c>PreviouslyBlocked</c> branch would stop firing. <c>19_SAFETY</c> §A game
/// already enabled can become blocked later forces <c>hook_enabled</c> to 0 on a
/// match and <c>06_DATA_MODEL</c> defines a non-null reason as "toggle disabled";
/// a command that clears the block while the match still stands is the "I
/// understand, continue anyway" button CLAUDE.md rule 2 forbids adding.
/// </para>
/// <para>
/// The blast radius would have been bounded — <c>FlGuardedInject</c> re-runs the
/// same directory scan against a path derived from the target's own pid, so this
/// was never an anti-cheat bypass — but the two scans differ in reach: a static
/// directory hit can exist before the process has loaded the module. Discarding a
/// persisted refusal is its own defect.
/// </para>
/// <para>
/// So a store implementing <c>RecordOperatorAcknowledgementAsync</c> merges those
/// two fields forward from whatever is already on disk, and has nothing in hand to
/// merge anything else from.
/// </para>
/// </remarks>
public sealed record OperatorAcknowledgement
{
    /// <summary>Which executable was acknowledged, as it is on disk right now.</summary>
    public required ExecutableFingerprint Fingerprint { get; init; }

    /// <summary>
    /// Which wording was shown. Stored on the record so a later change to it does
    /// not silently re-interpret acknowledgements already made.
    /// </summary>
    public required string DisclosureVersion { get; init; }

    /// <summary>When the operator acknowledged, UTC.</summary>
    public required DateTimeOffset AcknowledgedAt { get; init; }
}
