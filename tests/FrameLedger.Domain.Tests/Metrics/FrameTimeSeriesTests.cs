using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>Frame times from QPC, with the intervals that are not frame times left out rather than invented.</summary>
public sealed class FrameTimeSeriesTests
{
    [Fact]
    public void FrameTimesAreTheQpcDeltasInMilliseconds()
    {
        List<FrameSample> stream = FromFrameTimes([10, 20, 5]);

        FrameTimeSeries s = FrameTimeSeries.From(stream, null, Frequency);

        s.FrameTimesMs.Should().Equal(10, 20, 5);
        s.EndingSample.Should().Equal(1, 2, 3);
        s.Count.Should().Be(3);
        s.Presents.Should().Be(4);
        s.DurationSeconds.Should().BeApproximately(0.035, 1e-9);
        s.ExcludedForGaps.Should().Be(0);
        s.ExcludedNonPositive.Should().Be(0);
    }

    [Fact]
    public void TheAverageIsTimeBasedAndNotTheMeanOfInstantaneousFps()
    {
        // 03_METRICS: "Time-based, NOT the mean of instantaneous FPS values". One 100 ms frame and nine
        // 10 ms frames: mean-of-FPS says (9×100 + 10) / 10 = 91; presents/D says 10 / 0.19 = 52.6.
        List<FrameSample> stream = FromFrameTimes([100, 10, 10, 10, 10, 10, 10, 10, 10, 10]);

        FrameTimeSeries s = FrameTimeSeries.From(stream, null, Frequency);

        s.AverageFps.Should().BeApproximately(10 / 0.19, 0.01);
        s.AverageFps.Should().BeLessThan(91, "a slow frame costs its share of the wall clock, not its share of the count");
    }

    [Fact]
    public void AGapExcludesTheIntervalThatSpansItInsteadOfCountingALongFrame()
    {
        // A torn or overwritten slot between sample 1 and sample 2: the reader knows a record is missing, so
        // the 30 ms between the two survivors is two unknown frames, not one long one.
        List<FrameSample> stream = FromFrameTimes([10, 30, 10]);

        FrameTimeSeries s = FrameTimeSeries.From(stream, new HashSet<int> { 2 }, Frequency);

        s.FrameTimesMs.Should().Equal(10, 10);
        s.EndingSample.Should().Equal(1, 3);
        s.ExcludedForGaps.Should().Be(1);
        s.DurationSeconds.Should().BeApproximately(0.05, 1e-9, "the session's wall clock still spans the gap");
    }

    [Fact]
    public void AnIntervalTheClockDidNotAdvanceAcrossIsNotAFrameTime()
    {
        List<FrameSample> stream = FromFrameTimes([10, 10]);
        stream.Add(Present(stream[0].Qpc));    // a lapped drain returned an older record last

        FrameTimeSeries s = FrameTimeSeries.From(stream, null, Frequency);

        s.FrameTimesMs.Should().Equal(10, 10);
        s.ExcludedNonPositive.Should().Be(1);
        s.DurationSeconds.Should().Be(0, "first to last present is negative here, and a negative duration is clamped rather than signed");
    }

    [Fact]
    public void FewerThanTwoPresentsHaveNoIntervalAndNoAverage()
    {
        FrameTimeSeries one = FrameTimeSeries.From([Present(5)], null, Frequency);
        FrameTimeSeries none = FrameTimeSeries.From([], null, Frequency);

        one.Count.Should().Be(0);
        one.AverageFps.Should().BeNull();
        one.DurationSeconds.Should().Be(0);
        none.Presents.Should().Be(0);
        none.AverageFps.Should().BeNull();
    }

    [Fact]
    public void AZeroFrequencyIsRefused() =>
        FluentActions.Invoking(() => FrameTimeSeries.From(Stream(2), null, 0)).Should().Throw<ArgumentOutOfRangeException>();
}
