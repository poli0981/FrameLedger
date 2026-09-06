// FrameLedger.CaptureHost — the unshipped driver of the guard loop.
//
// docs/12_BUILD.md publishes FrameLedger.App and FrameLedger.Agent and nothing
// else, and neither references this project. tools/package-closure-check.ps1 is
// what keeps that true rather than remembered.
//
// It exists because docs/HANDOFF.md's queue item 1 needs a NON-TEST binary to
// drive HookedCaptureGate -> FlGuardedInject -> ShmRingReader.TryAttach -> drain,
// and 20_OPEN_QUESTIONS §S27 explains at length why that binary must not be a
// flag on a shipped one.

using System.Diagnostics;
using System.Globalization;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Consent;
using FrameLedger.Application.Rules;
using FrameLedger.CaptureHost.Capture;
using FrameLedger.CaptureHost.Consent;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.CaptureHost.Telemetry;
using FrameLedger.Domain.Consent;
using FrameLedger.Infrastructure.AntiCheat;
using FrameLedger.Infrastructure.Io;
using FrameLedger.Infrastructure.Ipc;
using FrameLedger.Infrastructure.Rules;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost;

internal static class Program
{
    // Distinguishable from the outside, because the end-to-end test is a separate process and a
    // substring match on stdout is a weaker assertion than an exit code.
    private const int _exitOk = 0;
    private const int _exitUsage = 1;
    private const int _exitRefused = 2;
    private const int _exitRulesFailed = 3;
    private const int _exitAttachRefused = 4;
    private const int _exitStoppedForSafety = 5;
    private const int _exitTargetNotResolved = 6;

    private static async Task<int> Main(string[] args)
    {
        ArgumentNullException.ThrowIfNull(args);

        CommandLine cmd = CommandLine.Parse(args);
        if (cmd.Error is not null)
        {
            HostConsole.Problem(cmd.Error);
            return _exitUsage;
        }

        // BEFORE ANYTHING ELSE. The guard reads its blocklist from one location and fail-closes when it
        // is absent, so a capture started before the seed lands refuses for a reason that has nothing to
        // do with the game (§S20). ShmDrainIntegrationTests records CI hitting exactly this.
        RulesSeedOutcome seeded = await new RulesSeeder(new FileSystemRulesStore())
            .EnsureSeededAsync()
            .ConfigureAwait(false);
        if (seeded is RulesSeedOutcome.WriteFailed or RulesSeedOutcome.PackagedSeedUnusable)
        {
            HostConsole.Problem($"rules: FAILED ({seeded})");
            return _exitRulesFailed;
        }

        var store = new FileGameConsentStore();
        return cmd.Verb switch
        {
            Verb.ConsentList => await ListAsync(store).ConfigureAwait(false),
            Verb.ConsentGrant => await GrantAsync(store, cmd.ExePath!).ConfigureAwait(false),
            Verb.ConsentRevoke => await RevokeAsync(store, cmd.ExePath!).ConfigureAwait(false),
            Verb.Capture => await CaptureAsync(store, cmd.ExePath!, cmd.Seconds).ConfigureAwait(false),
            Verb.Launch => await LaunchAsync(store, cmd.ExePath!, cmd.Arguments, cmd.Seconds).ConfigureAwait(false),
            Verb.ProbeLhm => LhmProbe.Run(cmd.Seconds == 0 ? LhmProbe.DefaultSeconds : cmd.Seconds),
            _ => _exitUsage,
        };
    }

    private static async Task<int> ListAsync(FileGameConsentStore store)
    {
        HostConsole.Line($"consent store: {store.Destination}");
        IReadOnlyList<GameConsentRecord> enabled = await store.ListEnabledAsync().ConfigureAwait(false);
        if (enabled.Count == 0)
        {
            HostConsole.Line("  (nothing is enabled — hooking is off for every game by default)");
            return _exitOk;
        }

        foreach (GameConsentRecord r in enabled)
        {
            HostConsole.Line(
                $"  {r.Fingerprint.ExePath}  consent={r.ConsentedAt:u}  provenance={r.Provenance}  " +
                $"disclosure={r.DisclosureVersion}  blocked={r.BlockedReason ?? "-"}  unverified={r.PreScanUnverified}");
        }

        return _exitOk;
    }

    private static async Task<int> GrantAsync(FileGameConsentStore store, string exePath)
    {
        ExecutableFingerprint? observed = ExecutableIdentity.Read(exePath);
        if (observed is null)
        {
            HostConsole.Problem($"no such executable: {exePath}");
            return _exitUsage;
        }

        // Console.IsInputRedirected is the check, and passing null is how the disclosure sees it. A
        // script that pipes the phrase in is not the explicit human action HANDOFF licenses.
        if (!OperatorDisclosure.Confirm(Console.Out, Console.IsInputRedirected ? null : Console.In))
        {
            HostConsole.Problem("nothing was written; hooking stays off for this game");
            return _exitRefused;
        }

        ConsentWriteOutcome outcome = await store.RecordOperatorAcknowledgementAsync(new OperatorAcknowledgement
        {
            Fingerprint = observed.Value,
            DisclosureVersion = OperatorDisclosure.Version,
            AcknowledgedAt = DateTimeOffset.UtcNow,
        }).ConfigureAwait(false);

        HostConsole.Line($"consent: {outcome}");
        return outcome == ConsentWriteOutcome.Written ? _exitOk : _exitRefused;
    }

    private static async Task<int> RevokeAsync(FileGameConsentStore store, string exePath)
    {
        ConsentWriteOutcome outcome = await store
            .RevokeAsync(ExecutableIdentity.Normalise(exePath))
            .ConfigureAwait(false);
        HostConsole.Line($"revoke: {outcome}");
        return outcome is ConsentWriteOutcome.Written or ConsentWriteOutcome.NotFound ? _exitOk : _exitRefused;
    }

    // §S22: the payload must resolve into the directory the GUARD's own code was loaded from,
    // compared by file id. There is no way to name a different one, which is the point.
    private static string Payload => Path.Combine(AppContext.BaseDirectory, "FrameLedger.Overlay.dll");

    private static async Task<int> CaptureAsync(FileGameConsentStore store, string exePath, int seconds)
    {
        string normalised = ExecutableIdentity.Normalise(exePath);
        ExecutableFingerprint? observed = ExecutableIdentity.Read(exePath);

        CaptureResult result = await BuildLoop(store, seconds)
            .RunAsync(normalised, observed, Payload).ConfigureAwait(false);

        return await ConcludeAsync(store, normalised, observed, result).ConfigureAwait(false);
    }

    /// <summary>Launch mode: the same loop, entered by starting the executable rather than finding it.</summary>
    private static async Task<int> LaunchAsync(FileGameConsentStore store, string exePath, string arguments,
        int seconds)
    {
        string normalised = ExecutableIdentity.Normalise(exePath);
        ExecutableFingerprint? observed = ExecutableIdentity.Read(exePath);

        CaptureResult result = await BuildLoop(store, seconds)
            .RunLaunchedAsync(normalised, observed, Payload, arguments).ConfigureAwait(false);

        return await ConcludeAsync(store, normalised, observed, result).ConfigureAwait(false);
    }

    private static async Task<int> ConcludeAsync(FileGameConsentStore store, string normalised,
        ExecutableFingerprint? observed, CaptureResult result)
    {
        HostConsole.Line($"session: {result.Reason}");
        if (result.Verdict.Reason != Domain.AntiCheat.AntiCheatRefusalReason.Allow)
        {
            HostConsole.Line($"  verdict: {result.Verdict.Reason} {result.Verdict.Family} {result.Verdict.Signal}");
        }

        if (result.Reason == SessionEndReason.RefusedConsentMissing)
        {
            HostConsole.Line(await WhyConsentMissingAsync(store, normalised, observed).ConfigureAwait(false));
        }

        if (result.Records.Count > 0)
        {
            // The exe FILE on disk, after the session so the read never overlaps the launch: the
            // second witness for a frame generator compiled into the executable (HANDOFF 7b).
            Report(result, ExecutableMarkerScan.Scan(normalised));
        }

        return result.Reason switch
        {
            SessionEndReason.TargetExited => _exitOk,
            SessionEndReason.Running => _exitOk,
            SessionEndReason.TargetNotRunning or SessionEndReason.TargetAmbiguous
                or SessionEndReason.TargetCannotBePinned or SessionEndReason.LaunchCannotStart => _exitTargetNotResolved,
            SessionEndReason.AttachRefused => _exitAttachRefused,
            SessionEndReason.SafetyUnhook or SessionEndReason.SupervisionLost
                or SessionEndReason.SupervisionFaulted or SessionEndReason.WriterStoppedBlocklisted => _exitStoppedForSafety,
            _ => _exitRefused,
        };
    }

    private static void Report(CaptureResult result, ExecutableMarkers markers)
    {
        IReadOnlyList<FlFrameRecord> dominant = StreamSegmenter.DominantStream(result.Records);
        IReadOnlyList<Segment> segments = StreamSegmenter.Segment(result.Records);
        // OVER EVERY RECORD, not over the dominant stream, and that asymmetry is deliberate.
        // g_slSeen is one process-wide word drained by whichever present arrives first, so an
        // evaluation belonging to the game's frame can be consumed by a UI swapchain's
        // present. Summing the denominator over one stream while counting presents over all
        // of them overstates the factor with no diagnostic; FgWindow takes both from one set
        // and refuses outright when the span holds more than one stream.
        FgWindow fg = FgWindow.From(result.Records, Stopwatch.Frequency);
        MeasuredFacts facts = MeasuredFacts.From(
            dominant, result.WriterState, Stopwatch.Frequency, result.TotalGaps, result.TotalDropped, fg,
            result.RuntimeModules, result.NgxDriver, markers);

        HostConsole.Line($"  guard ticks published: {result.GuardTicksPublished}");
        HostConsole.Line($"  records: {result.Records.Count} ({dominant.Count} on the dominant stream), " +
                          $"{segments.Count} segment(s), gaps {result.TotalGaps}, dropped {result.TotalDropped}");
        HostConsole.Line(FocusNote(result));
        // The longest raw intervals, placed against the moments this host touched the target: the
        // owner saw a one-second drop to 1 FPS under capture and nothing here could say whose it was.
        HostConsole.Line(StallReport.From(dominant, Stopwatch.Frequency, result.TouchQpc).Describe());

        // WHICH HOOKS THE WRITER ACTUALLY INSTALLED, and it is not cosmetic.
        //
        // Every N/A below has two very different causes and the same wording: the hook never
        // installed, or it installed LATE and the consumer requires its bit on EVERY record
        // (feature hooks install lazily on a 1 Hz watchdog, so the first second of a session
        // predates them). "no upscaler hook ran" is only true in the first case, and a real
        // measurement cannot tell them apart without this line.
        int withUpscaler = dominant.Count(r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler));
        int withParams = dominant.Count(r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.UpscalerParams));
        HostConsole.Line($"  hooks installed: {(FlHookFamily)result.WriterState.HooksInstalledMask}" +
                          $"   apiMask=0x{result.WriterState.ApiMask:X}   rtTier={result.WriterState.RtTier}");
        PrintCensus(result, markers);

        PrintWriterAndLoader(result);
        HostConsole.Line($"  records carrying Upscaler={withUpscaler}/{dominant.Count}  " +
                          $"UpscalerParams={withParams}/{dominant.Count}  " +
                          $"(a value below the total means the hook came up mid-session, not that it never did)");

        // WHICH FEATURE IDS ACTUALLY ARRIVED. §S30's "print them first", and the input to the
        // decision table pre-committed in 20_OPEN_QUESTIONS before this run happens.
        PrintDispatchCensuses(dominant);
        HostConsole.Line(RtCensus(dominant));

        // The RAW values, so a real-title run can be checked against the game's own settings.
        // Grouped rather than sampled: one record could be a transient, and what a verification
        // run needs is what the writer said for most of the session.
        foreach (var g in dominant.Where(r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.UpscalerParams))
                     .GroupBy(r => (r.RenderW, r.RenderH, r.UpscalerQuality, r.Upscaler))
                     .OrderByDescending(g => g.Count()).Take(3))
        {
            HostConsole.Line($"    render {g.Key.RenderW}x{g.Key.RenderH}  quality=0x{g.Key.UpscalerQuality:X2}  " +
                              $"upscaler={(FlUpscaler)g.Key.Upscaler}  on {g.Count()} record(s)");
        }

        HostConsole.Line(SessionReport.Render(facts));
    }

    /// <summary>
    /// The census, raw and named, beside the hooks — a real-title run has to be able to say which
    /// module names the loader answered for, not just which qualifier was chosen — and then the same
    /// modules with their FILE versions and paths, from outside the process: the census cannot tell
    /// 2.7.1 from 2.8.0, and §H5 case 3 turns on exactly that. A path under the driver store rather
    /// than the game directory is how a DLSS override shows up here.
    /// </summary>
    private static void PrintCensus(CaptureResult result, ExecutableMarkers markers)
    {
        var census = (FlRuntimeCensus)result.WriterState.RuntimeCensus;
        HostConsole.Line($"  runtime census: 0x{result.WriterState.RuntimeCensus:X}  ran={census.HasFlag(FlRuntimeCensus.Ran)}  " +
                          $"fg=[{CensusNames.Describe(census & FlRuntimeCensusFamilies.Fg)}]  " +
                          $"upscaler=[{CensusNames.Describe(census & FlRuntimeCensusFamilies.Upscaler)}]");
        PrintRuntimeModules(result.RuntimeModules);
        // The driver's own word on this process, out of process: the super-resolution identity the
        // hooks cannot see on NGX-direct titles (03_METRICS §Upscaling, the driver-reported rung).
        HostConsole.Line(result.NgxDriver.Describe());
        HostConsole.Line(markers.Describe());
        // Which Streamline buffer types the title tagged, on which route: the identity half of
        // frame generation (fl_shm.h §slTagCensus). A DLSS-G title tags hudless and UI every
        // frame; a super-resolution-only title tags neither.
        HostConsole.Line($"  Streamline tag census: 0x{result.WriterState.SlTagCensus:X}  " +
                          SlTagCensusNames.DescribeRoutes(result.WriterState.SlTagCensus));
        // DXGI's own present counter against this hook's, on the hooked chain(s): the in-process
        // answer to whether a pacer's generated presents are DXGI presents this hook misses (§H5).
        HostConsole.Line(DxgiCounterNote(result.WriterState));
    }

    /// <summary>DXGI's counter against ours, and what a ratio means; never a count of anything by itself.</summary>
    private static string DxgiCounterNote(FlWriterState writer)
    {
        if (writer.DxgiPresentSamples == 0)
        {
            return "  DXGI present counter: not read (no hooked present sampled it)";
        }

        double per = writer.DxgiPresentsUnseen / (double)writer.DxgiPresentSamples;
        string reading = writer.DxgiPresentsUnseen == 0
            ? "DXGI counted nothing this hook did not: frame generation was off, or whatever generates frames on this title presents below DXGI or on another object"
            : per >= 0.5
                ? "DXGI counted presents this hook never saw on the SAME chain - a pacer presenting through a body the inline patches do not cover; the generated presents ARE DXGI presents here and Displayed above is DXGI-counted from them"
                : "a few presents reached DXGI outside this hook - too few for a pacer, read the number rather than the ratio";
        return $"  DXGI present counter: unseen={writer.DxgiPresentsUnseen} over samples={writer.DxgiPresentSamples} " +
               $"({per.ToString("0.00", CultureInfo.InvariantCulture)} unseen per hooked present) - {reading}";
    }

    /// <summary>One line per census-named module the target had loaded, version and path beside it.</summary>
    private static void PrintRuntimeModules(RuntimeModuleSet modules)
    {
        HostConsole.Line($"  runtime modules: {modules.Modules.Count} of {CensusNames.ModuleFileNames.Count} " +
                          $"census-named modules resolved over {modules.Snapshots} snapshot(s), " +
                          $"{modules.Unreadable} unreadable");
        foreach (RuntimeModuleInfo m in modules.Modules.OrderBy(m => m.FileName, StringComparer.OrdinalIgnoreCase))
        {
            string version = m.Parsed?.ToString() ?? "(no version resource)";
            HostConsole.Line($"    module: {m.FileName}  {version}  {m.Path}");
        }
    }

    /// <summary>Which vendor dispatches actually arrived, one census per vendor route.</summary>
    /// <remarks>
    /// The Streamline census is §S30's "print them first". The AMD census is the second count
    /// HANDOFF 7c asks for before the first is trusted: PREPARE frames against UPSCALE
    /// dispatches, 1.00 when they agree and 2.00 when a writer has doubled one of them.
    /// </remarks>
    private static void PrintDispatchCensuses(IReadOnlyList<FlFrameRecord> dominant)
    {
        HostConsole.Line(SlCensus.From(dominant).Describe());
        HostConsole.Line(FfxCensus.From(dominant).Describe());
    }

    /// <summary>The loop and its four collaborators, wired the only way this host allows.</summary>
    private static CaptureLoop BuildLoop(FileGameConsentStore store, int seconds)
    {
        var guard = new NativeAntiCheatGuard();
        return new CaptureLoop(
            store,
            new HookedCaptureGate(guard),
            guard,
            new TargetResolver(),
            pid =>
            {
                // Null means the pid could not be PINNED — already gone, protected, or another user's.
                // The loop refuses rather than proceeding to inject into an identity it cannot hold.
                HeldProcessHandle? handle = HeldProcessHandle.TryOpen(pid);
                return handle is null ? null : new ProcessTargetLiveness(handle, pid);
            },
            pid =>
            {
                ShmRingReader? reader = ShmRingReader.TryAttach(pid, NativeAntiCheatGuard.BuildId(),
                    out ShmAttachRefusal refusal);
                return (reader is null ? null : new ShmCaptureSink(reader), refusal);
            },
            new CaptureOptions
            {
                // Zero keeps the product behaviour: run until the target exits. A positive
                // --seconds is an operator taking a bounded measurement, and CaptureLoop
                // already honoured MaxDuration -- nothing could set it.
                MaxDuration = seconds > 0 ? TimeSpan.FromSeconds(seconds) : TimeSpan.Zero,
            },
            pid => RuntimeModuleSnapshot.Take(pid, CensusNames.ModuleFileNames),
            NgxDriverProbe.Run,
            ProcessLauncher.Start);
    }

    /// <summary>
    /// Which of the three "consent missing" situations this actually was.
    /// </summary>
    /// <remarks>
    /// <b>The refusal signal says "the per-game consent dialog has not been accepted", and on a
    /// real title that sentence was false.</b> Alan Wake 2, 2026-08-20: consent had been granted
    /// forty minutes earlier, and the launcher updated the executable underneath it — 62,026,752
    /// bytes became 62,304,768. <c>HookRequest.FromConsent</c> correctly nulls
    /// <c>consentedAt</c> on a fingerprint mismatch and correctly leaves the stored record alone
    /// (<c>19_SAFETY</c>: a block is not a withdrawal of consent), but the operator is then told
    /// they never consented.
    /// <para>
    /// Three situations, three different things to do: consent for the first time; enable a game
    /// that has a record but is switched off; or check that an update was legitimate and consent
    /// again. Collapsing them is the same defect <c>fl_shm.h</c> spends a section on for
    /// "could not look" against "looked and found nothing", and it cost a diagnosis here — the
    /// cause was only reachable by reading the store and stat-ing the file by hand.
    /// </para>
    /// <para>
    /// Reported rather than fixed at the source: giving <c>AntiCheatRefusalReason</c> a fourth
    /// member changes a vocabulary <c>fl_guard.h</c>, <c>19_SAFETY</c> and the guard ABI all
    /// share, which is a specification change and not a diagnostic one.
    /// </para>
    /// </remarks>
    private static async Task<string> WhyConsentMissingAsync(
        FileGameConsentStore store, string normalisedExePath, ExecutableFingerprint? observed)
    {
        GameConsentRecord record = await store.FindAsync(normalisedExePath).ConfigureAwait(false);
        if (!record.IsFromStore)
        {
            return "  cause: no record for this executable — consent has never been given for it";
        }

        if (!record.HookEnabled)
        {
            return "  cause: a record exists and hooking is DISABLED for it";
        }

        if (observed is { } now && !record.Fingerprint.Matches(now))
        {
            return "  cause: THE EXECUTABLE CHANGED SINCE CONSENT — you did consent, to a different build.\n"
                   + $"    consented: size={record.Fingerprint.SizeBytes} mtime={record.Fingerprint.MtimeUnixMs}\n"
                   + $"    on disk  : size={now.SizeBytes} mtime={now.MtimeUnixMs}\n"
                   + "    The stored record is deliberately untouched (19_SAFETY: a block is not a withdrawal of\n"
                   + "    consent). Check the update was one you expected, then grant consent again.";
        }

        return "  cause: the record is present, enabled and matching — so the refusal came from elsewhere";
    }

    /// <summary>The writer's own line, then the LoadLibrary detour's (installed / woke / EARLY STOP).</summary>
    private static void PrintWriterAndLoader(CaptureResult result)
    {
        HostConsole.Line(WriterNote(result));
        foreach (string line in EarlyStop.Describe(result.WriterState, EarlyStop.StagedRulesPath))
        {
            HostConsole.Line(line);
        }

        foreach (string line in LaunchNote.Describe(result.LaunchWait, result.WriterState))
        {
            HostConsole.Line(line);
        }
    }

    /// <summary>
    /// The two numbers that separate "nothing happened" from "something went wrong".
    /// </summary>
    /// <remarks>
    /// <b>Neither was printed anywhere, and the first real-title run of the ray-tracing hooks
    /// was unreadable without them.</b> A capture that drains almost nothing has at least
    /// three causes with identical reports — the present hook was never called, every present
    /// was an occlusion probe the writer correctly drops (§S26), or a hook body faulted and the
    /// fault policy went dormant. <c>faultCount</c> and <c>status</c> are what tell them apart,
    /// and a 40 s capture that returned ONE record is what made their absence a defect rather
    /// than an omission.
    /// </remarks>
    private static string WriterNote(CaptureResult result) =>
        $"  writer: status={(FlStatus)result.WriterState.Status} faults={result.WriterState.FaultCount} " +
        $"layoutVersion={result.Handshake.LayoutVersion} attach={result.AttachRefusal}";

    /// <summary>
    /// The RT evidence counts, so the falsifier this PR pre-registered can be READ.
    /// </summary>
    /// <remarks>
    /// <b>Without this line the falsifier is unfalsifiable, which is the defect it exists to
    /// avoid.</b> The ray-tracing PR committed, before any real-title run, that
    /// <c>rt_frame_pct</c> should read ≈ 25% at ×4 and not ≈ 100% — the test of whether
    /// generated presents carry recorded RT work, and therefore of whether
    /// <c>rays_per_pixel</c> may be taken over RT-active presents at all
    /// (<c>03_METRICS</c> §RT/PT/RR). <c>MeasuredFacts.RayTracingOf</c> counts exactly that
    /// evidence and then publishes only the tri-state, so the number was computed and thrown
    /// away — the same shape as <c>FgWindow.Seconds</c> being computed and never printed, which
    /// cost §S30 a reconstructed span and a wrong residual.
    /// </remarks>
    private static string RtCensus(IReadOnlyList<FlFrameRecord> stream)
    {
        int claimed = stream.Count(r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Rt));
        int asBuild = stream.Count(r => ((FlRtFlags)r.RtFlags).HasFlag(FlRtFlags.AsBuildObserved));
        int dispatch = stream.Count(r => ((FlRtFlags)r.RtFlags).HasFlag(FlRtFlags.DispatchObserved));
        int either = stream.Count(r => ((FlRtFlags)r.RtFlags & (FlRtFlags.AsBuildObserved | FlRtFlags.DispatchObserved))
                                       != FlRtFlags.None);
        long volume = stream.Sum(r => (long)r.DispatchRaysVolume);

        // OVER THE CLAIMING WINDOW, not over every record, and the difference is the whole
        // reading. The RT hooks install on a watchdog tick, so the first second of a session
        // predates them and those records can carry no evidence by construction. Measured
        // 2026-08-20 on Cyberpunk at ×4: 24.2% of all records and 25.0% of the claiming
        // window — and 25.0% is the number the pre-registered falsifier is stated against.
        // Dividing by the whole stream dilutes it by the install prefix and would have had a
        // reader inferring the real figure instead of reading it.
        int claimStart = RecordWindow.ClaimedSuffixStart(
            stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Rt));
        int claimed2 = stream.Count - claimStart;
        double pct = claimed2 > 0 ? either * 100.0 / claimed2 : 0.0;
        double pctAll = stream.Count > 0 ? either * 100.0 / stream.Count : 0.0;

        // rays_per_pixel over the RT-ACTIVE presents, which is the denominator 03_METRICS
        // chose: the accumulator drains every present, so a frame's whole volume lands on one
        // present and the generated ones contribute nothing to either side of the ratio.
        var active = stream.Where(r => r.DispatchRaysVolume > 0).ToList();
        double perPixel = 0.0;
        if (active.Count > 0)
        {
            double pixels = active.Average(r => (double)r.OutputW * r.OutputH);
            if (pixels > 0)
            {
                perPixel = active.Average(r => (double)r.DispatchRaysVolume) / pixels;
            }
        }

        return $"  RT census: measured={claimed}/{stream.Count}  asBuild={asBuild}  dispatch={dispatch}  "
               + $"rt_frame_pct={pct:0.0}% of the claiming window ({pctAll:0.0}% of all)  volume={volume}  "
               + $"rays_per_pixel={perPixel:0.###} (over {active.Count} RT-active present(s))";
    }

    /// <summary>
    /// Whether the operator was actually watching the game, and it has three answers.
    /// </summary>
    /// <remarks>
    /// <b>Zero foreground ticks is NOT "you alt-tabbed".</b> A target owning no top-level
    /// window is unfocused on every tick of every run — <c>hook-harness</c> presents to a
    /// composition swapchain and has none — so reporting that as focus loss would fire on every
    /// integration run and teach the reader to ignore the line. Collapsing the two is how a
    /// diagnostic becomes noise, and this repository has the same shape recorded for
    /// <c>rtTier</c> and <c>upscalerQuality</c>: could-not-look and looked-and-found-nothing are
    /// different states and must stay so.
    /// </remarks>
    private static string FocusNote(CaptureResult result)
    {
        if (result.DrainTicks == 0)
        {
            return "  focus: no drain tick ran, so nothing sampled it";
        }

        if (result.ForegroundTicks == 0)
        {
            return $"  focus: the target never owned the foreground window across {result.DrainTicks} drain "
                   + "tick(s) — it owns no top-level window we can see, so focus says nothing about this "
                   + "session (this is the normal answer for a headless target)";
        }

        if (result.ForegroundTicks == result.DrainTicks)
        {
            return $"  focus: foreground on all {result.DrainTicks} drain tick(s)";
        }

        // THE 1.84 CASE. Frame generation stops while a title is unfocused, so a window spanning
        // the switch averages two configurations. FgWindow.BatchRefusal is what refuses the
        // number; this line is what tells the operator why, without which the refusal reads as a
        // tool defect rather than as a capture that needs re-running.
        return $"  focus: NOT FOREGROUND for {result.DrainTicks - result.ForegroundTicks} of "
               + $"{result.DrainTicks} drain tick(s) — frame generation stops while a title is unfocused, "
               + "so any ratio over this window averages a configuration that never existed";
    }
}
