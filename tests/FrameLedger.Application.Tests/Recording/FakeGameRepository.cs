using FrameLedger.Application.Persistence;
using FrameLedger.Domain.Consent;

namespace FrameLedger.Application.Tests.Recording;

internal sealed class FakeGameRepository : IGameRepository
{
    private long _nextId = 1;

    public Dictionary<string, GameRow> Rows { get; } = new(StringComparer.OrdinalIgnoreCase);

    public List<(long GameId, string Reason, DateTimeOffset At)> Disabled { get; } = [];

    public List<(long GameId, DateTimeOffset At)> Injections { get; } = [];

    public int CrashCount { get; set; }

    public ValueTask<GameRow> EnsureAsync(ExecutableFingerprint fingerprint, string name, CancellationToken ct = default)
    {
        if (!Rows.TryGetValue(fingerprint.ExePath, out GameRow? row))
        {
            row = new GameRow
            {
                Id = _nextId++,
                Name = name,
                Fingerprint = fingerprint,
                HookEnabled = false,
                HookCrashCount = 0,
                AddedAt = DateTimeOffset.UnixEpoch,
                UpdatedAt = DateTimeOffset.UnixEpoch,
            };
            Rows[fingerprint.ExePath] = row;
        }

        return ValueTask.FromResult(row);
    }

    public ValueTask<GameRow?> FindAsync(string normalisedExePath, CancellationToken ct = default) =>
        ValueTask.FromResult(Rows.GetValueOrDefault(normalisedExePath));

    public ValueTask<IReadOnlyList<GameRow>> ListAsync(CancellationToken ct = default) =>
        ValueTask.FromResult<IReadOnlyList<GameRow>>([.. Rows.Values]);

    public ValueTask<bool> AutoDisableHookAsync(long gameId, string reason, DateTimeOffset at, CancellationToken ct = default)
    {
        Disabled.Add((gameId, reason, at));
        return ValueTask.FromResult(true);
    }

    public ValueTask<int> RecordCrashAsync(long gameId, CancellationToken ct = default) => ValueTask.FromResult(++CrashCount);

    public ValueTask<bool> RecordInjectionAsync(long gameId, DateTimeOffset at, CancellationToken ct = default)
    {
        Injections.Add((gameId, at));
        return ValueTask.FromResult(true);
    }
}
