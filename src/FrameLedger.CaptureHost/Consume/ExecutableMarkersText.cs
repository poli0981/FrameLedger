using System.Globalization;
using FrameLedger.Application.Capture;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>The report's line for <see cref="ExecutableMarkers"/>. Text stays with the report (P2 PR-C).</summary>
internal static class ExecutableMarkersText
{
    /// <summary>What was found, or why nothing was looked at.</summary>
    public static string Describe(this ExecutableMarkers markers)
    {
        ArgumentNullException.ThrowIfNull(markers);
        const string head = "  executable markers (the exe FILE on disk, vendor SDK strings; presence is not identity): ";
        if (markers.Error is not null)
        {
            return head + "not scanned - " + markers.Error;
        }

        if (markers.BytesScanned == 0)
        {
            return head + "not scanned";
        }

        string size = (markers.BytesScanned / (1024.0 * 1024.0)).ToString("0.0", CultureInfo.InvariantCulture);
        List<ExecutableMarker> present = [.. markers.Present];
        if (present.Count == 0)
        {
            return head + $"none of {markers.Markers.Count} marker(s) in {size} MB";
        }

        string list = string.Join(", ", present.Select(m =>
            $"{m.Name} x{m.Hits.ToString(CultureInfo.InvariantCulture)} ({m.Vendor}{(m.FgCapable ? ", can generate frames" : "")})"));
        return head + $"{list} in {size} MB";
    }
}
