using FrameLedger.Application.Persistence;
using FrameLedger.Domain.Sessions;

namespace FrameLedger.Application.Recording;

/// <summary>
/// <c>19_SAFETY</c> §Crash &amp; stability safety: two crashes of the same game within 60 s of injection ⇒
/// hooking auto-disabled for that game, the reason on the <c>games</c> row, Tier 2 from then on.
/// </summary>
/// <remarks>
/// <para>
/// <b>Only a crash inside the window counts.</b> A game that crashes an hour in crashed on its own;
/// what this policy watches for is the shape where our injection is the plausible cause, and
/// <c>hook_crash_count</c> counts exactly those. A session with no injection (Tier 2, a refusal) counts
/// nothing, because nothing of ours was in the process.
/// </para>
/// <para>
/// <b>It disables; it never re-enables.</b> Turning hooking back on is the user's action
/// (<c>19_SAFETY</c>: re-enabling silently would mean a rules feed or a policy can switch injection on
/// for a game without anyone looking).
/// </para>
/// </remarks>
public sealed class CrashAutoDisablePolicy
{
    public static readonly TimeSpan Window = TimeSpan.FromSeconds(60);

    public const int CrashesToDisable = 2;

    public const string Reason = "crashed twice within 60 s of injection (19_SAFETY §Crash safety)";

    private readonly IGameRepository _games;

    public CrashAutoDisablePolicy(IGameRepository games) => _games = games ?? throw new ArgumentNullException(nameof(games));

    /// <summary>Whether this session's end is a crash inside the window after <paramref name="injectedAt"/>.</summary>
    public static bool IsEarlyCrash(ExitStatus status, DateTimeOffset? injectedAt, DateTimeOffset endedAt) =>
        status == ExitStatus.Crashed && injectedAt is { } at && endedAt - at <= Window && endedAt >= at;

    /// <summary>
    /// Records the crash when it is an early one, and disables hooking on the second. Returns what was done.
    /// </summary>
    public async ValueTask<CrashPolicyOutcome> ApplyAsync(long gameId, ExitStatus status, DateTimeOffset? injectedAt,
        DateTimeOffset endedAt, CancellationToken ct = default)
    {
        if (!IsEarlyCrash(status, injectedAt, endedAt))
        {
            return CrashPolicyOutcome.NotAnEarlyCrash;
        }

        int count = await _games.RecordCrashAsync(gameId, ct).ConfigureAwait(false);
        if (count < CrashesToDisable)
        {
            return CrashPolicyOutcome.Counted;
        }

        await _games.AutoDisableHookAsync(gameId, Reason, endedAt, ct).ConfigureAwait(false);
        return CrashPolicyOutcome.HookingDisabled;
    }
}
