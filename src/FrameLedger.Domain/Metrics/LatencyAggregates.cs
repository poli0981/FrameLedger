namespace FrameLedger.Domain.Metrics;

/// <summary>
/// <c>03_METRICS</c> §Core definitions, PC latency: mean and p95 of <c>reflexLatencyUs</c> over the samples that
/// claim <see cref="MeasuredFields.Latency"/> and carry a value (0 = unavailable). Tier 1 + Reflex only.
/// </summary>
public sealed record LatencyAggregates
{
    public required double? AverageUs { get; init; }

    public required double? P95Us { get; init; }

    /// <summary>Samples with a latency reading.</summary>
    public required int Count { get; init; }

    public static LatencyAggregates From(IReadOnlyList<FrameSample> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        List<double> values = [];
        foreach (FrameSample s in stream)
        {
            if (s.Claims(MeasuredFields.Latency) && s.ReflexLatencyUs > 0)
            {
                values.Add(s.ReflexLatencyUs);
            }
        }

        if (values.Count == 0)
        {
            return new LatencyAggregates { AverageUs = null, P95Us = null, Count = 0 };
        }

        return new LatencyAggregates
        {
            AverageUs = values.Average(),
            P95Us = Percentile.Linear(Percentile.Sorted(values), 0.95),
            Count = values.Count,
        };
    }
}
