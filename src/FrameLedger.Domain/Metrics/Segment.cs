namespace FrameLedger.Domain.Metrics;

/// <summary>One contiguous run of presents with one output size, on one stream.</summary>
public sealed record Segment
{
    public required uint SwapchainId { get; init; }

    public required ushort OutputW { get; init; }

    public required ushort OutputH { get; init; }

    public required IReadOnlyList<FrameSample> Samples { get; init; }
}
