using FrameLedger.Domain.AntiCheat;

namespace FrameLedger.Application.AntiCheat;

/// <summary>
/// The managed view of the anti-cheat guard — a FACADE over the single native
/// implementation, never a second one.
/// </summary>
/// <remarks>
/// <para>
/// <c>20_OPEN_QUESTIONS</c> §S13(a) put the authoritative guard in C++, and
/// §S15 item 1 records the consequence: the moment anything managed matches a
/// blocklist there are two matchers that can disagree, which is a fail-open by
/// construction. So this port exposes no rules, no blocklist and no evidence —
/// only the two questions the guard answers.
/// </para>
/// <para>
/// Note what is absent: there is no <c>Check</c> that yields something a caller
/// then passes to an injector. <see cref="GuardedInjectAsync"/> injects, or
/// refuses. The guard owns the chokepoint (§S13(b)).
/// </para>
/// </remarks>
public interface IAntiCheatGuard
{
    /// <summary>
    /// Run every pre-injection check without injecting. This is the 30 s
    /// in-session re-scan of <c>19_SAFETY</c> §During a session, which must
    /// reach a verdict about a process we are already inside.
    /// </summary>
    ValueTask<AntiCheatVerdict> EvaluateAsync(int targetPid, CancellationToken ct = default);

    /// <summary>
    /// Run every check and, only on a pass, inject. There is no overload that
    /// skips the checks and no way to supply evidence — the guard collects its
    /// own, so a caller can ask but only the guard answers.
    /// </summary>
    ValueTask<AntiCheatVerdict> GuardedInjectAsync(int targetPid, string payloadPath, CancellationToken ct = default);
}
