using FrameLedger.Infrastructure.Ipc;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>The real ring, behind <see cref="ICaptureSink"/>.</summary>
internal sealed class ShmCaptureSink(ShmRingReader reader) : ICaptureSink
{
    private readonly ShmRingReader _reader = reader ?? throw new ArgumentNullException(nameof(reader));

    public FlWriterState WriterState => _reader.WriterState;

    public FlShmHandshake Handshake => _reader.Handshake;

    public long TotalDropped => _reader.TotalDropped;

    public long TotalGaps => _reader.TotalGaps;

    public DrainResult Drain(Span<FlFrameRecord> into, IList<ulong> gapIndices) => _reader.Drain(into, gapIndices);

    public void PublishGuardResult(uint completedEvaluations, bool unhookRequested) =>
        _reader.PublishGuardResult(completedEvaluations, unhookRequested);

    public void SetPaused(bool paused) => _reader.SetPaused(paused);

    public void Dispose() => _reader.Dispose();
}
