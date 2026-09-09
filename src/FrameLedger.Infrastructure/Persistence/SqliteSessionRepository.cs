using System.Data.Common;
using Dapper;
using FrameLedger.Application.Persistence;
using Microsoft.Data.Sqlite;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>
/// <c>sessions</c>, <c>session_segments</c>, <c>frame_blobs</c> and <c>sensor_blobs</c>, written together.
/// </summary>
/// <remarks>
/// <c>04_CAPTURE</c> §Finalizing step 5: "write session row + blobs in one SQLite transaction". A failing
/// blob insert leaves no session row behind — the partial record stays on disk for the next recovery,
/// which is the honest state, rather than a row whose blobs never arrived.
/// </remarks>
public sealed class SqliteSessionRepository : ISessionRepository
{
    private const string _insertSegment =
        "INSERT INTO session_segments (session_id, swapchain_id, start_frame, end_frame, render_w, render_h, output_w, output_h, "
        + "upscaler, upscaler_quality, fg_mode, native_fps, displayed_fps, p1_low_fps) VALUES (@id, @SwapchainId, @StartFrame, "
        + "@EndFrame, @RenderW, @RenderH, @OutputW, @OutputH, @Upscaler, @UpscalerQuality, @FgMode, @NativeFps, @DisplayedFps, @P1LowFps)";

    private const string _insertFrames =
        "INSERT INTO frame_blobs (session_id, codec, sample_count, frametimes, frame_flags, frame_index, swapchain_ids, rt_flags, "
        + "render_res, dispatch_rays, pso_created, vram_proc, latency_us) VALUES (@id, @Codec, @SampleCount, @FrameTimes, @FrameFlags, "
        + "@FrameIndex, @SwapchainIds, @RtFlags, @RenderRes, @DispatchRays, @PsoCreated, @VramProc, @LatencyUs)";

    private const string _insertSensor =
        "INSERT INTO sensor_blobs (session_id, series, hz, codec, data) VALUES (@id, @Series, @Hz, @Codec, @Data)";

    private const string _selectFrames =
        "SELECT codec, sample_count, frametimes, frame_flags, frame_index, swapchain_ids, rt_flags, render_res, "
        + "dispatch_rays, pso_created, vram_proc, latency_us FROM frame_blobs WHERE session_id = @sessionId";

    private const string _exists = "SELECT COUNT(*) FROM sessions WHERE session_guid = @guid";

    private const string _sweepFrames =
        "DELETE FROM frame_blobs WHERE session_id IN ("
        + "SELECT id FROM sessions WHERE game_id = @gameId ORDER BY started_at DESC, id DESC LIMIT -1 OFFSET @keep)";

    private const string _sweepSensors =
        "DELETE FROM sensor_blobs WHERE session_id IN ("
        + "SELECT id FROM sessions WHERE game_id = @gameId ORDER BY started_at DESC, id DESC LIMIT -1 OFFSET @keep)";

    private readonly LedgerDatabase _db;

    public SqliteSessionRepository(LedgerDatabase db) => _db = db ?? throw new ArgumentNullException(nameof(db));

    public ValueTask<long> InsertFinalizedAsync(FinalizedSession session, CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(session);
        return _db.WriteAsync(async (c, tx, token) =>
        {
            if (await ExistsInAsync(c, tx, session.Row.SessionGuid, token).ConfigureAwait(false))
            {
                throw new InvalidOperationException($"session {session.Row.SessionGuid:D} is already stored");
            }

            long id = await c.ExecuteScalarAsync<long>(new CommandDefinition(
                SessionRowColumns.Insert, SessionRowColumns.Parameters(session.Row), tx, cancellationToken: token)).ConfigureAwait(false);
            await InsertSegmentsAsync(c, tx, id, session.Segments, token).ConfigureAwait(false);
            if (session.Frames is FrameBlobs frames)
            {
                await InsertFramesAsync(c, tx, id, frames, token).ConfigureAwait(false);
            }

            await InsertSensorsAsync(c, tx, id, session.Sensors, token).ConfigureAwait(false);
            return id;
        }, ct);
    }

    public ValueTask<bool> ExistsAsync(Guid sessionGuid, CancellationToken ct = default) =>
        _db.ReadAsync((c, token) => ExistsInAsync(c, null, sessionGuid, token), ct);

    public ValueTask<SessionRow?> FindAsync(Guid sessionGuid, CancellationToken ct = default) =>
        _db.ReadAsync((c, token) => SqliteReaders.ReadOneAsync(
            c,
            new CommandDefinition(SessionRowColumns.Select + " WHERE session_guid = @guid", new { guid = sessionGuid.ToString("D") }, cancellationToken: token),
            SessionRowColumns.Read), ct);

    public ValueTask<IReadOnlyList<SessionRow>> ListRecentAsync(int count, CancellationToken ct = default)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(count);
        return _db.ReadAsync((c, token) => SqliteReaders.ReadAllAsync(
            c,
            new CommandDefinition(SessionRowColumns.Select + " ORDER BY started_at DESC, id DESC LIMIT @count", new { count }, cancellationToken: token),
            SessionRowColumns.Read), ct);
    }

    public ValueTask<int> SweepRetentionAsync(long gameId, int keep, CancellationToken ct = default)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(keep);
        return _db.WriteAsync(async (c, tx, token) =>
        {
            // Aggregates and segments are kept forever; only the raw series go (06_DATA_MODEL §Retention).
            int frames = await c.ExecuteAsync(new CommandDefinition(_sweepFrames, new { gameId, keep }, tx, cancellationToken: token)).ConfigureAwait(false);
            await c.ExecuteAsync(new CommandDefinition(_sweepSensors, new { gameId, keep }, tx, cancellationToken: token)).ConfigureAwait(false);
            return frames;
        }, ct);
    }

    public ValueTask<FrameBlobs?> FindFramesAsync(long sessionId, CancellationToken ct = default) =>
        _db.ReadAsync((c, token) => SqliteReaders.ReadOneAsync(
            c, new CommandDefinition(_selectFrames, new { sessionId }, cancellationToken: token), ReadFrames), ct);

    private static FrameBlobs ReadFrames(DbDataReader r) => new()
    {
        Codec = r.GetString(0),
        SampleCount = r.GetInt64(1),
        FrameTimes = (byte[])r.GetValue(2),
        FrameFlags = (byte[])r.GetValue(3),
        FrameIndex = SqliteReaders.Blob(r, 4),
        SwapchainIds = SqliteReaders.Blob(r, 5),
        RtFlags = SqliteReaders.Blob(r, 6),
        RenderRes = SqliteReaders.Blob(r, 7),
        DispatchRays = SqliteReaders.Blob(r, 8),
        PsoCreated = SqliteReaders.Blob(r, 9),
        VramProc = SqliteReaders.Blob(r, 10),
        LatencyUs = SqliteReaders.Blob(r, 11),
    };

    private static async Task<bool> ExistsInAsync(SqliteConnection c, SqliteTransaction? tx, Guid guid, CancellationToken ct) =>
        await c.ExecuteScalarAsync<long>(new CommandDefinition(_exists, new { guid = guid.ToString("D") }, tx, cancellationToken: ct))
            .ConfigureAwait(false) > 0;

    private static async Task InsertSegmentsAsync(SqliteConnection c, SqliteTransaction tx, long id, IReadOnlyList<SegmentRow> segments, CancellationToken ct)
    {
        foreach (SegmentRow seg in segments)
        {
            var p = new
            {
                id,
                seg.SwapchainId,
                seg.StartFrame,
                seg.EndFrame,
                seg.RenderW,
                seg.RenderH,
                seg.OutputW,
                seg.OutputH,
                seg.Upscaler,
                seg.UpscalerQuality,
                seg.FgMode,
                seg.NativeFps,
                seg.DisplayedFps,
                seg.P1LowFps,
            };
            await c.ExecuteAsync(new CommandDefinition(_insertSegment, p, tx, cancellationToken: ct)).ConfigureAwait(false);
        }
    }

    private static async Task InsertFramesAsync(SqliteConnection c, SqliteTransaction tx, long id, FrameBlobs f, CancellationToken ct)
    {
        // Explicit DBNull per optional series: "this series was not measured" must land as NULL, never as an
        // empty blob (06_DATA_MODEL: NULL is N/A, never a zero), and a typed SqliteParameter says so exactly.
        SqliteCommand cmd = c.CreateCommand();
        await using (cmd.ConfigureAwait(false))
        {
            cmd.Transaction = tx;
            cmd.CommandText = _insertFrames;
            cmd.Parameters.AddWithValue("@id", id);
            cmd.Parameters.AddWithValue("@Codec", f.Codec);
            cmd.Parameters.AddWithValue("@SampleCount", f.SampleCount);
            cmd.Parameters.AddWithValue("@FrameTimes", f.FrameTimes.ToArray());
            cmd.Parameters.AddWithValue("@FrameFlags", f.FrameFlags.ToArray());
            cmd.Parameters.AddWithValue("@FrameIndex", Optional(f.FrameIndex));
            cmd.Parameters.AddWithValue("@SwapchainIds", Optional(f.SwapchainIds));
            cmd.Parameters.AddWithValue("@RtFlags", Optional(f.RtFlags));
            cmd.Parameters.AddWithValue("@RenderRes", Optional(f.RenderRes));
            cmd.Parameters.AddWithValue("@DispatchRays", Optional(f.DispatchRays));
            cmd.Parameters.AddWithValue("@PsoCreated", Optional(f.PsoCreated));
            cmd.Parameters.AddWithValue("@VramProc", Optional(f.VramProc));
            cmd.Parameters.AddWithValue("@LatencyUs", Optional(f.LatencyUs));
            await cmd.ExecuteNonQueryAsync(ct).ConfigureAwait(false);
        }
    }

    private static object Optional(ReadOnlyMemory<byte>? blob) => blob is { } b ? b.ToArray() : DBNull.Value;

    private static async Task InsertSensorsAsync(SqliteConnection c, SqliteTransaction tx, long id, IReadOnlyList<SensorBlob> sensors, CancellationToken ct)
    {
        foreach (SensorBlob sensor in sensors)
        {
            var p = new { id, sensor.Series, sensor.Hz, sensor.Codec, Data = sensor.Data.ToArray() };
            await c.ExecuteAsync(new CommandDefinition(_insertSensor, p, tx, cancellationToken: ct)).ConfigureAwait(false);
        }
    }
}
