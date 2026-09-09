using FrameLedger.Application.Telemetry;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>
/// Appends chunks to one session's <c>.partial</c>. Every method flushes the file before returning: the
/// threat is the Agent dying, not the machine losing power, and a chunk that reached the OS survives the
/// former. A kill at any byte loses at most the chunk being written (<c>PartialSessionFileTests</c>).
/// </summary>
public interface IPartialSessionWriter : IDisposable
{
    void AppendRecords(long firstOrdinal, ReadOnlySpan<FlFrameRecord> records);

    void AppendGaps(ReadOnlySpan<int> gapBefore);

    void AppendSensors(ReadOnlySpan<TelemetrySample> samples);

    void AppendTick(PartialTick tick);

    void AppendTouches(ReadOnlySpan<long> touchQpc);

    void AppendNote(DateTimeOffset at, string text);
}
