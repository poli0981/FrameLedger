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
/// only the questions the guard answers.
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

    /// <summary>
    /// Launch mode (P1 item 2). Wait — up to <paramref name="timeoutMs"/> — until the target has mapped a
    /// presentation runtime, then run every check and inject exactly as <see cref="GuardedInjectAsync"/>
    /// does. The wait decides WHEN the guard runs and nothing about whether it passes.
    /// </summary>
    /// <remarks>
    /// <c>20_OPEN_QUESTIONS</c> §S1: a target held at its first instruction has loaded nothing, so the
    /// module scan cannot run before the loader has. The guard therefore polls the loader's own answer
    /// (through the module seam, matching no blocklist) and runs in full the moment a runtime is there. A
    /// target that exits first, or never maps one, refuses with
    /// <see cref="AntiCheatRefusalReason.LaunchTargetExited"/> /
    /// <see cref="AntiCheatRefusalReason.LaunchNoPresentationRuntime"/>. The caller launched and holds
    /// the process; the guard creates and terminates nothing.
    /// </remarks>
    ValueTask<AntiCheatVerdict> GuardedInjectWhenReadyAsync(int targetPid, string payloadPath, int timeoutMs,
        CancellationToken ct = default);

    /// <summary>
    /// Check 4 asked before anything is launched: does this game directory ship
    /// anti-cheat? Answers FR-2.2's question — whether the hooking toggle may be
    /// offered for this title at all.
    /// </summary>
    /// <remarks>
    /// <para>
    /// ADVISORY ONLY. This does not gate injection; <see cref="EvaluateAsync"/>
    /// and <see cref="GuardedInjectAsync"/> run the same scan inside the guard,
    /// against a directory derived from the target's own pid rather than one a
    /// caller named. A caller that skips this question, or lies about the
    /// answer, changes nothing about whether injection is allowed.
    /// </para>
    /// <para>
    /// It lives on this port rather than a second one deliberately. Splitting it
    /// out would have kept <c>NoSecondMatcherTests</c>' method count at two
    /// without that test ever seeing the new surface — the count would have gone
    /// on passing while the thing it guards grew somewhere else.
    /// </para>
    /// </remarks>
    ValueTask<AntiCheatVerdict> PreScanGameDirectoryAsync(string gameDirectory, CancellationToken ct = default);
}
