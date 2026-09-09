using FluentAssertions;
using FrameLedger.Domain.Metrics;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>
/// <c>03_METRICS</c>' percentile method, pinned: linear interpolation between closest ranks, which is NOT
/// nearest-rank — and the cases where the two differ are the ones that decide whether a user comparing
/// against another tool sees the same 1% Low.
/// </summary>
public sealed class PercentileTests
{
    [Fact]
    public void InterpolatesBetweenTheClosestRanksLikeNumPyLinear()
    {
        double[] s = [1, 2, 3, 4];

        // rank = 0.5 × 3 = 1.5 → halfway between s[1] and s[2].
        Percentile.Linear(s, 0.5).Should().Be(2.5);
        // rank = 0.99 × 3 = 2.97 → 3 + 0.97 × 1. Nearest rank would say 4.
        Percentile.Linear(s, 0.99).Should().BeApproximately(3.97, 1e-12);
        Percentile.Linear(s, 0).Should().Be(1);
        Percentile.Linear(s, 1).Should().Be(4);
    }

    [Fact]
    public void DiffersFromNearestRankWhereItMatters()
    {
        // Ten frame times with one outlier: nearest-rank p99 is the outlier itself; linear lands between
        // the two top values. Tools differ here and users will compare numbers (03_METRICS).
        double[] s = [10, 10, 10, 10, 10, 10, 10, 10, 10, 50];

        double? linear = Percentile.Linear(s, 0.99);

        linear.Should().BeApproximately(46.4, 1e-9, "rank 8.91 sits 91% of the way from 10 to 50");
        linear.Should().NotBe(50.0, "which is what nearest-rank would report");
    }

    [Fact]
    public void ASingleValueIsEveryPercentile()
    {
        Percentile.Linear([7.5], 0).Should().Be(7.5);
        Percentile.Linear([7.5], 0.999).Should().Be(7.5);
    }

    [Fact]
    public void AnEmptySeriesHasNoPercentile() => Percentile.Linear([], 0.5).Should().BeNull();

    [Fact]
    public void ThePercentileMustBeAFraction()
    {
        FluentActions.Invoking(() => Percentile.Linear([1], -0.1)).Should().Throw<ArgumentOutOfRangeException>();
        FluentActions.Invoking(() => Percentile.Linear([1], 1.1)).Should().Throw<ArgumentOutOfRangeException>();
    }

    [Fact]
    public void SortedIsACopy()
    {
        double[] original = [3, 1, 2];

        IReadOnlyList<double> sorted = Percentile.Sorted(original);

        sorted.Should().Equal(1, 2, 3);
        original.Should().Equal([3, 1, 2], "the caller's series is not reordered underneath it");
    }

    [Fact]
    public void TheSufficiencyGuardsAreTheDocumentedNumbers()
    {
        Percentile.P1SufficientFrames.Should().Be(1_000);
        Percentile.P01SufficientFrames.Should().Be(10_000);
    }
}
