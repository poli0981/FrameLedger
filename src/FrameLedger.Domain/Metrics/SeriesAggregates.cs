namespace FrameLedger.Domain.Metrics;

/// <summary>
/// <c>03_METRICS</c> §Sensor aggregates: the mean of the non-null samples and their maximum, and
/// <b>N/A — never 0 — when there are none</b>.
/// </summary>
public sealed record SeriesAggregates
{
    public required double? Average { get; init; }

    public required double? Max { get; init; }

    /// <summary>Samples that carried a value.</summary>
    public required int Count { get; init; }

    public static SeriesAggregates Of(IEnumerable<double?> samples)
    {
        ArgumentNullException.ThrowIfNull(samples);

        double sum = 0;
        double max = double.NegativeInfinity;
        int count = 0;
        foreach (double? s in samples)
        {
            if (s is not double v)
            {
                continue;
            }

            count++;
            sum += v;
            max = Math.Max(max, v);
        }

        return new SeriesAggregates
        {
            Average = count > 0 ? sum / count : null,
            Max = count > 0 ? max : null,
            Count = count,
        };
    }
}
