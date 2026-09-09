namespace FrameLedger.Domain.Metrics;

/// <summary>
/// <c>03_METRICS</c> §Core definitions' percentile method: sort ascending, linear interpolation between the
/// closest ranks (NumPy <c>linear</c>, Excel <c>PERCENTILE</c>). Documented and tested because tools differ
/// and users will compare numbers.
/// </summary>
public static class Percentile
{
    /// <summary>Frames the 1% Low needs before it is published (FR-4.8).</summary>
    public const int P1SufficientFrames = 1_000;

    /// <summary>Frames the 0.1% Low needs before it is published (FR-4.8).</summary>
    public const int P01SufficientFrames = 10_000;

    /// <summary>
    /// The <paramref name="p"/>-th percentile (0 … 1) of an ASCENDING series, by linear interpolation between
    /// closest ranks: rank <c>p × (n − 1)</c>, the value between its floor and ceiling elements.
    /// </summary>
    /// <returns>Null on an empty series.</returns>
    public static double? Linear(IReadOnlyList<double> sortedAscending, double p)
    {
        ArgumentNullException.ThrowIfNull(sortedAscending);
        ArgumentOutOfRangeException.ThrowIfLessThan(p, 0.0);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(p, 1.0);

        int n = sortedAscending.Count;
        if (n == 0)
        {
            return null;
        }

        double rank = p * (n - 1);
        int lo = (int)Math.Floor(rank);
        int hi = Math.Min(lo + 1, n - 1);
        double frac = rank - lo;
        return sortedAscending[lo] + (frac * (sortedAscending[hi] - sortedAscending[lo]));
    }

    /// <summary>A sorted copy of <paramref name="series"/>.</summary>
    public static IReadOnlyList<double> Sorted(IReadOnlyList<double> series)
    {
        ArgumentNullException.ThrowIfNull(series);

        double[] copy = [.. series];
        Array.Sort(copy);
        return copy;
    }
}
