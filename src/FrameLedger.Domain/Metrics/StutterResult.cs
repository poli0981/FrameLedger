namespace FrameLedger.Domain.Metrics;

/// <summary>The stutter verdict over one frame-time series.</summary>
public sealed record StutterResult
{
    /// <summary>Per frame: <c>ft[i] &gt; 2 × rollingMedian(ft, 19)[i]</c>.</summary>
    public required IReadOnlyList<bool> IsStutter { get; init; }

    /// <summary>Frames flagged.</summary>
    public int Count => IsStutter.Count(static s => s);

    /// <summary><c>Σ ft[stutter] / Σ ft × 100</c>.</summary>
    public required double TimePct { get; init; }
}
