using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;

namespace FrameLedger.Application.Consent;

/// <summary>
/// Where the three inputs <c>HookedCaptureGate</c> checks actually come from.
/// </summary>
/// <remarks>
/// <para>
/// <c>20_OPEN_QUESTIONS</c> §S27: the guard ABI "deliberately does NOT carry
/// per-game consent … This ABI enforces the ANTI-CHEAT gate — the part that
/// protects accounts — not the opt-in." The opt-in half is
/// <c>HookedCaptureGate</c>, and its three inputs — <c>hook_enabled</c>,
/// <c>hook_consent_at</c>, <c>hook_blocked_reason</c> — came from a <c>games</c>
/// table that existed in <c>06_DATA_MODEL</c> and in no <c>.cs</c> file. This port
/// is that source. §S27 rejects synthesising them, and a record written by an
/// explicit human act is not synthesis.
/// </para>
/// <para>
/// <b>No method takes a store path</b>, for the reason <c>IRulesStore</c> gives:
/// a port with a path parameter is how the location becomes selectable again. The
/// adapter knows the one location; the policy above it does not.
/// </para>
/// <para>
/// <b>Nothing here can author an anti-cheat fact.</b>
/// <see cref="RecordGuardBlockAsync"/> takes an <see cref="AntiCheatVerdict"/> and
/// never a reason string, so a managed component cannot write a block that no guard
/// produced — <c>04_CAPTURE</c> §The guard and §S15's one-matcher rule, applied to
/// persistence rather than to matching.
/// </para>
/// <para>
/// P2's SQLite is another adapter for this port. It is not written, and
/// <c>06_DATA_MODEL</c> declines to guess <c>0001_init.sql</c> before its consumers
/// exist.
/// </para>
/// </remarks>
public interface IGameConsentStore
{
    /// <summary>
    /// The record for <paramref name="normalisedExePath"/>, or the refusing default
    /// when there is none.
    /// </summary>
    /// <remarks>
    /// Returns <c>default(GameConsentRecord)</c> rather than null on purpose: the
    /// default reports <c>IsFromStore == false</c> and carries
    /// <c>HookEnabled == false</c> and <c>ConsentedAt == null</c>, so a caller that
    /// forgets to check still refuses. A nullable return would make forgetting a
    /// <c>NullReferenceException</c> at best and a fail-open at worst.
    /// </remarks>
    ValueTask<GameConsentRecord> FindAsync(string normalisedExePath, CancellationToken ct = default);

    /// <summary>Every record with hooking enabled.</summary>
    /// <remarks>
    /// Here because <c>12_BUILD</c> §The Vulkan layer is not registered at install
    /// time makes "registered only while at least one Vulkan game has hooking
    /// enabled" a rule someone has to be able to evaluate, and answering it later
    /// through a second interface would mean two views of the same state.
    /// </remarks>
    ValueTask<IReadOnlyList<GameConsentRecord>> ListEnabledAsync(CancellationToken ct = default);

    /// <summary>
    /// Enable hooking for one game, on the strength of an explicit human act.
    /// </summary>
    /// <remarks>
    /// <para>
    /// It takes an <see cref="OperatorAcknowledgement"/> rather than a whole record,
    /// so it structurally cannot clear a block — see that type for what that
    /// prevents. An implementation merges <c>BlockedReason</c> and
    /// <c>PreScanUnverified</c> forward from whatever is already on disk.
    /// </para>
    /// <para>
    /// The caller is responsible for having shown a disclosure and for the
    /// <c>DisclosureVersion</c> naming it. A store cannot verify that a human read
    /// anything, which is exactly why the provenance is a recorded field rather than
    /// something inferred from the timestamp being non-null.
    /// </para>
    /// </remarks>
    ValueTask<ConsentWriteOutcome> RecordOperatorAcknowledgementAsync(
        OperatorAcknowledgement acknowledgement, CancellationToken ct = default);

    /// <summary>
    /// Turn hooking off for one game and withdraw the consent behind it.
    /// </summary>
    /// <remarks>
    /// Clears <c>ConsentedAt</c> and resets <c>Provenance</c> to
    /// <see cref="ConsentProvenance.NotRecorded"/>, so re-enabling requires being
    /// shown the disclosure again. That is the opposite of what happens on a
    /// <i>block</i>, where <c>19_SAFETY</c> preserves the stamp because the user did
    /// consent and a title changing under them is not a withdrawal — here the
    /// withdrawal is the whole act. It never touches <c>BlockedReason</c>.
    /// </remarks>
    ValueTask<ConsentWriteOutcome> RevokeAsync(string normalisedExePath, CancellationToken ct = default);

    /// <summary>
    /// Record that a real guard verdict blocked this title
    /// (<c>19_SAFETY</c> §A game already enabled can become blocked later).
    /// </summary>
    /// <remarks>
    /// <para>
    /// Takes the verdict, not a message. An implementation must refuse an allowing
    /// verdict, and must treat a default-constructed one — which has evaluated
    /// nothing — as "could not verify" rather than as a block.
    /// </para>
    /// <para>
    /// It forces <c>HookEnabled</c> to false and <b>preserves</b>
    /// <c>ConsentedAt</c>: the user did consent, and a block is not a withdrawal of
    /// consent. Nothing here ever clears a block — who may, and whether it takes a
    /// user action, is unanswered by any document and is recorded as an open
    /// question rather than decided by an implementation.
    /// </para>
    /// </remarks>
    ValueTask<ConsentWriteOutcome> RecordGuardBlockAsync(
        ExecutableFingerprint fingerprint, AntiCheatVerdict refusal, CancellationToken ct = default);
}
