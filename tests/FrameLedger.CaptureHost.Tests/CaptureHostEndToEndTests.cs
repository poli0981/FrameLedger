using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Consent;
using FrameLedger.Application.Persistence;
using FrameLedger.Application.Recording;
using FrameLedger.CaptureHost.Consent;
using FrameLedger.Domain.Consent;
using FrameLedger.Domain.Sessions;
using FrameLedger.Infrastructure.Persistence;
using FrameLedger.Infrastructure.Recording;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests;

/// <summary>
/// The host as a separate process, which is the only arrangement that can show
/// <c>guardTicks</c> advancing from a NON-TEST binary.
/// </summary>
/// <remarks>
/// <para>
/// <b>hook-harness is the only thing this suite ever injects into</b> — our own
/// dummy D3D app, built from this tree, carrying no anti-cheat and belonging to no
/// publisher. Injecting into it raises no consent question at all: no game, no
/// account, no terms of service. <c>TheTargetAndTheConsentRecordAreBothOurOwnHarness</c>
/// asserts that constraint on the suite itself, so it cannot grow into §S9's
/// user-runnable injector by increments.
/// </para>
/// <para>
/// <b>The self-constraint covers the CONSENT RECORD as well as the target</b>, which
/// <c>ShmDrainIntegrationTests</c>' original version did not need to. That test could
/// only ever inject where its own hardcoded fixture pointed; this one writes a
/// record that satisfies the opt-in gate, and nothing about a record limits which
/// executable it names. One edited string would generalise it to any binary on the
/// machine, so the record's path is pinned too.
/// </para>
/// <para>
/// <b>Category=Integration, and CI will not run it.</b> §S19(b): the guard refuses a
/// .NET test host that has loaded a <c>protect</c>-matching module, measured on CI.
/// On this dev box the guard allows it, so these are dev-box-reproducible and
/// merge-gate-absent — the same split <c>ShmDrainIntegrationTests</c> lives with, and
/// stated rather than implied.
/// </para>
/// </remarks>
[Trait("Category", "Integration")]
public sealed class CaptureHostEndToEndTests : IDisposable
{
    private static string Harness => Path.Combine(AppContext.BaseDirectory, "hook-harness.exe");

    private static string Host => Path.Combine(AppContext.BaseDirectory, "FrameLedger.CaptureHost.exe");

    /// <summary>
    /// The ONE executable this suite may write a consent record for.
    /// </summary>
    /// <remarks>
    /// Every write goes through here, so "which binary did the suite consent to?" has
    /// one answer and one place to change it — and changing it is what the assertion
    /// below turns red.
    /// </remarks>
    private static string ConsentedExecutable => Harness;

    /// <summary>The host's own ledger, beside its binary (P2 PR-B): the same SQLite adapter the Agent will use.</summary>
    private static string HostLedger => Path.Combine(Path.GetDirectoryName(Host)!, LedgerPaths.DatabaseFileName);

    /// <summary>Writes the ONE consent record this suite may write, through the real store, and closes the ledger again.</summary>
    private static async Task<ConsentWriteOutcome> GrantConsentAsync()
    {
        ExecutableFingerprint fingerprint = FrameLedger.Infrastructure.Io.ExecutableIdentity.Read(ConsentedExecutable)!.Value;
        LedgerDatabase db = await LedgerDatabase.OpenAsync(HostLedger, ct: TestContext.Current.CancellationToken).ConfigureAwait(false);
        await using (db.ConfigureAwait(false))
        {
            return await new SqliteGameConsentStore(db).RecordOperatorAcknowledgementAsync(new OperatorAcknowledgement
            {
                Fingerprint = fingerprint,
                DisclosureVersion = OperatorDisclosure.Version,
                AcknowledgedAt = DateTimeOffset.UtcNow,
            }, TestContext.Current.CancellationToken).ConfigureAwait(false);
        }
    }

    private static async Task<GameConsentRecord> FindConsentAsync()
    {
        LedgerDatabase db = await LedgerDatabase.OpenAsync(HostLedger, ct: TestContext.Current.CancellationToken).ConfigureAwait(false);
        await using (db.ConfigureAwait(false))
        {
            return await new SqliteGameConsentStore(db).FindAsync(
                FrameLedger.Infrastructure.Io.ExecutableIdentity.Normalise(ConsentedExecutable), TestContext.Current.CancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>The ledger and WAL's two sidecars; a guarded delete of each.</summary>
    private static void DeleteHostLedger()
    {
        foreach (string suffix in new[] { "", "-wal", "-shm" })
        {
            try
            {
                File.Delete(HostLedger + suffix);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
            }
        }
    }

    public void Dispose()
    {
        // The host writes beside its own binary, which for a ProjectReference'd exe is this test's
        // output directory. Leaving a consent record there would make the NEXT run of the refusal case
        // pass for the wrong reason.
        DeleteHostLedger();
    }

    [Fact]
    public void TheTargetAndTheConsentRecordAreBothOurOwnHarness()
    {
        Path.GetFileName(ConsentedExecutable).Should().Be("hook-harness.exe");
        Path.GetDirectoryName(ConsentedExecutable).Should().Be(
            AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar));
        Path.GetFileName(Host).Should().Be("FrameLedger.CaptureHost.exe");
    }

    private static Process StartHarness(string arguments)
    {
        File.Exists(Harness).Should().BeTrue(
            "hook-harness.exe must be staged beside the test binary (FrameLedger.DrainFixtures.targets). "
            + "This FAILS rather than skipping: an integration test that quietly does nothing when its "
            + "fixture is absent is a gate that cannot fail.");
        File.Exists(Host).Should().BeTrue("the host must be built and copied beside this test");

        var p = Process.Start(new ProcessStartInfo(Harness, arguments)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
        })!;

        Thread.Sleep(800);
        p.HasExited.Should().BeFalse("the harness must be running before the host looks for it");
        return p;
    }

    private static Process StartHost(params string[] args)
    {
        var psi = new ProcessStartInfo(Host)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };

        foreach (string a in args)
        {
            psi.ArgumentList.Add(a);
        }

        return Process.Start(psi)!;
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
        }

        p.Dispose();
    }

    [Fact]
    public async Task WithNoConsentRecordTheHostRefusesAndNothingIsEverInjected()
    {
        // HANDOFF's acceptance criterion for item 1, from OUTSIDE the process: the refusal happens and
        // FlGuardedInject is never reached.
        //
        // THE HARNESS IS STARTED FIRST, and that is load-bearing. The host resolves its target before
        // it consults consent — a HookRequest cannot be built without a pid — so with nothing running
        // it would exit at TargetNotRunning, both structural assertions would still hold, and the
        // canary would be inert: a host mutated to call GuardedInjectAsync directly would ALSO exit at
        // TargetNotRunning and this would stay green with the chokepoint removed.
        // THE PRECONDITION IS ASSERTED, not assumed, and it is asserted because this test failed once
        // for exactly that reason: a consent record left behind by a sibling case made the host refuse
        // for a DIFFERENT reason, and the run before it had passed. A test whose verdict depends on
        // what ran before it is one this repository does not accept, so the leftover is now a failure
        // with its own message rather than an outcome that happens to differ.
        // Directory first, then a guarded delete. `File.Delete` on a path whose PARENT does not exist
        // throws DirectoryNotFoundException, and the parent is created only when some test actually
        // writes a record — so on a clean build output this line threw, and the case's verdict depended
        // on which sibling had run before it. In the very test whose comment says a test whose verdict
        // depends on what ran before it is one this repository does not accept.
        DeleteHostLedger();

        (await FindConsentAsync()).IsFromStore.Should().BeFalse(
                "this case is about an ABSENT record; a leftover one from a sibling test would make the "
                + "host refuse for a different reason and the assertions below would say nothing");

        Process harness = StartHarness("--real --hold-presenting 20");
        try
        {
            using Process host = StartHost("capture", "--exe", ConsentedExecutable);
            string stdout = await host.StandardOutput.ReadToEndAsync(TestContext.Current.CancellationToken);
            await host.WaitForExitAsync(TestContext.Current.CancellationToken);

            stdout.Should().Contain(nameof(SessionEndReason.RefusedHookNotEnabled),
                "an absent record is not-enabled before it is unconsented, and the two are different sentences");
            host.ExitCode.Should().NotBe(0);

            // THE STRONGEST EXTERNAL EVIDENCE AVAILABLE that FlGuardedInject was never reached: the
            // Overlay creates Local\FrameLedger.Ring.<pid> on its init thread, so if the section does
            // not exist, nothing was loaded into that process.
            harness.HasExited.Should().BeFalse("the target must still be alive for the next assertion to mean anything");
            Action open = () => MemoryMappedFile.OpenExisting(
                $@"Local\FrameLedger.Ring.{harness.Id}", MemoryMappedFileRights.Read).Dispose();
            open.Should().Throw<FileNotFoundException>("no ring means no Overlay means no injection");
        }
        finally
        {
            Kill(harness);
        }
    }

    [Fact]
    public async Task AConsentedSessionAdvancesGuardTicksFromANonTestBinary()
    {
        // The other half of HANDOFF's acceptance: "guardTicks advancing from a non-test binary".
        //
        // The record is written through the real store rather than through `consent grant`, which
        // refuses redirected stdin by design. That is the suite standing in for a human, so the
        // self-constraint above pins WHICH executable it may stand in for.
        (await GrantConsentAsync()).Should().Be(ConsentWriteOutcome.Written);

        Process harness = StartHarness("--real --hold-presenting 40");
        Process? host = null;
        try
        {
            host = StartHost("capture", "--exe", ConsentedExecutable);

            // THE TEST READS THE CONTROL BLOCK WITH ITS OWN VIEW, and never writes to it. PublishGuardResult
            // being the single writer of guardTicks and unhookRequested is what holds the publish ORDER
            // that ShmRingReader documents; a second writer would take that away.
            uint ticks = await PollGuardTicksAsync(harness.Id, atLeast: 1, TimeSpan.FromSeconds(20));

            ticks.Should().BeGreaterThanOrEqualTo(1u,
                "the host publishes its first tick immediately on attach, because the Overlay's 65 s "
                + "supervision clock starts when the mapping is published and not when we adopt it");

            FlWriterState state = await PollWriterPastInitAsync(harness.Id, TimeSpan.FromSeconds(15));

            // INIT after the budget is not a slow start — it is MinHook or the dummy-device probe having
            // failed, which means the ring will never move. That is CaptureSession's
            // WriterNeverInstalledHooks, and it must not read as a passing session here either.
            state.Status.Should().Be((uint)FlStatus.Ready,
                "the Overlay hooked and is recording; INIT past the budget means InstallPresentHooks "
                + "failed and no frame will ever be written");
            state.FaultCount.Should().Be(0u);
            (state.ApiMask & (1u << (int)FlApi.D3D11)).Should().NotBe(0u);
        }
        finally
        {
            if (host is not null)
            {
                Kill(host);
            }

            Kill(harness);
        }
    }

    [Fact]
    public async Task LaunchModeStartsTheHarnessItselfAndReportsWhatRanUnhooked()
    {
        // P1 item 2, from OUTSIDE the process: the host starts the consented harness, the guard waits
        // for dxgi to map and injects, and the report carries the two numbers 20_OPEN_QUESTIONS §S1
        // deferred on -- the wait, and DXGI's count of presents before the first hooked one.
        (await GrantConsentAsync()).Should().Be(ConsentWriteOutcome.Written);

        using Process host = StartHost("launch", "--exe", ConsentedExecutable, "--args", "--real --hold-presenting 9",
            "--seconds", "6");
        string stdout = await host.StandardOutput.ReadToEndAsync(TestContext.Current.CancellationToken);
        await host.WaitForExitAsync(TestContext.Current.CancellationToken);

        stdout.Should().Contain("launch: the guard injected", "the wait is printed, because it is the number §S1 lacked");
        stdout.Should().Contain("presents before the first hooked present:");
        stdout.Should().NotContain("not read", "the harness presents, so the first hooked present happened");
        host.ExitCode.Should().Be(0, stdout);

        // P2 PR-D, THE MILESTONE'S FIRST HALF from outside the process: the session is a ROW in the host's
        // own ledger, with the blobs beside it, written by the same code path the Agent will use.
        stdout.Should().Contain("ledger: session ").And.Contain("SAVED as sessions.id=", stdout);
        LedgerDatabase db = await LedgerDatabase.OpenAsync(HostLedger, ct: TestContext.Current.CancellationToken).ConfigureAwait(true);
        await using (db.ConfigureAwait(true))
        {
            var sessions = new SqliteSessionRepository(db);
            IReadOnlyList<SessionRow> rows = await sessions.ListRecentAsync(1, TestContext.Current.CancellationToken).ConfigureAwait(true);
            SessionRow row = rows.Should().ContainSingle().Subject;
            row.Tier.Should().Be(CaptureTier.Hooked);
            row.Mode.Should().Be(CaptureMode.Launch);
            row.ExitStatus.Should().Be(ExitStatus.Normal, "the harness was still presenting when the bounded capture ended");
            row.FrameCount.Should().BeGreaterThan(100, "6 s at ~120/s");
            row.PresentedFps.Should().BeGreaterThan(30);
            row.Api.Should().Be("d3d11");
            row.GuardTicksPublished.Should().BeGreaterThanOrEqualTo(1);
            row.LaunchWaitMs.Should().NotBeNull();
            row.OverlayBuildId.Should().NotBeNullOrEmpty("the handshake names the Overlay build");
            row.TelemetrySource.Should().Contain("l1", "the host composes L1 + L2 for every session");
            row.QpcFrequency.Should().Be(System.Diagnostics.Stopwatch.Frequency);
            FrameBlobs? frames = await sessions.FindFramesAsync(row.Id, TestContext.Current.CancellationToken).ConfigureAwait(true);
            frames.Should().NotBeNull("the blobs land in the same transaction as the row");
            frames!.SampleCount.Should().Be(row.FrameCount);
            // THIS session's .partial is gone; a sibling case kills a host on purpose and leaves one behind.
            string guid = System.Text.RegularExpressions.Regex.Match(stdout, @"ledger: session (?<guid>[0-9a-f]{32}) SAVED",
                System.Text.RegularExpressions.RegexOptions.ExplicitCapture, TimeSpan.FromSeconds(1)).Groups["guid"].Value;
            guid.Should().HaveLength(32, stdout);
            File.Exists(Path.Combine(PartialDirectory, guid + ".partial")).Should().BeFalse("finalize deletes the .partial");
        }
    }

    private static string PartialDirectory => Path.Combine(Path.GetDirectoryName(Host)!, "tmp");

    private static async Task AssertInterruptedRowAsync(Guid guid, int expectedFrames)
    {
        LedgerDatabase db = await LedgerDatabase.OpenAsync(HostLedger, ct: TestContext.Current.CancellationToken).ConfigureAwait(false);
        await using (db.ConfigureAwait(false))
        {
            SessionRow? row = await new SqliteSessionRepository(db).FindAsync(guid, TestContext.Current.CancellationToken).ConfigureAwait(false);
            row.Should().NotBeNull();
            row!.ExitStatus.Should().Be(ExitStatus.Interrupted);
            row.Tier.Should().Be(CaptureTier.Hooked);
            row.FrameCount.Should().Be(expectedFrames, "the prefix is the session");
            row.DurationSeconds.Should().BeGreaterThanOrEqualTo(5, "ended at the last flush, past the host's minimum");
            row.CaptureNotes.Should().Contain("recovered from .partial");
        }
    }

    [Fact]
    public async Task AKilledHostLeavesAPartialThatRecoverTurnsIntoAnInterruptedSession()
    {
        // P2 PR-D, the recovery half from OUTSIDE the process: the host is killed mid-session, the file it
        // flushed every 3 s is the only record, and `recover` -- what the Agent runs first at every start --
        // turns it into an `interrupted` row and removes it. Nothing here waits on a clock it does not own:
        // the guard tick is polled for, and the kill comes after two flushes have had time to land.
        (await GrantConsentAsync()).Should().Be(ConsentWriteOutcome.Written);
        Directory.CreateDirectory(PartialDirectory);
        HashSet<string> before = [.. Directory.EnumerateFiles(PartialDirectory, "*.partial")];

        Process harness = StartHarness("--real --hold-presenting 40");
        Process? host = null;
        string? file = null;
        try
        {
            host = StartHost("capture", "--exe", ConsentedExecutable);
            await PollGuardTicksAsync(harness.Id, atLeast: 1, TimeSpan.FromSeconds(20));
            await Task.Delay(TimeSpan.FromSeconds(8), TestContext.Current.CancellationToken);
            Kill(host);

            file = Directory.EnumerateFiles(PartialDirectory, "*.partial").Should().ContainSingle(f => !before.Contains(f),
                "the killed host left exactly one file, its own").Subject;
            PartialSession? partial = PartialSessionFile.Read(file);
            partial.Should().NotBeNull();
            partial!.LastTick.Should().NotBeNull("two 3 s flushes fit inside 8 s");
            partial.Records.Should().NotBeEmpty();

            using Process recover = StartHost("recover");
            string stdout = await recover.StandardOutput.ReadToEndAsync(TestContext.Current.CancellationToken);
            await recover.WaitForExitAsync(TestContext.Current.CancellationToken);

            recover.ExitCode.Should().Be(0, stdout);
            string guid = Path.GetFileNameWithoutExtension(file);
            stdout.Should().Contain(guid + ": Recovered as session", stdout);
            File.Exists(file).Should().BeFalse("recovery removes what it stored");

            await AssertInterruptedRowAsync(Guid.ParseExact(guid, "N"), partial.Records.Count);
        }
        finally
        {
            if (host is not null)
            {
                Kill(host);
            }

            Kill(harness);
            if (file is not null && File.Exists(file))
            {
                File.Delete(file);
            }
        }
    }

    [Fact]
    public async Task LaunchModeOnAVulkanTargetAttachesToTheLayersRingAndInjectsNothing()
    {
        // P1 item 3, from OUTSIDE the process: the host launches the harness in its Vulkan mode with the
        // layer's environment, the guard passes and injects nothing (TargetIsVulkanLayered), and the
        // records come from the layer's ring. Skipped honestly where there is no Vulkan loader or no
        // presentable device -- the harness says so with exit 77, which the session reports as
        // LaunchTargetExited before any ring existed.
        if (!File.Exists(Path.Combine(Environment.SystemDirectory, "vulkan-1.dll")))
        {
            Assert.Skip("no Vulkan loader on this machine");
        }

        (await GrantConsentAsync()).Should().Be(ConsentWriteOutcome.Written);

        using Process host = StartHost("launch", "--exe", ConsentedExecutable, "--args", "--vulkan --hold-presenting 8",
            "--seconds", "5");
        Task<string> stderrTask = host.StandardError.ReadToEndAsync(TestContext.Current.CancellationToken);
        string stdout = await host.StandardOutput.ReadToEndAsync(TestContext.Current.CancellationToken);
        await host.WaitForExitAsync(TestContext.Current.CancellationToken);
        stdout += "\n---- stderr ----\n" + await stderrTask + $"\n---- exit {host.ExitCode} ----\n";

        // The harness says so itself (exit 77, a "[SKIP]" line on the shared console) when this
        // machine has a loader but no device -- a hosted runner's shape. Its exit races the guard's
        // poll, so the session may read LaunchTargetExited or, if it died under the scan, whatever
        // the scan could not read; the harness's own line is the signal that does not race.
        if (stdout.Contains("[SKIP]", StringComparison.Ordinal)
            || stdout.Contains(nameof(SessionEndReason.LaunchTargetExited), StringComparison.Ordinal))
        {
            Assert.Skip("the harness found no presentable Vulkan device on this machine: " + stdout);
        }

        stdout.Should().Contain("vulkan layer: ", "the launch names the manifest it wrote and the enable-list entry");
        stdout.Should().Contain("capture side: the Vulkan implicit layer", stdout);
        stdout.Should().Contain("apiMask=0x8", "FL_API_VULKAN is bit 3; the ring is the layer's, not an Overlay's");
        stdout.Should().NotContain("records: 0 ", stdout);
        host.ExitCode.Should().Be(0, stdout);
    }

    [Fact]
    public async Task WhenTheTargetExitsTheHostStopsAndSaysWhy()
    {
        // §S29(e). The writer leaves status READY and writeIndex frozen on the way out — DllMain has no
        // DLL_PROCESS_DETACH teardown — so this can only come from the held process handle.
        (await GrantConsentAsync()).Should().Be(ConsentWriteOutcome.Written);

        Process harness = StartHarness("--real --hold-presenting 6");
        Process? host = null;
        try
        {
            host = StartHost("capture", "--exe", ConsentedExecutable);
            string stdout = await host.StandardOutput.ReadToEndAsync(TestContext.Current.CancellationToken);
            await host.WaitForExitAsync(TestContext.Current.CancellationToken);

            stdout.Should().Contain(nameof(SessionEndReason.TargetExited));
            stdout.Should().Contain("guard ticks published:");
            host.ExitCode.Should().Be(0, "a game closing is the ordinary end of a session");
        }
        finally
        {
            if (host is not null)
            {
                Kill(host);
            }

            Kill(harness);
        }
    }

    /// <summary>
    /// Waits for the writer to leave <see cref="FlStatus.Init"/>, because a tick and
    /// <c>INIT</c> are a legitimate simultaneous state.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>A real ordering in <c>InitThread</c>, not flakiness.</b> <c>PublishHandshake</c>
    /// writes <c>layoutVersion</c> at step 2; <c>InstallPresentHooks</c> — which creates a
    /// throwaway WARP D3D11 device and swapchain to read the vtable, tens of milliseconds
    /// — runs at step 5; <c>status</c> becomes <c>READY</c> only at step 6.
    /// <c>TryAttach</c> succeeds as soon as <c>layoutVersion</c> lands, and the host
    /// publishes its first tick immediately on attach BY DESIGN, because the Overlay's
    /// 65 s supervision clock starts at mapping publish. So reading status once after the
    /// first tick asserts a race rather than the property.
    /// </para>
    /// <para>
    /// Found by the post-merge run on a machine whose WARP had degraded — D3D12 WARP
    /// returning <c>DXGI_ERROR_DRIVER_INTERNAL_ERROR</c>, measured — which widened step 5
    /// enough to lose a race that had been won every previous time. The machine state was
    /// the trigger; this was the defect, and it was mine.
    /// </para>
    /// </remarks>
    private static async Task<FlWriterState> PollWriterPastInitAsync(int pid, TimeSpan budget)
    {
        FlWriterState state = default;
        DateTimeOffset deadline = DateTimeOffset.UtcNow + budget;
        while (DateTimeOffset.UtcNow < deadline)
        {
            using (MemoryMappedFile mmf = MemoryMappedFile.OpenExisting(
                       $@"Local\FrameLedger.Ring.{pid}", MemoryMappedFileRights.Read))
            using (MemoryMappedViewAccessor view = mmf.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read))
            {
                state = ReadWriterState(view);
            }

            // "HOOKED AND RECORDING" IS TWO EVENTS, AND apiMask IS THE SECOND ONE. status becomes READY
            // at InitThread step 6, immediately after the hooks are installed and BEFORE any present has
            // gone through them; apiMask is set inside FindOrAdd, on the first present the hook actually
            // sees. So READY with apiMask 0 is a legitimate window, and waiting only for READY left the
            // apiMask assertion racing — it failed once in five full-suite runs on 2026-08-06 and never
            // once in six isolated ones, which is the signature of a window widened by contention.
            //
            // A status that is neither INIT nor READY (SELF_DISABLED, UNHOOKED) is terminal: return it
            // and let the assertions report what it is, rather than spinning to the budget.
            bool stillStarting = state.Status == (uint)FlStatus.Init
                || (state.Status == (uint)FlStatus.Ready && state.ApiMask == 0);
            if (!stillStarting)
            {
                return state;
            }

            await Task.Delay(100, TestContext.Current.CancellationToken).ConfigureAwait(false);
        }

        return state;
    }

    private static async Task<uint> PollGuardTicksAsync(int pid, uint atLeast, TimeSpan budget)
    {
        DateTimeOffset deadline = DateTimeOffset.UtcNow + budget;
        uint last = 0;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                using MemoryMappedFile mmf = MemoryMappedFile.OpenExisting(
                    $@"Local\FrameLedger.Ring.{pid}", MemoryMappedFileRights.Read);
                using MemoryMappedViewAccessor view = mmf.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read);
                last = view.ReadUInt32(ShmLayout.ControlOffset + 12);    // FlControlBlock.guardTicks @12
                if (last >= atLeast)
                {
                    return last;
                }
            }
            catch (FileNotFoundException)
            {
                // The host has not injected yet.
            }

            await Task.Delay(100, TestContext.Current.CancellationToken).ConfigureAwait(false);
        }

        return last;
    }

    private static unsafe FlWriterState ReadWriterState(MemoryMappedViewAccessor view)
    {
        byte* p = null;
        view.SafeMemoryMappedViewHandle.AcquirePointer(ref p);
        try
        {
            return *(FlWriterState*)(p + ShmLayout.WriterOffset);
        }
        finally
        {
            view.SafeMemoryMappedViewHandle.ReleasePointer();
        }
    }
}
