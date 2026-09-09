using FrameLedger.Application.Recording;
using FrameLedger.Application.Telemetry;
using FrameLedger.Shared;

namespace FrameLedger.Application.Tests.Recording;

/// <summary>An in-memory <see cref="IPartialSessionStore"/>: what a writer appended is what a reader gets back.</summary>
internal sealed class FakePartialSessionStore : IPartialSessionStore
{
    public Dictionary<Guid, Entry> Files { get; } = [];

    /// <summary>Every entry ever created, kept after deletion so a test can read what was written.</summary>
    public Dictionary<Guid, Entry> Created { get; } = [];

    public List<Guid> Deleted { get; } = [];

    public IPartialSessionWriter Create(PartialHeader header)
    {
        var entry = new Entry(header);
        Files[header.SessionGuid] = entry;
        Created[header.SessionGuid] = entry;
        return entry;
    }

    public IReadOnlyList<Guid> ListPending() => [.. Files.Keys];

    public PartialSession? Read(Guid sessionGuid) => Files.TryGetValue(sessionGuid, out Entry? e) ? e.ToSession() : null;

    public void Delete(Guid sessionGuid)
    {
        Deleted.Add(sessionGuid);
        Files.Remove(sessionGuid);
    }

    internal sealed class Entry(PartialHeader header) : IPartialSessionWriter
    {
        public PartialHeader Header { get; } = header;

        public List<FlFrameRecord> Records { get; } = [];

        public List<int> Gaps { get; } = [];

        public List<TelemetrySample> Sensors { get; } = [];

        public List<long> Touches { get; } = [];

        public List<PartialNote> Notes { get; } = [];

        public PartialTick? LastTick { get; private set; }

        public int Ticks { get; private set; }

        public bool Disposed { get; private set; }

        public bool Truncated { get; set; }

        public void AppendRecords(long firstOrdinal, ReadOnlySpan<FlFrameRecord> records)
        {
            if (firstOrdinal != Records.Count)
            {
                throw new InvalidOperationException($"records chunk at {firstOrdinal} does not continue {Records.Count}");
            }

            Records.AddRange(records.ToArray());
        }

        public void AppendGaps(ReadOnlySpan<int> gapBefore) => Gaps.AddRange(gapBefore.ToArray());

        public void AppendSensors(ReadOnlySpan<TelemetrySample> samples) => Sensors.AddRange(samples.ToArray());

        public void AppendTick(PartialTick tick)
        {
            LastTick = tick;
            Ticks++;
        }

        public void AppendTouches(ReadOnlySpan<long> touchQpc) => Touches.AddRange(touchQpc.ToArray());

        public void AppendNote(DateTimeOffset at, string text) => Notes.Add(new PartialNote(at, text));

        public void Dispose() => Disposed = true;

        public PartialSession ToSession() => new()
        {
            Header = Header,
            Records = Records,
            GapBefore = Gaps,
            Sensors = Sensors,
            TouchQpc = Touches,
            Notes = Notes,
            LastTick = LastTick,
            Truncated = Truncated,
        };
    }
}
