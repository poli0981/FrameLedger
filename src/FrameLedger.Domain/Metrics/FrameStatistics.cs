namespace FrameLedger.Domain.Metrics;

/// <summary>
/// <c>03_METRICS</c> §Core definitions over one frame-time series: the medians and lows as <c>1000 / p</c>,
/// min and max, the population σ — each null where the definition has nothing to stand on.
/// </summary>
/// <remarks>
/// <para>
/// <b>The lows are application frames only, and a present-only series is LABELLED.</b> Generated frames
/// smooth display cadence but do not represent simulation stalls; mixing them hides real stutter. When
/// frame generation is not measured the series IS the presents, and <see cref="Presented"/> carries the
/// label the report must print — an unlabelled low is a claim about application frames a present-only
/// writer cannot make.
/// </para>
/// <para>
/// <b>Sufficiency guards (FR-4.8):</b> 1% Low needs ≥ 1,000 frames, 0.1% Low ≥ 10,000; otherwise null,
/// never an estimate.
/// </para>
/// </remarks>
public sealed record FrameStatistics
{
    /// <summary>Frames the statistics are over.</summary>
    public required int Count { get; init; }

    /// <summary>True when the series is presents rather than application frames — print the <c>(presented)</c> label.</summary>
    public required bool Presented { get; init; }

    /// <summary><c>1000 / p50(ft)</c>.</summary>
    public required double? MedianFps { get; init; }

    /// <summary><c>1000 / p99(ft)</c>; null under 1,000 frames.</summary>
    public required double? P1LowFps { get; init; }

    /// <summary><c>1000 / p99.9(ft)</c>; null under 10,000 frames.</summary>
    public required double? P01LowFps { get; init; }

    /// <summary><c>1000 / max(ft)</c>.</summary>
    public required double? MinFps { get; init; }

    /// <summary><c>1000 / min(ft)</c>.</summary>
    public required double? MaxFps { get; init; }

    /// <summary>Population standard deviation of the frame times, ms.</summary>
    public required double? StdDevMs { get; init; }

    /// <summary>Mean frame time, ms — the reciprocal of the time-based average.</summary>
    public required double? MeanMs { get; init; }

    /// <summary>The statistics of a series with nothing in it: every value null.</summary>
    public static FrameStatistics Empty(bool presented) => new()
    {
        Count = 0,
        Presented = presented,
        MedianFps = null,
        P1LowFps = null,
        P01LowFps = null,
        MinFps = null,
        MaxFps = null,
        StdDevMs = null,
        MeanMs = null,
    };

    public static FrameStatistics From(IReadOnlyList<double> frameTimesMs, bool presented)
    {
        ArgumentNullException.ThrowIfNull(frameTimesMs);

        int n = frameTimesMs.Count;
        if (n == 0)
        {
            return Empty(presented);
        }

        IReadOnlyList<double> sorted = Percentile.Sorted(frameTimesMs);
        double mean = frameTimesMs.Average();
        double variance = frameTimesMs.Sum(t => (t - mean) * (t - mean)) / n;

        return new FrameStatistics
        {
            Count = n,
            Presented = presented,
            MedianFps = Fps(Percentile.Linear(sorted, 0.5)),
            P1LowFps = n >= Percentile.P1SufficientFrames ? Fps(Percentile.Linear(sorted, 0.99)) : null,
            P01LowFps = n >= Percentile.P01SufficientFrames ? Fps(Percentile.Linear(sorted, 0.999)) : null,
            MinFps = Fps(sorted[^1]),
            MaxFps = Fps(sorted[0]),
            StdDevMs = Math.Sqrt(variance),
            MeanMs = mean,
        };
    }

    /// <summary><c>1000 / ms</c>; null for a non-positive frame time, which is not a rate.</summary>
    private static double? Fps(double? ms) => ms is > 0 ? 1000.0 / ms.Value : null;
}
