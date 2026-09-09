using FluentAssertions;
using FrameLedger.Domain.Metrics;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>
/// <c>03_METRICS</c> §Core definitions as goldens: the lows are <c>1000 / p</c> by linear interpolation, the
/// sufficiency guards sit at exactly 1,000 and 10,000, and an empty series is N/A everywhere.
/// </summary>
public sealed class FrameStatisticsTests
{
    private static List<double> Ramp(int n) => [.. Enumerable.Range(1, n).Select(i => (double)i)];

    [Fact]
    public void MedianMinMaxAndSigmaOverASmallSeries()
    {
        FrameStatistics s = FrameStatistics.From([10, 20, 30, 40], presented: false);

        s.Count.Should().Be(4);
        s.Presented.Should().BeFalse();
        s.MedianFps.Should().Be(1000.0 / 25);
        s.MinFps.Should().Be(1000.0 / 40, "min FPS is the SLOWEST frame");
        s.MaxFps.Should().Be(1000.0 / 10, "max FPS is the fastest frame");
        s.MeanMs.Should().Be(25);
        s.StdDevMs.Should().BeApproximately(Math.Sqrt(125), 1e-9, "population σ, not sample σ");
        s.P1LowFps.Should().BeNull("4 frames is under the 1,000 the 1% Low needs");
        s.P01LowFps.Should().BeNull();
    }

    [Fact]
    public void TheOnePercentLowNeedsExactlyOneThousandFrames()
    {
        FrameStatistics.From(Ramp(999), presented: false).P1LowFps.Should().BeNull();

        FrameStatistics at = FrameStatistics.From(Ramp(1000), presented: false);
        at.P1LowFps.Should().Be(1000.0 / (1 + (0.99 * 999)), "rank 0.99 × 999 = 989.01, interpolated");
        at.P01LowFps.Should().BeNull();
    }

    [Fact]
    public void ThePointOnePercentLowNeedsExactlyTenThousandFrames()
    {
        FrameStatistics.From(Ramp(9_999), presented: false).P01LowFps.Should().BeNull();

        FrameStatistics at = FrameStatistics.From(Ramp(10_000), presented: false);
        at.P01LowFps.Should().Be(1000.0 / (1 + (0.999 * 9_999)));
        at.P1LowFps.Should().NotBeNull();
    }

    [Fact]
    public void ThePresentedLabelIsCarriedNotDecided()
    {
        // When frame generation is not measured the lows are over presents and the report prints
        // "(presented)"; the statistics carry the flag and never infer it.
        FrameStatistics.From([10, 10], presented: true).Presented.Should().BeTrue();
        FrameStatistics.Empty(presented: true).Presented.Should().BeTrue();
    }

    [Fact]
    public void AnEmptySeriesIsNotApplicableEverywhereAndNeverZero()
    {
        FrameStatistics s = FrameStatistics.From([], presented: false);

        s.Count.Should().Be(0);
        s.MedianFps.Should().BeNull();
        s.MinFps.Should().BeNull();
        s.MaxFps.Should().BeNull();
        s.MeanMs.Should().BeNull();
        s.StdDevMs.Should().BeNull();
    }

    [Fact]
    public void AZeroFrameTimeIsNotARate()
    {
        FrameStatistics s = FrameStatistics.From([0, 10], presented: false);

        s.MaxFps.Should().BeNull("1000 / 0 is not a frame rate");
        s.MinFps.Should().Be(100);
    }
}
