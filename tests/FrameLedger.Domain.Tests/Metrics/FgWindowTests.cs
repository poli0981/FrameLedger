using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>
/// Every way an honest set of samples can be turned into a dishonest FG factor — each one a
/// <see cref="FgRefusalKind"/> rather than a number.
/// </summary>
/// <remarks>
/// Moved from the capture host's tests when the arithmetic moved to Domain (P2 PR-A). The fixtures are
/// the writer's shape: one evaluation is drained by the FIRST present after it and the other <c>k−1</c>
/// carry zero, so <c>batches</c> equals application frames and <c>evaluations/batch</c> is 1 on a correct
/// writer — the premise these fixtures exist to make falsifiable.
/// </remarks>
public sealed class FgWindowTests
{
    [Fact]
    public void AFactorOfOneIsPublishedAsNoneSinceRowP1Landed()
    {
        // 2026-09-04: Cyberpunk 2077's off leg read 1.00 on the same build whose ×3 / ×4 legs read 2.99 /
        // 3.99, which excludes the one explanation that made 1.0 unpublishable.
        FgWindow w = FgWindow.From(FgStream(appFrames: 80, k: 1), Frequency);

        w.Refusal.Should().BeNull();
        w.Factor.Should().BeApproximately(1.0, 0.01);
        w.IsNone.Should().BeTrue();
        w.IsActive.Should().BeFalse();
        w.Evaluations.Should().Be(w.Presents, "none means the two counts agree");
    }

    [Fact]
    public void ASteadyRatioBetweenNoneAndTheCadenceThresholdIsRefusedAsUnnameable()
    {
        // 1.25 in every bucket: uniform, so the mixed-window guard is silent, and no vendor ships a ×1.25.
        List<FrameSample> stream = [];
        ulong qpc = 1_000_000;
        for (int f = 0; f < 160; f++)
        {
            int presents = f % 4 == 3 ? 2 : 1;
            for (int p = 0; p < presents; p++)
            {
                stream.Add(new FrameSample
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    Measured = MeasuredFields.OutputRes | MeasuredFields.PresentArgs | MeasuredFields.Fg | MeasuredFields.FgCounts,
                    FgEvaluations = (byte)(p == 0 ? 1 : 0),
                    FgMode = FgKind.Unknown,
                });
                qpc += (ulong)Step;
            }
        }

        FgWindow w = FgWindow.From(stream, Frequency);

        w.Factor.Should().BeNull();
        w.IsNone.Should().BeFalse();
        w.Refusal!.Kind.Should().Be(FgRefusalKind.AmbiguousBand);
        w.Refusal.Overall.Should().BeApproximately(1.25, 0.01);
    }

    [Fact]
    public void AFactorIsPublishedWhenEveryRefusalPasses()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4), Frequency);

        w.Refusal.Should().BeNull();
        w.Factor.Should().BeApproximately(4.0, 0.01);
        w.Batches.Should().Be(40);
        w.Evaluations.Should().Be(40);
        w.EvaluationsPerBatch.Should().BeApproximately(1.0, 0.001);
        w.IsActive.Should().BeTrue();

        // NATIVE IS COUNTED, NOT DERIVED, so this identity is a check rather than a tautology. Displayed
        // uses intervals and Native uses records, so they differ by exactly one frame's worth.
        (w.NativeFps!.Value * w.Factor!.Value).Should().BeApproximately(
            w.DisplayedFps!.Value, 1.0 / w.Seconds + 0.001,
            "Native x Factor must reconstruct Displayed, or the trio does not describe one window");
    }

    [Fact]
    public void AnFgStateChangeMidSessionRefusesRatherThanAveraging()
    {
        // Half the session at x4 and half with frame generation off: the whole-window factor is
        // (160 + 160) / 40 = 8, an x8 reading from an x4 configuration, and every other guard is silent.
        List<FrameSample> stream = [.. FgStream(appFrames: 40, k: 4)];
        ulong qpc = stream[^1].Qpc;
        for (int i = 0; i < 160; i++)
        {
            qpc += (ulong)Step;
            stream.Add(new FrameSample
            {
                Qpc = qpc,
                SwapchainId = 1,
                Measured = MeasuredFields.OutputRes | MeasuredFields.PresentArgs | MeasuredFields.Fg | MeasuredFields.FgCounts,
                FgEvaluations = 0,
                FgMode = FgKind.Unknown,
            });
        }

        FgWindow w = FgWindow.From(stream, Frequency);

        w.Factor.Should().BeNull();
        w.NativeFps.Should().BeNull();
        w.Refusal!.Kind.Should().Be(FgRefusalKind.NonUniform);
        w.Refusal.Subject.Should().Be(FgRefusalSubject.Factor);
        w.Refusal.BucketCount.Should().Be(FgWindow.Buckets);
        w.Refusal.BucketIndex.Should().Be(0, "the FIRST bucket to depart is named, and the x4 half departs from an x8 whole");
        w.Refusal.BucketValue.Should().BeApproximately(4.0, 0.01);
        w.Refusal.Overall.Should().BeApproximately(8.0, 0.01, "the averaged number is above the physically achievable 4");
        w.BucketFactors[^1].Should().Be(double.PositiveInfinity, "the off half has no evaluations at all");
    }

    [Fact]
    public void NoEvaluationCountedIsADataGapAndNeverAOne()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, evalsPerFrame: 0), Frequency);

        w.Factor.Should().BeNull("1.0 is CLAUDE.md rule 6's forbidden number");
        w.Refusal!.Kind.Should().Be(FgRefusalKind.NoEvaluations);
    }

    [Fact]
    public void NoSampleClaimingTheCountBitIsNotCountedAtAll()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, counted: false), Frequency);

        w.Presents.Should().Be(0);
        w.Refusal!.Kind.Should().Be(FgRefusalKind.NotCounted);
        w.Histogram.Should().BeEmpty();
        w.BatchRefusal!.Kind.Should().Be(FgRefusalKind.NoBatches);
    }

    [Fact]
    public void TheWindowExcludesSamplesWrittenBeforeTheHookInstalled()
    {
        List<FrameSample> cold = FgStream(appFrames: 10, k: 4, counted: false);
        List<FrameSample> warm = FgStream(appFrames: 40, k: 4, startQpc: cold[^1].Qpc + (ulong)Step);

        FgWindow w = FgWindow.From([.. cold, .. warm], Frequency);

        w.Presents.Should().Be(160, "the 40 samples that predate the install are not part of the claim");
        w.Factor.Should().BeApproximately(4.0, 0.01);
    }

    [Fact]
    public void TwoEvaluationsPerApplicationFrameAreVisibleEvenThoughEveryOtherGuardIsSilent()
    {
        // THE PREMISE CHECK: at two per frame the factor is 2.0 — above 1.0, not equal to 1.0, and it still
        // moves with the setting — so only this number says the premise is wrong.
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, evalsPerFrame: 2), Frequency);

        w.EvaluationsPerBatch.Should().BeApproximately(2.0, 0.001);
        w.Factor.Should().BeApproximately(2.0, 0.01, "the factor looks plausible, which is exactly why the premise needs its own number");
    }

    [Fact]
    public void ASecondStreamInTheSpanRefusesBecauseTheDrainWordIsProcessWide()
    {
        List<FrameSample> stream = [.. FgStream(appFrames: 40, k: 4)];
        stream.Add(stream[^1] with { SwapchainId = 2, Qpc = stream[^1].Qpc + (ulong)Step });

        FgWindow w = FgWindow.From(stream, Frequency);

        w.Factor.Should().BeNull();
        w.Refusal!.Kind.Should().Be(FgRefusalKind.MultipleStreams);
        w.Refusal.Count.Should().Be(2);
        w.Streams.Should().Be(2);
    }

    [Fact]
    public void AnUnidentifiedSampleRefusesRatherThanBeingCountedAsAPresent()
    {
        List<FrameSample> stream = [.. FgStream(appFrames: 40, k: 4)];
        stream.Add(stream[^1] with { SwapchainId = 0, Qpc = stream[^1].Qpc + (ulong)Step });

        FgWindow w = FgWindow.From(stream, Frequency);

        w.Factor.Should().BeNull();
        w.Refusal!.Kind.Should().Be(FgRefusalKind.Unattributed);
        w.Refusal.Count.Should().Be(1);
    }

    [Fact]
    public void ASaturatedCountRefusesRatherThanDividingByAFloor()
    {
        List<FrameSample> stream = [.. FgStream(appFrames: 40, k: 4)];
        stream[0] = stream[0] with { FgEvaluations = 255 };

        FgWindow w = FgWindow.From(stream, Frequency);

        w.Factor.Should().BeNull();
        w.Refusal!.Kind.Should().Be(FgRefusalKind.CountSaturated);
        w.Saturated.Should().Be(1);
    }

    [Fact]
    public void AWindowTooShortToCheckForAStateChangeDoesNotPublishOne()
    {
        // 32 samples is under the 64 the uniformity check needs. Publishing a factor whose uniformity was
        // never checked is the same claim as publishing one that failed it.
        FgWindow w = FgWindow.From(FgStream(appFrames: 8, k: 4), Frequency);

        w.Factor.Should().BeNull();
        w.Refusal!.Kind.Should().Be(FgRefusalKind.TooShortToCheck);
        w.Refusal.Count.Should().Be(32);
        w.BucketFactors.Should().BeEmpty();
    }

    [Fact]
    public void AnAltTabMidCaptureIsCaughtByTheBatchGuardWhileTheFactorGuardCannotSeeIt()
    {
        // THE 1.84 CASE. On this route fgEvaluations is ZERO on every sample, so the factor-side guard
        // returns at the data-gap clause and never assesses uniformity; presents/batch is what the reader
        // takes away, and it needs its own guard.
        List<FrameSample> stream = Concat(
            FgStream(appFrames: 96, k: 2, evalsPerFrame: 0),
            FgStream(appFrames: 32, k: 1, evalsPerFrame: 0));

        FgWindow w = FgWindow.From(stream, Frequency);

        w.PresentsPerBatch.Should().BeApproximately(1.75, 0.01, "this is the averaged number, and it describes neither half");
        w.Refusal!.Kind.Should().Be(FgRefusalKind.NoEvaluations, "the factor guard keys on fgEvaluations, which is zero here");
        w.BatchRefusal!.Kind.Should().Be(FgRefusalKind.NonUniform);
        w.BatchRefusal.Subject.Should().Be(FgRefusalSubject.PresentsPerBatch);
    }

    [Fact]
    public void AUniformWindowPublishesTheProxyEvenThoughNoEvaluationWasCounted()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, evalsPerFrame: 0), Frequency);

        w.Factor.Should().BeNull("no evaluation was counted, so fg_factor stays N/A");
        w.PresentsPerBatch.Should().BeApproximately(4.0, 0.01);
        w.BatchRefusal.Should().BeNull("every bucket agrees, so the ratio describes one configuration");
    }

    [Fact]
    public void AWindowTooShortToBucketRefusesTheProxyToo()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 8, k: 4, evalsPerFrame: 0), Frequency);

        w.PresentsPerBatch.Should().BeApproximately(4.0, 0.01, "the number exists");
        w.BatchRefusal!.Kind.Should().Be(FgRefusalKind.TooShortToCheck);
        w.BatchRefusal.Subject.Should().Be(FgRefusalSubject.PresentsPerBatch);
    }

    [Fact]
    public void TheProxyRefusesOnTheSameAttributionFactsTheFactorDoes()
    {
        List<FrameSample> stream = [.. FgStream(appFrames: 40, k: 4, evalsPerFrame: 0)];
        stream.Add(stream[^1] with { SwapchainId = 2, Qpc = stream[^1].Qpc + (ulong)Step });

        FgWindow w = FgWindow.From(stream, Frequency);

        w.BatchRefusal!.Kind.Should().Be(FgRefusalKind.MultipleStreams);
        w.BatchRefusal.Subject.Should().Be(FgRefusalSubject.PresentsPerBatch);

        List<FrameSample> orphaned = [.. FgStream(appFrames: 40, k: 4, evalsPerFrame: 0)];
        orphaned.Add(orphaned[^1] with { SwapchainId = 0, Qpc = orphaned[^1].Qpc + (ulong)Step });
        FgWindow.From(orphaned, Frequency).BatchRefusal!.Kind.Should().Be(FgRefusalKind.Unattributed);
    }

    [Fact]
    public void TheProxyDoesNotRefuseOnASaturatedCountBecauseItDoesNotDivideByIt()
    {
        // The two saturation sentinels are the factor's: a saturated fgEvaluations byte says nothing about
        // presents per batch, and refusing the proxy on it would hide the one number that survives the route.
        List<FrameSample> stream = [.. FgStream(appFrames: 40, k: 4)];
        stream[0] = stream[0] with { FgEvaluations = 255 };

        FgWindow w = FgWindow.From(stream, Frequency);

        w.Refusal!.Kind.Should().Be(FgRefusalKind.CountSaturated);
        w.BatchRefusal.Should().BeNull();
    }

    [Fact]
    public void NoBatchAtAllIsNotARatioOfZero()
    {
        FgWindow w = FgWindow.From(
            FgStream(appFrames: 40, k: 4, evalsPerFrame: 0).Select(s => s with { Features = FeatureBits.None }).ToList(),
            Frequency);

        w.Batches.Should().Be(0);
        w.PresentsPerBatch.Should().BeNull();
        w.EvaluationsPerBatch.Should().BeNull();
        w.BatchRefusal!.Kind.Should().Be(FgRefusalKind.NoBatches);
    }

    [Fact]
    public void TheDiagnosticsSurviveEveryRefusal()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 40, k: 4, evalsPerFrame: 255), Frequency);

        w.Refusal.Should().NotBeNull();
        w.Batches.Should().Be(40);
        w.EvaluationsPerBatch.Should().BeApproximately(255.0, 0.001);
        w.Histogram[5].Should().Be(40, "255 lumps into the last histogram slot");
        w.Saturated.Should().Be(40);
    }

    [Fact]
    public void NothingIsDividedWhenThereIsNoDuration()
    {
        FgWindow w = FgWindow.From(FgStream(appFrames: 1, k: 1), Frequency);

        w.Presents.Should().Be(1);
        w.Seconds.Should().Be(0);
        w.DisplayedFps.Should().BeNull();
        w.NativeFps.Should().BeNull();
    }
}
