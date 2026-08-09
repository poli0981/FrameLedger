namespace FrameLedger.Domain.Consent;

/// <summary>
/// Which executable a consent record is about, and enough of it to notice that the
/// executable changed.
/// </summary>
/// <remarks>
/// <para>
/// The field set is deliberately the same as <c>Detection.DetectionCacheKey</c>'s,
/// minus the rules version: <c>19_SAFETY</c> §A game already enabled can become
/// blocked later makes "path, size or mtime" the documented trigger for re-running
/// the static pre-scan, so those three are what a consent record has to be able to
/// compare against.
/// </para>
/// <para>
/// <b>The path is the whole path, and there is no filename fallback.</b>
/// <c>04_CAPTURE</c> §Process watcher permits a filename fallback for the
/// <i>watchlist</i>, where a wrong match costs a stale-path badge. Applied to
/// consent it has the opposite polarity: a different binary with the same filename
/// would inherit an existing consent record, which is a widening. Matching is
/// <c>OrdinalIgnoreCase</c> on the normalised full path and nothing else.
/// </para>
/// <para>
/// Pure data. Domain references nothing, so nothing here touches the filesystem —
/// the adapter that resolves a real path fills this in.
/// </para>
/// </remarks>
public readonly record struct ExecutableFingerprint
{
    /// <summary>The normalised full path to the executable.</summary>
    public required string ExePath { get; init; }

    /// <summary>Its size in bytes.</summary>
    public required long SizeBytes { get; init; }

    /// <summary>Its last-write time, unix milliseconds UTC.</summary>
    public required long MtimeUnixMs { get; init; }

    /// <summary>
    /// True when <paramref name="observed"/> is the same executable this record was
    /// written for.
    /// </summary>
    /// <remarks>
    /// Any of the three differing is a mismatch. It does not follow that consent is
    /// withdrawn — <c>19_SAFETY</c> is explicit that <c>hook_consent_at</c> is
    /// preserved across a later block — only that <i>this</i> session may not use it.
    /// </remarks>
    public bool Matches(ExecutableFingerprint observed) =>
        string.Equals(ExePath, observed.ExePath, StringComparison.OrdinalIgnoreCase)
        && SizeBytes == observed.SizeBytes
        && MtimeUnixMs == observed.MtimeUnixMs;
}
