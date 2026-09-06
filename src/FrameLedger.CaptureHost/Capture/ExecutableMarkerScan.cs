using System.Text;
using FrameLedger.CaptureHost.Consume;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// Streams the executable file from disk once and counts the vendor SDK strings in it.
/// </summary>
/// <remarks>
/// Chunked with an overlap of the longest marker, so a string straddling two reads is still one hit — a test
/// pins that with a tiny chunk. ASCII, case-sensitive: these are symbol prefixes and product names as the
/// SDKs spell them. Per-marker hits are capped so a file that repeats a string cannot make the scan slow to
/// describe. Every failure is an <see cref="ExecutableMarkers.Error"/>, never a throw into the loop.
/// </remarks>
internal static class ExecutableMarkerScan
{
    private const int _defaultChunkBytes = 4 * 1024 * 1024;

    private const int _hitCap = 1000;

    /// <summary>
    /// The strings, what they belong to, and whether that SDK can generate frames. Only the last column moves the
    /// census's sentence; the rest are printed for the reader.
    /// </summary>
    private static readonly (string Name, string Vendor, bool FgCapable)[] _table =
    [
        ("FidelityFX", "AMD FidelityFX", false),
        ("ffxFsr2", "AMD FSR 2 API", false),
        ("ffxFsr3", "AMD FSR 3 API (upscaler and frame interpolation)", true),
        ("ffxFrameInterpolation", "AMD FSR 3 frame interpolation", true),
        ("NVSDK_NGX", "NVIDIA NGX SDK (the static half; DLSS-G still needs nvngx_dlssg.dll)", false),
        ("xessD3D12", "Intel XeSS SDK", false),
        ("xefgSwapChain", "Intel XeSS-FG SDK", true),
    ];

    public static ExecutableMarkers Scan(string exePath) => Scan(exePath, _defaultChunkBytes);

    public static ExecutableMarkers Scan(string exePath, int chunkBytes)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(chunkBytes, 64);
        int[] hits = new int[_table.Length];
        byte[][] needles = [.. _table.Select(t => Encoding.ASCII.GetBytes(t.Name))];
        int overlap = needles.Max(n => n.Length) - 1;
        long scanned;
        try
        {
            scanned = ScanFile(exePath, chunkBytes, needles, overlap, hits);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return new ExecutableMarkers([], 0, e.Message);
        }

        List<ExecutableMarker> markers = [];
        for (int i = 0; i < _table.Length; i++)
        {
            markers.Add(new ExecutableMarker(_table[i].Name, _table[i].Vendor, _table[i].FgCapable, hits[i]));
        }

        return new ExecutableMarkers(markers, scanned, null);
    }

    private static long ScanFile(string exePath, int chunkBytes, byte[][] needles, int overlap, int[] hits)
    {
        using FileStream f = new(exePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        byte[] buffer = new byte[overlap + chunkBytes];
        int carried = 0;
        long scanned = 0;
        while (true)
        {
            int read = f.Read(buffer, carried, chunkBytes);
            if (read == 0)
            {
                break;
            }

            scanned += read;
            int length = carried + read;
            CountIn(buffer.AsSpan(0, length), needles, hits, carried);

            // Carry the tail so a marker split across two reads is seen once, on the next pass.
            carried = Math.Min(overlap, length);
            Buffer.BlockCopy(buffer, length - carried, buffer, 0, carried);
        }

        return scanned;
    }

    /// <summary>
    /// Count each needle in <paramref name="window"/>. A hit is counted only if it does not lie wholly inside
    /// the carried prefix (<paramref name="carried"/> bytes), which the previous pass already counted.
    /// </summary>
    private static void CountIn(ReadOnlySpan<byte> window, byte[][] needles, int[] hits, int carried)
    {
        for (int n = 0; n < needles.Length; n++)
        {
            ReadOnlySpan<byte> needle = needles[n];
            int at = 0;
            while (hits[n] < _hitCap)
            {
                int found = window[at..].IndexOf(needle);
                if (found < 0)
                {
                    break;
                }

                int start = at + found;
                if (start + needle.Length > carried)
                {
                    hits[n]++;
                }

                at = start + 1;
            }
        }
    }
}
