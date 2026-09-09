using System.Diagnostics.CodeAnalysis;

namespace FrameLedger.Application.Telemetry;

/// <summary>
/// One layer of adapter-wide GPU telemetry (<c>docs/18_GPU_VENDOR_APIS.md</c> §Abstraction).
/// </summary>
/// <remarks>
/// <para>
/// Implementations compose rather than compete: <c>BaselineTelemetrySource</c> (L1),
/// <c>LhmTelemetrySource</c> (L2), <c>NvapiTelemetrySource</c> (L3), merged by a
/// <c>CompositeTelemetrySource</c> with fixed per-field precedence. L2 came first (2026-09-03,
/// because §M5 could not be measured without it); L1 and the composite followed on 2026-09-09;
/// L3 is P2's PR-E2.
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
    /// True once the layer has been disabled for the session (<c>18_GPU_VENDOR_APIS</c> §Runtime
    /// policy: it threw or hung twice). Never clears. Distinguishes "nothing yet" from "never
    /// again" for the composite descriptor, which lists the layers still standing.
    /// </summary>
    bool IsDisabled { get; }

    /// <summary>
    /// The most recent sample. False when there is none yet, or when the source has been
    /// disabled — the two are distinguishable through the implementation, not through
    /// this port, because the Agent treats both as <c>N/A</c>.
    /// </summary>
    bool TryRead([NotNullWhen(true)] out GpuSample? sample);
}
