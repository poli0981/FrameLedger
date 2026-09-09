using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>
/// <c>14_TESTING</c>'s RT goldens: AS-builds-only ⇒ Yes (the named regression), dispatch-only ⇒ Yes, an
/// RT-capable device with the AS-build hook and nothing observed ⇒ No, D3D11 ⇒ N/A — and the ×4 falsifier.
/// </summary>
public sealed class RtVerdictTests
{
    private static readonly WriterFacts _capable = new()
    {
        RtTier = (uint)RtTierValue.CapableMin,
        HooksInstalled = HookFamilies.Present | HookFamilies.RtDispatch | HookFamilies.RtAsBuild,
    };

    private static List<FrameSample> RtStream(int n, Func<int, RtEvidenceBits> evidence, Func<int, uint>? volume = null) =>
        [.. Stream(n, extra: MeasuredFields.Rt).Select((s, i) => s with { Rt = evidence(i), DispatchRaysVolume = volume?.Invoke(i) ?? 0 })];

    [Fact]
    public void AsBuildsAloneAreYes()
    {
        // The inline-RayQuery title: DispatchRays is never called, AS builds are — and a writer with only
        // the dispatch hook would have read this as silence.
        List<FrameSample> stream = RtStream(40, _ => RtEvidenceBits.AsBuildObserved);

        RtVerdict.RayTracing(stream, _capable).Should().Be(Tri.Yes);
    }

    [Fact]
    public void DispatchesAloneAreYes() =>
        RtVerdict.RayTracing(RtStream(40, _ => RtEvidenceBits.DispatchObserved), _capable).Should().Be(Tri.Yes);

    [Fact]
    public void ACapableDeviceWithTheAsBuildHookAndNoEvidenceIsNo()
    {
        RtVerdict.RayTracing(RtStream(40, _ => RtEvidenceBits.None), _capable).Should().Be(Tri.No);
    }

    [Fact]
    public void NoIsUnreachableWithoutEachOfItsThreeConjuncts()
    {
        List<FrameSample> silent = RtStream(40, _ => RtEvidenceBits.None);

        RtVerdict.RayTracing(silent, _capable with { HooksInstalled = HookFamilies.Present | HookFamilies.RtDispatch })
            .Should().Be(Tri.NotApplicable, "a writer without the AS-build hook sees nothing on a RayQuery-only title");
        RtVerdict.RayTracing(silent, _capable with { RtTier = (uint)RtTierValue.NotQueried })
            .Should().Be(Tri.NotApplicable, "rtTier 0 means NOT QUERIED, not `not capable`");
        RtVerdict.RayTracing(silent, _capable with { RtTier = (uint)RtTierValue.Unsupported })
            .Should().Be(Tri.NotApplicable, "a device that cannot ray-trace produces no evidence, so `No` says nothing");
        RtVerdict.RayTracing(Stream(40), _capable).Should().Be(Tri.NotApplicable, "D3D11: no sample claims the measurement");
    }

    [Fact]
    public void TheFivePercentGateIsAShareOfPresents()
    {
        // 2 of 40 = 5% → Yes; 1 of 40 = 2.5% → not Yes, and not No either while evidence exists.
        RtVerdict.RayTracing(RtStream(40, i => i < 2 ? RtEvidenceBits.DispatchObserved : RtEvidenceBits.None), _capable).Should().Be(Tri.Yes);
        RtVerdict.RayTracing(RtStream(40, i => i < 1 ? RtEvidenceBits.DispatchObserved : RtEvidenceBits.None), _capable)
            .Should().Be(Tri.NotApplicable, "one evidence frame in 40 is under the gate, and `No` needs zero");
    }

    [Fact]
    public void TheClaimingWindowExcludesTheInstallPrefix()
    {
        // The RT hooks come up at sample 10; the 10 before it claim nothing and cannot carry evidence.
        List<FrameSample> stream = [.. Stream(10), .. RtStream(30, _ => RtEvidenceBits.AsBuildObserved)];

        RtSummary s = RtVerdict.Summarise(stream);

        s.Claimed.Should().Be(30);
        s.Evidence.Should().Be(30);
        s.FramePct.Should().Be(100);
        RtVerdict.RayTracing(stream, _capable).Should().Be(Tri.Yes);
    }

    [Fact]
    public void FramePctIsDilutedByFrameGenerationAndRaysPerPixelIsNot()
    {
        // The pre-registered falsifier: at ×4 the title path-traces every APPLICATION frame, so one present
        // in four carries the volume. rt_frame_pct reads ≈ 25% (over presents, by decision); rays_per_pixel
        // is taken over the RT-active presents only and stays at the truth.
        const uint rays = 3840u * 2160u;
        List<FrameSample> stream = RtStream(400,
            i => i % 4 == 0 ? RtEvidenceBits.DispatchObserved : RtEvidenceBits.None,
            i => i % 4 == 0 ? rays : 0);

        RtSummary s = RtVerdict.Summarise(stream);

        s.FramePct.Should().Be(25);
        s.ActivePresents.Should().Be(100);
        s.RaysPerPixel.Should().BeApproximately(1.0, 1e-9);
        s.TotalVolume.Should().Be(100L * rays);
        RtVerdict.RayTracing(stream, _capable).Should().Be(Tri.Yes, "25% clears the 5% gate by five times");
    }

    [Fact]
    public void ASaturatedVolumeRefusesRaysPerPixel()
    {
        List<FrameSample> stream = RtStream(20, _ => RtEvidenceBits.DispatchObserved, i => i == 3 ? uint.MaxValue : 1000u);

        RtSummary s = RtVerdict.Summarise(stream);

        s.VolumeSaturated.Should().Be(1);
        s.RaysPerPixel.Should().BeNull("a saturated volume is a floor, not a count");
    }

    [Fact]
    public void NoOutputSizeMeansNoPixelsToDivideBy()
    {
        List<FrameSample> stream = [.. RtStream(20, _ => RtEvidenceBits.DispatchObserved, _ => 1000u).Select(s => s with { OutputW = 0, OutputH = 0 })];

        RtVerdict.Summarise(stream).RaysPerPixel.Should().BeNull();
        RtVerdict.Summarise([]).FramePct.Should().BeNull();
    }

    [Fact]
    public void RayReconstructionIsDecidedOverThePresentsThatDrainedABatch()
    {
        FrameSample plain = Present(1);
        FrameSample batch = plain with { Features = FeatureBits.RayReconstructionObserved };
        FrameSample rr = plain with { Features = FeatureBits.RayReconstructionObserved | FeatureBits.RayReconstruction };

        RtVerdict.RayReconstruction([plain, plain, plain]).Should().Be(Tri.NotApplicable, "nothing drained a batch");
        RtVerdict.RayReconstruction([plain, batch, plain, plain]).Should().Be(Tri.No, "a batch carried no RR — the one aggregatable RR negative");
        RtVerdict.RayReconstruction([plain, batch, plain, rr]).Should().Be(Tri.Yes, "any batch carrying RR is Yes; a title that turned it off mid-session still ran it");
        RtVerdict.RayReconstruction([plain with { Features = FeatureBits.RayReconstruction }]).Should().Be(Tri.NotApplicable,
            "the fact bit without its OBSERVED bit is nobody's claim");
    }

    [Fact]
    public void WriterFactsCarryTheCapabilityThreshold()
    {
        WriterFacts.Nothing.RtCapable.Should().BeFalse();
        WriterFacts.Nothing.HooksInstalled.Should().Be(HookFamilies.None);
        new WriterFacts { RtTier = 11 }.RtCapable.Should().BeTrue();
        new WriterFacts { RtTier = 1 }.RtCapable.Should().BeFalse();
    }
}
