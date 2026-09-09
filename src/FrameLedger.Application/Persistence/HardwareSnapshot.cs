using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace FrameLedger.Application.Persistence;

/// <summary>
/// <c>hardware_snapshots</c>: one row per distinct machine state, deduplicated by the hash of its
/// normalised fields (<c>06_DATA_MODEL</c> §Hardware change markers, FR-6.3).
/// </summary>
public sealed record HardwareSnapshot
{
    public string? CpuName { get; init; }

    public string? GpuName { get; init; }

    /// <summary>From the vendor API, not WMI — accurate enough to be worth charting.</summary>
    public string? GpuDriver { get; init; }

    public double? RamGb { get; init; }

    public string? OsBuild { get; init; }

    public string? DisplayRes { get; init; }

    public double? DisplayHz { get; init; }

    /// <summary>SHA-256 over the normalised fields — trimmed, lower-cased, tab-joined, invariant numbers.</summary>
    public string Hash
    {
        get
        {
            string joined = string.Join('\t',
                Norm(CpuName), Norm(GpuName), Norm(GpuDriver),
                RamGb?.ToString("R", CultureInfo.InvariantCulture) ?? string.Empty,
                Norm(OsBuild), Norm(DisplayRes),
                DisplayHz?.ToString("R", CultureInfo.InvariantCulture) ?? string.Empty);
            return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(joined)));
        }
    }

    private static string Norm(string? s) => (s ?? string.Empty).Trim().ToUpperInvariant();
}
