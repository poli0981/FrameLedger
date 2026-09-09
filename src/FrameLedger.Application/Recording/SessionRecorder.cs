using FrameLedger.Application.Capture;
using FrameLedger.Application.Persistence;
using FrameLedger.Application.Telemetry;
using FrameLedger.Domain.Consent;
using FrameLedger.Domain.Sessions;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>
/// One session, end to end (<c>04_CAPTURE</c> §Session recorder): the row's identity and time base, the
/// game and hardware rows, the <c>.partial</c> from before the first record to after the last, the loop,
/// the exit classification, the finalize, the crash policy.
/// </summary>
/// <remarks>
/// <para>
/// <b>The state machine as built.</b> The loop owns <c>Guarded → Injecting → Capturing</c> inside
/// <c>CaptureSession</c> and reports them as one outcome, so the recorder observes: <c>Started</c> (the
/// file exists), <c>Attached</c> (the ring is ours — Tier 1 from here), <c>Ended</c> (with the loop's
/// reason), then <c>Saved</c> / <c>Discarded</c>; a loop that never attached is Tier 2 with the reason.
/// Each transition is a note in the file, which is the breadcrumb <c>19_SAFETY</c> §Crash safety wants
/// written "before injection".
/// </para>
/// <para>
/// <b>Everything here runs on the loop's task.</b> The observer's tick is where the telemetry queue is
/// drained and the <c>.partial</c> flushed; the poller has its own thread and the loop is the only
/// reader of the ring (<c>04_CAPTURE</c> §Threading model).
/// </para>
/// </remarks>
public sealed class SessionRecorder
{
    private readonly ICaptureSessionFactory _sessions;
    private readonly IGameRepository _games;
    private readonly IHardwareSnapshotRepository _snapshots;
    private readonly IHardwareSnapshotSource _hardware;
    private readonly IPartialSessionStore _partials;
    private readonly SessionFinalizer _finalizer;
    private readonly ICrashEventSource _crashes;
    private readonly CrashAutoDisablePolicy _crashPolicy;
    private readonly Func<ITelemetryPoller?> _pollers;
    private readonly TimeProvider _clock;
    private readonly RecorderOptions _options;

    public SessionRecorder(ICaptureSessionFactory sessions, IGameRepository games, IHardwareSnapshotRepository snapshots,
        IHardwareSnapshotSource hardware, IPartialSessionStore partials, SessionFinalizer finalizer, ICrashEventSource crashes,
        Func<ITelemetryPoller?> pollers, TimeProvider clock, RecorderOptions? options = null)
    {
        _sessions = sessions ?? throw new ArgumentNullException(nameof(sessions));
        _games = games ?? throw new ArgumentNullException(nameof(games));
        _snapshots = snapshots ?? throw new ArgumentNullException(nameof(snapshots));
        _hardware = hardware ?? throw new ArgumentNullException(nameof(hardware));
        _partials = partials ?? throw new ArgumentNullException(nameof(partials));
        _finalizer = finalizer ?? throw new ArgumentNullException(nameof(finalizer));
        _crashes = crashes ?? throw new ArgumentNullException(nameof(crashes));
        _pollers = pollers ?? throw new ArgumentNullException(nameof(pollers));
        _clock = clock ?? throw new ArgumentNullException(nameof(clock));
        _crashPolicy = new CrashAutoDisablePolicy(games);
        _options = options ?? new RecorderOptions();
    }

    public async Task<RecordedSession> RecordAsync(RecordRequest request, CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        DateTimeOffset startedAt = _clock.GetUtcNow();
        long qpcEpoch = _clock.GetTimestamp();
        var guid = Guid.NewGuid();

        ExecutableFingerprint fingerprint = request.Observed ?? new ExecutableFingerprint { ExePath = request.NormalisedExePath, SizeBytes = 0, MtimeUnixMs = 0 };
        GameRow game = await _games.EnsureAsync(fingerprint, request.GameName ?? Path.GetFileNameWithoutExtension(request.NormalisedExePath), ct).ConfigureAwait(false);
        long snapshotId = await _snapshots.EnsureAsync(_hardware.Take(), startedAt, ct).ConfigureAwait(false);

        ITelemetryPoller? poller = _pollers();
        try
        {
            poller?.Start();
            var header = new PartialHeader
            {
                SessionGuid = guid,
                StartedAt = startedAt,
                QpcEpoch = (ulong)qpcEpoch,
                QpcFrequency = _clock.TimestampFrequency,
                GameId = game.Id,
                SnapshotId = snapshotId,
                ExePath = request.NormalisedExePath,
                Tier = CaptureTier.NotHooked,
                Mode = request.Mode,
                TelemetryDescriptor = poller?.Descriptor,
            };

            using var writer = new PartialSessionWriter(_partials.Create(header), _clock, _options.PartialFlushInterval);
            var run = new Run(writer, poller, _clock);
            writer.Note("started " + request.Mode);
            CaptureOutcome outcome = await RunLoopAsync(request, run, ct).ConfigureAwait(false);
            DateTimeOffset endedAt = _clock.GetUtcNow();
            run.FinalFlush();
            writer.Note("ended " + outcome.Reason);

            return await FinalizeAsync(request, game, header, outcome, run, endedAt, writer, ct).ConfigureAwait(false);
        }
        finally
        {
            poller?.Dispose();
        }
    }

    private async Task<CaptureOutcome> RunLoopAsync(RecordRequest request, Run run, CancellationToken ct)
    {
        CaptureSession session = _sessions.Create(run);
        return request.Mode == CaptureMode.Launch
            ? await session.RunLaunchedAsync(request.NormalisedExePath, request.Observed, request.PayloadPath, request.Arguments, ct).ConfigureAwait(false)
            : await session.RunAsync(request.NormalisedExePath, request.Observed, request.PayloadPath, ct).ConfigureAwait(false);
    }

    private async Task<RecordedSession> FinalizeAsync(RecordRequest request, GameRow game, PartialHeader header, CaptureOutcome outcome,
        Run run, DateTimeOffset endedAt, PartialSessionWriter writer, CancellationToken ct)
    {
        bool hooked = outcome.AttachRefusal == ShmAttachRefusal.Ok;
        bool hadPid = hooked || outcome.TargetPid != 0;
        bool crashEvent = hadPid && _crashes.FoundCrash(Path.GetFileName(request.NormalisedExePath), header.StartedAt, endedAt + ExitStatusMapper.CrashWitnessGrace);
        ExitStatus exit = ExitStatusMapper.Map(outcome.Reason, outcome.ExitCode, crashEvent);

        SessionRow skeleton = Skeleton(header, outcome, endedAt, exit, crashEvent, hooked);
        var input = new FinalizeInput
        {
            Skeleton = skeleton,
            Hooked = hooked ? Hooked(outcome, header.QpcFrequency) : null,
            Sensors = writer.Sensors,
            RetentionKeep = _options.RetentionKeep,
            MinimumSessionLength = _options.MinimumSessionLength,
        };
        FinalizedSession built = _finalizer.Build(input);

        writer.Note("finalizing " + exit);
        FinalizeOutcome saved = await _finalizer.FinalizeAsync(input, ct).ConfigureAwait(false);
        writer.Dispose();
        _partials.Delete(header.SessionGuid);

        CrashPolicyOutcome crash = CrashPolicyOutcome.NotAnEarlyCrash;
        if (hooked)
        {
            await _games.RecordInjectionAsync(game.Id, run.AttachedAt ?? header.StartedAt, ct).ConfigureAwait(false);
            crash = await _crashPolicy.ApplyAsync(game.Id, exit, run.AttachedAt, endedAt, ct).ConfigureAwait(false);
        }

        return new RecordedSession
        {
            SessionGuid = header.SessionGuid,
            Outcome = outcome,
            Row = built.Row with { Id = saved.SessionId ?? 0 },
            ExitStatus = exit,
            Finalize = saved,
            CrashPolicy = crash,
            CrashEventFound = crashEvent,
        };
    }

    private static SessionRow Skeleton(PartialHeader h, CaptureOutcome o, DateTimeOffset endedAt, ExitStatus exit, bool crashEvent, bool hooked)
    {
        string notes = ExitStatusMapper.Describe(o.Reason, o.ExitCode, crashEvent);
        if (!hooked)
        {
            notes += "; tier2: attach=" + o.AttachRefusal;
            if (o.Verdict.Reason != Domain.AntiCheat.AntiCheatRefusalReason.Allow)
            {
                notes += "; guard=" + o.Verdict.Reason + (string.IsNullOrEmpty(o.Verdict.Family) ? "" : "/" + o.Verdict.Family)
                         + (string.IsNullOrEmpty(o.Verdict.Signal) ? "" : "/" + o.Verdict.Signal);
            }
        }

        return new SessionRow
        {
            SessionGuid = h.SessionGuid,
            GameId = h.GameId,
            SnapshotId = h.SnapshotId,
            StartedAt = h.StartedAt,
            EndedAt = endedAt,
            QpcEpoch = h.QpcEpoch,
            QpcFrequency = h.QpcFrequency,
            Tier = hooked ? CaptureTier.Hooked : CaptureTier.NotHooked,
            Mode = h.Mode,
            ExitStatus = exit,
            CaptureNotes = notes,
            LateAttach = hooked && h.Mode == CaptureMode.Attach,
            TelemetrySource = h.TelemetryDescriptor,
            OverlayBuildId = hooked ? BuildIdOf(o.Handshake) : null,
            LaunchWaitMs = o.LaunchWait is { } w ? (long)w.TotalMilliseconds : null,
            DrainTicks = hooked ? o.DrainTicks : null,
            ForegroundTicks = hooked ? o.ForegroundTicks : null,
            GuardTicksPublished = hooked ? o.GuardTicksPublished : null,
        };
    }

    private static AggregationInput Hooked(CaptureOutcome o, long qpcFrequency) => new()
    {
        Records = o.Records,
        GapBefore = o.GapBefore,
        Writer = o.WriterState,
        QpcFrequency = qpcFrequency,
        TotalGaps = o.TotalGaps,
        TotalDropped = o.TotalDropped,
        Modules = o.RuntimeModules,
        Ngx = o.NgxDriver,
    };

    private static string? BuildIdOf(FlShmHandshake handshake)
    {
        string id = handshake.BuildIdString();
        return id.Length == 0 ? null : id;
    }

    /// <summary>The recorder's ears on the loop: the flush and the telemetry drain, on the loop's task.</summary>
    private sealed class Run(PartialSessionWriter writer, ITelemetryPoller? poller, TimeProvider clock) : ICaptureObserver
    {
        private readonly List<TelemetrySample> _drained = [];
        private CaptureProgress? _last;

        public DateTimeOffset? AttachedAt { get; private set; }

        public void Attached(int pid, FlShmHandshake handshake)
        {
            AttachedAt = clock.GetUtcNow();
            writer.Note($"attached pid={pid} layout={handshake.LayoutVersion} build={handshake.BuildIdString()}");
        }

        public void Tick(CaptureProgress progress)
        {
            _last = progress;
            DrainTelemetry();
            writer.OnTick(progress);
        }

        public void FinalFlush()
        {
            DrainTelemetry();
            if (_last is not null)
            {
                writer.OnTick(_last, force: true);
            }
        }

        private void DrainTelemetry()
        {
            if (poller is null)
            {
                return;
            }

            _drained.Clear();
            poller.Drain(_drained);
            writer.AddSensors(_drained);
        }
    }
}
