namespace FrameLedger.Domain.Consent;

/// <summary>
/// What disclosure preceded a consent record. The default means none did.
/// </summary>
/// <remarks>
/// <para>
/// <c>19_SAFETY</c> §User-facing consent makes enabling hooking a per-game action
/// gated by a one-time dialog stating four things in plain language. A bare
/// timestamp cannot say whether that happened, and a timestamp is all
/// <c>games.hook_consent_at</c> is — so a record could carry a consent time that
/// nothing had ever disclosed anything for.
/// </para>
/// <para>
/// <b>There are exactly two members, and FR-2.1's value is deliberately not one
/// of them.</b> The reviewed consent wording does not exist: no <c>.resx</c> file
/// exists anywhere in this tree, and <c>09_I18N</c> requires the <c>Safety_*</c>
/// keys to be reviewed with the same care as the legal documents. Declaring a
/// <c>ConsentDialog</c> member that no code can produce is the shape §S29(c) was
/// raised for — an API that reads as sanctioned because it is declared. A test
/// asserts the member count, so adding one is a deliberate act rather than a
/// convenience.
/// </para>
/// </remarks>
public enum ConsentProvenance
{
    /// <summary>
    /// Nobody disclosed anything. The zero value, so a record nobody filled in
    /// cannot read as consent — the rule <c>AntiCheatVerdict</c> and
    /// <c>ShmAttachRefusal.NotEvaluated</c> already follow.
    /// </summary>
    NotRecorded = 0,

    /// <summary>
    /// An operator acknowledged the injection risk at the unshipped capture host.
    /// </summary>
    /// <remarks>
    /// <b>This is not FR-2.1 consent and must never be presented as it.</b> It is
    /// a developer acting on their own machine at a binary <c>12_BUILD</c> does not
    /// publish, having read an English-only console statement. It exists so
    /// <c>HookedCaptureGate</c>'s three inputs have a real source for the first
    /// time (§S27 rejects synthesising them) without manufacturing the reviewed
    /// artifact that FR-2.1 requires.
    /// </remarks>
    UnshippedHostOperator = 1,
}
