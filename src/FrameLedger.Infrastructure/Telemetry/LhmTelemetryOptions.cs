namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>How <see cref="LhmTelemetrySource"/> polls.</summary>
public sealed record LhmTelemetryOptions
{
    /// <summary><c>18_GPU_VENDOR_APIS</c> §L2: never faster than 500 ms.</summary>
    public static readonly TimeSpan MinimumInterval = TimeSpan.FromMilliseconds(500);

    /// <summary>Default 1000 ms, per the same section.</summary>
    public TimeSpan Interval { get; init; } = TimeSpan.FromSeconds(1);

    /// <summary>
    /// How long one poll may run before it is counted as a hang. A hang is a fault, and
    /// two faults disable the layer for the session.
    /// </summary>
    public TimeSpan HangThreshold { get; init; } = TimeSpan.FromSeconds(5);

    /// <summary>
    /// Enable LHM's CPU and memory groups. Only when the process is elevated and PawnIO is
    /// installed; the caller decides, this type only carries the decision.
    /// </summary>
    public bool EnableCpuAndMemory { get; init; }
}
