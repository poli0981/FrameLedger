using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>
/// The stutter rule, and the golden <c>14_TESTING</c> names: a gap does not appear as a stutter.
/// </summary>
public sealed class StutterDetectorTests
{
    [Fact]
    public void AFrameMoreThanTwiceTheRollingMedianIsAStutter()
    {
        double[] series = [.. Enumerable.Repeat(10.0, 40)];
        series[20] = 21;    // > 2 × 10
        series[30] = 20;    // exactly 2 × 10 is NOT a stutter

        StutterResult? r = StutterDetector.Detect(series);

        r.Should().NotBeNull();
        r!.Count.Should().Be(1);
        r.IsStutter[20].Should().BeTrue();
        r.IsStutter[30].Should().BeFalse();
        r.TimePct.Should().BeApproximately(21.0 / (38 * 10 + 21 + 20) * 100, 1e-9);
    }

    [Fact]
    public void AGapDoesNotAppearAsAStutter()
    {
        // Twenty-one 10 ms frames with one missing record in the middle. Counted as one 20 ms interval the
        // frame would sit at exactly 2× (not a stutter here, but a 30 ms hole would be); excluded, it is
        // not in the series at all, so nothing can be flagged on the reader's own accounting.
        List<FrameSample> stream = FromFrameTimes([.. Enumerable.Repeat(10.0, 10), 30, .. Enumerable.Repeat(10.0, 10)]);

        FrameTimeSeries withGap = FrameTimeSeries.From(stream, new HashSet<int> { 11 }, Frequency);
        FrameTimeSeries merged = FrameTimeSeries.From(stream, null, Frequency);

        StutterDetector.Detect(withGap.FrameTimesMs)!.Count.Should().Be(0, "the interval spanning the gap is excluded");
        StutterDetector.Detect(merged.FrameTimesMs)!.Count.Should().Be(1, "and this is the stutter the gap would have fabricated");
    }

    [Fact]
    public void FewerThanNineteenFramesIsNotApplicable()
    {
        StutterDetector.Detect([.. Enumerable.Repeat(10.0, 18)]).Should().BeNull();
    }

    [Fact]
    public void AnAllZeroSeriesHasNoStutterTime()
    {
        StutterResult? r = StutterDetector.Detect([.. Enumerable.Repeat(0.0, 20)]);

        r!.Count.Should().Be(0);
        r.TimePct.Should().Be(0);
    }

    [Fact]
    public void PsoStutterIsTheShareOfStutterFramesThatCompiledSomething()
    {
        List<FrameSample> stream = FromFrameTimes([.. Enumerable.Repeat(10.0, 10), 30, .. Enumerable.Repeat(10.0, 10), 30, .. Enumerable.Repeat(10.0, 10)]);
        for (int i = 0; i < stream.Count; i++)
        {
            stream[i] = stream[i] with { Measured = stream[i].Measured | MeasuredFields.Pso, PsoCreated = (ushort)(i == 11 ? 3 : 0) };
        }

        FrameTimeSeries series = FrameTimeSeries.From(stream, null, Frequency);
        StutterResult r = StutterDetector.Detect(series.FrameTimesMs)!;

        r.Count.Should().Be(2);
        StutterDetector.PsoStutterPct(r, series, stream).Should().Be(50.0, "one of the two stutter frames compiled a pipeline");
    }

    [Fact]
    public void PsoStutterIsNotApplicableWithoutStuttersOrWithoutThePsoHook()
    {
        List<FrameSample> quiet = FromFrameTimes(Enumerable.Repeat(10.0, 20));
        FrameTimeSeries quietSeries = FrameTimeSeries.From(quiet, null, Frequency);
        StutterDetector.PsoStutterPct(StutterDetector.Detect(quietSeries.FrameTimesMs)!, quietSeries, quiet)
            .Should().BeNull("nothing stuttered");

        List<FrameSample> unhooked = FromFrameTimes([.. Enumerable.Repeat(10.0, 10), 30, .. Enumerable.Repeat(10.0, 10)]);
        FrameTimeSeries unhookedSeries = FrameTimeSeries.From(unhooked, null, Frequency);
        StutterDetector.PsoStutterPct(StutterDetector.Detect(unhookedSeries.FrameTimesMs)!, unhookedSeries, unhooked)
            .Should().BeNull("no sample claims the PSO measurement, so 0% would be a negative nobody measured");
    }
}
