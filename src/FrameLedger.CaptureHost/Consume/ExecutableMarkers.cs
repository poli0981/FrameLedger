using System.Globalization;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// What the executable FILE carries of the vendor SDKs — read from disk, the way the fingerprint reads its size
/// and mtime, never from the process (CLAUDE.md rule 4 is about game memory; this is the file the user pointed at).
/// </summary>
/// <remarks>
/// <para>
/// <b>Why it exists (<c>HANDOFF</c> 7b, 2026-09-06).</b> A title with FSR 3 compiled into its executable loads
/// no vendor module, so the runtime census is empty and the Presented qualifier said *"this number cannot
/// include in-process generated frames"* — wrong in the dangerous direction, on a shape measured twice (Rune
/// Factory, Wukong). A marker in the file says the SDK's code or its symbol names are IN the executable. It does
/// not say the code ran: the qualifier moves from *cannot* to *MAY*, never to an identity.
/// </para>
/// <para>
/// <b>A marker is not identity, and a shipped-DLL title can carry the same strings</b> (a UE plugin's exe
/// references its plugin by name). So this is consulted only where the module census has nothing to say.
/// </para>
/// </remarks>
internal sealed record ExecutableMarkers(IReadOnlyList<ExecutableMarker> Markers, long BytesScanned, string? Error)
{
    /// <summary>The loop was run without a scan.</summary>
    public static readonly ExecutableMarkers NotScanned = new([], 0, null);

    public bool Scanned => Error is null && BytesScanned > 0;

    /// <summary>Markers found at least once.</summary>
    public IEnumerable<ExecutableMarker> Present => Markers.Where(m => m.Hits > 0);

    /// <summary>Markers found that belong to an SDK able to generate frames.</summary>
    public IEnumerable<ExecutableMarker> FgCapablePresent => Present.Where(m => m.FgCapable);

    public bool AnyFgCapable => FgCapablePresent.Any();

    /// <summary>The names of the frame-generation-capable markers found, for a sentence.</summary>
    public string FgCapableNames => string.Join(", ", FgCapablePresent.Select(m => m.Name));

    /// <summary>The report's line: what was found, or why nothing was looked at.</summary>
    public string Describe()
    {
        const string head = "  executable markers (the exe FILE on disk, vendor SDK strings; presence is not identity): ";
        if (Error is not null)
        {
            return head + "not scanned - " + Error;
        }

        if (BytesScanned == 0)
        {
            return head + "not scanned";
        }

        string size = (BytesScanned / (1024.0 * 1024.0)).ToString("0.0", CultureInfo.InvariantCulture);
        List<ExecutableMarker> present = [.. Present];
        if (present.Count == 0)
        {
            return head + $"none of {Markers.Count} marker(s) in {size} MB";
        }

        string list = string.Join(", ", present.Select(m =>
            $"{m.Name} x{m.Hits.ToString(CultureInfo.InvariantCulture)} ({m.Vendor}{(m.FgCapable ? ", can generate frames" : "")})"));
        return head + $"{list} in {size} MB";
    }
}
