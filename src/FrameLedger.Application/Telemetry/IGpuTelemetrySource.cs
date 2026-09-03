using System.Diagnostics.CodeAnalysis;

namespace FrameLedger.Application.Telemetry;

/// <summary>
/// One layer of adapter-wide GPU telemetry (<c>docs/18_GPU_VENDOR_APIS.md</c> §Abstraction).
/// </summary>
/// <remarks>
/// <para>
/// Implementations compose rather than compete: <c>BaselineTelemetrySource</c> (L1),
/// <c>LhmTelemetrySource</c> (L2), <c>NvapiTelemetrySource</c> (L3), merged by a
/// <c>CompositeTelemetrySource</c> with fixed per-field precedence. As of 2026-09-03 only L2
/// exists, and it exists because §M5 could not be measured without it.
/// </para>
/// <para>
/// <b>Read-only, always.</b> The libraries behind these layers can also set clocks, fan
/// curves and power limits. Nothing reachable through this port may call a setter.
/// </para>
/// <para>
/// <b>A layer that throws or hangs twice is disabled for the session</b> and reports
/// <see cref="GpuCapabilities.None"/>; it is never retried in a loop
/// (<c>18_GPU_VENDOR_APIS</c> §Runtime policy).
/// </para>
/// </remarks>
public interface IGpuTelemetrySource : IDisposable
{
    TelemetryLayer Layer { get; }

    /// <summary>
    /// Which fields this source has produced a real value for on this machine. Monotonic
    /// within a session until the source is disabled, at which point it is
    /// <see cref="GpuCapabilities.None"/>.
    /// </summary>
    GpuCapabilities Capabilities { get; }

    /// <summary>
    /// The most recent sample. False when there is none yet, or when the source has been
    /// disabled — the two are distinguishable through the implementation, not through
    /// this port, because the Agent treats both as <c>N/A</c>.
    /// </summary>
    bool TryRead([NotNullWhen(true)] out GpuSample? sample);
}
