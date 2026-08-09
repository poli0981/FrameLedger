using System.Diagnostics;
using System.Text.RegularExpressions;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
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
        MeasuredFacts facts = MeasuredFacts.From(Stream(120, Stopwatch.Frequency / 120), default, Stopwatch.Frequency, 0, 0);

        facts.Upscaler.Should().BeNull("no upscaler hook ran, and `none` would be a measured negative");
        facts.FgMode.Should().BeNull("03_METRICS' ladder rung 4 says `none`, which here would be a lie");
        facts.FgFactor.Should().BeNull("1.0 is CLAUDE.md rule 6's forbidden number");
        facts.NativeFps.Should().BeNull("F_app = presents − Σ fgEvaluations needs a hook that counted");
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
        MeasuredFacts facts = MeasuredFacts.From(Stream(120, Stopwatch.Frequency / 120), default, Stopwatch.Frequency, 0, 0);

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
            MeasuredFacts.From(Stream(60, Stopwatch.Frequency / 60), default, Stopwatch.Frequency, 0, 0));

        text.Should().Contain("FG state not measured");
        FgFactorShape().IsMatch(text).Should().BeFalse("an ×N beside a single number reads as a measured factor");
        text.Should().NotContain("Native FPS", "showing it as N/A beside a real Displayed figure invites "
                                               + "reading Displayed as Native");
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

        MeasuredFacts.From(stream, default, Stopwatch.Frequency, 0, 0).HonestyViolations.Should().Be(1);
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

        var notQueried = capable with { RtTier = 0 };
        MeasuredFacts.From(stream, notQueried, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(
            Tri.NotApplicable, "rtTier 0 means NOT QUERIED, not `not capable`");
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

        MeasuredFacts.From(stream, default, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(Tri.Yes);

        // Evidence with the mask bit CLEAR is a writer contradicting itself, and the honest reading of
        // "nobody looked" wins over a flag that could only have been set by someone who did.
        List<FlFrameRecord> unmasked = [.. stream.Select(r =>
        {
            r.MeasuredMask &= unchecked((ushort)~(ushort)FlMeasured.Rt);
            return r;
        })];
        MeasuredFacts.From(unmasked, default, Stopwatch.Frequency, 0, 0).RayTracing.Should().Be(Tri.NotApplicable);
    }

    [Fact]
    public void TheRetiredUpscalerValueIsNeverDecodedAsRayReconstruction()
    {
        // 03_METRICS §Upscaling still lists `dlss_rr` as an upscaler value and is stale against layout
        // v3, which RETIRED and RESERVED slot 2 precisely because it made Ray Reconstruction mutually
        // exclusive with DLSS super-resolution. Decoding 2 as anything resurrects the conflation.
        List<FlFrameRecord> stream = [.. Stream(5, 1000).Select(r =>
        {
            r.MeasuredMask |= (ushort)FlMeasured.Upscaler;
            r.Upscaler = (byte)FlUpscaler.RetiredRayReconstruction;
            return r;
        })];

        MeasuredFacts.From(stream, default, Stopwatch.Frequency, 0, 0).Upscaler.Should().BeNull();
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

        MeasuredFacts.From(stream, default, Stopwatch.Frequency, 0, 0).Upscaler.Should().BeNull(
            "a hook that ran and could not tell is still N/A — a different N/A, but not a name");
    }

    [Fact]
    public void GapsAndDropsAreReportedRatherThanAbsorbed()
    {
        MeasuredFacts facts = MeasuredFacts.From(Stream(10, 1000), default, Stopwatch.Frequency, totalGaps: 0, totalDropped: 4);

        facts.HasDataGaps.Should().BeTrue();
        SessionReport.Render(facts).Should().Contain("torn or overwritten");
    }
}
