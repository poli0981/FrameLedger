using FluentAssertions;
using FrameLedger.Domain.Metrics;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>The render → output extent: <c>03_METRICS</c>' ratio over two measured sizes, modal, never a preset name.</summary>
public sealed class UpscaleExtentTests
{
    private static FrameSample Sample(ushort renderW, ushort renderH, ushort outputW, ushort outputH,
        MeasuredFields extra = MeasuredFields.UpscalerParams | MeasuredFields.Upscaler) => new()
        {
            SwapchainId = 1,
            OutputW = outputW,
            OutputH = outputH,
            RenderW = renderW,
            RenderH = renderH,
            UpscalerQuality = 0xFF,
            UpscalerSharpness = 0xFF,
            Upscaler = UpscalerKind.Dlss,
            Measured = MeasuredFields.OutputRes | MeasuredFields.PresentArgs | extra,
        };

    [Fact]
    public void CyberpunkAtBalancedIs172xAnd58Percent()
    {
        // Measured 2026-08-15 against the title's own menu: 1485x835 at 2560x1440 is DLSS Balanced's 0.58,
        // and a writer that hardcoded a plausible resolution cannot produce it.
        var stream = Enumerable.Range(0, 40).Select(_ => Sample(1485, 835, 2560, 1440)).ToList();

        UpscaleExtent? e = UpscaleExtent.From(stream);

        e.Should().NotBeNull();
        e!.Ratio.Should().BeApproximately(1.724, 0.001);
        e.RenderScalePercent.Should().BeApproximately(58.0, 0.5);
        e.Records.Should().Be(40);
        e.Measured.Should().Be(40);
        e.DistinctGroups.Should().Be(1);
    }

    [Fact]
    public void ASettingsChangeMidWindowIsTheDominantTupleWithTheCount()
    {
        var stream = Enumerable.Range(0, 30).Select(_ => Sample(1707, 960, 2560, 1440))
            .Concat(Enumerable.Range(0, 60).Select(_ => Sample(1485, 835, 2560, 1440))).ToList();

        UpscaleExtent? e = UpscaleExtent.From(stream);

        e.Should().NotBeNull();
        e!.RenderW.Should().Be(1485);
        e.RenderH.Should().Be(835);
        e.OutputW.Should().Be(2560);
        e.OutputH.Should().Be(1440);
        e.Records.Should().Be(60);
        e.Measured.Should().Be(90);
        e.DistinctGroups.Should().Be(2);
    }

    [Fact]
    public void AParamsBitOnOneInNPresentsUnderFrameGenerationStillYieldsTheExtent()
    {
        // THE SHAPE EVERY FG-ON CAPTURE HAS: identity on every sample, params only on the present that
        // drained the dispatch (1 in N). Keyed on the params bit the window was empty.
        List<FrameSample> stream = [];
        for (int f = 0; f < 30; f++)
        {
            for (int p = 0; p < 3; p++)
            {
                stream.Add(p == 0
                    ? Sample(1506, 847, 2560, 1440)
                    : Sample(0, 0, 2560, 1440, extra: MeasuredFields.Upscaler));
            }
        }

        UpscaleExtent? e = UpscaleExtent.From(stream);

        e.Should().NotBeNull("one present in three carried both sizes, inside a live identity window");
        e!.Records.Should().Be(30);
        e.Measured.Should().Be(30);
        e.DistinctGroups.Should().Be(1);
        e.RenderScalePercent.Should().BeApproximately(59.0, 0.5);
    }

    [Fact]
    public void AnIntermittentIdentityBitIsStillNoWindow()
    {
        List<FrameSample> stream =
        [
            Sample(1506, 847, 2560, 1440),
            Sample(0, 0, 2560, 1440, extra: MeasuredFields.None),
            Sample(1506, 847, 2560, 1440),
        ];

        UpscaleExtent.From(stream).Should().BeNull();
    }

    [Fact]
    public void SamplesWithoutBothBitsDoNotCount()
    {
        List<FrameSample> stream =
        [
            Sample(1485, 835, 2560, 1440, extra: MeasuredFields.Upscaler),    // params bit clear
            Sample(1485, 835, 0, 0),                                          // no output size
        ];

        UpscaleExtent.From(stream).Should().BeNull();
    }

    [Fact]
    public void AParamsOnlyWriterIsWindowedOnTheParamsBitItself()
    {
        // No identity bit anywhere — a params-only writer publishes the params bit per sample, and that
        // is the install window when there is no identity one to prefer.
        var stream = Enumerable.Range(0, 10).Select(_ => Sample(1280, 720, 2560, 1440, extra: MeasuredFields.UpscalerParams)).ToList();

        UpscaleExtent? e = UpscaleExtent.From(stream);

        e.Should().NotBeNull();
        e!.Ratio.Should().BeApproximately(2.0, 0.001);
        e.RenderScalePercent.Should().BeApproximately(50.0, 0.001);
    }

    [Fact]
    public void NoWindowAtAllIsNull() =>
        UpscaleExtent.From([Sample(1485, 835, 2560, 1440, extra: MeasuredFields.None)]).Should().BeNull();
}
