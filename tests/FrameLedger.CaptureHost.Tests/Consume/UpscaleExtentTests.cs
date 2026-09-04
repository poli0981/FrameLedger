using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The render → output line: <c>03_METRICS</c>' ratio over two measured sizes, modal, with the
/// settings-moved flag — and never a preset name.
/// </summary>
public sealed class UpscaleExtentTests
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

    [Fact]
    public void CyberpunkAtBalancedIs172xAnd58Percent()
    {
        // The number measured on 2026-08-15 against the title's own menu: 1485x835 at 2560x1440 is
        // DLSS Balanced's 0.58, and a writer that hardcoded a plausible resolution cannot produce it.
        var stream = Enumerable.Range(0, 40).Select(_ => Record(1485, 835, 2560, 1440)).ToList();

        UpscaleExtent? e = UpscaleExtent.From(stream);

        e.Should().NotBeNull();
        e!.Ratio.Should().BeApproximately(1.724, 0.001);
        e.RenderScalePercent.Should().BeApproximately(58.0, 0.5);
        e.Records.Should().Be(40);
        e.Measured.Should().Be(40);
        e.DistinctGroups.Should().Be(1);
    }

    [Fact]
    public void TheReportPrintsBothSizesTheRatioAndTheScaleAndNoPresetName()
    {
        var stream = Enumerable.Range(0, 40).Select(_ => Record(1485, 835, 2560, 1440)).ToList();
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams),
            RuntimeCensus = (uint)FlRuntimeCensus.Ran,
        };

        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0);
        string text = SessionReport.Render(facts);

        facts.UpscaleRatio.Should().BeApproximately(1.724, 0.001);
        text.Should().Contain("render -> output: 1485x835 -> 2560x1440 = 1.72x (58% render scale) on 40 of 40 record(s)");
        text.Should().NotContain("Balanced", "a preset name derived from the ratio is HANDOFF 7a's owner decision, not this line's");
        text.Should().NotContain("SETTINGS MOVED");
    }

    [Fact]
    public void ASettingsChangeMidWindowIsTheDominantTupleWithTheFlag()
    {
        // Quality for the first third, Balanced after. Averaging would print a resolution nobody
        // rendered at; the modal tuple is a resolution somebody did, and the flag says it moved.
        var stream = Enumerable.Range(0, 30).Select(_ => Record(1707, 960, 2560, 1440))
            .Concat(Enumerable.Range(0, 60).Select(_ => Record(1485, 835, 2560, 1440))).ToList();

        UpscaleExtent? e = UpscaleExtent.From(stream);
        string text = SessionReport.Render(MeasuredFacts.From(stream, new FlWriterState
        {
            HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams),
        }, Stopwatch.Frequency, 0, 0));

        e.Should().NotBeNull();
        e!.RenderW.Should().Be(1485);
        e.Records.Should().Be(60);
        e.Measured.Should().Be(90);
        e.DistinctGroups.Should().Be(2);
        text.Should().Contain("SETTINGS MOVED: 2 distinct extents");
    }

    [Fact]
    public void RecordsWithoutBothBitsDoNotCount()
    {
        // A value without its bit is the writer's defect; params without an output size cannot make
        // a ratio; a window with neither is null and the report says so rather than printing 0.
        List<FlFrameRecord> stream =
        [
            Record(1485, 835, 2560, 1440, extra: FlMeasured.Upscaler),          // params bit clear
            Record(1485, 835, 0, 0),                                             // no output size
        ];

        UpscaleExtent.From(stream).Should().BeNull();
        string text = SessionReport.Render(MeasuredFacts.From(stream,
            new FlWriterState { HooksInstalledMask = (uint)FlHookFamily.Present }, Stopwatch.Frequency, 0, 0));
        text.Should().Contain("render -> output: N/A (no record carried both a render size and an output size)");
    }
}
