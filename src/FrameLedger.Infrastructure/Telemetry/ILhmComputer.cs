using LibreHardwareMonitor.Hardware;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// The seam between <see cref="LhmTelemetrySource"/> and LibreHardwareMonitor's concrete
/// <c>Computer</c>.
/// </summary>
/// <remarks>
/// <para>
/// <c>IHardware</c> and <c>ISensor</c> are already interfaces, so a test can fake a whole
/// sensor tree with NSubstitute; only <c>Computer</c> itself is concrete, and it is the
/// thing that touches drivers. This interface is the minimum the source needs from it —
/// open, update, enumerate, close — so the fault policy and the capability rules can be
/// tested on a machine with no GPU at all, which is what CI is.
/// </para>
/// <para>
/// <b>No setter is reachable through this seam</b>, and none may be added
/// (<c>18_GPU_VENDOR_APIS</c> §Runtime policy: read-only, always).
/// </para>
/// </remarks>
public interface ILhmComputer
{
    /// <summary>Initialise the enabled hardware groups. May throw; the source counts that as a fault.</summary>
    void Open();

    /// <summary>Refresh every sensor value. One call per poll.</summary>
    void Update();

    /// <summary>The top-level hardware tree after <see cref="Open"/>.</summary>
    IReadOnlyList<IHardware> Hardware { get; }

    /// <summary>Release the library's handles. Must not be called while <see cref="Update"/> is in progress.</summary>
    void Close();
}
