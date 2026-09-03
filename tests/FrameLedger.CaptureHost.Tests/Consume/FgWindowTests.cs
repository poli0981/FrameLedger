using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// Every way an honest set of records can be turned into a dishonest FG factor.
/// </summary>
/// <remarks>
/// The writer's shape is reproduced exactly: one evaluation is drained by the FIRST present
/// after it and the other <c>k−1</c> carry zero, and only the draining present gets
/// <see cref="FlFeatureFlags.RayReconstructionObserved"/> — the writer sets that bit under
/// <c>seen != 0</c>. So <c>batches</c> equals application frames and
/// <c>evaluations/batch</c> is 1 on a correct writer, which is the premise these fixtures
/// exist to make falsifiable.
/// </remarks>
public sealed class FgWindowTests
{
    private static readonly long _step = Stopwatch.Frequency / 240;

    /// <summary>appFrames × k records, evalsPerFrame evaluations drained by each group's first.</summary>
    private static List<FlFrameRecord> FgStream(int appFrames, int k, byte evalsPerFrame = 1,
        uint swapchain = 1, bool counted = true)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        for (int f = 0; f < appFrames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                bool drains = p == 0;
                stream.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = swapchain,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs
                                            | (counted ? FlMeasured.Fg | FlMeasured.FgCounts : FlMeasured.None)),
                    FgEvaluations = drains ? evalsPerFrame : (byte)0,
                    FgMode = (byte)(drains ? FlFgMode.DlssG : FlFgMode.Unknown),
                    FeatureFlags = (byte)(drains ? FlFeatureFlags.RayReconstructionObserved : FlFeatureFlags.None),
                });
                qpc += (ulong)_step;
            }
        }

        return stream;
    }

    /// <summary>Two segments back to back, on one monotonic clock across the join.</summary>
    private static List<FlFrameRecord> Concat(List<FlFrameRecord> first, List<FlFrameRecord> second)
    {
        List<FlFrameRecord> all = [.. first];
        ulong qpc = all[^1].Qpc;
        foreach (FlFrameRecord r in second)
        {
            FlFrameRecord copy = r;
            qpc += (ulong)_step;
            copy.Qpc = qpc;
            all.Add(copy);
        }

        return all;
    }

    [Fact]
    public void AFactorIsPublishedWhenEveryRefusalPasses()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4), Stopwatch.Frequency);

        w.Refusal.Should().BeNull();
        w.Factor.Should().BeApproximately(4.0, 0.01);
        w.Batches.Should().Be(40);
        w.Evaluations.Should().Be(40);
        w.EvaluationsPerBatch.Should().BeApproximately(1.0, 0.001);

        // NATIVE IS COUNTED, NOT DERIVED, so this identity is a check rather than a tautology.
        // Displayed uses intervals and Native uses records, so they differ by exactly one
        // frame's worth — assert that, rather than rounding it away.
        (w.NativeFps!.Value * w.Factor!.Value).Should().BeApproximately(
            w.DisplayedFps!.Value, 1.0 / w.Seconds + 0.001,
            "Native x Factor must reconstruct Displayed, or the trio does not describe one window");
    }

    [Fact]
    public void AnFgStateChangeMidSessionRefusesRatherThanAveraging()
    {
        // THE DEFECT THIS PINS, and it produces a number ABOVE the physically achievable one.
        // Half the session at x4 and half with frame generation off: the whole-window factor
        // is (160 + 160) / 40 = 8, i.e. an x8 reading from an x4 configuration, and NativeFps
        // inherits the whole error while every other guard stays silent — the count is not
        // zero, the factor is not 1.0, and it is not below 1.0.
        List<FlFrameRecord> stream = [.. FgStream(appFrames: 40, k: 4)];
        ulong qpc = stream[^1].Qpc;
        for (int i = 0; i < 160; i++)
        {
            qpc += (ulong)_step;
            stream.Add(new FlFrameRecord
            {
                Qpc = qpc,
                SwapchainId = 1,
                MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs
                                        | FlMeasured.Fg | FlMeasured.FgCounts),
                FgEvaluations = 0,
                FgMode = (byte)FlFgMode.Unknown,
            });
        }

        FgWindow w = FgWindow.From(stream, Stopwatch.Frequency);

        w.Factor.Should().BeNull();
        w.NativeFps.Should().BeNull();
        w.Refusal.Should().Contain("frame-generation state changed");
    }

    [Fact]
    public void NoEvaluationCountedIsADataGapAndNeverAOne()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, evalsPerFrame: 0), Stopwatch.Frequency);

        w.Factor.Should().BeNull("1.0 is CLAUDE.md rule 6's forbidden number");
        w.Refusal.Should().Contain("no application-frame token was counted");
    }

    [Fact]
    public void TheWindowExcludesRecordsWrittenBeforeTheHookInstalled()
    {
        List<FlFrameRecord> cold = FgStream(appFrames: 10, k: 4, counted: false);
        List<FlFrameRecord> warm = FgStream(appFrames: 40, k: 4);
        for (int i = 0; i < warm.Count; i++)
        {
            FlFrameRecord r = warm[i];
            r.Qpc = cold[^1].Qpc + (ulong)((i + 1) * _step);
            warm[i] = r;
        }

        FgWindow w = FgWindow.From([.. cold, .. warm], Stopwatch.Frequency);

        w.Presents.Should().Be(160, "the 40 records that predate the install are not part of the claim");
        w.Factor.Should().BeApproximately(4.0, 0.01);
    }

    [Fact]
    public void TwoEvaluationsPerApplicationFrameAreVISIBLEEvenThoughEveryOtherGuardIsSilent()
    {
        // THE PREMISE CHECK, and the reason it exists. HANDOFF item 3 assumes
        // slEvaluateFeature(kFeatureDLSS_G) fires ONCE per application frame; nothing in this
        // repository has verified it. At two per frame the factor is 2.0 — above 1.0, not
        // equal to 1.0, and it still moves with the setting, so the over-counting guard, the
        // structurally-1.0 guard and a three-point sweep are ALL green. Only this number says
        // the premise is wrong, and it needs no game and no settings file.
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, evalsPerFrame: 2), Stopwatch.Frequency);

        w.EvaluationsPerBatch.Should().BeApproximately(2.0, 0.001);
        w.Factor.Should().BeApproximately(2.0, 0.01,
            "the factor looks plausible, which is exactly why the premise needs its own number");
    }

    [Fact]
    public void ASecondStreamInTheSpanRefusesBecauseTheDrainWordIsProcessWide()
    {
        List<FlFrameRecord> stream = [.. FgStream(appFrames: 40, k: 4)];
        FlFrameRecord ui = stream[^1];
        ui.SwapchainId = 2;
        ui.Qpc += (ulong)_step;
        stream.Add(ui);

        FgWindow w = FgWindow.From(stream, Stopwatch.Frequency);

        w.Factor.Should().BeNull();
        w.Refusal.Should().Contain("swapchains presented in the window");
    }

    [Fact]
    public void AnUnidentifiedRecordRefusesRatherThanBeingCountedAsAPresent()
    {
        List<FlFrameRecord> stream = [.. FgStream(appFrames: 40, k: 4)];
        FlFrameRecord orphan = stream[^1];
        orphan.SwapchainId = 0;
        orphan.Qpc += (ulong)_step;
        stream.Add(orphan);

        FgWindow w = FgWindow.From(stream, Stopwatch.Frequency);

        w.Factor.Should().BeNull();
        w.Refusal.Should().Contain("swapchainId 0");
    }

    [Fact]
    public void ASaturatedCountRefusesRatherThanDividingByAFloor()
    {
        List<FlFrameRecord> stream = [.. FgStream(appFrames: 40, k: 4)];
        FlFrameRecord capped = stream[0];
        capped.FgEvaluations = 255;
        stream[0] = capped;

        FgWindow w = FgWindow.From(stream, Stopwatch.Frequency);

        w.Factor.Should().BeNull();
        w.Refusal.Should().Contain("saturation sentinel");
    }

    [Fact]
    public void AWindowTooShortToCheckForAStateChangeDoesNotPublishOne()
    {
        // 32 records is under the 64 the uniformity check needs. Publishing a factor whose
        // uniformity was never checked is the same claim as publishing one that failed it.
        FgWindow w = FgWindow.From(FgStream(appFrames: 8, k: 4), Stopwatch.Frequency);

        w.Factor.Should().BeNull();
        w.Refusal.Should().Contain("below the 64");
    }

    [Fact]
    public void AnAltTabMidCaptureIsCAUGHTByTheBatchGUARDWhileTheFactORGuardCannotSEEIt()
    {
        // THE 1.84 CASE, AND IT IS THE ONE THAT ACTUALLY HAPPENED. Cyberpunk 2077 at ×2,
        // 2026-08-16: the operator switched away mid-capture, frame generation stopped while the
        // title was unfocused, and the window averaged intervals at 2.00 with intervals near
        // 1.00. The report published nothing about it.
        //
        // On this route fgEvaluations is ZERO on every record — kFeatureDLSS_G is never
        // evaluated — so BucketFactors divides by zero everywhere and RefusalFor returns at the
        // data-gap clause long before uniformity is considered. The factor-side guard is
        // therefore not merely quiet, it is structurally unreachable, and the number a reader
        // takes away is presents/batch, which until now had nothing behind it at all.
        List<FlFrameRecord> stream = Concat(
            FgStream(appFrames: 96, k: 2, evalsPerFrame: 0),
            FgStream(appFrames: 32, k: 1, evalsPerFrame: 0));

        FgWindow w = FgWindow.From(stream, Stopwatch.Frequency);

        w.PresentsPerBatch.Should().BeApproximately(1.75, 0.01,
            "this is the averaged number the report prints, and it describes neither half");

        // The factor-side refusal names the DATA GAP and says nothing about the state change —
        // which is the defect, stated as an assertion rather than as a comment.
        w.Refusal.Should().Contain("no application-frame token was counted");
        w.Refusal.Should().NotContain("changed during",
            "the factor guard keys on fgEvaluations, which is zero here, so it cannot see this");

        w.BatchRefusal.Should().NotBeNull().And.Contain("presents-per-batch ratio changed");
    }

    [Fact]
    public void AUniformWindowPUBLISHESTheProxyEvenThoughNoEvaluationWasCounted()
    {
        // THE GREEN HALF, and without it the guard above is indistinguishable from one that
        // refuses everything. This is the ordinary Cyberpunk shape: ×4 throughout, zero
        // evaluations counted, one stream, focused for the whole run. The factor is still
        // refused — nothing counted an application frame — and the PROXY is readable, which is
        // the distinction the report has to be able to draw.
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, evalsPerFrame: 0), Stopwatch.Frequency);

        w.Factor.Should().BeNull("no evaluation was counted, so fg_factor stays N/A");
        w.PresentsPerBatch.Should().BeApproximately(4.0, 0.01);
        w.BatchRefusal.Should().BeNull("every bucket agrees, so the ratio describes one configuration");
    }

    [Fact]
    public void AWindowTooShortToBucketRefusesTheProxyToo()
    {
        // Publishing a ratio whose uniformity was never checked is the same claim as publishing
        // one that failed the check — the rule the factor already lives under, applied to the
        // proxy that is printed beside it.
        FgWindow w = FgWindow.From(FgStream(appFrames: 8, k: 4, evalsPerFrame: 0), Stopwatch.Frequency);

        w.PresentsPerBatch.Should().BeApproximately(4.0, 0.01, "the number exists");
        w.BatchRefusal.Should().NotBeNull().And.Contain("below the 64");
    }

    [Fact]
    public void TheProxyRefusesOnTheSameATTRIBUTIONFactsTheFactorDoes()
    {
        // g_slSeen is ONE PROCESS-WIDE WORD. A batch belonging to the game's frame can be
        // drained by a UI swapchain's present, so presents/batch over two streams has a
        // denominator nobody can name — exactly the reason the factor refuses, and it does not
        // stop applying because the numerator changed.
        List<FlFrameRecord> stream = [.. FgStream(appFrames: 40, k: 4, evalsPerFrame: 0)];
        FlFrameRecord ui = stream[^1];
        ui.SwapchainId = 2;
        ui.Qpc += (ulong)_step;
        stream.Add(ui);

        FgWindow w = FgWindow.From(stream, Stopwatch.Frequency);

        w.BatchRefusal.Should().NotBeNull().And.Contain("swapchains presented in the window");
    }

    [Fact]
    public void NoBatchAtAllIsNotARatioOfZero()
    {
        // A present-only writer drains nothing, so there is no ratio — not 0, and not 1.0.
        FgWindow w = FgWindow.From(
            FgStream(appFrames: 40, k: 4, evalsPerFrame: 0).Select(r =>
            {
                r.FeatureFlags = (byte)FlFeatureFlags.None;
                return r;
            }).ToList(),
            Stopwatch.Frequency);

        w.Batches.Should().Be(0);
        w.PresentsPerBatch.Should().BeNull();
        w.BatchRefusal.Should().NotBeNull().And.Contain("no present drained a Streamline batch");
    }

    [Fact]
    public void TheDiagnosticsSurviveEveryRefusal()
    {
        // A refusal must not take the evidence with it: the premise number and the histogram
        // are what a verification run reads to find out WHY, and they are meaningful whether
        // or not a factor was allowed.
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, evalsPerFrame: 255), Stopwatch.Frequency);

        w.Refusal.Should().NotBeNull();
        w.Batches.Should().Be(40);
        w.EvaluationsPerBatch.Should().BeApproximately(255.0, 0.001);
        w.Histogram[5].Should().Be(40, "255 lumps into the last histogram slot");
        w.Saturated.Should().Be(40);
    }
}
