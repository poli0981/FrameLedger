using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Infrastructure.AntiCheat;
using FrameLedger.Infrastructure.Ipc;
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
/// </remarks>
[Trait("Category", "Integration")]
public sealed class ShmDrainIntegrationTests
{
    private static string Harness => Path.Combine(AppContext.BaseDirectory, "hook-harness.exe");

    private static string Payload => Path.Combine(AppContext.BaseDirectory, "FrameLedger.Overlay.dll");

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

    private static void AssertRecordsAreHonest(List<FlFrameRecord> records)
    {
        // frameIndex is assigned once per OBSERVED present, so contiguity from 0 is the exact form of
        // "every present we saw became exactly one record".
        for (int i = 0; i < records.Count; i++)
        {
            records[i].FrameIndex.Should().Be((uint)i, "frameIndex must be a contiguous run from 0");
        }

        records.Select(r => r.Qpc).Should().BeInAscendingOrder("QPC is read at hook entry");

        // A present-only writer measured the output resolution and nothing else; the mask is what stops
        // the zero-defaults being read as measured facts.
        records.Should().OnlyContain(r => r.MeasuredMask == (byte)FlMeasured.OutputRes,
            "the writer must claim exactly what it measured — no more, and no less");
        records.Should().OnlyContain(r => (r.RtFlags & (byte)FlRtFlags.NotMeasured) != 0,
            "rtFlags = 0 would assert a MEASURED 'this title does not ray-trace'");
        records.Should().OnlyContain(r => r.Api == (byte)FlApi.D3D11);
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

                for (int tick = 0; tick < 12; tick++)
                {
                    bool mayContinue = await supervisor.ScanOnceAsync(harness.Id, TestContext.Current.CancellationToken);
                    reader.PublishGuardResult(supervisor.CompletedEvaluations, supervisor.UnhookRequested);
                    mayContinue.Should().BeTrue("our own harness must not start matching the blocklist mid-run");

                    DrainResult r = reader.Drain(buffer, gaps);
                    records.AddRange(buffer.AsSpan(0, r.Copied).ToArray());
                    await Task.Delay(150, TestContext.Current.CancellationToken);
                }

                // A FLOOR, NOT AN EXACT COUNT. Injection lands ~800 ms after spawn, so every present
                // before the hook installs is structurally lost — "N presents → N records" is
                // unsatisfiable here, and an acceptance criterion that cannot pass carries as little
                // information as one that cannot fail.
                records.Should().HaveCountGreaterThan(50, "the harness presents at ~120/s for 12 s");

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

    [Fact]
    public async Task AnUnhookRequestStopsTheCaptureSideAndTheDrainCanSeeIt()
    {
        // The safety stop, driven through the reader the Agent will use rather than through a test that
        // pokes the mapping directly. 19_SAFETY calls this the single most important runtime behaviour
        // in the capture layer.
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
                for (int i = 0; i < 40 && seen < 10; i++)
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
