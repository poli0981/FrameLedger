using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Recording;
using FrameLedger.Shared;

namespace FrameLedger.Application.Tests.Recording;

/// <summary>
/// The identity rules and the withhold rule, pinned where they now live. The capture host's report
/// fixtures pin the same rules through their strings; these pin the decisions by token.
/// </summary>
public sealed class FgLadderTests
{
    private static FlFrameRecord Rec(FlMeasured mask, FlFgMode fg = FlFgMode.NotReported, FlUpscaler up = FlUpscaler.NotReported) =>
        new() { MeasuredMask = (ushort)mask, FgMode = (byte)fg, Upscaler = (byte)up, SwapchainId = 1 };

    [Fact]
    public void AnyRecordNamingATechnologyWinsOverUnknownAndNoClaimIsNoIdentity()
    {
        FlFrameRecord[] unclaimed = [Rec(FlMeasured.None, FlFgMode.DlssG), Rec(FlMeasured.None, FlFgMode.DlssG)];
        FlFrameRecord[] mostlyUnknown = [Rec(FlMeasured.Fg, FlFgMode.Unknown), Rec(FlMeasured.Fg, FlFgMode.Unknown), Rec(FlMeasured.Fg, FlFgMode.DlssG), Rec(FlMeasured.Fg, FlFgMode.Unknown)];
        FlFrameRecord[] allUnknown = [Rec(FlMeasured.Fg, FlFgMode.Unknown), Rec(FlMeasured.Fg, FlFgMode.Unknown)];

        FgLadder.Identity(unclaimed).Should().BeNull("a value under a clear bit is not a claim");
        FgLadder.FgHookRan(unclaimed).Should().BeFalse();
        FgLadder.Identity(mostlyUnknown).Should().Be(FlFgMode.DlssG, "one present in N drains the evaluation");
        FgLadder.Identity(allUnknown).Should().BeNull("UNKNOWN is our coverage being short, never a negative");
        FgLadder.FgHookRan(allUnknown).Should().BeTrue();
    }

    [Fact]
    public void TheUpscalerIdentityFollowsTheSameRuleAndTheRetiredValueDecodesAsNothing()
    {
        FlFrameRecord[] dlss = [Rec(FlMeasured.Upscaler, up: FlUpscaler.Unknown), Rec(FlMeasured.Upscaler, up: FlUpscaler.Dlss)];
        FlFrameRecord[] retired = [Rec(FlMeasured.Upscaler, up: FlUpscaler.RetiredRayReconstruction)];
        FlFrameRecord[] none = [Rec(FlMeasured.Upscaler, up: FlUpscaler.None)];

        FgLadder.UpscalerIdentity(dlss).Should().Be(FlUpscaler.Dlss);
        FgLadder.UpscalerIdentity(retired).Should().BeNull();
        FgLadder.UpscalerHookRan(retired).Should().BeTrue();
        FgLadder.UpscalerIdentity(none).Should().Be(FlUpscaler.None, "a hook that measured no upscaler is a measurement");
        FgLadder.UpscalerIdentity([]).Should().BeNull();
    }

    [Fact]
    public void NoneIsWithheldOnlyOnTheStreamlineShapeWithoutADxgiReading()
    {
        var modulesOld = new RuntimeModuleSet([new RuntimeModuleInfo(FgLadder.SlInterposerFileName, "x", "2,7,1,0", new Version(2, 7, 1, 0))], 1, 0);
        var modulesNew = new RuntimeModuleSet([new RuntimeModuleInfo(FgLadder.SlInterposerFileName, "x", "2,8,0,0", new Version(2, 8, 0, 0))], 1, 0);
        FlRuntimeCensus loaded = FlRuntimeCensus.Ran | FlRuntimeCensus.SlDlssG;
        var notRead = new FlWriterState();
        var readClean = new FlWriterState { DxgiPresentSamples = 100, DxgiPresentsUnseen = 0 };
        var readUnseen = new FlWriterState { DxgiPresentSamples = 100, DxgiPresentsUnseen = 300 };

        FgLadder.WithholdNone(FlRuntimeCensus.None, modulesNew, notRead).Should().BeNull("the census did not run");
        FgLadder.WithholdNone(FlRuntimeCensus.Ran, modulesNew, notRead).Should().BeNull("no sl.dlss_g.dll");
        FgLadder.WithholdNone(loaded, modulesOld, notRead).Should().BeNull("2.7.1 paces below DXGI; none may stand");
        FgLadder.WithholdNone(loaded, modulesNew, notRead).Should().Contain("2.8.0.0").And.Contain("not read");
        FgLadder.WithholdNone(loaded, null, notRead).Should().Contain("could not be read");
        FgLadder.WithholdNone(loaded, modulesNew, readClean).Should().BeNull("DXGI agrees with the count");
        FgLadder.WithholdNone(loaded, modulesNew, readUnseen).Should().Contain("disagree");
        FgLadder.DxgiBesideNone(notRead).Should().BeNull();
        FgLadder.DxgiBesideNone(readClean).Should().Contain("agrees");
    }

    [Fact]
    public void TheCountDecidesNoneAndIdentityDecidesTheName()
    {
        FgLadder.Resolve(null, null, null).Should().Be(FgVerdict.NotMeasured);
        FgLadder.Resolve(FlFgMode.DlssG, null, null).Should().Be(FgVerdict.Named);
        FgLadder.Resolve(FlFgMode.FsrFg, NoneWindow(), null).Should().Be(FgVerdict.Named, "a generated batch drained is a generated batch");
        FgLadder.Resolve(null, NoneWindow(), null).Should().Be(FgVerdict.None);
        FgLadder.Resolve(FlFgMode.DlssG, NoneWindow(), null).Should().Be(FgVerdict.NoneInputsTagged);
        FgLadder.Resolve(null, NoneWindow(), "withheld").Should().Be(FgVerdict.NoneWithheld);
        FgLadder.Resolve(FlFgMode.DlssG, NoneWindow(), "withheld").Should().Be(FgVerdict.NoneWithheld);
        FgLadder.Resolve(null, ActiveWindow(), null).Should().Be(FgVerdict.ActiveUnidentified);
        FgLadder.Resolve(FlFgMode.DlssG, ActiveWindow(), null).Should().Be(FgVerdict.Named);
    }

    /// <summary>A window whose factor is 1.0: every present carried an evaluation.</summary>
    private static Domain.Metrics.FgWindow NoneWindow() => Window(presents: 100, evaluations: 100);

    private static Domain.Metrics.FgWindow ActiveWindow() => Window(presents: 200, evaluations: 100);

    private static Domain.Metrics.FgWindow Window(int presents, int evaluations)
    {
        var samples = new List<Domain.Metrics.FrameSample>(presents);
        int perBatch = presents / evaluations;
        for (int i = 0; i < presents; i++)
        {
            samples.Add(new Domain.Metrics.FrameSample
            {
                Qpc = 1_000_000 + (ulong)i * 100_000,
                FrameIndex = (uint)i,
                SwapchainId = 1,
                Measured = Domain.Metrics.MeasuredFields.Fg | Domain.Metrics.MeasuredFields.FgCounts,
                FgEvaluations = (byte)(i % perBatch == 0 ? 1 : 0),
            });
        }

        return Domain.Metrics.FgWindow.From(samples, 10_000_000);
    }
}
