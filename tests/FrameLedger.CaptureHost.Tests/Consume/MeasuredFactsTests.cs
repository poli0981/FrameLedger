using System.Diagnostics;
using System.Text.RegularExpressions;
using FluentAssertions;
using FrameLedger.Application.Metrics;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// What a present-only writer may and may not be turned into.
/// </summary>
/// <remarks>
/// The synthetic records here are exactly what <c>dllmain.cpp</c> writes today —
/// <c>MeasuredMask == OutputRes | PresentArgs</c>, everything else zero — and
/// <c>guard_test.cpp</c> asserts that shape against the real Overlay in the merge
/// gate. So these fixtures are not a guess about the writer; they are the writer's
/// own contract restated on the reading side.
/// </remarks>
public sealed partial class MeasuredFactsTests
{
    private static FlFrameRecord Present(uint index, ulong qpc, uint swapchain = 1) => new()
    {
        FrameIndex = index,
        Qpc = qpc,
        SwapchainId = swapchain,
        OutputW = 3840,
        OutputH = 2160,
        Api = (byte)FlApi.D3D11,
        MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs),
    };

    // A writer that installed the PRESENT hook and nothing else.
    //
    // `default` used to stand in for this, and stopped being honest when
    // hooksInstalledMask gained a producer: a writer publishing 0 now means "I
    // installed nothing", so claiming FL_MEASURED_OUTPUT_RES from it is exactly
    // the over-claim MeasuredFacts.EntitledBy exists to catch. The Overlay sets
    // FL_HOOK_PRESENT in InitThread, so this is what a real present-only writer
    // publishes.
    private static readonly FlWriterState _presentOnly =
        new() { HooksInstalledMask = (uint)FlHookFamily.Present };

    private static List<FlFrameRecord> Stream(int n, long tickStep)
    {
        var list = new List<FlFrameRecord>(n);
        ulong qpc = 1_000_000;
        for (int i = 0; i < n; i++)
        {
            list.Add(Present((uint)i, qpc));
            qpc += (ulong)tickStep;
        }

        return list;
    }

    [GeneratedRegex(@"[x×] ?\d", RegexOptions.None, matchTimeoutMilliseconds: 1000)]
    private static partial Regex FgFactorShape();

    [Fact]
    public void APresentOnlyStreamReportsNAForEverythingItDidNotMeasure()
    {
        MeasuredFacts facts = MeasuredFacts.From(Stream(120, Stopwatch.Frequency / 120), _presentOnly, Stopwatch.Frequency, 0, 0);

        facts.Upscaler.Should().BeNull("no upscaler hook ran, and `none` would be a measured negative");
        facts.FgMode.Should().BeNull("03_METRICS' ladder rung 4 says `none`, which here would be a lie");
        facts.FgFactor.Should().BeNull("1.0 is CLAUDE.md rule 6's forbidden number");
        facts.NativeFps.Should().BeNull("F_app = Σ fgEvaluations needs a hook that counted");
        facts.UpscaleRatio.Should().BeNull("renderW/H are 0 and the ratio would divide by zero");
        facts.RayTracing.Should().Be(Tri.NotApplicable);
        facts.RayReconstruction.Should().Be(Tri.NotApplicable);
        facts.PathTracing.Should().Be(Tri.NotApplicable);
        facts.Hdr.Should().Be(Tri.NotApplicable);
        facts.HonestyViolations.Should().Be(0);
    }

    [Fact]
    public void TheOneNumberItMayPublishIsDisplayedFpsAndItIsRight()
    {
        // 120 presents one 120th of a second apart. 119 intervals over 119/120 s.
        MeasuredFacts facts = MeasuredFacts.From(Stream(120, Stopwatch.Frequency / 120), _presentOnly, Stopwatch.Frequency, 0, 0);

        facts.DisplayedFps.Should().BeApproximately(120, 1,
            "the QPC-to-seconds conversion uses Stopwatch.Frequency, measured equal to "
            + "QueryPerformanceFrequency on this platform");
    }

    [Fact]
    public void TheRenderedReportNeverShowsAnFgFactorItDoesNotHave()
    {
        // CLAUDE.md rule 6 is a rule about the RENDERER too: Native, Displayed and the factor appear
        // together or the number is not an FPS claim at all. "We never format it" is exactly the kind
        // of property that survives a refactor by accident, so it is asserted.
        string text = SessionReport.Render(
            MeasuredFacts.From(Stream(60, Stopwatch.Frequency / 60), _presentOnly, Stopwatch.Frequency, 0, 0));

        text.Should().Contain("Presented FPS");
        text.Should().Contain("frame generation: NOT measured", "a present-only writer with no census leaves the question open");
        FgFactorShape().IsMatch(text).Should().BeFalse("an ×N beside a single number reads as a measured factor");
        text.Should().NotContain("Native FPS", "showing it as N/A beside a real Displayed figure invites "
                                               + "reading Displayed as Native");
    }

    [Fact]
    public void TheRenderedReportShowsTheWHOLETrioWhenItHasOne()
    {
        // THE OTHER DIRECTION OF RULE 6, and it was untested: a renderer that never printed
        // Native or the factor at all would have passed the case above for the wrong reason.
        // Both halves of "together or not at all" have to be reachable.
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        long step = Stopwatch.Frequency / 240;
        for (int f = 0; f < 40; f++)
        {
            for (int p = 0; p < 4; p++)
            {
                stream.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs
                                            | FlMeasured.Fg | FlMeasured.FgCounts),
                    FgEvaluations = (byte)(p == 0 ? 1 : 0),
                    FgMode = (byte)(p == 0 ? FlFgMode.DlssG : FlFgMode.Unknown),
                    FeatureFlags = (byte)(p == 0 ? FlFeatureFlags.RayReconstructionObserved : FlFeatureFlags.None),
                });
                qpc += (ulong)step;
            }
        }

        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity
                                        | FlHookFamily.FgEvaluations),
        };
        string text = SessionReport.Render(MeasuredFacts.From(
            stream, writer, Stopwatch.Frequency, 0, 0, FgWindow.From(FrameSampleMapper.Map(stream), Stopwatch.Frequency)));

        text.Should().Contain("Native FPS").And.Contain("Displayed FPS").And.Contain("x4 FG");
        text.Should().Contain("tokens/batch").And.Contain("histogram");
        text.Should().Contain("frame generation: DlssG");
    }

    [Fact]
    public void TheRenderedReportSaysNoneWhenTheCountSaysEveryPresentWasAnApplicationFrame()
    {
        // 08_UI's bare `144 FPS` shape, reachable since 2026-09-04 (§S31 row P1). No pair, no
        // factor, no qualifier — and never the word "Native" beside a lone number.
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        long step = Stopwatch.Frequency / 84;
        for (int f = 0; f < 200; f++)
        {
            stream.Add(new FlFrameRecord
            {
                Qpc = qpc,
                SwapchainId = 1,
                MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Fg | FlMeasured.FgCounts),
                FgEvaluations = 1,
                FgMode = (byte)FlFgMode.Unknown,
            });
            qpc += (ulong)step;
        }

        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.FgEvaluations),
            RuntimeCensus = (uint)(FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.NvngxDlssG),
        };
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0, FgWindow.From(FrameSampleMapper.Map(stream), Stopwatch.Frequency));
        string text = SessionReport.Render(facts);

        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        facts.FgFactor.Should().BeApproximately(1.0, 0.01);
        text.Should().Contain("  FPS: ").And.Contain("frame generation: none");
        text.Should().NotContain("Native FPS").And.NotContain("Presented FPS").And.NotContain("MAY include");
        FgFactorShape().IsMatch(text).Should().BeFalse("none prints no factor");
        text.Should().Contain("frame generation: None");
    }

    /// <summary>The shape the one measured route produces: batches drain, evaluations never do.</summary>
    private static List<FlFrameRecord> BatchStream(int appFrames, int k, ref ulong qpc)
    {
        List<FlFrameRecord> stream = [];
        long step = Stopwatch.Frequency / 240;
        for (int f = 0; f < appFrames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                stream.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs
                                            | FlMeasured.Fg | FlMeasured.FgCounts),
                    FgMode = (byte)FlFgMode.Unknown,
                    FeatureFlags = (byte)(p == 0 ? FlFeatureFlags.RayReconstructionObserved : FlFeatureFlags.None),
                });
                qpc += (ulong)step;
            }
        }

        return stream;
    }

    private static readonly FlWriterState _streamlineWriter = new()
    {
        HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity
                                    | FlHookFamily.FgEvaluations),
    };

    private static string RenderOf(List<FlFrameRecord> stream) => SessionReport.Render(MeasuredFacts.From(
        stream, _streamlineWriter, Stopwatch.Frequency, 0, 0, FgWindow.From(FrameSampleMapper.Map(stream), Stopwatch.Frequency)));

    [Fact]
    public void ThePROXYIsNeverPrintedWithoutItsVerdictOrItsSpan()
    {
        // presents/batch is the sharpest number this report produces and the one a real-title run
        // reads — and it was printed with NOTHING behind it, because the only uniformity check
        // divides by Σ fgEvaluations, which is zero on this exact route. span= was computed and
        // never printed at all, so §S30's draft reconstructed it from a DIFFERENT window and got a
        // rate that moved across 78.6-83 on window choice alone.
        ulong qpc = 1_000_000;
        string text = RenderOf(BatchStream(appFrames: 40, k: 4, ref qpc));

        text.Should().Contain("presents/batch=4").And.Contain("span=");
        text.Should().Contain("is a PROXY", "the reader must not be able to take the number without the label");
        text.Should().Contain("uniform across every bucket");
        text.Should().NotContain("NOT READABLE", "this window is one configuration throughout");
    }

    [Fact]
    public void AnAltTabbedWindowRendersTheProxyAsNOTREADABLE()
    {
        // The 1.84 case, end to end through the renderer: ×2 for most of the capture, then frame
        // generation stops while the title is unfocused. The averaged ratio describes neither half.
        ulong qpc = 1_000_000;
        List<FlFrameRecord> stream = [.. BatchStream(96, 2, ref qpc), .. BatchStream(32, 1, ref qpc)];

        string text = RenderOf(stream);

        text.Should().Contain("NOT READABLE").And.Contain("presents-per-batch ratio changed");
    }

    [Fact]
    public void ARecordThatOverClaimsIsCountedRatherThanAveraged()
    {
        // The managed mirror of guard_test.cpp's honesty assertion. The day a feature hook lands, an
        // over-claiming record has to be visible rather than silently folded into a number.
        List<FlFrameRecord> stream = [.. Stream(10, 1000)];
        FlFrameRecord bad = stream[3];
        bad.MeasuredMask |= (ushort)FlMeasured.Fg;
        bad.FgMode = (byte)FlFgMode.None;
        stream[3] = bad;

        MeasuredFacts.From(stream, _presentOnly, Stopwatch.Frequency, 0, 0).HonestyViolations.Should().Be(1);
    }

    [Fact]
    public void RayTracingNoNeedsAllThreeConjuncts()
    {
        // 03_METRICS: an RT-capable device, the AS-BUILD hook INSTALLED, and no evidence all session.
        // The middle one is the one that is easy to drop, and dropping it publishes a confident `No`
        // about a title that ray-traces every frame through inline RayQuery.
        List<FlFrameRecord> stream = [.. Stream(20, 1000).Select(r =>
        {
            r.MeasuredMask |= (ushort)FlMeasured.Rt;
            return r;
        })];

        var capable = new FlWriterState
        {
            RtTier = 11,
            HooksInstalledMask = (uint)(FlHookFamily.RtDispatch | FlHookFamily.RtAsBuild),
        };

        MeasuredFacts.From(stream, capable, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(Tri.No);

        var dispatchOnly = capable with { HooksInstalledMask = (uint)FlHookFamily.RtDispatch };
        MeasuredFacts.From(stream, dispatchOnly, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(
            Tri.NotApplicable, "a writer without the AS-build hook sees nothing on a RayQuery-only title");

        var notQueried = capable with { RtTier = (uint)FlRtTier.NotQueried };
        MeasuredFacts.From(stream, notQueried, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(
            Tri.NotApplicable, "rtTier 0 means NOT QUERIED, not `not capable`");

        // THE THIRD STATE, which did not exist until rtTier got a producer. D3D12's own
        // NOT_SUPPORTED is 0, so the writer substitutes FlRtTier.Unsupported to keep "we asked and
        // this device cannot" distinguishable from "nobody asked". Both reach N/A here — and they
        // must reach it for DIFFERENT reasons, which is why the previous case is not enough on its
        // own: a consumer that collapsed Unsupported back to 0 would pass that one and this one, but
        // a `>= CapableMin` written as `!= 0` would pass that one and FAIL this.
        var incapable = capable with { RtTier = (uint)FlRtTier.Unsupported };
        MeasuredFacts.From(stream, incapable, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(
            Tri.NotApplicable, "a device that cannot ray-trace produces no evidence, so `No` says nothing");
    }

    [Fact]
    public void RayTracingYesNeedsEvidenceAndItsMaskBitTogether()
    {
        List<FlFrameRecord> stream = [.. Stream(20, 1000).Select(r =>
        {
            r.MeasuredMask |= (ushort)FlMeasured.Rt;
            r.RtFlags = (byte)FlRtFlags.AsBuildObserved;
            return r;
        })];

        MeasuredFacts.From(stream, _presentOnly, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(Tri.Yes);

        // Evidence with the mask bit CLEAR is a writer contradicting itself, and the honest reading of
        // "nobody looked" wins over a flag that could only have been set by someone who did.
        List<FlFrameRecord> unmasked = [.. stream.Select(r =>
        {
            r.MeasuredMask &= unchecked((ushort)~(ushort)FlMeasured.Rt);
            return r;
        })];
        MeasuredFacts.From(unmasked, _presentOnly, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(Tri.NotApplicable);
    }

    /// <summary>
    /// The writer's real shape: only the present that DRAINED a batch carries featureFlags.
    /// </summary>
    /// <remarks>
    /// <c>dllmain.cpp</c> sets <c>RayReconstructionObserved</c> under <c>seen != 0</c>, so at ×4
    /// roughly one present in four carries it — measured 24% on the Cyberpunk stream. Reproducing
    /// that here is the whole point: a fixture where every record carries the bit is a fixture in
    /// which the defect cannot appear.
    /// </remarks>
    private static List<FlFrameRecord> RrStream(int appFrames, int k, FlFeatureFlags onBatch,
        ulong startQpc = 1_000_000)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = startQpc;
        long step = Stopwatch.Frequency / 240;
        for (int f = 0; f < appFrames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                stream.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler),
                    Upscaler = (byte)(p == 0 ? FlUpscaler.Dlss : FlUpscaler.Unknown),
                    FeatureFlags = (byte)(p == 0 ? onBatch : FlFeatureFlags.None),
                });
                qpc += (ulong)step;
            }
        }

        return stream;
    }

    private static readonly FlWriterState _identityWriter =
        new() { HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity) };

    [Fact]
    public void RayReconstructionIsANSWEREDOnAFrameGeneratingTitle()
    {
        // THE DEFECT THIS PINS. The verdict demanded RayReconstructionObserved on EVERY record while
        // the writer sets it only on the present that drained a batch — one in four at ×4. So the
        // condition could not hold at any multiplier above 1, and the answer was decided by the
        // frame-generation setting rather than by whether Ray Reconstruction ran. Cyberpunk 2077 ran
        // kFeatureDLSS_RR on 2,523 of 2,523 batches and this reported N/A.
        var facts = MeasuredFacts.From(
            RrStream(40, 4, FlFeatureFlags.RayReconstructionObserved | FlFeatureFlags.RayReconstruction),
            _identityWriter, Stopwatch.Frequency, 0, 0);

        facts.RayReconstruction.Should().Be(Tri.Yes);
        facts.HonestyViolations.Should().Be(0, "the fixture must be a record set the writer could produce");
    }

    [Fact]
    public void BatchesWithNoRayReconstructionAreNoRatherThanNA()
    {
        // THE BRANCH THAT WAS UNREACHABLE, and without it the case above passes for a renderer that
        // answers Yes to everything. A Streamline title running DLSS super-resolution and NOT Ray
        // Reconstruction produces batches that carry OBSERVED and never the fact bit — which is a
        // measured negative, and the one thing 03_METRICS' RR row allows to be aggregated as one.
        MeasuredFacts.From(RrStream(40, 4, FlFeatureFlags.RayReconstructionObserved),
            _identityWriter, Stopwatch.Frequency, 0, 0)
            .RayReconstruction.Should().Be(Tri.No);
    }

    [Fact]
    public void NoBatchObservedIsNAAndNeverNo()
    {
        // The direction that must NOT move. An NGX-direct title running DLSS-RR every frame drains
        // no Streamline batch at all, so nothing observed it — and `No` there would be a fabricated
        // negative about a title that is very much running it. The install prefix is the same shape
        // and now drops out for free rather than forcing the whole session to N/A.
        MeasuredFacts.From(RrStream(40, 4, FlFeatureFlags.None),
            _identityWriter, Stopwatch.Frequency, 0, 0)
            .RayReconstruction.Should().Be(Tri.NotApplicable);
    }

    [Fact]
    public void TheINSTALLPrefixDoesNotDecideTheWholeSessionsRayReconstruction()
    {
        // Feature hooks install lazily on a 1 Hz watchdog, so the first second of every session
        // predates them and those records carry no featureFlags at all. Under the old `All` rule a
        // single such record forced N/A over a session that answered the question 160 times.
        List<FlFrameRecord> cold = RrStream(10, 4, FlFeatureFlags.None);
        List<FlFrameRecord> warm = RrStream(40, 4,
            FlFeatureFlags.RayReconstructionObserved | FlFeatureFlags.RayReconstruction,
            startQpc: cold[^1].Qpc + 1_000);

        MeasuredFacts.From([.. cold, .. warm], _identityWriter, Stopwatch.Frequency, 0, 0)
            .RayReconstruction.Should().Be(Tri.Yes);
    }

    [Fact]
    public void TheRetiredUpscalerValueIsNeverDecodedAsRayReconstruction()
    {
        // 03_METRICS §Upscaling listed `dlss_rr` as an upscaler value until the PR that added this
        // file removed it — layout v3 RETIRED and RESERVED slot 2 precisely because it made Ray
        // Reconstruction mutually exclusive with DLSS super-resolution, and the two run together.
        // Decoding 2 as anything resurrects the conflation the record had already dropped.
        //
        // The present tense here outlived the edit it described by one PR, which is the same
        // doc-truth defect this project keeps recording — in a comment, this time, where nothing
        // gates it.
        List<FlFrameRecord> stream = [.. Stream(5, 1000).Select(r =>
        {
            r.MeasuredMask |= (ushort)FlMeasured.Upscaler;
            r.Upscaler = (byte)FlUpscaler.RetiredRayReconstruction;
            return r;
        })];

        MeasuredFacts.From(stream, _presentOnly, Stopwatch.Frequency, 0, 0).Upscaler.Should().BeNull();
    }

    [Fact]
    public void AnUnknownUpscalerIsNotAnUpscalerName()
    {
        List<FlFrameRecord> stream = [.. Stream(5, 1000).Select(r =>
        {
            r.MeasuredMask |= (ushort)FlMeasured.Upscaler;
            r.Upscaler = (byte)FlUpscaler.Unknown;
            return r;
        })];

        MeasuredFacts.From(stream, _presentOnly, Stopwatch.Frequency, 0, 0).Upscaler.Should().BeNull(
            "a hook that ran and could not tell is still N/A — a different N/A, but not a name");
    }

    [Fact]
    public void AHookThatCameUpMidSessionIsStillMeasured()
    {
        // THE DEFECT THIS PINS. `stream.All(...)` was unsatisfiable by construction: feature hooks
        // install lazily from the 1 Hz watchdog, so the opening of every session predates them and the
        // consumer reported "no upscaler hook ran" about a session where the hook was live for 97% of
        // the presents. Program.cs grew a per-bit record count to explain that in prose; this makes it
        // unnecessary.
        List<FlFrameRecord> stream = [.. Stream(100, 1000).Select((r, i) =>
        {
            if (i >= 12)
            {
                r.MeasuredMask |= (ushort)FlMeasured.Upscaler;
                r.Upscaler = (byte)FlUpscaler.Dlss;
            }

            return r;
        })];

        MeasuredFacts.From(stream, _presentOnly, Stopwatch.Frequency, 0, 0).Upscaler.Should().Be(
            "Dlss", "12 cold records at the head are the install window, not an absence of measurement");
    }

    [Fact]
    public void AStreamNoRecordEverClaimedStaysNotApplicable()
    {
        // The other direction, and the one that keeps the fix above from degenerating into "always
        // claim": with no record carrying the bit the maximal claiming suffix is empty, and empty must
        // read as N/A rather than as an aggregate over nothing.
        MeasuredFacts facts = MeasuredFacts.From(Stream(40, 1000), _presentOnly, Stopwatch.Frequency, 0, 0);

        facts.Upscaler.Should().BeNull();
        facts.RayTracing.Should().Be(Tri.NotApplicable);
        facts.Hdr.Should().Be(Tri.NotApplicable);
    }

    [Fact]
    public void AnIntermittentBitIsNotAnInstallWindow()
    {
        // Set, cleared, set again. The trailing run looks exactly like a clean install window on its
        // own, so without the boundary check it would be averaged as if it were the whole session —
        // publishing a value about an interval nobody measured. hooksInstalledMask is monotonic, so
        // this shape can only come from a writer defect, and the honest reading of a writer
        // contradicting itself is N/A.
        List<FlFrameRecord> stream = [.. Stream(30, 1000).Select((r, i) =>
        {
            if (i is < 5 or >= 20)
            {
                r.MeasuredMask |= (ushort)FlMeasured.Upscaler;
                r.Upscaler = (byte)FlUpscaler.Dlss;
            }

            return r;
        })];

        MeasuredFacts.From(stream, _presentOnly, Stopwatch.Frequency, 0, 0).Upscaler.Should().BeNull();
    }

    [Fact]
    public void RayTracingCountsEvidenceOverExactlyTheRecordsItClaims()
    {
        // ONE RECORD SET FOR BOTH SIDES OF THE RATIO. 10 cold records then 10 claiming records, all
        // ten carrying AS-build evidence. Counting evidence over the suffix and dividing by the whole
        // stream gives 10/20 = 50% — still over the 5% gate, so that error would not show here — but
        // the reverse pairing is what this pins: `measured` must be decided over the same records the
        // denominator counts, or the two disagree the moment evidence is sparse.
        List<FlFrameRecord> stream = [.. Stream(20, 1000).Select((r, i) =>
        {
            if (i >= 10)
            {
                r.MeasuredMask |= (ushort)FlMeasured.Rt;
                r.RtFlags = (byte)FlRtFlags.AsBuildObserved;
            }

            return r;
        })];

        MeasuredFacts.From(stream, _presentOnly, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(
            Tri.Yes, "the RT hook came up at record 10 and every record after it carries evidence");

        // And the 5% gate still has to be able to say no: one evidence record in twenty claiming ones
        // is 5% exactly, so make it one in forty.
        List<FlFrameRecord> sparse = [.. Stream(41, 1000).Select((r, i) =>
        {
            r.MeasuredMask |= (ushort)FlMeasured.Rt;
            if (i == 40)
            {
                r.RtFlags = (byte)FlRtFlags.AsBuildObserved;
            }

            return r;
        })];

        MeasuredFacts.From(sparse, _presentOnly, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(
            Tri.NotApplicable, "one evidence record in 41 is under the 5% gate, and `No` needs the "
                               + "AS-build hook installed and an RT-capable device besides");
    }

    [Fact]
    public void HdrSurvivesTheInstallWindowToo()
    {
        // The fourth member of the class, and the one the first sweep of it missed. SetColorSpace1 is
        // unbuilt today so this cannot regress in production yet — which is exactly why it is asserted
        // now, while the answer is still cheap to get right.
        List<FlFrameRecord> stream = [.. Stream(50, 1000).Select((r, i) =>
        {
            if (i >= 7)
            {
                r.MeasuredMask |= (ushort)FlMeasured.Hdr;
                r.ColorSpace = (byte)FlColorSpace.Hdr10;
            }

            return r;
        })];

        MeasuredFacts.From(stream, _presentOnly, Stopwatch.Frequency, 0, 0).Hdr.Should().Be(Tri.Yes);
    }

    [Fact]
    public void GapsAndDropsAreReportedRatherThanAbsorbed()
    {
        MeasuredFacts facts = MeasuredFacts.From(Stream(10, 1000), _presentOnly, Stopwatch.Frequency, totalGaps: 0, totalDropped: 4);

        facts.HasDataGaps.Should().BeTrue();
        SessionReport.Render(facts).Should().Contain("torn or overwritten");
    }
}
