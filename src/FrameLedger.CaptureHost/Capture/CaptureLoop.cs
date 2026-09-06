using System.Diagnostics;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Consent;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;
using FrameLedger.Infrastructure.Ipc;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// The loop <c>04_CAPTURE</c> specifies and nothing production drove until now.
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
/// </remarks>
internal sealed class CaptureLoop(
    IGameConsentStore store,
    HookedCaptureGate gate,
    IAntiCheatGuard guard,
    ITargetResolver resolver,
    Func<int, ITargetLiveness?> liveness,
    Func<int, (ICaptureSink? Sink, ShmAttachRefusal Refusal)> attach,
    CaptureOptions options,
    Func<int, RuntimeModuleSet>? modules = null,
    Func<int, NgxDriverState>? ngx = null,
    Func<string, string, (int Pid, ITargetLiveness Alive)?>? launcher = null)
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
    public async Task<CaptureResult> RunAsync(string normalisedExePath, ExecutableFingerprint? observed,
        string payloadPath, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);

        int? pid = resolver.Resolve(normalisedExePath, out SessionEndReason resolveReason);
        if (pid is null)
        {
            return new CaptureResult { Reason = resolveReason };
        }

        using ITargetLiveness? alive = liveness(pid.Value);
        if (alive is null)
        {
            return new CaptureResult { Reason = SessionEndReason.TargetCannotBePinned };
        }

        GameConsentRecord record = await store.FindAsync(normalisedExePath, ct).ConfigureAwait(false);
        if (ConsentRefusal(record, observed) is SessionEndReason refused)
        {
            return new CaptureResult { Reason = refused };
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
    public async Task<CaptureResult> RunLaunchedAsync(string normalisedExePath, ExecutableFingerprint? observed,
        string payloadPath, string arguments, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);
        if (launcher is null)
        {
            throw new InvalidOperationException("this loop was built without a launcher; launch mode is unavailable");
        }

        var started = Stopwatch.StartNew();
        (int Pid, ITargetLiveness Alive)? launched = launcher(normalisedExePath, arguments ?? string.Empty);
        if (launched is null)
        {
            return new CaptureResult { Reason = SessionEndReason.LaunchCannotStart };
        }

        using ITargetLiveness alive = launched.Value.Alive;
        GameConsentRecord record = await store.FindAsync(normalisedExePath, ct).ConfigureAwait(false);
        if (ConsentRefusal(record, observed) is SessionEndReason refused)
        {
            return new CaptureResult { Reason = refused };
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
    private async Task<CaptureResult> SessionAsync(int pid, ITargetLiveness alive, GameConsentRecord record,
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
            return new CaptureResult { Reason = RefusalOf(verdict.Reason), Verdict = verdict, LaunchWait = launchWait };
        }

        TimeSpan attachBudget = layered ? options.LaunchWaitBudget : options.AttachBudget;
        (ICaptureSink? sink, ShmAttachRefusal refusal) = await AttachAsync(pid, attachBudget, ct).ConfigureAwait(false);
        if (sink is null)
        {
            return new CaptureResult
            {
                Reason = SessionEndReason.AttachRefused,
                Verdict = verdict,
                AttachRefusal = refusal,
                LaunchWait = launchWait,
            };
        }

        using (sink)
        {
            CaptureResult result = await DrainAsync(pid, alive, sink, verdict, ct).ConfigureAwait(false);
            return result with { LaunchWait = launchWait };
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
            (ICaptureSink? sink, refusal) = attach(pid);
            if (sink is not null || refusal != ShmAttachRefusal.Incomplete)
            {
                return (sink, refusal);
            }

            await Task.Delay(50, ct).ConfigureAwait(false);
        }

        return (null, refusal);
    }

    private async Task<CaptureResult> DrainAsync(int pid, ITargetLiveness alive, ICaptureSink sink,
        AntiCheatVerdict verdict, CancellationToken ct)
    {
        var supervisor = new GuardSupervisor(guard);
        var records = new List<FlFrameRecord>();
        var gaps = new List<ulong>();
        var focus = new FocusTally();
        var loaded = new ModuleTally(modules, ngx);
        SessionEndReason end = await SuperviseAsync(pid, alive, sink, supervisor, records, gaps, focus, loaded, ct)
            .ConfigureAwait(false);

        return new CaptureResult
        {
            RuntimeModules = loaded.Set,
            NgxDriver = loaded.Ngx,
            TouchQpc = loaded.TouchQpc,
            Reason = end,
            Verdict = verdict,
            AttachRefusal = ShmAttachRefusal.Ok,
            Records = records,
            WriterState = sink.WriterState,
            Handshake = sink.Handshake,
            GuardTicksPublished = supervisor.CompletedEvaluations,
            TotalDropped = sink.TotalDropped,
            TotalGaps = sink.TotalGaps,
            DrainTicks = focus.Ticks,
            ForegroundTicks = focus.Foreground,
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
    /// </remarks>
    private async Task<SessionEndReason> SuperviseAsync(int pid, ITargetLiveness alive, ICaptureSink sink,
        GuardSupervisor supervisor, List<FlFrameRecord> records, IList<ulong> gaps, FocusTally focus,
        ModuleTally loaded, CancellationToken ct)
    {
        var buffer = new FlFrameRecord[512];

        bool mayContinue = await ScanAsync(pid, sink, supervisor, loaded, ct).ConfigureAwait(false);

        DateTimeOffset nextScan = DateTimeOffset.UtcNow + options.ScanInterval;
        DateTimeOffset attachSettledAt = DateTimeOffset.UtcNow + options.AttachBudget;
        DateTimeOffset? hardStop = options.MaxDuration > TimeSpan.Zero
            ? DateTimeOffset.UtcNow + options.MaxDuration
            : null;

        SessionEndReason end = SessionEndReason.Running;
        Exception? faulted = null;
        while (mayContinue)
        {
            focus.Sample(alive.IsForeground);
            DrainInto(sink, buffer, gaps, records);

            end = SessionEndClassifier.Classify(
                alive.HasExited, sink.WriterState.Status, supervisor.UnhookRequested,
                DateTimeOffset.UtcNow > attachSettledAt);
            if (end != SessionEndReason.Running)
            {
                break;
            }

            if (hardStop is not null && DateTimeOffset.UtcNow >= hardStop)
            {
                break;
            }

            if (DateTimeOffset.UtcNow >= nextScan)
            {
                try
                {
                    mayContinue = await ScanAsync(pid, sink, supervisor, loaded, ct).ConfigureAwait(false);
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
        loaded.Take(pid);

        // ONE LAST DRAIN ON EVERY PATH, including the faulted one — that is the whole point of catching
        // the mid-loop throw rather than letting it leave the method. Then let go promptly: holding the
        // section keeps the named object alive, and a recycled pid's Overlay would hit
        // ERROR_ALREADY_EXISTS creating its ring and refuse to start at all.
        DrainInto(sink, buffer, gaps, records);
        return Conclude(end, faulted, mayContinue);
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
    private sealed class ModuleTally(Func<int, RuntimeModuleSet>? source, Func<int, NgxDriverState>? ngxSource)
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
                Set = Set.Merge(source(pid));
            }

            if (ngxSource is not null)
            {
                Ngx = Ngx.Merge(ngxSource(pid));
            }
        }
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
    private static void DrainInto(ICaptureSink sink, FlFrameRecord[] buffer, IList<ulong> gaps,
        List<FlFrameRecord> records)
    {
        DrainResult r;
        do
        {
            r = sink.Drain(buffer, gaps);
            records.AddRange(buffer.AsSpan(0, r.Copied).ToArray());
        }
        while (r.Copied == buffer.Length);
    }
}
