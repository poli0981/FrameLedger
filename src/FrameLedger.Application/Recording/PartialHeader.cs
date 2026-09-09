using FrameLedger.Domain.Sessions;

namespace FrameLedger.Application.Recording;

/// <summary>
/// The first chunk of a <c>.partial</c>: everything recovery needs to finalize the session without the
/// process that started it — identity, time base, which game and which snapshot, how it was captured.
/// </summary>
public sealed record PartialHeader
{
    /// <summary>Bumped when a chunk's layout changes; a reader refuses a version it does not know.</summary>
    public const int CurrentFormatVersion = 1;

    public int FormatVersion { get; init; } = CurrentFormatVersion;

    public required Guid SessionGuid { get; init; }

    public required DateTimeOffset StartedAt { get; init; }

    public required ulong QpcEpoch { get; init; }

    public required long QpcFrequency { get; init; }

    public required long GameId { get; init; }

    public required long SnapshotId { get; init; }

    public required string ExePath { get; init; }

    public int? Pid { get; init; }

    public required CaptureTier Tier { get; init; }

    public required CaptureMode Mode { get; init; }

    public string? OverlayBuildId { get; init; }

    public string? TelemetryDescriptor { get; init; }

    public long? LaunchWaitMs { get; init; }
}
