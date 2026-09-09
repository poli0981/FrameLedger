using FluentAssertions;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Consent;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;
using FrameLedger.Infrastructure.Persistence;
using FrameLedger.Shared;

namespace FrameLedger.Application.Tests.Capture;

/// <summary>
/// The loop's ordering rules, each of which is load-bearing.
/// </summary>
/// <remarks>
/// Merge-gated: no injection, no native fixture, no Integration trait. What these
/// cannot cover — that a real Overlay reads the tick we publish — is the end-to-end
/// class, and the split is stated rather than left implied.
/// </remarks>
public sealed class CaptureSessionTests : IAsyncDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "fl-loop-" + Guid.NewGuid().ToString("N"));

    private LedgerDatabase? _db;

    private const string _exe = @"C:\Games\Title\game.exe";
    private const string _disclosure = "operator-disclosure/test";
    private const int _pid = 4242;

    private static ExecutableFingerprint Fingerprint =>
        new() { ExePath = _exe, SizeBytes = 90_000, MtimeUnixMs = 1_700_000_000_000 };

    public async ValueTask DisposeAsync()
    {
        if (_db is not null)
        {
            await _db.DisposeAsync().ConfigureAwait(false);
        }

        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (DirectoryNotFoundException)
        {
        }
    }

    private sealed class CountingGuard : IAntiCheatGuard
    {
        public int InjectCalls { get; private set; }

        public int EvaluateCalls { get; private set; }

        public AntiCheatVerdict Verdict { get; set; } = AntiCheatVerdict.Allowed();

        public AntiCheatVerdict EvaluateVerdict { get; set; } = AntiCheatVerdict.Allowed();

        public Exception? EvaluateThrows { get; set; }

        /// <summary>Throw only from the Nth evaluation onward, so the first scan can succeed.</summary>
        public int EvaluateThrowsAfter { get; set; } = int.MaxValue;

        public ValueTask<AntiCheatVerdict> EvaluateAsync(int targetPid, CancellationToken ct = default)
        {
            EvaluateCalls++;
            if (EvaluateThrows is not null || EvaluateCalls > EvaluateThrowsAfter)
            {
                return ValueTask.FromException<AntiCheatVerdict>(
                    EvaluateThrows ?? new InvalidOperationException("service query blocked"));
            }

            return ValueTask.FromResult(EvaluateVerdict);
        }

        public ValueTask<AntiCheatVerdict> GuardedInjectAsync(int targetPid, string payloadPath,
            CancellationToken ct = default)
        {
            InjectCalls++;
            return ValueTask.FromResult(Verdict);
        }

        public ValueTask<AntiCheatVerdict> GuardedInjectWhenReadyAsync(int targetPid, string payloadPath,
            int timeoutMs, CancellationToken ct = default)
        {
            InjectCalls++;
            return ValueTask.FromResult(Verdict);
        }

        public ValueTask<AntiCheatVerdict> PreScanGameDirectoryAsync(string gameDirectory,
            CancellationToken ct = default) => ValueTask.FromResult(AntiCheatVerdict.Allowed());
    }

    private sealed class FakeSink : ICaptureSink
    {
        private int _remaining;

        public FakeSink(int recordsToServe = 0) => _remaining = recordsToServe;

        public List<(uint Ticks, bool Unhook)> Published { get; } = [];

        public List<int> DrainCallSizes { get; } = [];

        /// <summary>
        /// Drains and publishes in ONE ordered list, so ordering between them is observable.
        /// </summary>
        /// <remarks>
        /// Two independent lists could not express it: <c>TheFirstTickIsPublishedBeforeAnyDrain</c>
        /// asserted only that the first tick was numbered 1, which is true whether the first scan runs
        /// before the drain loop or is deferred to the first 30 s boundary. The test was named for a
        /// property no assertion in it could see.
        /// </remarks>
        public List<string> Events { get; } = [];

        public int DrainsWithoutGapList { get; private set; }

        public FlWriterState State = new() { Status = (uint)FlStatus.Ready };

        public FlWriterState WriterState => State;

        public FlShmHandshake Handshake => default;

        public long TotalDropped { get; set; }

        public long TotalGaps { get; set; }

        public DrainResult Drain(Span<FlFrameRecord> into, IList<ulong> gapIndices)
        {
            if (gapIndices is null)
            {
                DrainsWithoutGapList++;
            }

            int n = Math.Min(into.Length, _remaining);
            for (int i = 0; i < n; i++)
            {
                into[i] = new FlFrameRecord { FrameIndex = (uint)i, Qpc = (ulong)(1000 + i), SwapchainId = 1 };
            }

            _remaining -= n;
            DrainCallSizes.Add(n);
            Events.Add("drain");
            return new DrainResult(n, 0, 0);
        }

        public void PublishGuardResult(uint completedEvaluations, bool unhookRequested)
        {
            Published.Add((completedEvaluations, unhookRequested));
            Events.Add("publish");
        }

        public void SetPaused(bool paused)
        {
        }

        public int LogFlushRequests { get; private set; }

        public void RequestLogFlush() => LogFlushRequests++;

        public void Dispose()
        {
        }
    }

    private sealed class FakeLiveness : ITargetLiveness
    {
        private int _foregroundSamples = int.MaxValue;

        public bool HasExited { get; set; }

        public int? ExitCode { get; set; }

        /// <summary>How many samples answer true before focus is lost. Default: every one.</summary>
        public int ForegroundFor
        {
            get => _foregroundSamples;
            set => _foregroundSamples = value;
        }

        /// <summary>
        /// Consumed per read, so a test can put the switch at a known tick rather than at a
        /// known time — the loop's cadence is a wall clock and must not be an input to an
        /// assertion.
        /// </summary>
        public bool IsForeground => _foregroundSamples-- > 0;

        public void Dispose()
        {
        }
    }

    private sealed class FixedResolver(int? pid, SessionEndReason reason) : ITargetResolver
    {
        public int? Resolve(string normalisedExePath, out SessionEndReason r)
        {
            r = reason;
            return pid;
        }
    }

    private async Task<IGameConsentStore> StoreWithAsync(bool enabled = true, bool preScanUnverified = false)
    {
        // The same SQLite adapter the host and the Agent use, on a scratch ledger under this test's directory.
        _db ??= await LedgerDatabase.OpenAsync(Path.Combine(_dir, LedgerPaths.DatabaseFileName), ct: TestContext.Current.CancellationToken)
            .ConfigureAwait(false);
        var store = new SqliteGameConsentStore(_db);
        if (enabled)
        {
            await store.RecordOperatorAcknowledgementAsync(new OperatorAcknowledgement
            {
                Fingerprint = Fingerprint,
                DisclosureVersion = _disclosure,
                AcknowledgedAt = DateTimeOffset.UnixEpoch,
            }, TestContext.Current.CancellationToken).ConfigureAwait(false);
        }

        if (preScanUnverified)
        {
            await store.RecordGuardBlockAsync(Fingerprint, default, TestContext.Current.CancellationToken)
                .ConfigureAwait(false);
        }

        return store;
    }

    private static CaptureSession Loop(IGameConsentStore store, CountingGuard guard, FakeSink? sink,
        FakeLiveness? alive = null, int? pid = _pid, SessionEndReason resolveReason = SessionEndReason.Running,
        CaptureOptions? options = null) =>
        new(store,
            new HookedCaptureGate(guard),
            guard,
            new FixedResolver(pid, resolveReason),
            new DelegateLivenessSource(_ => alive ?? new FakeLiveness()),
            new DelegateRingAttacher(_ => (sink, sink is null ? ShmAttachRefusal.BuildIdMismatch : ShmAttachRefusal.Ok)),
            options ?? new CaptureOptions
            {
                DrainInterval = TimeSpan.FromMilliseconds(1),
                ScanInterval = TimeSpan.FromMilliseconds(5),
                AttachBudget = TimeSpan.FromMilliseconds(50),
                MaxDuration = TimeSpan.FromMilliseconds(120),
                LogFlushGrace = TimeSpan.FromMilliseconds(1),
            });

    [Fact]
    public async Task TheModuleSnapshotIsTakenBesideEveryGuardScanAndOnceMoreBeforeTheLastDrain()
    {
        // The snapshot rides on the scan's cadence, never its own: one directly after each publish,
        // and one more after the loop exits so a module that loaded after the final scan is still
        // named. The sink's ordered event list is what makes the "directly after" observable.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        int snapshots = 0;
        var loop = new CaptureSession(
            await StoreWithAsync(), new HookedCaptureGate(guard), guard, new FixedResolver(_pid, SessionEndReason.Running),
            new DelegateLivenessSource(_ => new FakeLiveness()),
            new DelegateRingAttacher(_ => (sink, ShmAttachRefusal.Ok)),
            new CaptureOptions
            {
                DrainInterval = TimeSpan.FromMilliseconds(1),
                ScanInterval = TimeSpan.FromMilliseconds(5),
                AttachBudget = TimeSpan.FromMilliseconds(50),
                MaxDuration = TimeSpan.FromMilliseconds(120),
                LogFlushGrace = TimeSpan.FromMilliseconds(1),
            },
            new DelegateModuleSnapshot(pid =>
            {
                pid.Should().Be(_pid);
                snapshots++;
                sink.Events.Add("modules");
                return new RuntimeModuleSet(
                    [new RuntimeModuleInfo("sl.interposer.dll", @"C:\Games\Title\sl.interposer.dll", "2,8,0,0", new Version(2, 8, 0, 0))],
                    Snapshots: 1, Unreadable: 0);
            }));

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        snapshots.Should().Be(sink.Published.Count + 1, "one per scan, plus the one before the last drain");
        r.RuntimeModules.Snapshots.Should().Be(snapshots);
        r.RuntimeModules.Unreadable.Should().Be(0);
        r.RuntimeModules.VersionOf("sl.interposer.dll").Should().Be(new Version(2, 8, 0, 0));
        r.RuntimeModules.Modules.Should().ContainSingle("the same module across snapshots is one entry");
        for (int i = 0; i < sink.Events.Count; i++)
        {
            if (string.Equals(sink.Events[i], "publish", StringComparison.Ordinal))
            {
                sink.Events[i + 1].Should().Be("modules", "the snapshot follows the publish directly");
            }
        }

        sink.Events[^2].Should().Be("modules", "the final snapshot precedes the final drain");
        sink.Events[^1].Should().Be("drain");
    }

    [Fact]
    public async Task ALoopBuiltWithoutASnapshotSourceReportsAnEmptySet()
    {
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.RuntimeModules.Should().BeSameAs(RuntimeModuleSet.Empty);
        r.NgxDriver.Should().BeSameAs(NgxDriverState.NotRun, "no probe was given to the loop");
    }

    [Fact]
    public async Task TheDriverProbeRidesOnTheSameTouchAsTheModuleSnapshotAndIsMergedOntoTheResult()
    {
        // The probe is out of process and answers per pid; it rides the scan's cadence exactly as the
        // module snapshot does, and a later answer that differs is reported as a CHANGE, not averaged.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        int probes = 0;
        var loop = new CaptureSession(
            await StoreWithAsync(), new HookedCaptureGate(guard), guard, new FixedResolver(_pid, SessionEndReason.Running),
            new DelegateLivenessSource(_ => new FakeLiveness()),
            new DelegateRingAttacher(_ => (sink, ShmAttachRefusal.Ok)),
            new CaptureOptions
            {
                DrainInterval = TimeSpan.FromMilliseconds(1),
                ScanInterval = TimeSpan.FromMilliseconds(5),
                AttachBudget = TimeSpan.FromMilliseconds(50),
                MaxDuration = TimeSpan.FromMilliseconds(120),
                LogFlushGrace = TimeSpan.FromMilliseconds(1),
            },
            modules: null,
            ngx: new DelegateNgxProbe(pid =>
            {
                pid.Should().Be(_pid);
                probes++;
                sink.Events.Add("ngx");
                // The first reading has DLSS not yet created; every later one has it created and evaluated.
                ulong sr = probes == 1 ? NgxOverrideFlags.Initialized | NgxOverrideFlags.DllExists
                                       : NgxOverrideFlags.Initialized | NgxOverrideFlags.DllExists | NgxOverrideFlags.CreatedAndEvaluated;
                return NgxDriverState.Parse($"NGXSTATE status=ANSWERED sr=0x{sr:X16} rr=0x1 fg=0x5 ratio=0.0000 mode=0 preset=0 fgcount=0 fgpreset=0 fgmode=0 driver=61664");
            }));

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        probes.Should().Be(sink.Published.Count + 1, "one per scan, plus the one before the last drain");
        r.NgxDriver.Outcome.Should().Be(NgxProbeOutcome.Answered);
        r.NgxDriver.Readings.Should().Be(probes);
        r.NgxDriver.Answered.Should().Be(probes);
        r.NgxDriver.SrCreatedAndEvaluated.Should().BeTrue("the last answered reading is the state");
        r.NgxDriver.Changed.Should().BeTrue("the first reading differed from the later ones");
        r.NgxDriver.Driver.Should().Be(61664u);
        for (int i = 0; i < sink.Events.Count; i++)
        {
            if (string.Equals(sink.Events[i], "publish", StringComparison.Ordinal))
            {
                sink.Events[i + 1].Should().Be("ngx", "the probe follows the publish directly, on the snapshot's touch");
            }
        }
    }

    [Fact]
    public async Task NoRecordRefusesConsentAndNothingIsEverInjected()
    {
        // HANDOFF's acceptance for item 1: "Assert ConsentMissing with FlGuardedInject NEVER reached."
        // HookNotEnabled is what an ABSENT record produces, because the gate checks enablement first.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(enabled: false), guard, sink);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.RefusedHookNotEnabled);
        guard.InjectCalls.Should().Be(0);
        sink.Published.Should().BeEmpty("nothing was attached to, so nothing could be published");
    }

    [Fact]
    public async Task PreScanUnverifiedRefusesWithoutBecomingPreviouslyBlocked()
    {
        // 05_DETECTION: "could not verify" is neither a hit nor a pass. Routing it through the gate
        // would force one of the two collapses it forbids, so it is refused before a request is built.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(preScanUnverified: true), guard, sink);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.PreScanCouldNotVerify);
        r.Reason.Should().NotBe(SessionEndReason.RefusedPreviouslyBlocked);
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task AnUnresolvedTargetNeverReachesConsentOrTheGuard()
    {
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink,
            pid: null, resolveReason: SessionEndReason.TargetNotRunning);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.TargetNotRunning);
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task TwoProcessesOfTheSameGameRefuseRatherThanPickOne()
    {
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink,
            pid: null, resolveReason: SessionEndReason.TargetAmbiguous);

        (await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken))
            .Reason.Should().Be(SessionEndReason.TargetAmbiguous);
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task TheFirstTickIsPublishedBeforeAnyDrain()
    {
        // The Overlay's 65 s FL_GUARD_TICK_DEADLINE_MS clock starts at mapping publish, not at attach,
        // so a first scan deferred to the 30 s boundary is already half way to a supervision stop.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        // THE ORDERING, OBSERVED. Asserting `Published[0].Ticks == 1` alone was decoration: it holds
        // whether the first scan runs before the drain loop or is deferred to the first 30 s boundary,
        // because either way the first tick published is the first tick counted.
        sink.Events.Should().NotBeEmpty();
        sink.Events[0].Should().Be("publish",
            "the Overlay's 65 s supervision clock starts when the mapping is published, not when we "
            + "adopt it, so a first scan deferred by 30 s is already half way to a supervision stop");
        sink.Published[0].Ticks.Should().Be(1u);
        r.GuardTicksPublished.Should().BeGreaterThanOrEqualTo(1u);
    }

    [Fact]
    public async Task ThePublishedTickIsTheSupervisorsCompletedEvaluations()
    {
        // NEVER a counter this loop keeps. A loop-owned tick attests that this process is alive while
        // the guard loop can be dead — the exact signal fl_shm.h spends sixteen lines forbidding.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);

        await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        sink.Published.Select(p => p.Ticks).Should().BeEquivalentTo(
            Enumerable.Range(1, sink.Published.Count).Select(i => (uint)i),
            options => options.WithStrictOrdering());
        sink.Published[^1].Ticks.Should().Be((uint)guard.EvaluateCalls);
    }

    [Fact]
    public async Task AGuardRefusalStopsTheSessionAndPublishesTheLatch()
    {
        var guard = new CountingGuard
        {
            EvaluateVerdict = AntiCheatVerdict.Refused(
            AntiCheatRefusalReason.BlockedModule, "BattlEye", "BEClient_x64.dll")
        };
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.SafetyUnhook);
        sink.Published.Should().ContainSingle().Which.Unhook.Should().BeTrue();
    }

    [Fact]
    public async Task AGuardThatThrowsStopsTheTickRatherThanBeingSwallowed()
    {
        // GuardSupervisor deliberately does not catch. If the guard throws, the tick does not advance
        // and the capture side sees supervision stop, which is the correct outcome.
        var guard = new CountingGuard { EvaluateThrows = new InvalidOperationException("service query blocked") };
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);

        Func<Task> act = () => loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        await act.Should().ThrowAsync<InvalidOperationException>();
    }

    [Fact]
    public async Task AQuietButLiveTargetIsNeverASessionEnd()
    {
        // §S29(e). The sink serves nothing at all and status stays READY — byte-for-byte a loading
        // screen. The session must run to its own limit, not decide the game is gone.
        var guard = new CountingGuard();
        using var sink = new FakeSink(recordsToServe: 0);
        using var alive = new FakeLiveness { HasExited = false };
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink, alive);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.Running, "a frozen writeIndex means nothing about liveness");
        r.Records.Should().BeEmpty();
    }

    [Fact]
    public async Task AnExitedTargetEndsTheSessionEvenWhileStatusStillSaysReady()
    {
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        using var alive = new FakeLiveness { HasExited = true };
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink, alive);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.TargetExited);
        sink.State.Status.Should().Be((uint)FlStatus.Ready, "the mapping never said the target had gone");
    }

    [Fact]
    public async Task TheLoopCountsWhetherTheOperatorWasWATCHINGTheGame()
    {
        // Frame generation stops while a title is unfocused, so a window spanning an alt-tab
        // averages two configurations: measured 2026-08-16, that turned a ×2 capture into an
        // achieved presents/batch of 1.84. FgWindow.BatchRefusal is what REFUSES such a window
        // from the records; these two counts are what let the report name the cause.
        //
        // BOTH NUMBERS OR NEITHER. "Unfocused for 40 ticks" is a finding only if the target ever
        // held the foreground — a windowless target, which hook-harness is, answers false on
        // every tick of every run and means nothing by it.
        var guard = new CountingGuard();
        using var sink = new FakeSink(recordsToServe: 10);
        using var alive = new FakeLiveness { ForegroundFor = 1 };
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink, alive);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.DrainTicks.Should().BeGreaterThan(1, "the loop ran more than one tick inside its own duration");
        r.ForegroundTicks.Should().Be(1,
            "the fake yields focus after one sample, so the count is about the SAMPLE and not about "
            + "how fast this machine happened to run the loop");
    }

    [Fact]
    public async Task ADrainThatFillsTheBufferIsCalledAgainInTheSameTick()
    {
        // Drain copies at most into.Length and returns, so one call per tick caps throughput and the
        // excess becomes drops that look like a stall.
        var guard = new CountingGuard();
        using var sink = new FakeSink(recordsToServe: 1200);
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Records.Should().HaveCount(1200);
        sink.DrainCallSizes.Take(2).Should().AllSatisfy(n => n.Should().Be(512));
    }

    [Fact]
    public async Task DrainIsNeverCalledWithoutAGapList()
    {
        // The parameter defaults to null and the default is the wrong behaviour: 07_IPC needs a torn
        // record recorded at its INDEX, or the two surrounding intervals merge into one fabricated
        // stutter.
        var guard = new CountingGuard();
        using var sink = new FakeSink(recordsToServe: 10);
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);

        await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        sink.DrainsWithoutGapList.Should().Be(0);
    }

    [Fact]
    public async Task ATerminalAttachRefusalIsNotRetriedIntoATimeout()
    {
        // COUNTING THE ATTEMPTS IS THE POINT. Asserting only the final Reason and AttachRefusal was
        // decoration: both are identical whether the terminal refusal returns on the first attempt or
        // is retried until the budget expires, so the test could not fail for the property it names.
        var guard = new CountingGuard();
        int attempts = 0;
        var loop = new CaptureSession(
            await StoreWithAsync(), new HookedCaptureGate(guard), guard, new FixedResolver(_pid, SessionEndReason.Running),
            new DelegateLivenessSource(_ => new FakeLiveness()),
            new DelegateRingAttacher(_ =>
            {
                attempts++;
                return ((ICaptureSink?)null, ShmAttachRefusal.BuildIdMismatch);
            }),
            new CaptureOptions { AttachBudget = TimeSpan.FromMilliseconds(500) });

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.AttachRefused);
        r.AttachRefusal.Should().Be(ShmAttachRefusal.BuildIdMismatch,
            "the cause must survive to the caller; a build-id mismatch tells the user to restart the game");
        attempts.Should().Be(1, "a terminal refusal is an answer, and retrying one turns it into a timeout");
    }

    [Fact]
    public async Task AnIncompleteAttachIsRetriedUntilTheHandshakeIsPublished()
    {
        // THE GREEN HALF, without which the case above is satisfied by a loop that never retries at
        // all. Incomplete is the one retryable refusal: the Overlay publishes layoutVersion last,
        // behind a release fence, so "not ready yet" is a real transient state.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        int attempts = 0;
        var loop = new CaptureSession(
            await StoreWithAsync(), new HookedCaptureGate(guard), guard, new FixedResolver(_pid, SessionEndReason.Running),
            new DelegateLivenessSource(_ => new FakeLiveness()),
            new DelegateRingAttacher(_ => ++attempts < 3 ? (null, ShmAttachRefusal.Incomplete) : (sink, ShmAttachRefusal.Ok)),
            new CaptureOptions
            {
                DrainInterval = TimeSpan.FromMilliseconds(1),
                ScanInterval = TimeSpan.FromMilliseconds(5),
                AttachBudget = TimeSpan.FromSeconds(2),
                MaxDuration = TimeSpan.FromMilliseconds(60),
            });

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        attempts.Should().Be(3);
        r.Reason.Should().NotBe(SessionEndReason.AttachRefused);
        r.AttachRefusal.Should().Be(ShmAttachRefusal.Ok);
    }

    [Fact]
    public async Task APidThatCannotBePinnedRefusesInsteadOfInjecting()
    {
        // A handle we could not open means the pid is not reserved, so it can be recycled between here
        // and FlGuardedInject — and there is an awaited file read in that window. 19_SAFETY §Elevated /
        // protected targets: report "cannot attach", never escalate creatively.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        var loop = new CaptureSession(
            await StoreWithAsync(), new HookedCaptureGate(guard), guard, new FixedResolver(_pid, SessionEndReason.Running),
            new DelegateLivenessSource(_ => null),
            new DelegateRingAttacher(_ => (sink, ShmAttachRefusal.Ok)),
            new CaptureOptions());

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.TargetCannotBePinned);
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task AnUnreadableExecutableRefusesRatherThanComparingTheRecordWithItself()
    {
        // `observed ?? record.Fingerprint` made FromConsent's mismatch check compare the record against
        // ITSELF and always pass, so "we could not look at the binary" decided as "the binary is the
        // consented one" — the one polarity every other default here refuses.
        var guard = new CountingGuard();
        using var sink = new FakeSink();
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);

        CaptureOutcome r = await loop.RunAsync(_exe, observed: null, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.ExecutableUnreadable);
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task AGuardThatThrowsMidSessionKeepsTheRecordsItAlreadyDrained()
    {
        // The two consequences are separable and were conflated. Not advancing the tick is what stops
        // the capture side and is correct; losing the whole session's records with the stack is not.
        var guard = new CountingGuard();
        using var sink = new FakeSink(recordsToServe: 40);
        CaptureSession loop = Loop(await StoreWithAsync(), guard, sink);
        guard.EvaluateThrowsAfter = 1;

        CaptureOutcome r = await loop.RunAsync(_exe, Fingerprint, "payload.dll", TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.SupervisionFaulted);
        r.Records.Should().NotBeEmpty("the drain ran before the guard threw, and those frames are real");
        sink.Published.Should().ContainSingle("the tick must NOT advance past the evaluation that threw");
    }
}
