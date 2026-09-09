using FluentAssertions;
using FrameLedger.Domain.Metrics;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>Both edges pinned, as <c>03_METRICS</c> asks: truncated symmetric, not padded, not shifted.</summary>
public sealed class RollingMedianTests
{
    [Fact]
    public void TheEdgesUseTheLargestCenteredOddWindowThatFits()
    {
        // 19 frames, window 19: the middle frame sees all 19; frame 0 sees only itself; frame 1 sees
        // frames 0..2; frame 2 sees 0..4 — and symmetrically at the end.
        double[] series = [.. Enumerable.Range(0, 19).Select(i => (double)(i * 10))];

        IReadOnlyList<double>? m = RollingMedian.Of(series);

        m.Should().NotBeNull();
        m![0].Should().Be(0, "a window of one is the value itself");
        m[1].Should().Be(10, "frames 0..2");
        m[2].Should().Be(20, "frames 0..4");
        m[9].Should().Be(90, "the full 19-frame window, centered");
        m[17].Should().Be(170, "frames 16..18");
        m[18].Should().Be(180);
    }

    [Fact]
    public void ATruncatedWindowIsNotPaddedWithTheEdgeValue()
    {
        // Padded with the first value, frame 1's median over [0,0,0,...,10,20] would be 0. Symmetric
        // truncation gives the median of [0,10,20] = 10.
        double[] series = [0, 10, 20, .. Enumerable.Repeat(1000.0, 30)];

        RollingMedian.Of(series)![1].Should().Be(10);
    }

    [Fact]
    public void AnOutlierInsideTheWindowDoesNotMoveTheMedian()
    {
        double[] series = [.. Enumerable.Repeat(8.0, 25)];
        series[12] = 200;

        IReadOnlyList<double>? m = RollingMedian.Of(series);

        m![12].Should().Be(8, "one spike among nineteen leaves the median where it was");
    }

    [Fact]
    public void ShorterThanTheWindowIsNull()
    {
        RollingMedian.Of([.. Enumerable.Repeat(1.0, 18)]).Should().BeNull();
        RollingMedian.Of([.. Enumerable.Repeat(1.0, 19)]).Should().HaveCount(19);
    }

    [Fact]
    public void TheWindowMustBeOddAndPositive()
    {
        FluentActions.Invoking(() => RollingMedian.Of([1, 2, 3], window: 2)).Should().Throw<ArgumentOutOfRangeException>();
        FluentActions.Invoking(() => RollingMedian.Of([1, 2, 3], window: 0)).Should().Throw<ArgumentOutOfRangeException>();
        RollingMedian.Of([3, 1, 2], window: 3).Should().Equal(3, 2, 2);
    }
}
