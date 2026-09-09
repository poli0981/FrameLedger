using FrameLedger.Domain.Consent;

namespace FrameLedger.Application.Persistence;

/// <summary>
/// The <c>games</c> table minus its consent columns (those are <c>Consent.IGameConsentStore</c>'s, and
/// nothing here can set <c>hook_enabled</c> to 1 or stamp <c>hook_consent_at</c>).
/// </summary>
/// <remarks>
/// No method takes a database path, for the reason <c>IRulesStore</c> gives: a port with a path parameter
/// is how the location becomes selectable again.
/// </remarks>
public interface IGameRepository
{
    /// <summary>The row for <paramref name="fingerprint"/>'s path, created with hooking OFF when absent.</summary>
    ValueTask<GameRow> EnsureAsync(ExecutableFingerprint fingerprint, string name, CancellationToken ct = default);

    ValueTask<GameRow?> FindAsync(string normalisedExePath, CancellationToken ct = default);

    ValueTask<IReadOnlyList<GameRow>> ListAsync(CancellationToken ct = default);

    /// <summary>
    /// <c>04_CAPTURE</c> §Crash &amp; exit classification: two crashes within 60 s of injection ⇒ hooking is
    /// auto-disabled, with the reason on the row for the UI to explain and offer a manual re-enable.
    /// </summary>
    ValueTask<bool> AutoDisableHookAsync(long gameId, string reason, DateTimeOffset at, CancellationToken ct = default);

    /// <summary>Bumps <c>hook_crash_count</c>; returns the new count.</summary>
    ValueTask<int> RecordCrashAsync(long gameId, CancellationToken ct = default);

    ValueTask<bool> RecordInjectionAsync(long gameId, DateTimeOffset at, CancellationToken ct = default);
}
