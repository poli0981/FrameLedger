using FrameLedger.Domain.Sessions;

namespace FrameLedger.Application.Persistence;

/// <summary>
/// One <c>sessions</c> row, column for column. Every measured value is nullable and null means
/// <c>N/A</c> — a Tier-2 session carries the header, the tier, the reason and nothing else.
/// </summary>
/// <remarks>
/// A DTO, not a calculator: the numbers come from <c>Domain.Metrics</c> and the recorder (PR-D) puts
/// them here. Tri-states are stored as their <c>06_DATA_MODEL</c> text (<c>na</c> / <c>no</c> / <c>yes</c>)
/// with their source beside them.
/// </remarks>
public sealed record SessionRow
{
    public long Id { get; init; }

    public required Guid SessionGuid { get; init; }

    public required long GameId { get; init; }

    public required long SnapshotId { get; init; }

    public required DateTimeOffset StartedAt { get; init; }

    public required DateTimeOffset EndedAt { get; init; }

    public double DurationSeconds => (EndedAt - StartedAt).TotalSeconds;

    public required ulong QpcEpoch { get; init; }

    public required long QpcFrequency { get; init; }

    public required CaptureTier Tier { get; init; }

    public required CaptureMode Mode { get; init; }

    public string? CaptureNotes { get; init; }

    public bool LateAttach { get; init; }

    public string? TelemetrySource { get; init; }

    public string? OverlayBuildId { get; init; }

    public required ExitStatus ExitStatus { get; init; }

    // the drain's own accounting
    public long? DrainTicks { get; init; }

    public long? ForegroundTicks { get; init; }

    public long? RecordsBeforeAttach { get; init; }

    public long? DxgiPresentsBeforeHook { get; init; }

    public long GapCount { get; init; }

    public long? GuardTicksPublished { get; init; }

    public long? LaunchWaitMs { get; init; }

    // presentation
    public string? Api { get; init; }

    public string? PresentMode { get; init; }

    public string? SwapEffect { get; init; }

    public string HdrFlag { get; init; } = "na";

    public string? HdrSource { get; init; }

    public string? SyncIntervalMode { get; init; }

    // upscaling / FG
    public string? Upscaler { get; init; }

    public string? UpscalerQuality { get; init; }

    public int? UpscalerSharpness { get; init; }

    public string? UpscalerDriverReported { get; init; }

    public int? RenderW { get; init; }

    public int? RenderH { get; init; }

    public int? OutputW { get; init; }

    public int? OutputH { get; init; }

    public double? UpscaleRatio { get; init; }

    public bool SettingsChangedMidSession { get; init; }

    public string FgMode { get; init; } = "na";

    public string? FgSource { get; init; }

    public double? FgFactor { get; init; }

    public string? FgDriverReported { get; init; }

    public long? FgRuntimeCensus { get; init; }

    public string? FgNoneWithheldReason { get; init; }

    public double? PresentedFps { get; init; }

    public string? PresentedQualifier { get; init; }

    public long? DxgiUnseenTotal { get; init; }

    public long? DxgiPresentSamples { get; init; }

    public string? DisplayedCountedBy { get; init; }

    public long? SlTagCensus { get; init; }

    public string? SlInterposerVersion { get; init; }

    public string? RuntimeModulesJson { get; init; }

    public string? ExecutableMarkersJson { get; init; }

    public string? NgxDriverWordsJson { get; init; }

    // ray tracing
    public string RtFlag { get; init; } = "na";

    public string? RtSource { get; init; }

    public string PtFlag { get; init; } = "na";

    public string? PtSource { get; init; }

    public double? PtConfidence { get; init; }

    public string RrFlag { get; init; } = "na";

    public string? RrSource { get; init; }

    public double? RtFramePct { get; init; }

    public double? RaysPerPixel { get; init; }

    public long? RtPsoCount { get; init; }

    public long? RtTier { get; init; }

    public long? HooksInstalledMask { get; init; }

    public long? RasterPsoCount { get; init; }

    // frame statistics
    public long FrameCount { get; init; }

    public long AppFrameCount { get; init; }

    public long DisplayedFrameCount { get; init; }

    public long DroppedFrames { get; init; }

    public double? NativeFps { get; init; }

    public double? DisplayedFps { get; init; }

    public double? MedianFps { get; init; }

    public double? P1LowFps { get; init; }

    public double? P01LowFps { get; init; }

    public double? DisplayedP1LowFps { get; init; }

    public double? MinFps { get; init; }

    public double? MaxFps { get; init; }

    public double? FrametimeStdDevMs { get; init; }

    public long? StutterCount { get; init; }

    public double? StutterTimePct { get; init; }

    public double? PsoStutterPct { get; init; }

    public bool? ReflexActive { get; init; }

    public long? LatencyAvgUs { get; init; }

    public long? LatencyP95Us { get; init; }

    // data quality
    public long DroppedRecords { get; init; }

    public long FaultCount { get; init; }

    public long DataQualityWarnings { get; init; }

    public long? WriterStatusAtEnd { get; init; }

    public long? EarlyStopFamily { get; init; }

    public long? LoaderSignals { get; init; }

    // memory & sensors
    public double? VramProcAvgMb { get; init; }

    public double? VramProcMaxMb { get; init; }

    public double? VramBudgetExceededPct { get; init; }

    public double? VramAdapterMaxMb { get; init; }

    public double? AvgCpuTemp { get; init; }

    public double? MaxCpuTemp { get; init; }

    public double? AvgGpuTemp { get; init; }

    public double? MaxGpuTemp { get; init; }

    public double? MaxGpuHotspot { get; init; }

    public double? AvgGpuLoad { get; init; }

    public double? AvgCpuLoad { get; init; }

    public double? AvgRamMb { get; init; }

    public double? AvgGpuPowerW { get; init; }

    public double? ThrottlePct { get; init; }
}
