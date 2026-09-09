using System.Globalization;
using FrameLedger.Application.Persistence;
using FrameLedger.Application.Recording;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;
using Microsoft.Win32;

namespace FrameLedger.Infrastructure.Recording;

/// <summary>
/// The machine as of now: the first hardware adapter DXGI lists (name, user-mode driver version), the CPU
/// name from the registry's processor key, the memory the runtime sees, the OS build. Display resolution
/// and refresh are left null in P2 — nothing consumes them until the UI's change markers (FR-6.3, P3).
/// </summary>
public sealed class HardwareSnapshotSource : IHardwareSnapshotSource
{
    private const string _processorKey = @"HARDWARE\DESCRIPTION\System\CentralProcessor\0";

    private readonly IDxgiAdapters _dxgi;

    public HardwareSnapshotSource(IDxgiAdapters dxgi) => _dxgi = dxgi ?? throw new ArgumentNullException(nameof(dxgi));

    public HardwareSnapshot Take()
    {
        GpuAdapterIdentity? gpu = null;
        try
        {
            gpu = _dxgi.Enumerate().FirstOrDefault(static a => !a.IsSoftware);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            // No factory is a snapshot with no GPU, not a session that cannot start.
        }

        return new HardwareSnapshot
        {
            CpuName = CpuName(),
            GpuName = gpu?.Name,
            GpuDriver = gpu?.DriverVersion,
            RamGb = Math.Round(GC.GetGCMemoryInfo().TotalAvailableMemoryBytes / (1024.0 * 1024 * 1024), 1),
            OsBuild = Environment.OSVersion.Version.ToString(),
        };
    }

    private static string? CpuName()
    {
        try
        {
            using RegistryKey? key = Registry.LocalMachine.OpenSubKey(_processorKey);
            return key?.GetValue("ProcessorNameString") is string s && !string.IsNullOrWhiteSpace(s)
                ? s.Trim().Replace("  ", " ", StringComparison.Ordinal)
                : null;
        }
        catch (Exception ex) when (ex is System.Security.SecurityException or IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
