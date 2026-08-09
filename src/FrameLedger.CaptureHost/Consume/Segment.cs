using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>One contiguous run of presents with one set of settings, on one stream.</summary>
internal sealed record Segment
{
    public required uint SwapchainId { get; init; }

    public required ushort OutputW { get; init; }

    public required ushort OutputH { get; init; }

    public required IReadOnlyList<FlFrameRecord> Records { get; init; }
}
