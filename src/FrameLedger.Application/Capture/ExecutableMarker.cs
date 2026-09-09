namespace FrameLedger.Application.Capture;

/// <summary>
/// One vendor marker looked for in the executable FILE on disk, and how many times it occurred.
/// </summary>
/// <param name="Name">The ASCII string searched for, case-sensitive — an API symbol prefix or a product name.</param>
/// <param name="Vendor">What the string belongs to, for the report.</param>
/// <param name="FgCapable">
/// True when the SDK the marker belongs to can generate frames — the only markers that move the
/// census's "cannot include generated frames" sentence to "MAY".
/// </param>
/// <param name="Hits">Occurrences in the file, capped; 0 means absent.</param>
public sealed record ExecutableMarker(string Name, string Vendor, bool FgCapable, int Hits);
