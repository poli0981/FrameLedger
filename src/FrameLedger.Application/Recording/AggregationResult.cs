using FrameLedger.Application.Persistence;
using FrameLedger.Domain.Metrics;

namespace FrameLedger.Application.Recording;

/// <summary>The row with every measured column filled, the segments beside it, and the series the blobs are cut from.</summary>
public sealed record AggregationResult
{
    public required SessionRow Row { get; init; }

    public required IReadOnlyList<SegmentRow> Segments { get; init; }

    /// <summary>Every record, mapped, in drained order — what the blobs encode.</summary>
    public required IReadOnlyList<FrameSample> Samples { get; init; }

    /// <summary>The dominant stream — what the statistics were computed over.</summary>
    public required IReadOnlyList<FrameSample> Dominant { get; init; }

    public required FgVerdict FgVerdict { get; init; }

    /// <summary>The frame-time series behind the statistics; null on an empty session.</summary>
    public FrameTimeSeries? Series { get; init; }
}
