using FrameLedger.Application.Persistence;

namespace FrameLedger.Application.Tests.Recording;

internal sealed class FakeSessionRepository : ISessionRepository
{
    public List<FinalizedSession> Stored { get; } = [];

    public List<(long GameId, int Keep)> Sweeps { get; } = [];

    public HashSet<Guid> PreExisting { get; } = [];

    public int SweptPerCall { get; set; }

    public ValueTask<long> InsertFinalizedAsync(FinalizedSession session, CancellationToken ct = default)
    {
        if (PreExisting.Contains(session.Row.SessionGuid) || Stored.Any(s => s.Row.SessionGuid == session.Row.SessionGuid))
        {
            throw new InvalidOperationException("session_guid already stored");
        }

        Stored.Add(session);
        return ValueTask.FromResult((long)Stored.Count);
    }

    public ValueTask<bool> ExistsAsync(Guid sessionGuid, CancellationToken ct = default) =>
        ValueTask.FromResult(PreExisting.Contains(sessionGuid) || Stored.Any(s => s.Row.SessionGuid == sessionGuid));

    public ValueTask<SessionRow?> FindAsync(Guid sessionGuid, CancellationToken ct = default) =>
        ValueTask.FromResult(Stored.Select(s => s.Row).FirstOrDefault(r => r.SessionGuid == sessionGuid));

    public ValueTask<IReadOnlyList<SessionRow>> ListRecentAsync(int count, CancellationToken ct = default) =>
        ValueTask.FromResult<IReadOnlyList<SessionRow>>([.. Stored.Select(s => s.Row).TakeLast(count).Reverse()]);

    public ValueTask<int> SweepRetentionAsync(long gameId, int keep, CancellationToken ct = default)
    {
        Sweeps.Add((gameId, keep));
        return ValueTask.FromResult(SweptPerCall);
    }

    public ValueTask<FrameBlobs?> FindFramesAsync(long sessionId, CancellationToken ct = default) =>
        ValueTask.FromResult(sessionId >= 1 && sessionId <= Stored.Count ? Stored[(int)sessionId - 1].Frames : null);
}
