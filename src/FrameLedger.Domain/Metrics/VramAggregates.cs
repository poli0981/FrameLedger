namespace FrameLedger.Domain.Metrics;

/// <summary>
/// <c>03_METRICS</c> §Per-process VRAM: average and maximum of the 1 Hz held sample, and the share of samples
/// over the adapter budget — the same MiB divisor on both sides, so the comparison carries no bias.
/// </summary>
public sealed record VramAggregates
{
    public required double? AverageMb { get; init; }

    public required double? MaxMb { get; init; }

    /// <summary>Null when the writer published no budget (0 means "nobody wrote it").</summary>
    public required double? BudgetExceededPct { get; init; }

    /// <summary>Samples that claimed <see cref="MeasuredFields.Vram"/>.</summary>
    public required int Count { get; init; }

    public static VramAggregates From(IReadOnlyList<FrameSample> stream, uint? budgetMb)
    {
        ArgumentNullException.ThrowIfNull(stream);

        long sum = 0;
        uint max = 0;
        int count = 0;
        int over = 0;
        foreach (FrameSample s in stream)
        {
            if (!s.Claims(MeasuredFields.Vram))
            {
                continue;
            }

            count++;
            sum += s.VramUsedMb;
            max = Math.Max(max, s.VramUsedMb);
            if (budgetMb is uint budget && s.VramUsedMb > budget)
            {
                over++;
            }
        }

        return new VramAggregates
        {
            AverageMb = count > 0 ? sum / (double)count : null,
            MaxMb = count > 0 ? max : null,
            BudgetExceededPct = count > 0 && budgetMb is not null ? over * 100.0 / count : null,
            Count = count,
        };
    }
}
