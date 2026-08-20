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
using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Consent;
using FrameLedger.Application.Rules;
using FrameLedger.CaptureHost.Capture;
using FrameLedger.CaptureHost.Consent;
using FrameLedger.CaptureHost.Consume;
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

    private static async Task<int> CaptureAsync(FileGameConsentStore store, string exePath, int seconds)
    {
        string normalised = ExecutableIdentity.Normalise(exePath);
        ExecutableFingerprint? observed = ExecutableIdentity.Read(exePath);

        // §S22: the payload must resolve into the directory the GUARD's own code was loaded from,
        // compared by file id. There is no way to name a different one, which is the point.
        string payload = Path.Combine(AppContext.BaseDirectory, "FrameLedger.Overlay.dll");

        var guard = new NativeAntiCheatGuard();
        var loop = new CaptureLoop(
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
            });

        CaptureResult result = await loop.RunAsync(normalised, observed, payload).ConfigureAwait(false);

        HostConsole.Line($"session: {result.Reason}");
        if (result.Verdict.Reason != Domain.AntiCheat.AntiCheatRefusalReason.Allow)
        {
            HostConsole.Line($"  verdict: {result.Verdict.Reason} {result.Verdict.Family} {result.Verdict.Signal}");
        }

        if (result.Records.Count > 0)
        {
            Report(result);
        }

        return result.Reason switch
        {
            SessionEndReason.TargetExited => _exitOk,
            SessionEndReason.Running => _exitOk,
            SessionEndReason.TargetNotRunning or SessionEndReason.TargetAmbiguous
                or SessionEndReason.TargetCannotBePinned => _exitTargetNotResolved,
            SessionEndReason.AttachRefused => _exitAttachRefused,
            SessionEndReason.SafetyUnhook or SessionEndReason.SupervisionLost
                or SessionEndReason.SupervisionFaulted => _exitStoppedForSafety,
            _ => _exitRefused,
        };
    }

    private static void Report(CaptureResult result)
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
            dominant, result.WriterState, Stopwatch.Frequency, result.TotalGaps, result.TotalDropped, fg);

        HostConsole.Line($"  guard ticks published: {result.GuardTicksPublished}");
        HostConsole.Line($"  records: {result.Records.Count} ({dominant.Count} on the dominant stream), " +
                          $"{segments.Count} segment(s), gaps {result.TotalGaps}, dropped {result.TotalDropped}");
        HostConsole.Line(FocusNote(result));

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
        HostConsole.Line($"  records carrying Upscaler={withUpscaler}/{dominant.Count}  " +
                          $"UpscalerParams={withParams}/{dominant.Count}  " +
                          $"(a value below the total means the hook came up mid-session, not that it never did)");

        // WHICH FEATURE IDS ACTUALLY ARRIVED. §S30's "print them first", and the input to the
        // decision table pre-committed in 20_OPEN_QUESTIONS before this run happens.
        HostConsole.Line(SlCensus.From(dominant).Describe());

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
