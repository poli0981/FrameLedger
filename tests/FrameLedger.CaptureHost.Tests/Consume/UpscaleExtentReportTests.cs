using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The <c>render -&gt; output</c> line: both sizes, the ratio, the scale, the settings-moved flag — and never a
/// preset name. The arithmetic's own tests live with <c>Domain.Metrics.UpscaleExtent</c>.
/// </summary>
public sealed class UpscaleExtentReportTests
{
    private static FlFrameRecord Record(ushort renderW, ushort renderH, ushort outputW, ushort outputH,
        FlMeasured extra = FlMeasured.UpscalerParams | FlMeasured.Upscaler) => new()
        {
            SwapchainId = 1,
            OutputW = outputW,
            OutputH = outputH,
            RenderW = renderW,
            RenderH = renderH,
            UpscalerQuality = 0xFF,
            UpscalerSharpness = 0xFF,
            Upscaler = (byte)FlUpscaler.Dlss,
            MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | extra),
        };

    private static readonly FlWriterState _paramsWriter = new()
    {
        HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams),
        RuntimeCensus = (uint)FlRuntimeCensus.Ran,
    };

    [Fact]
    public void TheReportPrintsBothSizesTheRatioAndTheScaleAndNoPresetName()
    {
        var stream = Enumerable.Range(0, 40).Select(_ => Record(1485, 835, 2560, 1440)).ToList();

        MeasuredFacts facts = MeasuredFacts.From(stream, _paramsWriter, Stopwatch.Frequency, 0, 0);
        string text = SessionReport.Render(facts);

        facts.UpscaleRatio.Should().BeApproximately(1.724, 0.001);
        text.Should().Contain("render -> output: 1485x835 -> 2560x1440 = 1.72x (58% render scale) on 40 of 40 record(s)");
        text.Should().NotContain("Balanced", "a preset name derived from the ratio is HANDOFF 7a's owner decision, not this line's");
        text.Should().NotContain("SETTINGS MOVED");
    }

    [Fact]
    public void ASettingsChangeMidWindowPrintsTheDominantTupleWithTheFlag()
    {
        var stream = Enumerable.Range(0, 30).Select(_ => Record(1707, 960, 2560, 1440))
            .Concat(Enumerable.Range(0, 60).Select(_ => Record(1485, 835, 2560, 1440))).ToList();

        string text = SessionReport.Render(MeasuredFacts.From(stream, _paramsWriter, Stopwatch.Frequency, 0, 0));

        text.Should().Contain("1485x835 -> 2560x1440").And.Contain("on 60 of 90 record(s)");
        text.Should().Contain("SETTINGS MOVED: 2 distinct extents");
    }

    [Fact]
    public void RecordsWithoutBothBitsPrintNotApplicableRatherThanZero()
    {
        List<FlFrameRecord> stream =
        [
            Record(1485, 835, 2560, 1440, extra: FlMeasured.Upscaler),
            Record(1485, 835, 0, 0),
        ];

        string text = SessionReport.Render(MeasuredFacts.From(stream,
            new FlWriterState { HooksInstalledMask = (uint)FlHookFamily.Present }, Stopwatch.Frequency, 0, 0));

        text.Should().Contain("render -> output: N/A (no record carried both a render size and an output size)");
    }
}
