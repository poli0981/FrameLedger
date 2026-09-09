namespace FrameLedger.Application.Persistence;

/// <summary>Everything one finalized session writes, in one transaction.</summary>
public sealed record FinalizedSession
{
    public required SessionRow Row { get; init; }

    public IReadOnlyList<SegmentRow> Segments { get; init; } = [];

    public FrameBlobs? Frames { get; init; }

    public IReadOnlyList<SensorBlob> Sensors { get; init; } = [];
}
