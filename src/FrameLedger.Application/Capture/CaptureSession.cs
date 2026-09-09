using System.Diagnostics;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Consent;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;
using FrameLedger.Shared;

namespace FrameLedger.Application.Capture;

/// <summary>
/// The session loop <c>04_CAPTURE</c> specifies: gate, inject, attach, drain at 10 Hz under the 30 s
/// re-scan, until something ends it.
/// </summary>
/// <remarks>
/// <para>
/// <b>This is the first production driver of <c>FlControlBlock.guardTicks</c></b> —
/// the sending half of the 30 s re-scan that <c>19_SAFETY</c> calls the most
/// important runtime behaviour in the capture layer, and that <c>README</c> already
/// promises. The write path (<c>ShmRingReader.PublishGuardResult</c>) and the read
/// path (the Overlay watchdog) both existed and were tested; only the loop was
/// missing, and a missing loop reads as a missing subsystem.
/// </para>
/// <para>The ordering rules below are each load-bearing and each has a test.</para>
/// <para>
/// <b>Promoted out of the unshipped capture host on 2026-09-09 (P2 PR-C) as a pure move</b> — the
/// <c>CaptureLoop</c> that ran every hooked session since 2026-08-06, now over ports instead of
/// delegates so the Agent's composition root can supply the adapters: <see cref="IRingAttacher"/>,
/// <see cref="ITargetLivenessSource"/>, <see cref="IProcessLauncher"/>, <see cref="IRuntimeModuleSnapshot"/>,
/// <see cref="INgxDriverProbe"/>. <b>This is the only code that touches the ring during a session</b>
/// (<c>04_CAPTURE</c> §Threading model): drain, <c>PublishGuardResult</c>, <c>SetPaused</c>,
/// <c>RequestLogFlush</c> all happen on this loop's task, and the ring reader gains no lock for anyone else.
/// </para>
/// </remarks>
public sealed class CaptureSession(
    IGameConsentStore store,
    HookedCaptureGate gate,
    IAntiCheatGuard guard,
    ITargetResolver resolver,
    ITargetLivenessSource liveness,
    IRingAttacher attacher,
    CaptureOptions options,
    IRuntimeModuleSnapshot? modules = null,
    INgxDriverProbe? ngx = null,
    IProcessLauncher? launcher = null,
    ICaptureObserver? observer = null)
{
    /// <summary>Start one session, or say why not.</summary>
    /// <remarks>
    /// <para>
    /// <b>(1) The target is resolved by image path alone, before consent</b> — a
    /// <see cref="HookRequest"/> cannot be built without a pid, and inventing an
    /// "unknown pid" so the order could be reversed would be the worse trade.
    /// </para>
    /// <para>
    /// <b>(2) The handle is held before the injection, and a pid we cannot pin is a
    /// refusal rather than a session.</b> Pids recycle, the ring is named after one,
    /// and there is an awaited file read between resolution and injection — exactly
    /// long enough for an exit-and-recycle (§S29(e)).
    /// </para>
    /// <para>
    /// <b>(3) The pre-scan's "could not verify" is refused here and never routed
    /// through the gate</b>: the gate has no reason code for it, and giving it one
    /// would force one of the two collapses <c>05_DETECTION</c> forbids — disabling
    /// the toggle with no appeal, or clearing it.
    /// </para>
    /// <para>
    /// <b>An executable we could not read is not the consented one.</b> This passed
    /// <c>observed ?? record.Fingerprint</c>, which made
    /// <see cref="HookRequest.FromConsent"/>'s mismatch check compare the record
    /// against ITSELF and always pass — so "we could not look at the binary" decided
    /// as "the binary is the one that was consented to", in the one direction every
    /// other default here refuses.
    /// </para>
    /// <para>
    /// <b>(4) The gate, and only the gate.</b> <c>GuardedInjectAsync</c> is not
    /// reachable from this class by any other route, which is CLAUDE.md rule 1 made
    /// structural rather than reviewed.
    /// </para>
    /// </remarks>
    public async Task<CaptureOutcome> RunAsync(string normalisedExePath, ExecutableFingerprint? observed,
        string payloadPath, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);

        int? pid = resolver.Resolve(normalisedExePath, out SessionEndReason resolveReason);
        if (pid is null)
        {
            return new CaptureOutcome { Reason = resolveReason };
        }

        using ITargetLiveness? alive = liveness.TryPin(pid.Value);
        if (alive is null)
        {
            return new CaptureOutcome { Reason = SessionEndReason.TargetCannotBePinned };
        }

        GameConsentRecord record = await store.FindAsync(normalisedExePath, ct).ConfigureAwait(false);
        if (ConsentRefusal(record, observed) is SessionEndReason refused)
        {
            return new CaptureOutcome { Reason = refused };
        }

        return await SessionAsync(pid.Value, alive, record, observed!.Value, payloadPath, started: null, ct)
            .ConfigureAwait(false);
    }

    /// <summary>Launch mode (P1 item 2): start the consented executable, then the same session.</summary>
    /// <remarks>
    /// <para>
    /// <b>The process is started FIRST and consent is still the gate's, one process later.</b> Attach mode
    /// resolves a pid before consent because a <see cref="HookRequest"/> needs one; here the pid is made
    /// rather than found, and the operator asked for the game to start. A refusal of any kind — no
    /// record, an anti-cheat hit, no runtime inside the budget — leaves the title running, unhooked,
    /// which is the product's Tier 2: this host never terminates what it launched.
    /// </para>
    /// <para>
    /// <b>The guard is reached through its waiting entry, with the loop's budget, and never the plain
    /// one</b>: <see cref="HookRequest.WaitForPresentationRuntimeMs"/> is what routes it, and the wait is
    /// measured here — from the start of the process to the guard's answer — because that number is
    /// the cost <c>20_OPEN_QUESTIONS</c> §S1 deferred on. <c>FlWriterState.dxgiPresentsBeforeHook</c>
    /// is its other half.
    /// </para>
    /// </remarks>
    public async Task<CaptureOutcome> RunLaunchedAsync(string normalisedExePath, ExecutableFingerprint? observed,
        string payloadPath, string arguments, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);
        if (launcher is null)
        {
            throw new InvalidOperationException("this loop was built without a launcher; launch mode is unavailable");
        }

        var started = Stopwatch.StartNew();
        (int Pid, ITargetLiveness Alive)? launched = launcher.Start(normalisedExePath, arguments ?? string.Empty);
        if (launched is null)
        {
            return new CaptureOutcome { Reason = SessionEndReason.LaunchCannotStart };
        }

        using ITargetLiveness alive = launched.Value.Alive;
        GameConsentRecord record = await store.FindAsync(normalisedExePath, ct).ConfigureAwait(false);
        if (ConsentRefusal(record, observed) is SessionEndReason refused)
        {
            return new CaptureOutcome { Reason = refused };
        }

        return await SessionAsync(launched.Value.Pid, alive, record, observed!.Value, payloadPath, started, ct)
            .ConfigureAwait(false);
    }

    /// <summary>The two refusals the loop makes itself, before the gate (see <see cref="RunAsync"/>'s remarks).</summary>
    private static SessionEndReason? ConsentRefusal(GameConsentRecord record, ExecutableFingerprint? observed)
    {
        if (record.PreScanUnverified)
        {
            return SessionEndReason.PreScanCouldNotVerify;
        }

        return observed is null ? SessionEndReason.ExecutableUnreadable : null;
    }

    /// <summary>Gate, attach, drain — shared by both modes; <paramref name="started"/> non-null is launch mode.</summary>
    private async Task<CaptureOutcome> SessionAsync(int pid, ITargetLiveness alive, GameConsentRecord record,
        ExecutableFingerprint observed, string payloadPath, Stopwatch? started, CancellationToken ct)
    {
        int waitMs = started is null ? 0 : checked((int)options.LaunchWaitBudget.TotalMilliseconds);
        HookRequest request = HookRequest.FromConsent(record, observed, pid, payloadPath, waitMs);
        AntiCheatVerdict verdict = await gate.StartAsync(request, ct).ConfigureAwait(false);
        TimeSpan? launchWait = started?.Elapsed;
        // THE ONE VERDICT THAT IS NEITHER: the guard passed and injected nothing because the target
        // presents through Vulkan, where the implicit layer is the capture side (P1 item 3). The ring
        // to attach to is the layer's, and it appears at the title's first vkCreateDevice -- which is
        // why the attach budget is launch mode's, not the 5 s an already-injected Overlay gets.
        bool layered = verdict.Reason == AntiCheatRefusalReason.TargetIsVulkanLayered;
        if (!verdict.IsAllowed && !layered)
        {
            return new CaptureOutcome { Reason = RefusalOf(verdict.Reason), Verdict = verdict, LaunchWait = launchWait };
        }

        TimeSpan attachBudget = layered ? options.LaunchWaitBudget : options.AttachBudget;
        (ICaptureSink? sink, ShmAttachRefusal refusal) = await AttachAsync(pid, attachBudget, ct).ConfigureAwait(false);
        if (sink is null)
        {
            return new CaptureOutcome
            {
                Reason = SessionEndReason.AttachRefused,
                Verdict = verdict,
                AttachRefusal = refusal,
                LaunchWait = launchWait,
            };
        }

        using (sink)
        {
            observer?.Attached(pid, sink.Handshake);
            CaptureOutcome result = await DrainAsync(pid, alive, sink, verdict, ct).ConfigureAwait(false);
            return result with { LaunchWait = launchWait, TargetPid = pid, ExitCode = alive.ExitCode };
        }
    }

    private static SessionEndReason RefusalOf(AntiCheatRefusalReason reason) => reason switch
    {
        AntiCheatRefusalReason.HookNotEnabled => SessionEndReason.RefusedHookNotEnabled,
        AntiCheatRefusalReason.ConsentMissing => SessionEndReason.RefusedConsentMissing,
        AntiCheatRefusalReason.PreviouslyBlocked => SessionEndReason.RefusedPreviouslyBlocked,
        AntiCheatRefusalReason.LaunchTargetExited => SessionEndReason.LaunchTargetExited,
        AntiCheatRefusalReason.LaunchNoPresentationRuntime => SessionEndReason.LaunchNoPresentationRuntime,
        _ => SessionEndReason.RefusedByGuard,
    };

    /// <summary>
    /// Retries ONLY on <see cref="ShmAttachRefusal.Incomplete"/>, bounded by wall
    /// clock rather than by attempt count.
    /// </summary>
    /// <remarks>
    /// <c>Incomplete</c> collapses four causes — no mapping yet, handshake not
    /// published, <c>PointerOffset != 0</c>, no build id of our own — and only the
    /// middle one is transient. Every other refusal is terminal, and retrying one
    /// would turn a clear answer into a timeout.
    /// </remarks>
    private async Task<(ICaptureSink? Sink, ShmAttachRefusal Refusal)> AttachAsync(int pid, TimeSpan budget,
        CancellationToken ct)
    {
        ShmAttachRefusal refusal = ShmAttachRefusal.NotEvaluated;
        DateTimeOffset deadline = DateTimeOffset.UtcNow + budget;
        while (DateTimeOffset.UtcNow < deadline)
        {
            (ICaptureSink? sink, refusal) = attacher.TryAttach(pid);
            if (sink is not null || refusal != ShmAttachRefusal.Incomplete)
            {
                return (sink, refusal);
            }

            await Task.Delay(50, ct).ConfigureAwait(false);
        }

        return (null, refusal);
    }

    private async Task<CaptureOutcome> DrainAsync(int pid, ITargetLiveness alive, ICaptureSink sink,
        AntiCheatVerdict verdict, CancellationToken ct)
    {
        var supervisor = new GuardSupervisor(guard);
        var state = new DrainState(new ModuleTally(modules, ngx));
        SessionEndReason end = await SuperviseAsync(pid, alive, sink, supervisor, state, ct).ConfigureAwait(false);

        return new CaptureOutcome
        {
            RuntimeModules = state.Loaded.Set,
            NgxDriver = state.Loaded.Ngx,
            TouchQpc = state.Loaded.TouchQpc,
            Reason = end,
            Verdict = verdict,
            AttachRefusal = ShmAttachRefusal.Ok,
            Records = state.Records,
            GapBefore = state.GapBefore,
            WriterState = sink.WriterState,
            Handshake = sink.Handshake,
            GuardTicksPublished = supervisor.CompletedEvaluations,
            TotalDropped = sink.TotalDropped,
            TotalGaps = sink.TotalGaps,
            DrainTicks = state.Focus.Ticks,
            ForegroundTicks = state.Focus.Foreground,
        };
    }

    /// <summary>Drain and supervise until something ends the session.</summary>
    /// <remarks>
    /// <para>
    /// <b>The first tick goes out immediately, not at the first 30 s boundary.</b>
    /// The Overlay's 65 s <c>FL_GUARD_TICK_DEADLINE_MS</c> clock starts when the
    /// mapping is published in <c>InitThread</c>, not when we attach — "never
    /// advanced and stopped advancing are the same state" — so a first scan deferred
    /// by 30 s is already half way to a supervision-loss stop before the session has
    /// produced a frame.
    /// </para>
    /// <para>
    /// <b>Exceptions are deliberately not caught around <c>ScanOnceAsync</c>.</b> If
    /// the guard throws, the tick does not advance and the capture side sees
    /// supervision stop, which is the correct outcome. Catching and continuing here
    /// is precisely how a supervisor comes to look alive while doing nothing.
    /// </para>
    /// <para>
    /// <b>The published tick is ALWAYS the supervisor's own counter</b>, never one
    /// this loop keeps. <c>guardTicks</c> counts completed evaluations on the far
    /// side of a returned verdict; a loop-owned counter would attest that this
    /// process is alive while the guard loop is dead, which is exactly the signal
    /// <c>fl_shm.h</c> spends sixteen lines forbidding.
    /// </para>
    /// <para>
    /// <b>A mid-loop throw is caught, and the FIRST scan's is not — the two are
    /// different events.</b> Letting a mid-session exception leave this method
    /// conflated two separable consequences: not advancing the tick is what stops the
    /// capture side and is correct, while losing the whole session's drained records
    /// with the stack is required by nothing — the final drain never ran and
    /// <c>records</c> went out of scope with it. The first scan still throws: there is
    /// no session to preserve yet, and a guard that cannot answer at all is not a
    /// session start.
    /// </para>
    /// <para>
    /// <b>The observer sees every tick, on this task, after the drain</b> (P2 PR-D): the
    /// recorder's <c>.partial</c> flush and the telemetry drain ride here, so the loop stays the
    /// only writer of the buffers it hands out.
    /// </para>
    /// </remarks>
    private async Task<SessionEndReason> SuperviseAsync(int pid, ITargetLiveness alive, ICaptureSink sink,
        GuardSupervisor supervisor, DrainState state, CancellationToken ct)
    {
        var buffer = new FlFrameRecord[512];

        bool mayContinue = await ScanAsync(pid, sink, supervisor, state.Loaded, ct).ConfigureAwait(false);

        DateTimeOffset nextScan = DateTimeOffset.UtcNow + options.ScanInterval;
        DateTimeOffset attachSettledAt = DateTimeOffset.UtcNow + options.AttachBudget;
        DateTimeOffset? hardStop = options.MaxDuration > TimeSpan.Zero
            ? DateTimeOffset.UtcNow + options.MaxDuration
            : null;

        SessionEndReason end = SessionEndReason.Running;
        Exception? faulted = null;
        while (mayContinue)
        {
            state.Focus.Sample(alive.IsForeground);
            DrainInto(sink, buffer, state);
            observer?.Tick(state.Progress(sink, supervisor));

            end = SessionEndClassifier.Classify(
                alive.HasExited, sink.WriterState.Status, supervisor.UnhookRequested,
                DateTimeOffset.UtcNow > attachSettledAt);
            if (end != SessionEndReason.Running || (hardStop is not null && DateTimeOffset.UtcNow >= hardStop))
            {
                break;
            }

            if (DateTimeOffset.UtcNow >= nextScan)
            {
                try
                {
                    mayContinue = await ScanAsync(pid, sink, supervisor, state.Loaded, ct).ConfigureAwait(false);
                }
                catch (Exception ex) when (ex is not OperationCanceledException)
                {
                    faulted = ex;
                    break;
                }

                nextScan = DateTimeOffset.UtcNow + options.ScanInterval;
            }

            await Task.Delay(options.DrainInterval, ct).ConfigureAwait(false);
        }

        // ONE LAST SNAPSHOT before the last drain, so a module that loaded after the final scan is
        // still named; on TargetExited it reads nothing, which the tally counts rather than hides.
        state.Loaded.Take(pid);

        // ONE LAST DRAIN ON EVERY PATH, including the faulted one — that is the whole point of catching
        // the mid-loop throw rather than letting it leave the method. Then let go promptly: holding the
        // section keeps the named object alive, and a recycled pid's Overlay would hit
        // ERROR_ALREADY_EXISTS creating its ring and refuse to start at all.
        DrainInto(sink, buffer, state);
        observer?.Tick(state.Progress(sink, supervisor));
        await FlushNativeLogAsync(sink, alive, end, ct).ConfigureAwait(false);
        return Conclude(end, faulted, mayContinue);
    }

    /// <summary>
    /// The native log, before letting go (<c>17_HOOK_ENGINE</c> §Native logging): the Overlay's watchdog flushes
    /// on its next tick, so the request is followed by one tick's grace — only while the target is alive to act
    /// on it (a stopped Overlay flushed on its own way out).
    /// </summary>
    private async Task FlushNativeLogAsync(ICaptureSink sink, ITargetLiveness alive, SessionEndReason end,
        CancellationToken ct)
    {
        if (alive.HasExited || end == SessionEndReason.TargetExited)
        {
            return;
        }

        sink.RequestLogFlush();
        await Task.Delay(options.LogFlushGrace, ct).ConfigureAwait(false);
    }

    /// <summary>One guard scan: evaluate, publish the supervisor's own count, then snapshot the modules.</summary>
    /// <remarks>
    /// <b>The module snapshot rides beside the guard scan, never on its own cadence.</b> The scan is
    /// the moment this host already enumerates the target's modules, and a vendor runtime that loads
    /// late (a title enabling frame generation from its menu) is caught by the next scan the way the
    /// Overlay's watchdog catches it on the next tick. The scan's exception propagates: the caller
    /// decides whether a throw ends the session or only the supervision.
    /// </remarks>
    private static async Task<bool> ScanAsync(int pid, ICaptureSink sink, GuardSupervisor supervisor,
        ModuleTally loaded, CancellationToken ct)
    {
        bool mayContinue = await supervisor.ScanOnceAsync(pid, ct).ConfigureAwait(false);
        sink.PublishGuardResult(supervisor.CompletedEvaluations, supervisor.UnhookRequested);
        loaded.Take(pid);
        return mayContinue;
    }

    /// <summary>
    /// Accumulates module snapshots and the driver's NGX word; a loop built without a source takes none.
    /// The NGX probe rides on the same touch as the snapshot: out of process, and the moment a title that
    /// enabled DLSS from its menu is caught by the next scan, exactly as a late-loading module is.
    /// </summary>
    private sealed class ModuleTally(IRuntimeModuleSnapshot? source, INgxDriverProbe? ngxSource)
    {
        public RuntimeModuleSet Set { get; private set; } = RuntimeModuleSet.Empty;

        public NgxDriverState Ngx { get; private set; } = NgxDriverState.NotRun;

        /// <summary>
        /// When this host touched the target — a guard scan just completed, and the module snapshot
        /// is about to run — as QPC ticks (<see cref="System.Diagnostics.Stopwatch.GetTimestamp"/> is
        /// QueryPerformanceCounter on Windows, the clock the records carry). Recorded whether or not a
        /// snapshot source exists, because it is the stall diagnostic's alibi, not the snapshot's.
        /// </summary>
        public List<long> TouchQpc { get; } = [];

        public void Take(int pid)
        {
            TouchQpc.Add(System.Diagnostics.Stopwatch.GetTimestamp());
            if (source is not null)
            {
                Set = Set.Merge(source.Take(pid));
            }

            if (ngxSource is not null)
            {
                Ngx = Ngx.Merge(ngxSource.Run(pid));
            }
        }
    }

    /// <summary>Everything one session accumulates, owned by the loop; the observer reads it between ticks.</summary>
    private sealed class DrainState(ModuleTally loaded)
    {
        public List<FlFrameRecord> Records { get; } = [];

        public List<ulong> Gaps { get; } = [];

        /// <summary>Indices into <see cref="Records"/> whose record follows a torn slot or a drop.</summary>
        public List<int> GapBefore { get; } = [];

        /// <summary>A gap seen at the end of a batch, waiting for the next record to attach to.</summary>
        public bool PendingGap { get; set; }

        public FocusTally Focus { get; } = new();

        public ModuleTally Loaded { get; } = loaded;

        public CaptureProgress Progress(ICaptureSink sink, GuardSupervisor supervisor) => new()
        {
            Records = Records,
            GapBefore = GapBefore,
            WriterState = sink.WriterState,
            TotalDropped = sink.TotalDropped,
            TotalGaps = sink.TotalGaps,
            DrainTicks = Focus.Ticks,
            ForegroundTicks = Focus.Foreground,
            GuardTicksPublished = supervisor.CompletedEvaluations,
            TouchQpc = Loaded.TouchQpc,
            RuntimeModules = Loaded.Set,
            NgxDriver = Loaded.Ngx,
        };
    }

    /// <summary>
    /// Turns the loop's exit condition into the reason a caller sees.
    /// </summary>
    /// <remarks>
    /// A refusal from OUR supervisor is a safety unhook whatever the writer has got
    /// round to saying: we published the stop, and the Overlay reacts within a frame.
    /// <see cref="SessionEndClassifier"/> cannot know that, because the mapping stores
    /// one status for both stops.
    /// </remarks>
    private static SessionEndReason Conclude(SessionEndReason end, Exception? faulted, bool mayContinue)
    {
        if (faulted is not null)
        {
            return SessionEndReason.SupervisionFaulted;
        }

        return mayContinue ? end : SessionEndReason.SafetyUnhook;
    }

    /// <summary>
    /// Drains until caught up, not once.
    /// </summary>
    /// <remarks>
    /// <c>Drain</c> copies at most <c>into.Length</c> and returns, so one call per
    /// tick silently caps throughput at buffer-size × cadence and the excess becomes
    /// drops that look like a stall. The gap list is ALWAYS passed: the parameter
    /// defaults to null and the default is the wrong behaviour — <c>07_IPC</c> needs
    /// a torn record recorded at its index, or the two surrounding intervals merge
    /// into one fabricated stutter.
    /// </remarks>
    private static void DrainInto(ICaptureSink sink, FlFrameRecord[] buffer, DrainState state)
    {
        DrainResult r;
        do
        {
            int gapsBefore = state.Gaps.Count;
            r = sink.Drain(buffer, state.Gaps);
            MarkGaps(state, r, gapsBefore);
            state.Records.AddRange(buffer.AsSpan(0, r.Copied).ToArray());
        }
        while (r.Copied == buffer.Length);
    }

    /// <summary>
    /// Which copied record each torn slot (and an overwrite skip) preceded: walk the slots this call
    /// examined in order, and the first record after a bad slot carries the gap
    /// (<c>07_IPC</c>: a torn record is a gap at its index, never a merged interval).
    /// </summary>
    private static void MarkGaps(DrainState state, DrainResult r, int gapsBefore)
    {
        bool pending = state.PendingGap || r.Dropped > 0;
        if (r.Gaps == 0)
        {
            if (pending && r.Copied > 0)
            {
                state.GapBefore.Add(state.Records.Count);
                pending = false;
            }

            state.PendingGap = pending;
            return;
        }

        var torn = new HashSet<ulong>(state.Gaps.Skip(gapsBefore));
        int k = 0;
        for (ulong slot = r.FirstSlot; k < r.Copied || torn.Count > 0; slot++)
        {
            if (torn.Remove(slot))
            {
                pending = true;
                continue;
            }

            if (k >= r.Copied)
            {
                break;
            }

            if (pending)
            {
                state.GapBefore.Add(state.Records.Count + k);
                pending = false;
            }

            k++;
        }

        state.PendingGap = pending;
    }
}
