namespace FrameLedger.Application.Persistence;

/// <summary>
/// <c>sessions</c> and its blobs. <c>04_CAPTURE</c> §Finalizing step 5: the row, the segments and the blobs
/// land in ONE transaction, or none of them do.
/// </summary>
public interface ISessionRepository
{
    /// <summary>Inserts everything, atomically; returns the new <c>sessions.id</c>.</summary>
    /// <exception cref="InvalidOperationException">The <c>session_guid</c> is already stored.</exception>
    ValueTask<long> InsertFinalizedAsync(FinalizedSession session, CancellationToken ct = default);

    /// <summary>Whether a session with this guid is already stored — the recovery path's first question.</summary>
    ValueTask<bool> ExistsAsync(Guid sessionGuid, CancellationToken ct = default);

    ValueTask<SessionRow?> FindAsync(Guid sessionGuid, CancellationToken ct = default);

    /// <summary>The newest sessions, all games, newest first.</summary>
    ValueTask<IReadOnlyList<SessionRow>> ListRecentAsync(int count, CancellationToken ct = default);

    /// <summary>
    /// <c>06_DATA_MODEL</c> §Retention: raw blobs are kept for the last <paramref name="keep"/> sessions of the
    /// game; aggregates and segments are kept forever. Returns how many sessions lost their blobs.
    /// </summary>
    ValueTask<int> SweepRetentionAsync(long gameId, int keep, CancellationToken ct = default);

    /// <summary>The <c>frame_blobs</c> row, or null when retention swept it or nothing was stored.</summary>
    ValueTask<FrameBlobs?> FindFramesAsync(long sessionId, CancellationToken ct = default);
}
