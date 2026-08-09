using FrameLedger.Infrastructure.Ipc;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// What <see cref="CaptureLoop"/> needs from the ring, so the loop's ordering rules
/// can be driven without a live game.
/// </summary>
/// <remarks>
/// <b>The seam lives entirely in the unshipped host.</b> <c>ShmRingReader</c> gains
/// no interface, no virtual member and no test hook: the shipped reader stays a
/// sealed class over a mapped view, and <see cref="ShmCaptureSink"/> is the only
/// adapter. A seam in shipped code purely so a test can reach it is what
/// <c>chokepoint-check</c>'s <c>FL_GUARD_TESTABLE</c> rule exists to prevent on the
/// native side, and the same argument applies here.
/// </remarks>
internal interface ICaptureSink : IDisposable
{
    FlWriterState WriterState { get; }

    FlShmHandshake Handshake { get; }

    long TotalDropped { get; }

    long TotalGaps { get; }

    DrainResult Drain(Span<FlFrameRecord> into, IList<ulong> gapIndices);

    void PublishGuardResult(uint completedEvaluations, bool unhookRequested);

    void SetPaused(bool paused);
}
