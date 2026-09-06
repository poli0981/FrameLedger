using FrameLedger.Domain.AntiCheat;

namespace FrameLedger.Application.AntiCheat;

/// <summary>
/// Decides whether a Tier-1 (hooked) capture may start, and turns a refusal
/// into something the UI can explain.
/// </summary>
/// <remarks>
/// <para>
/// This is the ONLY managed logic between the user's intent and the guard, and
/// it deliberately adds no judgement of its own about anti-cheat: it checks the
/// things the native guard structurally cannot see — per-game consent, which is
/// a record of something a human did — and then defers entirely.
/// </para>
/// <para>
/// <b>It exposes exactly one instance method, and that is a property a test
/// pins.</b> <see cref="StartAsync"/> is the only way in. There was a second —
/// <c>ShouldUnhookAsync</c>, an in-session re-scan that published no
/// <c>guardTicks</c> and did not latch — and it was deleted rather than repaired
/// (<c>20_OPEN_QUESTIONS</c> §S29(c)). Those two properties are the entire point
/// of <see cref="GuardSupervisor"/>, so a second route that had neither was a way
/// for the supervision counter to quietly stop meaning what it says: a drain loop
/// already holds this object, which made the weaker API the more discoverable one.
/// The polarity differed too — <c>ShouldUnhookAsync</c> returning <c>true</c> meant
/// STOP, while <see cref="GuardSupervisor.ScanOnceAsync"/> returning <c>true</c>
/// means MAY CONTINUE.
/// </para>
/// </remarks>
public sealed class HookedCaptureGate(IAntiCheatGuard guard)
{
    private readonly IAntiCheatGuard _guard = guard ?? throw new ArgumentNullException(nameof(guard));

    /// <summary>
    /// CLAUDE.md rule 1: injection is opt-in per game and never automatic. The
    /// native guard cannot enforce this — consent is a record of something a
    /// human did, which no process scan can see — so it is enforced here, before
    /// the guard is even asked.
    /// </summary>
    /// <remarks>
    /// The three inputs come from <c>IGameConsentStore</c>. This comment said
    /// "consent lives in the Agent's database" in the present tense while no
    /// database, no <c>games</c> table and no consent writer existed in any
    /// <c>.cs</c> file (§S27) — the same present-tense-claim-about-an-absent-thing
    /// shape CLAUDE.md's pinned-stack table records against itself. SQLite is P2's
    /// adapter for that port, and it is still unwritten.
    /// </remarks>
    public async ValueTask<AntiCheatVerdict> StartAsync(
        HookRequest request,
        CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!request.HookEnabled)
        {
            return AntiCheatVerdict.Refused(
                AntiCheatRefusalReason.HookNotEnabled,
                "not enabled",
                "hooking is off for this game; nothing is injected because a game was merely added");
        }

        if (request.ConsentedAt is null)
        {
            return AntiCheatVerdict.Refused(
                AntiCheatRefusalReason.ConsentMissing,
                "no consent",
                "the per-game consent dialog has not been accepted");
        }

        if (request.BlockedReason is { Length: > 0 } blocked)
        {
            // 19_SAFETY §A game already enabled can become blocked later: a
            // patch adds anti-cheat, or updated rules newly match. hook_enabled
            // is forced to 0 and this is set; honouring it here means a stale
            // in-memory watchlist cannot resurrect the game.
            return AntiCheatVerdict.Refused(AntiCheatRefusalReason.PreviouslyBlocked, "previously blocked", blocked);
        }

        // Launch mode is the same gate reached through the guard's WAITING entry: the consent checks
        // above are identical, and what differs is only WHEN the guard's full scan runs (P1 item 2).
        return request.WaitForPresentationRuntimeMs > 0
            ? await _guard.GuardedInjectWhenReadyAsync(request.TargetPid, request.PayloadPath,
                request.WaitForPresentationRuntimeMs, ct).ConfigureAwait(false)
            : await _guard.GuardedInjectAsync(request.TargetPid, request.PayloadPath, ct).ConfigureAwait(false);
    }
}
