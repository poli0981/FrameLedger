using FrameLedger.Shared;

namespace FrameLedger.Application.Capture;

/// <summary>
/// What <see cref="CaptureSession"/> needs from the ring, so the loop's ordering rules
/// can be driven without a live game.
/// </summary>
/// <remarks>
/// <b>The seam is this port, and the reader gains nothing.</b> <c>ShmRingReader</c> has
/// no interface, no virtual member and no test hook: the shipped reader stays a
/// sealed class over a mapped view, and <c>Infrastructure.Capture.ShmCaptureSink</c> is
/// the only adapter (<c>NoSecondRingReaderTests</c> pins that nothing else opens the ring). A seam in shipped code purely so a test can reach it is what
/// <c>chokepoint-check</c>'s <c>FL_GUARD_TESTABLE</c> rule exists to prevent on the
/// native side, and the same argument applies here.
/// </remarks>
public interface ICaptureSink : IDisposable
{
    FlWriterState WriterState { get; }

    FlShmHandshake Handshake { get; }

    long TotalDropped { get; }

    long TotalGaps { get; }

    DrainResult Drain(Span<FlFrameRecord> into, IList<ulong> gapIndices);

    void PublishGuardResult(uint completedEvaluations, bool unhookRequested);

    void SetPaused(bool paused);

    /// <summary>Ask the capture side to write its native log now (session end).</summary>
    void RequestLogFlush();
}
