using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Rules;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Infrastructure.AntiCheat;
using FrameLedger.Infrastructure.Ipc;
using FrameLedger.Infrastructure.Rules;
using FrameLedger.Shared;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// The write→read loop, end to end: the real guard injects the real Overlay into a real target, and the
/// real reader drains what it wrote. Everything before this exercised one side or the other.
/// </summary>
/// <remarks>
/// <para>
/// <b>WHY THIS IS A TEST AND NOT A FLAG ON THE AGENT.</b> An adversarial review of the original design
/// — which proposed <c>Agent --diag &lt;pid&gt;</c> — found it blocking, and it was right.
/// <c>FlGuardedInject</c> is the ANTI-CHEAT gate; <c>fl_guard_abi.h</c> says in its own header that it
/// deliberately carries no per-game consent and no <c>hook_enabled</c>. The only thing enforcing
/// CLAUDE.md rule 1 is <c>HookedCaptureGate</c>, whose three inputs come from a <c>games</c> table that
/// does not exist yet. So a flag on a shipped binary would have put a user-runnable path into any x64
/// process carrying no detected anti-cheat, with the disclosure never shown — "never automatic",
/// automatically.
/// </para>
/// <para>
/// Synthesising <c>HookEnabled = true, ConsentedAt = UtcNow</c> to satisfy that gate was also rejected:
/// it compiles, and it is a gate whose verdict is decided before it looks.
/// </para>
/// <para>
/// The target here is <c>hook-harness</c> — our own dummy D3D app, built from this tree, carrying no
/// anti-cheat and belonging to no publisher. Injecting into it raises no consent question at all. The
/// test asserts that constraint on itself below, so this cannot quietly grow into something that
/// injects elsewhere.
/// </para>
/// <para>
/// <b>NO BUDGET IN THIS FILE IS SIZED ON THE HARNESS'S MEASURED RATE.</b> Four assertions were, and
/// all four went red under load while nothing was wrong — the suite runs four test assemblies in
/// parallel, each spawning a harness and injecting an Overlay that creates a WARP device, so the
/// presenting thread is descheduled for far longer than a rate-derived constant allows. Every loop
/// here waits for the STATE it needs, bounded by a wall clock generous enough to be about the state
/// rather than about the machine.
/// </para>
/// <para>
/// <b>And when one of them fails, capture the MESSAGE before changing anything.</b> #62 spent two
/// rounds applying the remedy for a race to what turned out to be a loop bound and its assertion
/// disagreeing by one — a defect class is a hypothesis about the next failure, never a diagnosis of
/// it. <c>TheGuardInjectsTheOverlayAndTheReaderDrainsRealFrames</c> failed once in twelve runs on
/// 2026-08-06 with its message uncaptured; its rate-sized budget was removed because that is
/// defensible on its own terms, and that is explicitly NOT presented as the fix for that occurrence.
/// </para>
/// <para>
/// <b>QPC ORDER ACROSS A LAP IS UNASSERTABLE IN EITHER DIRECTION, and it cost three attempts to say
/// so.</b> Measured both ways on 2026-08-06: under the full suite the drop test's first drained record
/// came back ~148 ms LATER than the second; run alone, the same batch was perfectly ascending. Both are
/// legal. <c>Drain</c> resumes at <c>writeIndex - capacity</c> — the oldest SURVIVOR — and whether the
/// writer has already overwritten that slot when the copy reaches it is a race decided in microseconds.
/// The seqlock catches a tear DURING a copy; a slot cleanly overwritten BEFORE the copy started is
/// indistinguishable from a slot that was always that new.
/// </para>
/// <para>
/// So ascending qpc is a property of a reader that KEPT UP, and it is asserted only in
/// <c>AssertRecordsAreHonest</c> alongside the other attach-timing properties. Asserting it — or its
/// negation — against a deliberately lapped reader is a coin flip wearing a property's name. What
/// actually matters is downstream and is enforced where it belongs: <c>03_METRICS</c> derives frame
/// times from consecutive qpc, so a consumer trusting order across a drop would manufacture a NEGATIVE
/// interval. <c>MeasuredFacts</c> skips non-positive deltas for exactly this reason, and
/// <c>04_CAPTURE</c> requires a non-zero drop count to be surfaced as a session warning.
/// </para>
/// </remarks>
[Trait("Category", "Integration")]
public sealed class ShmDrainIntegrationTests
{
    private static string Harness => Path.Combine(AppContext.BaseDirectory, "hook-harness.exe");

    private static string Payload => Path.Combine(AppContext.BaseDirectory, "FrameLedger.Overlay.dll");

    /// <summary>
    /// Puts the machine into the state the product puts it in, using the product's own seeder.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>Without this the guard refuses every target with <c>RulesUnreadable</c>, and this test passed
    /// on my machine for exactly that reason: it already had a rules file.</b> CI does not, and said so
    /// — "the guard refused our own harness: RulesUnreadable". The guard reads its blocklist from one
    /// location under Local AppData and fail-closes when it is absent, which is correct and is also
    /// §S20's whole story: the first real injection's opening refusal was this.
    /// </para>
    /// <para>
    /// It runs the real <c>RulesSeeder</c> over the real <c>FileSystemRulesStore</c> — the same two
    /// lines <c>Agent/Program.cs</c> runs before anything else — rather than hand-installing a fixture.
    /// A test that seeded its own file would be testing a different rules file from the one the guard
    /// consumes, which is the class of mistake §S20 records for the seeder itself. It is idempotent:
    /// on a machine that already has a current file the outcome is <c>AlreadyCurrent</c> and nothing
    /// is written.
    /// </para>
    /// </remarks>
    private static async Task SeedRulesAsync()
    {
        RulesSeedOutcome outcome = await new RulesSeeder(new FileSystemRulesStore())
            .EnsureSeededAsync(TestContext.Current.CancellationToken)
            .ConfigureAwait(false);

        outcome.Should().NotBe(RulesSeedOutcome.WriteFailed);
        outcome.Should().NotBe(RulesSeedOutcome.PackagedSeedUnusable);
    }

    private static Process StartHarness(string arguments)
    {
        File.Exists(Harness).Should().BeTrue(
            "hook-harness.exe must be staged beside the test binary (FrameLedger.DrainFixtures.targets). "
            + "Run build.ps1 native first. This FAILS rather than skipping: an integration test that "
            + "quietly does nothing when its fixture is absent is a gate that cannot fail.");
        File.Exists(Payload).Should().BeTrue(
            "FrameLedger.Overlay.dll must sit in the guard's own directory or §S22 refuses every injection");

        var process = Process.Start(new ProcessStartInfo(Harness, arguments)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
        })!;

        Thread.Sleep(800);    // let it create its device and start presenting
        process.HasExited.Should().BeFalse("the harness must still be running when we inject");
        return process;
    }

    private static void Kill(Process p)
    {
        try
        {
            if (!p.HasExited)
            {
                p.Kill(entireProcessTree: true);
            }
        }
        catch (InvalidOperationException)
        {
            // Already gone. Nothing to do, and nothing about the assertions depends on it.
        }

        p.Dispose();
    }


    private static async Task<ShmRingReader> AttachAsync(int pid, string ownBuildId)
    {
        // The Overlay publishes its handshake on the init thread, which runs AFTER LoadLibrary returns,
        // so poll rather than sleeping a guessed amount.
        for (int i = 0; i < 100; i++)
        {
            ShmRingReader? reader = ShmRingReader.TryAttach(pid, ownBuildId, out _);
            if (reader is not null)
            {
                return reader;
            }

            await Task.Delay(50, TestContext.Current.CancellationToken).ConfigureAwait(false);
        }

        throw new InvalidOperationException("the injected Overlay never published a usable handshake");
    }

    /// <summary>
    /// Polls until the writer's status has left <see cref="FlStatus.Init"/>, bounded at 5 s.
    /// </summary>
    /// <remarks>
    /// The handshake <see cref="ShmRingReader.TryAttach"/> keys on is published before the present hooks go
    /// in, and the present hook records frames before the init thread's last steps (loader hook, loader
    /// words, opengl32) reach the READY store. A case that laps the ring in ~50 ms can therefore reach a
    /// status assertion while the init thread is still between the two -- measured 2026-09-09 as INIT (0)
    /// about one run in four, on an unmodified main and on a branch that touched no IPC code alike. An
    /// Overlay that never leaves INIT still fails the caller's assertion: the budget is bounded and the
    /// assertion is unchanged.
    /// </remarks>
    private static async Task WaitUntilInitFinishedAsync(ShmRingReader reader)
    {
        for (int i = 0; i < 100 && reader.WriterState.Status == (uint)FlStatus.Init; i++)
        {
            await Task.Delay(50, TestContext.Current.CancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// The honesty invariants that hold for EVERY fixture, whatever the reader did.
    /// </summary>
    /// <remarks>
    /// Split out from <see cref="AssertRecordsAreHonest"/> because the rest of that helper is about the
    /// attach TIMING and about a single-swapchain target, and a fixture that deliberately stalls the
    /// reader or overflows the writer's swapchain table satisfies neither. Reusing the whole helper
    /// there produced a red test whose failure message was about frame indices — indistinguishable from
    /// a genuine regression in the thing under test.
    /// </remarks>
    private static void AssertWriterClaimsOnlyWhatItMeasured(List<FlFrameRecord> records)
    {
        // QPC ORDER IS NOT HERE, and finding out why is worth the paragraph.
        //
        // It lived here until 2026-08-06, when the drop test failed with the FIRST drained record's qpc
        // ~148 ms LATER than the second. That is not a writer defect and not a reader defect: it is what
        // overwrite-oldest means. When the writer laps the reader, `Drain` resumes at
        // `writeIndex - capacity` — the oldest SURVIVING slot — and the writer is still running, so it
        // can overwrite that slot in the microseconds before the copy reaches it. The seqlock catches a
        // tear DURING a copy; a slot cleanly overwritten BEFORE the copy started is indistinguishable
        // from a slot that was always that new.
        //
        // So ascending qpc is a property of a reader that KEPT UP, which is the attach-timing family
        // that already lives in AssertRecordsAreHonest — not one of the writer-honesty invariants that
        // hold for every fixture. Asserting it against a deliberately-lapped reader is asserting that
        // the fixture failed to do the thing the test exists to make it do.

        // A present-only writer measured the output resolution and its own call arguments, and nothing
        // else. In layout v3 the zero-defaults are honest by construction — FlUpscaler.NotReported and
        // FlFgMode.NotReported are 0 — and the mask corroborates rather than being the sole defence.
        //
        // OutputRes is asserted as "no bit outside these three" rather than as an exact value, because
        // the overflow fixture legitimately produces records with PresentArgs alone: past 16 swapchains
        // the writer has no size to report and #57 made the bit conditional on there being one. The
        // third bit is DxgiPresents (2026-09-05): the present hook reads GetLastPresentCount on the chain
        // it saw, and every record after a chain's first claims the delta — a zero here, which the
        // byte assertion below pins, because this fixture presents only through the patched body.
        records.Should().OnlyContain(
            r => (r.MeasuredMask & ~(ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.DxgiPresents)) == 0,
            "the writer must claim exactly what it measured — no more");
        // ...AND A PAUSE IS NOT AN UNSEEN PRESENT. The paused-session case below lets the harness
        // present ~85 times while the hook declines them; the first record after the resume claimed
        // them all as "presents DXGI counted and this hook never saw" until the epoch guard in
        // RecordPresent made a declined present invalidate the chain's last reading. This assertion
        // runs on every fixture, so that case is the one that keeps the guard honest.
        records.Should().OnlyContain(r => r.DxgiUnseen == 0,
            "DXGI must count nothing this hook did not on a fixture that presents only through the patched body "
            + "— including across a pause, which the hook SAW and declined");
        records.Should().Contain(r => (r.MeasuredMask & (ushort)FlMeasured.DxgiPresents) != 0,
            "after a chain's first hooked present the counter has a previous value to difference against");
        records.Should().OnlyContain(r => (r.MeasuredMask & (ushort)FlMeasured.PresentArgs) != 0,
            "syncInterval and presentFlags are the call's own arguments; a DXGI present hook always has them");
        records.Should().OnlyContain(r => r.RtFlags == 0,
            "v3 rtFlags bits are *_OBSERVED, so a present-only writer sets none of them");
        records.Should().OnlyContain(r => (r.MeasuredMask & (ushort)FlMeasured.Rt) == 0,
            "and FlMeasured.Rt clear is what makes that zero read as N/A rather than a measured absence");
        records.Should().OnlyContain(r => r.Upscaler == (byte)FlUpscaler.NotReported,
            "the v3 zero-default must not say 'we looked and there was no upscaler'");
        records.Should().OnlyContain(r => r.FgMode == (byte)FlFgMode.NotReported);
        records.Should().OnlyContain(r => r.FeatureFlags == 0);
        records.Should().OnlyContain(r => r.Api == (byte)FlApi.D3D11);
    }

    private static void AssertRecordsAreHonest(List<FlFrameRecord> records)
    {
        // CONTIGUOUS, BUT NOT NECESSARILY FROM ZERO — and the difference is the reader working, not a
        // gap in the stream.
        //
        // This asserted `FrameIndex == i` and passed until the seeding call above was added, which
        // changed the timing enough to make the first drained record frameIndex 2. That is CORRECT:
        // TryAttach seeds the read index from writeIndex, precisely so a reader never ingests records
        // published before it attached. Between FlGuardedInject returning and the attach succeeding the
        // Overlay is already presenting, so the frames in that window belong to nobody.
        //
        // A test can be wrong in a way that looks exactly like the code being wrong. What "every present
        // we saw became exactly one record" actually means here is that the indices we DID see form an
        // unbroken run — no hole, no repeat — starting wherever we came in.
        //
        // BOTH THIS AND THE SINGLE-CHAIN ASSERTIONS BELOW ARE FIXTURE-DEPENDENT, which is why the
        // invariants that are not live in their own helper.
        records[0].FrameIndex.Should().BeLessThan(50u,
            "we attach within a few frames of injecting; a large first index means the attach seed drifted");

        for (int i = 1; i < records.Count; i++)
        {
            records[i].FrameIndex.Should().Be(
                records[i - 1].FrameIndex + 1,
                "frameIndex is assigned once per observed present, so the drained run must be unbroken");
        }

        AssertWriterClaimsOnlyWhatItMeasured(records);

        // Ascending qpc belongs HERE, with the other properties of a reader that kept up — see the note
        // in the helper above for why it is false across a lap.
        records.Select(r => r.Qpc).Should().BeInAscendingOrder("QPC is read at hook entry");

        records.Should().OnlyContain(r => (r.MeasuredMask & (ushort)FlMeasured.OutputRes) != 0);
        records.Should().OnlyContain(r => r.SwapchainId != 0, "0 means the writer could not identify it");
        records.Should().OnlyContain(r => r.OutputW > 0 && r.OutputH > 0);
    }

    private static void AssertWriterHealthy(ShmRingReader reader, string ownBuildId)
    {
        // The Overlay's own health, which the frame stream cannot report: a DLL that never hooked
        // anything, or self-disabled after three faults, produces an empty ring that looks exactly like
        // a game sitting in a loading screen.
        FlWriterState state = reader.WriterState;
        state.Status.Should().Be((uint)FlStatus.Ready);
        state.FaultCount.Should().Be(0u);
        (state.ApiMask & (1u << (int)FlApi.D3D11)).Should().NotBe(0u);

        reader.Handshake.BuildIdString().Should().Be(ownBuildId,
            "the refuse-to-attach comparison must be comparing two real values, not '' with ''");
    }

    [Fact]
    public void TheTargetIsOurOwnHarnessAndNothingElse()
    {
        // The constraint that keeps this test from becoming §S9's user-runnable injector by increments.
        // If someone later points it at a real game, this is what says no.
        Path.GetFileName(Harness).Should().Be("hook-harness.exe");
        Path.GetDirectoryName(Harness).Should().Be(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar));
    }

    [Fact]
    public async Task TheGuardInjectsTheOverlayAndTheReaderDrainsRealFrames()
    {
        await SeedRulesAsync();

        Process harness = StartHarness("--real --hold-presenting 12");
        try
        {
            var guard = new NativeAntiCheatGuard();

            // The REAL guard with REAL sources — no test seam. The harness carries no anti-cheat, so a
            // refusal here is a genuine finding about this machine, not a fixture problem.
            AntiCheatVerdict verdict = await guard.GuardedInjectAsync(harness.Id, Payload, TestContext.Current.CancellationToken);
            verdict.IsAllowed.Should().BeTrue(
                $"the guard refused our own harness: {verdict.Reason} {verdict.Family} {verdict.Signal}");

            string ownBuildId = NativeAntiCheatGuard.BuildId();
            using (ShmRingReader reader = await AttachAsync(harness.Id, ownBuildId))
            {
                // Supervision, through GuardSupervisor rather than a second tick counter. It advances at
                // exactly one site on the far side of a returned verdict; a timer-shaped tick is what
                // fl_shm.h spends sixteen lines forbidding.
                var supervisor = new GuardSupervisor(guard);

                var records = new List<FlFrameRecord>();
                var buffer = new FlFrameRecord[512];
                var gaps = new List<ulong>();

                // Drain until the floor is met or the hold runs out — see the class remarks for what
                // this does and does not claim.
                const int floor = 50;
                await DrainUnderSupervisionAsync(reader, supervisor, harness.Id, buffer, gaps, records, floor);

                supervisor.CompletedEvaluations.Should().BeGreaterThan(1u,
                    "the loop must have supervised more than once, or it is not the drain-under-supervision "
                    + "case it is named for");

                // A FLOOR, NOT AN EXACT COUNT. Injection lands ~800 ms after spawn, so every present
                // before the hook installs is structurally lost — "N presents → N records" is
                // unsatisfiable here, and an acceptance criterion that cannot pass carries as little
                // information as one that cannot fail.
                records.Should().HaveCountGreaterThan(floor, "the harness presents at ~120/s for 12 s");

                AssertRecordsAreHonest(records);

                gaps.Should().BeEmpty("a single writer at ~120/s against an 8192-slot ring tears nothing");
                reader.TotalDropped.Should().Be(0, "and a 150 ms drain cadence cannot fall 16 s behind");

                AssertWriterHealthy(reader, ownBuildId);
            }
        }
        finally
        {
            Kill(harness);
        }
    }

    /// <summary>
    /// Ticks and drains until the Overlay is demonstrably recording, and returns how much it produced.
    /// </summary>
    /// <remarks>
    /// A stop, a pause or a drop asserted against a capture side that was never writing proves nothing.
    /// This file has hit that trap before, which is why establishing it first is a shared step rather
    /// than something each case remembers.
    /// </remarks>
    private static async Task<int> EstablishRecordingAsync(ShmRingReader reader, FlFrameRecord[] buffer,
        Ref<uint> tick)
    {
        // 100 iterations, not 40. The harness presents at ~120/s so ten records take under a second —
        // but four test assemblies run in parallel, each spawning a harness and injecting an Overlay
        // that creates a WARP device, and a budget sized on the measured rate is the thing that goes
        // red under contention while nothing is wrong.
        const int enough = 10;
        int seen = 0;
        for (int i = 0; i < 100 && seen < enough; i++)
        {
            reader.PublishGuardResult(++tick.Value, unhookRequested: false);
            await Task.Delay(100, TestContext.Current.CancellationToken).ConfigureAwait(false);
            seen += reader.Drain(buffer).Copied;
        }

        // THE LOOP BOUND AND THE ASSERTION MUST BE THE SAME NUMBER, and they were not: the loop exited
        // at `seen >= 10` — usually exactly 10 — and the assertion demanded `> 10`, so it passed only
        // when one drain happened to bring in eleven or more at once. It read as a contention flake for
        // two rounds because the timing decided whether the off-by-one was visible. One constant now.
        seen.Should().BeGreaterThanOrEqualTo(
            enough, "the harness must be presenting before anything asserted below means anything");
        return seen;
    }

    private sealed class Ref<T>
    {
        public T Value = default!;
    }

    /// <summary>
    /// Waits for <c>writeIndex</c> to stop moving and returns where it stopped.
    /// </summary>
    /// <remarks>
    /// <b>Wait for the writer to settle; do not assume how long an in-flight present
    /// takes.</b> This was a fixed 250 ms delay commented "presents already in flight" —
    /// fine at the harness's ~120/s until the suite runs four assemblies in parallel and
    /// the presenting thread is descheduled past it. A present that entered
    /// <c>MayObserve()</c> BEFORE the pause flag was set then lands after the index is
    /// captured, and "a paused writer records nothing" fails against a writer that had
    /// in fact stopped. Same class as the three assertions #61 fixed: a state read at a
    /// moment chosen by a constant rather than by the state itself.
    /// <para>
    /// It keeps ticking throughout, because the pause path is only reachable on a frame
    /// where <c>guardTicks</c> has changed — that is the defect #46 fixed and the reason
    /// this test exists.
    /// </para>
    /// </remarks>
    private static async Task<ulong> SettleAsync(ShmRingReader reader, FlFrameRecord[] buffer, Ref<uint> tick)
    {
        ulong last = reader.WriterState.WriteIndex;
        for (int i = 0; i < 40; i++)
        {
            reader.PublishGuardResult(++tick.Value, unhookRequested: false);
            await Task.Delay(100, TestContext.Current.CancellationToken).ConfigureAwait(false);
            reader.Drain(buffer);

            ulong now = reader.WriterState.WriteIndex;
            if (now == last)
            {
                return now;    // two reads 100 ms apart with nothing between them
            }

            last = now;
        }

        return last;
    }

    /// <summary>
    /// Drains until caught up, because <c>Drain</c> copies at most <c>into.Length</c> and returns.
    /// </summary>
    /// <remarks>
    /// One call is not "the ring", and a test that made that mistake would under-count exactly when the
    /// reader had fallen behind — which is the state two of these cases are about.
    /// </remarks>
    /// <summary>
    /// Drains under a live guard loop until <paramref name="floor"/> records are in hand
    /// or the wall clock runs out.
    /// </summary>
    /// <remarks>
    /// The tick is published from <c>GuardSupervisor.CompletedEvaluations</c> at exactly
    /// one site, on the far side of a returned verdict — a timer-shaped tick is what
    /// <c>fl_shm.h</c> spends sixteen lines forbidding, and a loop-owned counter here
    /// would be one.
    /// </remarks>
    private static async Task DrainUnderSupervisionAsync(ShmRingReader reader, GuardSupervisor supervisor,
        int pid, FlFrameRecord[] buffer, IList<ulong> gaps, List<FlFrameRecord> records, int floor)
    {
        DateTimeOffset until = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (records.Count <= floor && DateTimeOffset.UtcNow < until)
        {
            bool mayContinue = await supervisor.ScanOnceAsync(pid, TestContext.Current.CancellationToken)
                .ConfigureAwait(false);
            reader.PublishGuardResult(supervisor.CompletedEvaluations, supervisor.UnhookRequested);
            mayContinue.Should().BeTrue("our own harness must not start matching the blocklist mid-run");

            DrainResult r = reader.Drain(buffer, gaps);
            records.AddRange(buffer.AsSpan(0, r.Copied).ToArray());
            await Task.Delay(150, TestContext.Current.CancellationToken).ConfigureAwait(false);
        }
    }

    private static List<FlFrameRecord> DrainAll(ShmRingReader reader)
    {
        var buffer = new FlFrameRecord[1024];
        var records = new List<FlFrameRecord>();
        var gaps = new List<ulong>();
        DrainResult r;
        do
        {
            r = reader.Drain(buffer, gaps);
            records.AddRange(buffer.AsSpan(0, r.Copied).ToArray());
        }
        while (r.Copied == buffer.Length);

        return records;
    }

    /// <summary>
    /// <c>pauseRequested</c>, driven across the process boundary for the first time.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Both halves existed and neither had ever been driven together:
    /// <c>ShmRingReader.SetPaused</c> had no test at all, and <c>MayObserve()</c> has read the flag
    /// since #46. The unit test beside this one proves the byte lands at the right offset; this proves
    /// the Overlay acts on it.
    /// </para>
    /// <para>
    /// <b>It keeps ticking through the pause, deliberately.</b> The defect #46 fixed only appeared on
    /// frames where <c>guardTicks</c> had CHANGED — the freshness check sat between the safety stop and
    /// the pause check and returned early, so the first present after every evaluation was recorded
    /// regardless of pause. A pause test that stopped ticking would not reach it.
    /// </para>
    /// <para>
    /// <b>A pause is invisible in the record stream</b>, and that is pinned here rather than discovered
    /// later: <c>MayObserve()</c> returns false BEFORE <c>rec.frameIndex = g_frameIndex++</c>, so the
    /// frame-index run stays perfectly contiguous while the qpc gap spans the whole pause. A consumer
    /// inferring gaps from <c>frameIndex</c> would read a pause as one enormous frame time — the
    /// fabricated interval <c>07_IPC</c> forbids for torn records.
    /// </para>
    /// </remarks>
    [Fact]
    public async Task APausedSessionStopsRecordingAndResumesWhereItLeftOff()
    {
        await SeedRulesAsync();

        Process harness = StartHarness("--real --hold-presenting 25");
        try
        {
            var guard = new NativeAntiCheatGuard();
            (await guard.GuardedInjectAsync(harness.Id, Payload, TestContext.Current.CancellationToken))
                .IsAllowed.Should().BeTrue();

            using ShmRingReader reader = await AttachAsync(harness.Id, NativeAntiCheatGuard.BuildId());

            var buffer = new FlFrameRecord[512];
            var tick = new Ref<uint>();
            await EstablishRecordingAsync(reader, buffer, tick);

            reader.SetPaused(true);
            ulong atPause = await SettleAsync(reader, buffer, tick);
            await Task.Delay(1200, TestContext.Current.CancellationToken);
            reader.PublishGuardResult(++tick.Value, unhookRequested: false);

            reader.WriterState.WriteIndex.Should().Be(atPause, "a paused writer records nothing");
            reader.WriterState.Status.Should().Be((uint)FlStatus.Ready, "pausing is not stopping");
            reader.WriterState.FaultCount.Should().Be(0u, "and it is not a fault either");

            // AND IT RESUMES. One-way would be a stop, and the Agent needs to end a pause it started.
            reader.SetPaused(false);
            for (int i = 0; i < 40 && reader.WriterState.WriteIndex == atPause; i++)
            {
                await Task.Delay(50, TestContext.Current.CancellationToken);
            }

            reader.WriterState.WriteIndex.Should().BeGreaterThan(atPause);

            // 60 iterations (3 s), not 20 (1 s). Swept with the settle loop above rather than left for
            // the next post-merge run: five records at the harness's ~120/s take ~42 ms, and a budget
            // sized on the measured rate is exactly what goes red under four parallel assemblies while
            // nothing is wrong.
            var after = new List<FlFrameRecord>();
            for (int i = 0; i < 60 && after.Count < 5; i++)
            {
                DrainResult r = reader.Drain(buffer);
                after.AddRange(buffer.AsSpan(0, r.Copied).ToArray());
                await Task.Delay(50, TestContext.Current.CancellationToken);
            }

            after.Should().NotBeEmpty("the writer resumed, so the reader must see it");
            AssertWriterClaimsOnlyWhatItMeasured(after);
        }
        finally
        {
            Kill(harness);
        }
    }

    /// <summary>
    /// The drop branch, against the real writer.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Every existing assertion about it is <c>== 0</c>: <c>guard_test</c>'s native drain and this
    /// file's own end-to-end case. Both are honest — a 150 ms cadence cannot fall 16 s behind — and
    /// both are structurally incapable of exercising the branch. The only other coverage is
    /// <c>ShmRingReaderTests</c>' synthetic writer, which the test itself drives.
    /// </para>
    /// <para>
    /// <c>04_CAPTURE</c> defines a non-zero drop count as "the Agent stalled for over ~16 s" and
    /// requires a session warning. Lapping an 8192-slot ring at the harness's default ~120/s takes
    /// 68 s, which is what <c>--present-interval-ms</c> exists for: uncapped, the harness measured
    /// 171,636 presents in 3 s, so the ring laps in well under a second.
    /// </para>
    /// <para>
    /// <b>The lap budget is sized for a LOADED machine, not for that measured rate.</b> The suite runs
    /// four test assemblies in parallel, each spawning its own harness and injecting an Overlay that
    /// creates a WARP device, and under that contention the writer's own present loop is starved. A
    /// 4 s budget failed once in three runs on 2026-08-06 — the vacuity guard reported the FIXTURE not
    /// reaching its state, which is honest and still a flake. 15 s keeps the guard meaningful (it still
    /// fails if the writer never laps) and fits inside the harness's 20 s hold.
    /// </para>
    /// <para>
    /// <b>Its canary is SHARED with the synthetic case, and that is worth saying rather than leaving
    /// to be discovered.</b> Deleting the <c>dropped = …</c> assignment turns this red and
    /// <c>ShmRingReaderTests.OverwrittenRecordsAreCountedAndTheReaderResumesAtTheOldestSurvivor</c> red
    /// too — verified — so observing red does not isolate this case. What this adds is not a branch the
    /// synthetic test misses; it is that the branch has now run against the REAL Overlay as the writer,
    /// with a real seqlock, a real lap and a real 64-byte copy racing it. The same argument does NOT
    /// justify a native twin: <c>ring_test</c> already drives the branch in-process in the merge gate,
    /// so a cross-process native case would add a shared canary and nothing else.
    /// </para>
    /// </remarks>
    [Fact]
    public async Task TheReaderReportsRecordsItLostWhileItWasNotDraining()
    {
        await SeedRulesAsync();

        Process harness = StartHarness("--real --hold-presenting 20 --present-interval-ms 0");
        try
        {
            var guard = new NativeAntiCheatGuard();
            (await guard.GuardedInjectAsync(harness.Id, Payload, TestContext.Current.CancellationToken))
                .IsAllowed.Should().BeTrue();

            using ShmRingReader reader = await AttachAsync(harness.Id, NativeAntiCheatGuard.BuildId());

            // TryAttach seeds the read index from writeIndex, so we start caught up and everything the
            // drop counter reports below is ours to have lost.
            //
            // RecordsBeforeAttach is NOT asserted to be non-zero: the attach poll can win the race with
            // the Overlay's first present, and it did on the first run of this test. That is the reader
            // working, not a fixture problem — and pinning attach timing here would be the same
            // fixture-dependent assertion that made reusing AssertRecordsAreHonest wrong.
            ulong publishedBeforeWeLooked = reader.RecordsBeforeAttach;
            reader.TotalDropped.Should().Be(0, "attaching to a ring in flight is not a stall");

            // DO NOT DRAIN. Keep ticking, so this is a test about drops and not accidentally one about
            // supervision loss — the Overlay stops observing 65 s after guardTicks last moved.
            ulong start = reader.WriterState.WriteIndex;
            uint tick = 0;
            for (int i = 0; i < 150 && reader.WriterState.WriteIndex - start < ShmLayout.DefaultCapacity + 500; i++)
            {
                reader.PublishGuardResult(++tick, unhookRequested: false);
                await Task.Delay(100, TestContext.Current.CancellationToken);
            }

            (reader.WriterState.WriteIndex - start).Should().BeGreaterThan(ShmLayout.DefaultCapacity,
                "the writer has to LAP the ring or there is nothing for the reader to have lost");

            List<FlFrameRecord> records = DrainAll(reader);

            reader.TotalDropped.Should().BeGreaterThan(0,
                "the writer overwrote slots this reader never consumed, and only the reader can know it");
            reader.RecordsBeforeAttach.Should().Be(publishedBeforeWeLooked,
                "the pre-attach history is a separate figure and a stall must not be added to it — "
                + "07_IPC keeps the two apart precisely so the drop warning's magnitude is not set by "
                + "how long the game had been running before anyone attached");

            records.Should().NotBeEmpty("everything still in the ring is still readable");
            AssertWriterClaimsOnlyWhatItMeasured(records);

            // QPC order is deliberately NOT asserted here, in either direction. See the class remarks.
            await WaitUntilInitFinishedAsync(reader);
            reader.WriterState.Status.Should().Be((uint)FlStatus.Ready, "dropping records is the READER falling behind");
            reader.WriterState.FaultCount.Should().Be(0u);
        }
        finally
        {
            Kill(harness);
        }
    }

    [Fact]
    public async Task AnUnhookRequestStopsTheCaptureSideAndTheDrainCanSeeIt()
    {
        // The safety stop, driven through the reader the Agent will use rather than through a test that
        // pokes the mapping directly. 19_SAFETY calls this the single most important runtime behaviour
        // in the capture layer.
        await SeedRulesAsync();

        Process harness = StartHarness("--real --hold-presenting 15");
        try
        {
            var guard = new NativeAntiCheatGuard();
            (await guard.GuardedInjectAsync(harness.Id, Payload, TestContext.Current.CancellationToken)).IsAllowed.Should().BeTrue();

            string ownBuildId = NativeAntiCheatGuard.BuildId();
            using (ShmRingReader reader = await AttachAsync(harness.Id, ownBuildId))
            {

                var buffer = new FlFrameRecord[512];
                uint tick = 0;

                // Establish that it IS recording. A stop asserted against a capture side that was never
                // writing proves nothing.
                int seen = 0;
                // `<= 10`, NOT `< 10`: the loop used to exit the moment `seen` reached 10 while the
                // assertion below demands MORE than 10, so it passed only when a drain overshot and
                // failed with "found 10" when one landed on the boundary. #62's shape, in a case that
                // sweep missed, and #62's remedy — make the LOOP guarantee what the assertion demands.
                for (int i = 0; i < 40 && seen <= 10; i++)
                {
                    reader.PublishGuardResult(++tick, unhookRequested: false);
                    await Task.Delay(100, TestContext.Current.CancellationToken);
                    seen += reader.Drain(buffer).Copied;
                }

                seen.Should().BeGreaterThan(10, "the harness must be presenting before the stop means anything");

                // THE STOP.
                reader.PublishGuardResult(++tick, unhookRequested: true);

                for (int i = 0; i < 100 && reader.WriterState.Status != (uint)FlStatus.Unhooked; i++)
                {
                    await Task.Delay(50, TestContext.Current.CancellationToken);
                }

                reader.WriterState.Status.Should().Be((uint)FlStatus.Unhooked);

                // And it stays stopped while the harness keeps presenting — an Overlay that merely
                // reported the status and carried on writing would be caught here.
                reader.Drain(buffer);
                ulong atStop = reader.WriterState.WriteIndex;
                await Task.Delay(700, TestContext.Current.CancellationToken);
                reader.WriterState.WriteIndex.Should().Be(atStop, "stopping is one-way");
            }
        }
        finally
        {
            Kill(harness);
        }
    }
}
