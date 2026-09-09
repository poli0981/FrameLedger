using FrameLedger.Application.Capture;
using FrameLedger.Application.Telemetry;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>Everything a hooked session drained, as the aggregator reads it — from a live outcome or a recovered <c>.partial</c> alike.</summary>
public sealed record AggregationInput
{
    public required IReadOnlyList<FlFrameRecord> Records { get; init; }

    /// <summary>Indices into <see cref="Records"/> whose record follows a gap.</summary>
    public required IReadOnlyList<int> GapBefore { get; init; }

    public required FlWriterState Writer { get; init; }

    public required long QpcFrequency { get; init; }

    public long TotalGaps { get; init; }

    public long TotalDropped { get; init; }

    public RuntimeModuleSet Modules { get; init; } = RuntimeModuleSet.Empty;

    public NgxDriverState Ngx { get; init; } = NgxDriverState.NotRun;

    public ExecutableMarkers Markers { get; init; } = ExecutableMarkers.NotScanned;

    public IReadOnlyList<TelemetrySample> Sensors { get; init; } = [];
}
