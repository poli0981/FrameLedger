namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>How <see cref="TelemetryPoller"/> runs.</summary>
public sealed record TelemetryPollerOptions
{
    /// <summary>
    /// The floor is L2's (<see cref="LhmTelemetryOptions.MinimumInterval"/>): the poller reads
    /// what the layers publish and nothing is gained by asking faster than the slowest one answers.
    /// </summary>
    public static readonly TimeSpan MinimumInterval = LhmTelemetryOptions.MinimumInterval;

    /// <summary>Default 1000 ms (<c>18_GPU_VENDOR_APIS</c> §Runtime policy: 1 Hz).</summary>
    public TimeSpan Interval { get; init; } = TimeSpan.FromSeconds(1);

    /// <summary>
    /// Samples held for the session loop before the oldest is dropped. 4096 at 1 Hz is over an
    /// hour of nobody draining, which is not a cadence problem but a dead consumer — and the
    /// count of what was lost is kept, like the ring's.
    /// </summary>
    public int QueueCapacity { get; init; } = 4096;
}
