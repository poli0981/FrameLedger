namespace FrameLedger.Application.Telemetry;

/// <summary>
/// Which of the three telemetry layers in <c>docs/18_GPU_VENDOR_APIS.md</c> a value came from.
/// </summary>
/// <remarks>
/// The numbers are the layer numbers in that document and are also the precedence
/// <c>CompositeTelemetrySource</c> is specified to apply (<c>L3 &gt; L2 &gt; L1</c>), so a
/// reader of <c>sessions.telemetry_source</c> can tell why a field is missing.
/// </remarks>
public enum TelemetryLayer
{
    /// <summary>No layer supplied the value. The zero a forgetful writer publishes, and it decodes as N/A.</summary>
    None = 0,

    /// <summary>L1 — DXGI (PDH deferred, <c>20_OPEN_QUESTIONS</c> §M10). No licence, every vendor.</summary>
    Baseline = 1,

    /// <summary>L2 — LibreHardwareMonitorLib. MPL-2.0, every vendor.</summary>
    Lhm = 2,

    /// <summary>L3 — NVAPI. MIT, NVIDIA only. Not written yet (P2 PR-E2).</summary>
    Nvapi = 3,
}
