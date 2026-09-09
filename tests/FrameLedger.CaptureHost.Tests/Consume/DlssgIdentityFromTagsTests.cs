using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Application.Metrics;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The identity half of frame generation from the tags a title sends (<c>fl_shm.h</c> §slTagCensus):
/// <c>DlssG</c> on a present that drained a HUD-less or UI tag, combined with the COUNT by one rule —
/// the count decides <c>none</c>, identity decides the name.
/// </summary>
public sealed class DlssgIdentityFromTagsTests
{
    private const FlHookFamily _hooks =
        FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams | FlHookFamily.FgEvaluations;

    /// <summary>k presents per application frame; the token and the DLSS_G mark land on the first of each.</summary>
    private static List<FlFrameRecord> Stream(int appFrames, int k, bool dlssgMark)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        long step = Stopwatch.Frequency / (60 * k);
        for (int f = 0; f < appFrames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                stream.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    OutputW = 2560,
                    OutputH = 1440,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler
                                            | FlMeasured.Fg | FlMeasured.FgCounts),
                    Upscaler = (byte)(p == 0 ? FlUpscaler.Dlss : FlUpscaler.Unknown),
                    FgEvaluations = (byte)(p == 0 ? 1 : 0),
                    FgMode = (byte)(p == 0 && dlssgMark ? FlFgMode.DlssG : FlFgMode.Unknown),
                });
                qpc += (ulong)step;
            }
        }

        return stream;
    }

    private static uint Census(FlSlTagType global, FlSlTagType frame = FlSlTagType.None, FlSlTagType local = FlSlTagType.None) =>
        ((uint)global << FlSlTagRoute.Global) | ((uint)frame << FlSlTagRoute.Frame) | ((uint)local << FlSlTagRoute.Local);

    private static (MeasuredFacts Facts, string Text) Render(List<FlFrameRecord> stream, FlRuntimeCensus census,
        uint tagCensus, RuntimeModuleSet? modules = null)
    {
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)_hooks,
            RuntimeCensus = (uint)census,
            SlTagCensus = tagCensus,
        };
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0,
            FgWindow.From(FrameSampleMapper.Map(stream), Stopwatch.Frequency), modules);
        return (facts, SessionReport.Render(facts));
    }

    private const FlSlTagType _dlssgList =
        FlSlTagType.Depth | FlSlTagType.MotionVectors | FlSlTagType.ScalingInput | FlSlTagType.Hudless | FlSlTagType.UiColorAlpha;

    [Fact]
    public void AnActiveCountBesideTheDlssgMarkIsNamedDlssG()
    {
        // Cyberpunk at MFG x3, Hell Is Us / Expedition 33 / Wukong at x4: the count was right and the
        // technology unnamed on every one, because kFeatureDLSS_G is never evaluated. With the
        // HUD-less / UI tags marking the drained present, the trio names it.
        (MeasuredFacts facts, string text) = Render(Stream(100, 4, dlssgMark: true),
            FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.NvngxDlssG, Census(_dlssgList));

        facts.Fg!.IsActive.Should().BeTrue();
        facts.DlssgInputsTagged.Should().BeTrue();
        facts.FgMode.Should().Be("DlssG");
        text.Should().Contain("Native FPS").And.Contain("x4 FG");
        text.Should().Contain("frame generation: DlssG");
        text.Should().NotContain("technology not identified");
    }

    [Fact]
    public void ACountedNoneBesideTheDlssgMarkIsStillNoneWithTheInputsNoted()
    {
        // A title may tag the DLSS-G inputs with frame generation switched off in its menu — the
        // mark is "feeding", not "generating". The count decides none; the identity is noted, never
        // promoted to DlssG, and no factor prints.
        (MeasuredFacts facts, string text) = Render(Stream(200, 1, dlssgMark: true),
            FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.NvngxDlssG, Census(_dlssgList));

        facts.Fg!.IsNone.Should().BeTrue();
        facts.FgMode.Should().Be(MeasuredFacts.FgNoneInputsTagged);
        text.Should().Contain("  FPS: ").And.Contain("frame generation: none");
        text.Should().Contain("DLSS-G inputs were tagged");
        text.Should().NotContain("frame generation: DlssG");
        text.Should().NotContain("Native FPS");
    }

    [Fact]
    public void ACountedNoneWithoutTheMarkIsPlainNone()
    {
        (MeasuredFacts facts, string text) = Render(Stream(200, 1, dlssgMark: false),
            FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.NvngxDlssG,
            Census(FlSlTagType.ScalingInput | FlSlTagType.Depth | FlSlTagType.MotionVectors));

        facts.DlssgInputsTagged.Should().BeFalse();
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        text.Should().Contain("frame generation: None").And.NotContain("DLSS-G inputs were tagged");
    }

    [Fact]
    public void TheWithheldShapeKeepsItsNAAndSaysTheInputsWereFed()
    {
        // Dying Light: The Beast: the count is blind to the generated presents (§H5), so `none` is
        // withheld; the tags say the title is feeding DLSS-G, which strengthens the qualifier without
        // becoming a count.
        var interposer = new RuntimeModuleSet(
            [new RuntimeModuleInfo("sl.interposer.dll", @"D:\Games\dltb\sl.interposer.dll", "2,8,0,0", new Version(2, 8, 0, 0))],
            Snapshots: 1, Unreadable: 0);
        (MeasuredFacts facts, string text) = Render(Stream(200, 1, dlssgMark: true),
            FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.SlDlss | FlRuntimeCensus.SlDlssG
            | FlRuntimeCensus.NvngxDlssG,
            Census(FlSlTagType.None, frame: _dlssgList), interposer);

        facts.NoneWithheld.Should().NotBeNull();
        facts.FgMode.Should().StartWith(MeasuredFacts.FgNoneWithheldPrefix);
        text.Should().Contain("Presented FPS:").And.Contain("WITHHELD");
        text.Should().Contain("FEEDING frame generation");
        text.Should().NotContain("frame generation: DlssG");
    }

    [Fact]
    public void FsrFgIdentityKeepsPrecedenceOverTheCount()
    {
        // A drained FRAMEGENERATION dispatch is a generated batch — a fact about generation, which
        // the tag mark is not. The pre-existing precedence is unchanged.
        List<FlFrameRecord> stream = Stream(100, 2, dlssgMark: false);
        for (int i = 0; i < stream.Count; i += 2)
        {
            FlFrameRecord r = stream[i];
            r.FgMode = (byte)FlFgMode.FsrFg;
            stream[i] = r;
        }

        (MeasuredFacts facts, _) = Render(stream, FlRuntimeCensus.Ran | FlRuntimeCensus.AmdFfxDx12, 0);

        facts.FgMode.Should().Be("FsrFg");
    }

    [Fact]
    public void TheCensusLineNamesEveryTypeOnEveryRoute()
    {
        uint census = Census(FlSlTagType.Hudless | FlSlTagType.UiAlpha, frame: FlSlTagType.ScalingInput,
            local: FlSlTagType.Depth | FlSlTagType.Other);

        string line = SlTagCensusNames.DescribeRoutes(census);

        line.Should().Be("global=[hudless, ui-alpha]  frame=[scaling-in]  local=[depth, other]");
        SlTagCensusNames.DescribeRoutes(0).Should().Be("global=[-]  frame=[-]  local=[-]");
        FlSlTagRoute.Any(census).Should().HaveFlag(FlSlTagType.DlssgInputs & FlSlTagType.Hudless);
        (FlSlTagRoute.Any(Census(FlSlTagType.Depth)) & FlSlTagType.DlssgInputs).Should().Be(FlSlTagType.None);
    }
}
