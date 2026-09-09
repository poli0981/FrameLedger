using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

public sealed class HdrVerdictTests
{
    [Fact]
    public void NoColorSpaceHookIsNotApplicable() => HdrVerdict.Of(Stream(5)).Should().Be(Tri.NotApplicable);

    [Fact]
    public void TheLastMeasuredColorSpaceDecides()
    {
        List<FrameSample> hdr = [.. Stream(5, extra: MeasuredFields.Hdr).Select(s => s with { ColorSpace = ColorSpaceKind.Hdr10 })];
        List<FrameSample> scrgb = [.. Stream(5, extra: MeasuredFields.Hdr).Select(s => s with { ColorSpace = ColorSpaceKind.ScRgb })];
        List<FrameSample> sdr = [.. Stream(5, extra: MeasuredFields.Hdr).Select(s => s with { ColorSpace = ColorSpaceKind.Sdr })];

        HdrVerdict.Of(hdr).Should().Be(Tri.Yes);
        HdrVerdict.Of(scrgb).Should().Be(Tri.Yes);
        HdrVerdict.Of(sdr).Should().Be(Tri.No, "SDR is a measured default once the hook is live, not an affirmative negative");
    }
}
