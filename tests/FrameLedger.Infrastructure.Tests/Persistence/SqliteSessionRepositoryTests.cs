using Dapper;
using FluentAssertions;
using FrameLedger.Application.Persistence;
using FrameLedger.Domain.Consent;
using FrameLedger.Domain.Sessions;
using FrameLedger.Infrastructure.Blobs;
using FrameLedger.Infrastructure.Persistence;

namespace FrameLedger.Infrastructure.Tests.Persistence;

/// <summary>
/// The session row, its segments and its blobs land in ONE transaction; a guid is stored once; retention keeps
/// the last N sessions' raw series and every aggregate.
/// </summary>
public sealed class SqliteSessionRepositoryTests
{
    private static CancellationToken Ct => TestContext.Current.CancellationToken;

    private static readonly ExecutableFingerprint _game = new() { ExePath = @"C:\Games\T\t.exe", SizeBytes = 1, MtimeUnixMs = 2 };

    private static async Task<(long GameId, long SnapshotId)> SeedAsync(LedgerFixture f)
    {
        long gameId = (await new SqliteGameRepository(f.Db).EnsureAsync(_game, "T", Ct).ConfigureAwait(false)).Id;
        long snapshotId = await new SqliteHardwareSnapshotRepository(f.Db).EnsureAsync(
            new HardwareSnapshot { GpuName = "RTX 5080", CpuName = "i7-14700KF" }, DateTimeOffset.UnixEpoch, Ct).ConfigureAwait(false);
        return (gameId, snapshotId);
    }

    private static SessionRow Row(long gameId, long snapshotId, Guid? guid = null, DateTimeOffset? started = null) => new()
    {
        SessionGuid = guid ?? Guid.NewGuid(),
        GameId = gameId,
        SnapshotId = snapshotId,
        StartedAt = started ?? DateTimeOffset.FromUnixTimeMilliseconds(1_700_000_000_000),
        EndedAt = (started ?? DateTimeOffset.FromUnixTimeMilliseconds(1_700_000_000_000)).AddSeconds(90),
        QpcEpoch = 123_456_789_012,
        QpcFrequency = 10_000_000,
        Tier = CaptureTier.Hooked,
        Mode = CaptureMode.Launch,
        ExitStatus = ExitStatus.Normal,
        FrameCount = 5400,
        AppFrameCount = 5400,
        DisplayedFrameCount = 5400,
        DroppedFrames = 0,
        Upscaler = "dlss",
        FgMode = "none",
        FgSource = "none",
        FgFactor = 1.0,
        RtFlag = "yes",
        RtSource = "measured",
        NativeFps = 60.0,
        MedianFps = 60.5,
        P1LowFps = 48.2,
        ReflexActive = true,
    };

    private static FrameBlobs Frames(int n) => new()
    {
        Codec = SeriesCodec.Tag,
        SampleCount = n,
        FrameTimes = SeriesCodec.EncodeFloat32([.. Enumerable.Repeat(16.6f, n)]),
        FrameFlags = SeriesCodec.EncodeBytes(new byte[n]),
        LatencyUs = SeriesCodec.EncodeUInt32([.. Enumerable.Repeat(12_000u, n)]),
    };

    [Fact]
    public async Task ASessionRoundTripsColumnForColumn()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        (long gameId, long snapshotId) = await SeedAsync(f);
        var repo = new SqliteSessionRepository(f.Db);
        SessionRow row = Row(gameId, snapshotId);

        long id = await repo.InsertFinalizedAsync(new FinalizedSession
        {
            Row = row,
            Segments = [new SegmentRow { SwapchainId = 1, StartFrame = 0, EndFrame = 5399, RenderW = 1485, RenderH = 835, OutputW = 2560, OutputH = 1440, Upscaler = "dlss", NativeFps = 60 }],
            Frames = Frames(5400),
            Sensors = [new SensorBlob { Series = "gpu_temp", Hz = 1, Codec = SeriesCodec.Tag, Data = SeriesCodec.EncodeFloat32([60f, 61f, 62f]) }],
        }, Ct);

        SessionRow back = (await repo.FindAsync(row.SessionGuid, Ct))!;
        back.Should().BeEquivalentTo(row with { Id = id });
        (await repo.ExistsAsync(row.SessionGuid, Ct)).Should().BeTrue();
        (await repo.ListRecentAsync(10, Ct)).Should().ContainSingle().Which.SessionGuid.Should().Be(row.SessionGuid);

        FrameBlobs frames = (await repo.FindFramesAsync(id, Ct))!;
        frames.SampleCount.Should().Be(5400);
        SeriesCodec.DecodeFloat32(frames.FrameTimes.ToArray()).Should().HaveCount(5400).And.OnlyContain(t => t == 16.6f);
        SeriesCodec.DecodeUInt32(frames.LatencyUs!.Value.ToArray()).Should().OnlyContain(v => v == 12_000u);
        string? kinds = await f.Db.ReadAsync((c, ct) => c.ExecuteScalarAsync<string?>(new CommandDefinition(
            "SELECT typeof(render_res) || '/' || typeof(latency_us) || '/' || typeof(frame_index) FROM frame_blobs", cancellationToken: ct)), Ct);
        kinds.Should().Be("null/blob/null", "an optional series that was not measured is stored as NULL");
        frames.RenderRes.HasValue.Should().BeFalse("nothing varied, so the column stayed NULL");
        frames.LatencyUs.HasValue.Should().BeTrue();

        // unix-ms on the row itself, and the tri-state text is what 06_DATA_MODEL says.
        (long startedAt, string? fg, string? rt) = await f.Db.ReadAsync(async (c, ct) => (
            await c.ExecuteScalarAsync<long>(new CommandDefinition("SELECT started_at FROM sessions", cancellationToken: ct)).ConfigureAwait(false),
            await c.ExecuteScalarAsync<string?>(new CommandDefinition("SELECT fg_mode FROM sessions", cancellationToken: ct)).ConfigureAwait(false),
            await c.ExecuteScalarAsync<string?>(new CommandDefinition("SELECT rt_flag FROM sessions", cancellationToken: ct)).ConfigureAwait(false)), Ct);
        startedAt.Should().Be(1_700_000_000_000);
        fg.Should().Be("none");
        rt.Should().Be("yes");
    }

    [Fact]
    public async Task ATierTwoSessionStoresTheHeaderAndNullEverywhereElse()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        (long gameId, long snapshotId) = await SeedAsync(f);
        var repo = new SqliteSessionRepository(f.Db);
        var row = new SessionRow
        {
            SessionGuid = Guid.NewGuid(),
            GameId = gameId,
            SnapshotId = snapshotId,
            StartedAt = DateTimeOffset.UnixEpoch,
            EndedAt = DateTimeOffset.UnixEpoch.AddSeconds(120),
            QpcEpoch = 0,
            QpcFrequency = 10_000_000,
            Tier = CaptureTier.NotHooked,
            Mode = CaptureMode.Attach,
            ExitStatus = ExitStatus.Normal,
            CaptureNotes = "RefusedHookNotEnabled",
        };

        await repo.InsertFinalizedAsync(new FinalizedSession { Row = row }, Ct);

        SessionRow back = (await repo.FindAsync(row.SessionGuid, Ct))!;
        back.Tier.Should().Be(CaptureTier.NotHooked);
        back.NativeFps.Should().BeNull();
        back.FgFactor.Should().BeNull();
        back.FgMode.Should().Be("na", "the DEFAULT is na, never none");
        back.RtFlag.Should().Be("na");
        back.Upscaler.Should().BeNull();
        back.CaptureNotes.Should().Be("RefusedHookNotEnabled");
        (await repo.FindFramesAsync(back.Id, Ct)).Should().BeNull();
    }

    [Fact]
    public async Task AFailingBlobInsertLeavesNoSessionRowBehind()
    {
        // 04_CAPTURE §Finalizing step 5, the whole point: two sensor blobs with the same series violate the
        // primary key, and the row inserted a moment earlier must roll back with them.
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        (long gameId, long snapshotId) = await SeedAsync(f);
        var repo = new SqliteSessionRepository(f.Db);
        SessionRow row = Row(gameId, snapshotId);
        SensorBlob blob = new() { Series = "gpu_temp", Hz = 1, Codec = SeriesCodec.Tag, Data = SeriesCodec.EncodeFloat32([1f]) };

        Func<Task> insert = async () => await repo.InsertFinalizedAsync(new FinalizedSession { Row = row, Sensors = [blob, blob] }, Ct).ConfigureAwait(false);

        await insert.Should().ThrowAsync<Microsoft.Data.Sqlite.SqliteException>();
        (await repo.ExistsAsync(row.SessionGuid, Ct)).Should().BeFalse("the transaction rolled back whole");
        long rows = await f.Db.ReadAsync((c, ct) => c.ExecuteScalarAsync<long>(new CommandDefinition("SELECT COUNT(*) FROM sessions", cancellationToken: ct)), Ct);
        rows.Should().Be(0);
    }

    [Fact]
    public async Task AGuidIsStoredOnce()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        (long gameId, long snapshotId) = await SeedAsync(f);
        var repo = new SqliteSessionRepository(f.Db);
        SessionRow row = Row(gameId, snapshotId);
        await repo.InsertFinalizedAsync(new FinalizedSession { Row = row }, Ct);

        Func<Task> again = async () => await repo.InsertFinalizedAsync(new FinalizedSession { Row = row }, Ct).ConfigureAwait(false);

        await again.Should().ThrowAsync<InvalidOperationException>("the recovery path asks ExistsAsync first, and the store refuses regardless");
    }

    [Fact]
    public async Task RetentionKeepsTheLastNSessionsRawSeriesAndEveryAggregate()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        (long gameId, long snapshotId) = await SeedAsync(f);
        var repo = new SqliteSessionRepository(f.Db);
        var ids = new List<long>();
        for (int i = 0; i < 5; i++)
        {
            ids.Add(await repo.InsertFinalizedAsync(new FinalizedSession
            {
                Row = Row(gameId, snapshotId, started: DateTimeOffset.UnixEpoch.AddHours(i)),
                Frames = Frames(10),
                Sensors = [new SensorBlob { Series = "gpu_temp", Hz = 1, Codec = SeriesCodec.Tag, Data = SeriesCodec.EncodeFloat32([1f]) }],
            }, Ct));
        }

        int swept = await repo.SweepRetentionAsync(gameId, keep: 2, Ct);

        swept.Should().Be(3);
        (await repo.FindFramesAsync(ids[0], Ct)).Should().BeNull("the oldest lost its raw series");
        (await repo.FindFramesAsync(ids[4], Ct)).Should().NotBeNull("the newest kept its raw series");
        (await repo.ListRecentAsync(10, Ct)).Should().HaveCount(5, "aggregates and segments are kept forever");
        long sensors = await f.Db.ReadAsync((c, ct) => c.ExecuteScalarAsync<long>(new CommandDefinition("SELECT COUNT(*) FROM sensor_blobs", cancellationToken: ct)), Ct);
        sensors.Should().Be(2);
    }

    [Fact]
    public async Task ListRecentIsNewestFirstAndBounded()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        (long gameId, long snapshotId) = await SeedAsync(f);
        var repo = new SqliteSessionRepository(f.Db);
        for (int i = 0; i < 3; i++)
        {
            await repo.InsertFinalizedAsync(new FinalizedSession { Row = Row(gameId, snapshotId, started: DateTimeOffset.UnixEpoch.AddHours(i)) }, Ct);
        }

        IReadOnlyList<SessionRow> recent = await repo.ListRecentAsync(2, Ct);

        recent.Should().HaveCount(2);
        recent[0].StartedAt.Should().BeAfter(recent[1].StartedAt);
    }

    [Fact]
    public async Task HardwareSnapshotsAreDeduplicatedByHash()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        var repo = new SqliteHardwareSnapshotRepository(f.Db);
        var a = new HardwareSnapshot { GpuName = "RTX 5080", GpuDriver = "616.64", RamGb = 32 };
        var b = new HardwareSnapshot { GpuName = " rtx 5080 ", GpuDriver = "616.64", RamGb = 32 };
        var c = new HardwareSnapshot { GpuName = "RTX 5080", GpuDriver = "616.88", RamGb = 32 };

        long idA = await repo.EnsureAsync(a, DateTimeOffset.UnixEpoch, Ct);
        long idB = await repo.EnsureAsync(b, DateTimeOffset.UnixEpoch.AddDays(1), Ct);
        long idC = await repo.EnsureAsync(c, DateTimeOffset.UnixEpoch.AddDays(2), Ct);

        idB.Should().Be(idA, "the hash is over normalised fields — trimmed and case-folded");
        idC.Should().NotBe(idA, "a driver update is a different machine state (FR-6.3)");
        (await repo.FindAsync(idA, Ct))!.GpuName.Should().Be("RTX 5080");
        (await repo.FindAsync(999, Ct)).Should().BeNull();
    }

    [Fact]
    public async Task TheGameRepositoryRecordsCrashesInjectionsAndTheAutoDisable()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        var games = new SqliteGameRepository(f.Db);
        GameRow row = await games.EnsureAsync(_game, "T", Ct);
        row.HookEnabled.Should().BeFalse("hooking is off for every newly added game");
        (await games.EnsureAsync(_game, "T-again", Ct)).Id.Should().Be(row.Id, "one row per path");

        (await games.RecordInjectionAsync(row.Id, DateTimeOffset.UnixEpoch.AddSeconds(5), Ct)).Should().BeTrue();
        (await games.RecordCrashAsync(row.Id, Ct)).Should().Be(1);
        (await games.RecordCrashAsync(row.Id, Ct)).Should().Be(2);
        (await games.AutoDisableHookAsync(row.Id, "crashed twice within 60 s of injection", DateTimeOffset.UnixEpoch.AddSeconds(9), Ct)).Should().BeTrue();

        GameRow after = (await games.FindAsync(_game.ExePath, Ct))!;
        after.HookCrashCount.Should().Be(2);
        after.HookEnabled.Should().BeFalse();
        after.HookAutoDisabledReason.Should().Contain("twice");
        after.HookLastInjectedAt.Should().Be(DateTimeOffset.UnixEpoch.AddSeconds(5));
        (await games.AutoDisableHookAsync(999, "nobody", DateTimeOffset.UnixEpoch, Ct)).Should().BeFalse();
    }

    [Fact]
    public async Task SettingsAndLegalAcceptanceReadWhatWasWritten()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        var settings = new SqliteSettingsStore(f.Db);
        var legal = new SqliteLegalAcceptanceStore(f.Db);

        (await settings.GetAsync("hooking.kill_switch", Ct)).Should().BeNull();
        await settings.SetAsync("hooking.kill_switch", "1", Ct);
        await settings.SetAsync("hooking.kill_switch", "0", Ct);
        (await settings.GetAsync("hooking.kill_switch", Ct)).Should().Be("0", "the second write replaces the first");

        (await legal.FindAsync("EULA", Ct)).Should().BeNull("nothing in the Agent writes legal_acceptance; the UI does");
        await f.Db.WriteAsync((c, tx, ct) => c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO legal_acceptance (doc, version, accepted_at) VALUES ('EULA', '2026-09', 1700000000000)", transaction: tx, cancellationToken: ct)), Ct);
        LegalAcceptance accepted = (await legal.FindAsync("EULA", Ct))!;
        accepted.Version.Should().Be("2026-09");
        accepted.AcceptedAt.Should().Be(DateTimeOffset.FromUnixTimeMilliseconds(1_700_000_000_000));
    }
}
