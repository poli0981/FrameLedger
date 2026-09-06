using FrameLedger.Domain.Consent;

namespace FrameLedger.Application.AntiCheat;

/// <summary>
/// What the Agent knows about a game before asking the guard, and the only shape
/// <see cref="HookedCaptureGate"/> accepts.
/// </summary>
/// <remarks>
/// <para>
/// <b>There is exactly one way to build one, and that is the whole point of the
/// type.</b> This was a <c>public sealed record</c> with <c>required</c> and
/// <c>init</c> members, so
/// <c>new HookRequest { TargetPid = pid, PayloadPath = p, HookEnabled = true,
/// ConsentedAt = DateTimeOffset.UtcNow }</c> passed every check in
/// <see cref="HookedCaptureGate.StartAsync"/> and reached <c>GuardedInjectAsync</c>
/// — verbatim the synthesis <c>20_OPEN_QUESTIONS</c> §S27 named and rejected as
/// "a gate whose verdict is decided before it looks". A store, a record and a
/// provenance flag do not close that hole; they only make the honest path exist
/// beside it. Making the dishonest one inexpressible is what closes it.
/// </para>
/// <para>
/// So: get-only properties, a private constructor, and
/// <see cref="FromConsent"/> as the sole entry. Everything it can do is
/// <i>downgrade</i> — a record that did not come from a store, or carries no
/// disclosure provenance, or describes a different executable from the one on
/// disk, produces a request the gate refuses. It decides nothing about anti-cheat
/// and produces no verdict.
/// </para>
/// </remarks>
public sealed class HookRequest
{
    private HookRequest(int targetPid, string payloadPath, bool hookEnabled, DateTimeOffset? consentedAt,
        string? blockedReason, int waitForPresentationRuntimeMs)
    {
        TargetPid = targetPid;
        PayloadPath = payloadPath;
        HookEnabled = hookEnabled;
        ConsentedAt = consentedAt;
        BlockedReason = blockedReason;
        WaitForPresentationRuntimeMs = waitForPresentationRuntimeMs;
    }

    /// <summary>
    /// Launch mode's budget: how long the guard may wait for the target to map a presentation runtime
    /// before injecting. Zero — the default, and attach mode — asks the guard to inject now.
    /// </summary>
    /// <remarks>
    /// It cannot widen what is injected or skip a check: a positive value routes the request through
    /// <see cref="IAntiCheatGuard.GuardedInjectWhenReadyAsync"/>, which runs the same full scan later
    /// rather than a smaller one sooner.
    /// </remarks>
    public int WaitForPresentationRuntimeMs { get; }

    /// <summary>The process to inject into.</summary>
    public int TargetPid { get; }

    /// <summary>The Overlay, which must resolve into the guard's own directory (§S22).</summary>
    public string PayloadPath { get; }

    /// <summary>Off by default for every newly added game (<c>19_SAFETY</c>).</summary>
    public bool HookEnabled { get; }

    /// <summary>
    /// <c>games.hook_consent_at</c>, as far as this session is entitled to read it.
    /// </summary>
    /// <remarks>
    /// <c>19_SAFETY</c> §User-facing consent requires this to be "stamped by the
    /// Agent, never supplied by a client". <b>A file-backed store cannot uphold
    /// that</b> — a file is by construction supplied by whoever can write it — and
    /// saying so is the honest position rather than implying the property holds.
    /// What holds instead: the store is a build-tree file belonging to an unshipped
    /// host, and the record carries a
    /// <see cref="GameConsentRecord.Provenance"/> whose default means no disclosure
    /// was shown. <c>04_CAPTURE</c> records the gap in the same PR.
    /// </remarks>
    public DateTimeOffset? ConsentedAt { get; }

    /// <summary><c>games.hook_blocked_reason</c>; non-null means the toggle is disabled.</summary>
    public string? BlockedReason { get; }

    /// <summary>
    /// Build the request the gate evaluates, from a stored record and the executable
    /// as it is on disk right now.
    /// </summary>
    /// <param name="record">What the store returned, or the refusing default.</param>
    /// <param name="observed">The executable as it is on disk right now.</param>
    /// <param name="targetPid">The process to inject into.</param>
    /// <param name="payloadPath">The Overlay, resolved beside the guard (§S22).</param>
    /// <param name="waitForPresentationRuntimeMs">Launch mode's budget; zero is attach mode.</param>
    public static HookRequest FromConsent(
        GameConsentRecord record, ExecutableFingerprint observed, int targetPid, string payloadPath,
        int waitForPresentationRuntimeMs = 0)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(payloadPath);
        ArgumentOutOfRangeException.ThrowIfNegative(waitForPresentationRuntimeMs);

        // A record nobody stored has enabled nothing and consented to nothing. Checked first so the
        // fields below are never read off a default value.
        if (!record.IsFromStore)
        {
            return new HookRequest(targetPid, payloadPath, hookEnabled: false, consentedAt: null,
                blockedReason: null, waitForPresentationRuntimeMs);
        }

        // A TIMESTAMP IS NOT CONSENT. games.hook_consent_at is a bare DateTime, so a record could carry
        // one that no disclosure ever preceded — which is the gap ConsentProvenance exists to close.
        // NotRecorded therefore refuses even when the timestamp is set, rather than the timestamp being
        // trusted because it is non-null.
        DateTimeOffset? consentedAt = record.Provenance == ConsentProvenance.NotRecorded
            ? null
            : record.ConsentedAt;

        // A DIFFERENT EXECUTABLE IS NOT THE CONSENTED ONE, and this refusal is for THIS SESSION ONLY.
        // 19_SAFETY §A game already enabled can become blocked later is explicit that hook_consent_at is
        // PRESERVED — "the user did consent; the block is not a withdrawal of consent" — so nothing here
        // writes to the store. The stored record is untouched and a later session against the matching
        // binary uses it unchanged.
        if (!record.Fingerprint.Matches(observed))
        {
            consentedAt = null;
        }

        return new HookRequest(targetPid, payloadPath, record.HookEnabled, consentedAt, record.BlockedReason,
            waitForPresentationRuntimeMs);
    }
}
