using LibreHardwareMonitor.PawnIo;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// The two facts about the process that decide what LibreHardwareMonitor may be asked for.
/// </summary>
/// <remarks>
/// <c>18_GPU_VENDOR_APIS</c> §L2: CPU and memory groups only when elevated <b>and</b> PawnIO
/// is available. GPU sensors are expected to need neither, and §M5 is the measurement of
/// that expectation — so both facts are printed by the probe beside every result.
/// </remarks>
public static class LhmEnvironment
{
    /// <summary>Whether this process runs elevated. What ADR-9 calls optional.</summary>
    public static bool IsElevated => Environment.IsPrivilegedProcess;

    /// <summary>
    /// Whether the PawnIO driver LHM uses for CPU and board sensors is installed. Null when
    /// the library could not answer, which is reported rather than read as "no".
    /// </summary>
    public static bool? IsPawnIoInstalled
    {
        get
        {
            try
            {
                return PawnIo.IsInstalled;
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                return null;
            }
        }
    }
}
