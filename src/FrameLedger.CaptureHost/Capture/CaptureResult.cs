using FrameLedger.Domain.AntiCheat;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>What one session did.</summary>
internal sealed record CaptureResult
{
    public required SessionEndReason Reason { get; init; }

    public AntiCheatVerdict Verdict { get; init; }

    public ShmAttachRefusal AttachRefusal { get; init; }

    public IReadOnlyList<FlFrameRecord> Records { get; init; } = [];

    public FlWriterState WriterState { get; init; }

    public FlShmHandshake Handshake { get; init; }

    /// <summary>
    /// The supervisor's completed-evaluation count, as published to
    /// <c>FlControlBlock.guardTicks</c>. Never a count this loop kept of its own.
    /// </summary>
    public uint GuardTicksPublished { get; init; }

    public long TotalDropped { get; init; }

    public long TotalGaps { get; init; }
}
