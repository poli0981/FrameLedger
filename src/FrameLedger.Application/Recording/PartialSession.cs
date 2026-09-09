using FrameLedger.Application.Telemetry;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>
/// What a <c>.partial</c> held when it was read back: its valid prefix, chunk by chunk, in order. A file
/// cut mid-chunk still yields everything before the cut (<see cref="Truncated"/> says so).
/// </summary>
public sealed record PartialSession
{
    public required PartialHeader Header { get; init; }

    public required IReadOnlyList<FlFrameRecord> Records { get; init; }

    /// <summary>Indices into <see cref="Records"/> whose record follows a gap.</summary>
    public required IReadOnlyList<int> GapBefore { get; init; }

    public required IReadOnlyList<TelemetrySample> Sensors { get; init; }

    public required IReadOnlyList<long> TouchQpc { get; init; }

    public required IReadOnlyList<PartialNote> Notes { get; init; }

    /// <summary>The last tick written, or null if the process died before the first flush.</summary>
    public required PartialTick? LastTick { get; init; }

    /// <summary>True when the file ended inside a chunk, or a chunk failed its check: the tail was lost.</summary>
    public required bool Truncated { get; init; }
}
