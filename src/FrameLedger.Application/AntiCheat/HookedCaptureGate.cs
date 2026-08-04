using FrameLedger.Domain.AntiCheat;

namespace FrameLedger.Application.AntiCheat;

/// <summary>
/// Decides whether a Tier-1 (hooked) capture may start, and turns a refusal
/// into something the UI can explain.
/// </summary>
/// <remarks>
/// This is the ONLY managed logic between the user's intent and the guard, and
/// it deliberately adds no judgement of its own about anti-cheat: it checks the
/// things the native guard structurally cannot see — per-game consent, which is
/// a record of something a human did — and then defers entirely.
/// </remarks>
public sealed class HookedCaptureGate(IAntiCheatGuard guard)
{
    private readonly IAntiCheatGuard _guard = guard ?? throw new ArgumentNullException(nameof(guard));

    /// <summary>
    /// CLAUDE.md rule 1: injection is opt-in per game and never automatic. The
    /// native guard cannot enforce this — consent lives in the Agent's database
    /// — so it is enforced here, before the guard is even asked.
    /// </summary>
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

        return await _guard.GuardedInjectAsync(request.TargetPid, request.PayloadPath, ct).ConfigureAwait(false);
    }

    /// <summary>
    /// The in-session re-scan. A refusal here means unhook, not merely
    /// "do not start" — <c>19_SAFETY</c> calls this the single most important
    /// runtime behaviour in the whole capture layer.
    /// </summary>
    public async ValueTask<bool> ShouldUnhookAsync(int targetPid, CancellationToken ct = default)
    {
        AntiCheatVerdict verdict = await _guard.EvaluateAsync(targetPid, ct).ConfigureAwait(false);
        return !verdict.IsAllowed;
    }
}
