using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>
/// Displayed counted by DXGI (<c>dxgiUnseen</c>, <c>20_OPEN_QUESTIONS</c> §H5 row P1-DXGI): on Dying Light: The
/// Beast the hook timed one present per application frame while DXGI's own counter on the same chain moved
/// by ≈ 3.9 — so the window counts <c>Displayed = hooked + unseen</c>, and every other title is byte-identical.
/// </summary>
public sealed class DxgiCountedWindowTests
{
    /// <summary>The DL:TB shape: one hooked present per application frame, each carrying <paramref name="unseen"/> DXGI-counted presents.</summary>
    private static List<FrameSample> Stream(int appFrames, byte unseen, bool claim = true, int from = 0, int to = int.MaxValue)
    {
        List<FrameSample> stream = [];
        ulong qpc = 1_000_000;
        for (int f = 0; f < appFrames; f++)
        {
            bool inRange = f >= from && f < to;
            stream.Add(new FrameSample
            {
                Qpc = qpc,
                SwapchainId = 1,
                OutputW = 2560,
                OutputH = 1440,
                Measured = MeasuredFields.OutputRes | MeasuredFields.PresentArgs | MeasuredFields.Upscaler
                           | MeasuredFields.Fg | MeasuredFields.FgCounts
                           | (claim ? MeasuredFields.DxgiPresents : MeasuredFields.None),
                Upscaler = UpscalerKind.Dlss,
                FgEvaluations = 1,
                FgMode = FgKind.DlssG,
                Features = FeatureBits.RayReconstructionObserved,
                DxgiUnseen = inRange ? unseen : (byte)0,
            });
            qpc += (ulong)(Frequency / 70);
        }

        return stream;
    }

    [Fact]
    public void ThreeUnseenPerHookedPresentMakesAFourTimesFactorLabelledDxgiCounted()
    {
        FgWindow w = FgWindow.From(Stream(200, unseen: 3), Frequency);

        w.Presents.Should().Be(200);
        w.DxgiUnseen.Should().Be(600);
        w.DxgiClaiming.Should().Be(200);
        w.DisplayedPresents.Should().Be(800);
        w.DxgiCounted.Should().BeTrue();
        w.Refusal.Should().BeNull();
        w.Factor.Should().BeApproximately(4.0, 0.001);
        w.IsActive.Should().BeTrue();
        w.PresentsPerBatch.Should().BeApproximately(4.0, 0.001);
        (w.DisplayedFps / w.NativeFps).Should().BeApproximately(4.0, 0.03, "Native × factor ≈ Displayed over ONE window");
    }

    [Fact]
    public void ZeroUnseenEverywhereIsByteIdenticalToTheWindowBeforeTheByteExisted()
    {
        FgWindow a = FgWindow.From(Stream(200, unseen: 0), Frequency);
        FgWindow b = FgWindow.From(Stream(200, unseen: 0, claim: false), Frequency);

        a.DxgiCounted.Should().BeFalse();
        a.DxgiClaiming.Should().Be(200);
        b.DxgiClaiming.Should().Be(0);
        a.DisplayedPresents.Should().Be(200);
        a.Factor.Should().Be(b.Factor);
        a.DisplayedFps.Should().Be(b.DisplayedFps);
        a.BucketFactors.Should().Equal(b.BucketFactors);
        a.IsNone.Should().BeTrue();
    }

    [Fact]
    public void APacerThatStopsHalfWayFailsUniformityLikeAnyOtherStateChange()
    {
        FgWindow w = FgWindow.From(Stream(400, unseen: 3, from: 0, to: 200), Frequency);

        w.DxgiUnseen.Should().Be(600);
        w.Refusal!.Kind.Should().Be(FgRefusalKind.NonUniform);
        w.Factor.Should().BeNull();
        w.BatchRefusal!.Kind.Should().Be(FgRefusalKind.NonUniform, "presents/batch takes the same numerator");
    }

    [Fact]
    public void ASaturatedByteIsASentinelAndTheFactorIsRefused()
    {
        List<FrameSample> s = Stream(200, unseen: 3);
        s[100] = s[100] with { DxgiUnseen = byte.MaxValue };

        FgWindow w = FgWindow.From(s, Frequency);

        w.DxgiSaturated.Should().Be(1);
        w.Refusal!.Kind.Should().Be(FgRefusalKind.DxgiSaturated);
        w.Refusal.Count.Should().Be(1);
        w.Factor.Should().BeNull();
    }

    [Fact]
    public void AByteUnderAClearBitIsNobodysCount()
    {
        FgWindow w = FgWindow.From(Stream(200, unseen: 3, claim: false), Frequency);

        w.DxgiUnseen.Should().Be(0, "an unclaimed byte is not read");
        w.Factor.Should().BeApproximately(1.0, 0.001);
    }
}
