namespace FrameLedger.Application.Rules;

/// <summary>
/// Everything <see cref="RulesSeeder"/> needs to know about the rules file, and
/// nothing else.
/// </summary>
/// <remarks>
/// <para>
/// <strong>No method takes a path.</strong> The rules source is not selectable —
/// §S3 removed the pipe's ability to name it and §S21 removed the environment's —
/// and a port with a path parameter is how it would become selectable again. The
/// adapter knows the one location; the policy above it does not.
/// </para>
/// <para>
/// <strong>Validation is not this layer's job either.</strong>
/// <see cref="IsUsableByTheGuard"/> asks the native guard's own parser, because
/// the managed detection reader structurally cannot see the <c>anticheat</c>
/// block (§S15 — there is no second matcher, and there never will be). A seeder
/// that validated with the managed reader would check everything except the half
/// the hard gate consumes.
/// </para>
/// </remarks>
public interface IRulesStore
{
    /// <summary>The rules file the guard reads, if one is there.</summary>
    /// <returns><c>null</c> when absent. Throws only on genuinely unexpected IO.</returns>
    ValueTask<byte[]?> ReadInstalledAsync(CancellationToken ct = default);

    /// <summary>The seed shipped in this build.</summary>
    ValueTask<byte[]> ReadPackagedSeedAsync(CancellationToken ct = default);

    /// <summary>
    /// The content hash this store last recorded as its own work, or <c>null</c>
    /// if it has never installed anything here.
    /// </summary>
    /// <remarks>
    /// Provenance rather than version. §S20's first design compared
    /// <c>rulesVersion</c> — measured against this repository's own history, every
    /// commit that changed the blocklist left that field untouched and the one
    /// commit that bumped it changed the blocklist not at all, so the comparison
    /// would have delivered none of the changes that mattered.
    /// </remarks>
    ValueTask<string?> ReadInstalledMarkerAsync(CancellationToken ct = default);

    /// <summary>Would the native guard accept this document?</summary>
    bool IsUsableByTheGuard(byte[] candidate);

    /// <summary>
    /// Write <paramref name="content"/> to the rules location, atomically, keeping
    /// a backup of whatever was there, and record the hash as ours.
    /// </summary>
    /// <param name="content">The bytes to install.</param>
    /// <param name="replaceExisting">
    /// <c>false</c> refuses if a file appeared after we looked — losing that race
    /// means somebody else installed one, which is not an error.
    /// </param>
    /// <param name="ct">Cancellation.</param>
    ValueTask<RulesWriteOutcome> WriteAsync(byte[] content, bool replaceExisting, CancellationToken ct = default);
}
