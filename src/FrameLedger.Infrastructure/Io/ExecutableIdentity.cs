using FrameLedger.Domain.Consent;

namespace FrameLedger.Infrastructure.Io;

/// <summary>
/// Turns a path a human typed into the identity a consent record is keyed on.
/// </summary>
/// <remarks>
/// <para>
/// <c>04_CAPTURE</c> §Process watcher normalises watchlist paths through
/// <c>GetFinalPathNameByHandle</c> so junctions and symlinks resolve to one name.
/// The same normalisation has to happen here, and for a stronger reason: the
/// watchlist's failure mode is a stale-path badge, while a consent record matched
/// under two different spellings of one path is either a refusal the user cannot
/// explain or — with a filename fallback — a different binary inheriting somebody
/// else's consent.
/// </para>
/// <para>
/// <b><c>File.ResolveLinkTarget</c>, not <c>GetFinalPathNameByHandleW</c>.</b> The
/// framework has done this since .NET 6, and reaching for the Win32 call would put
/// a second <c>DllImport</c> in the tree for a capability already present. It is
/// still here in <c>Infrastructure</c> rather than in the host, because CLAUDE.md
/// puts filesystem and OS interop on this side of the line.
/// </para>
/// </remarks>
public static class ExecutableIdentity
{
    /// <summary>
    /// The canonical full path for <paramref name="path"/>, with links resolved.
    /// </summary>
    /// <remarks>
    /// Falls back to <see cref="Path.GetFullPath(string)"/> when the file is absent
    /// or unreadable. That is deliberate: a path that cannot be resolved must still
    /// produce a stable key, so a missing target reports "no record" rather than
    /// throwing somewhere the caller reads as a different failure.
    /// </remarks>
    public static string Normalise(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        string full = Path.GetFullPath(path);
        try
        {
            FileSystemInfo? target = File.ResolveLinkTarget(full, returnFinalTarget: true);
            return target?.FullName ?? full;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            return full;
        }
    }

    /// <summary>
    /// The executable as it is on disk right now, or null when it is not there.
    /// </summary>
    public static ExecutableFingerprint? Read(string path)
    {
        string normalised = Normalise(path);
        try
        {
            var info = new FileInfo(normalised);
            if (!info.Exists)
            {
                return null;
            }

            return new ExecutableFingerprint
            {
                ExePath = normalised,
                SizeBytes = info.Length,

                // 19_SAFETY names "path, size or mtime" as the re-scan trigger. UTC unix-ms matches
                // CLAUDE.md's timestamp rule and DetectionCacheKey's existing shape.
                MtimeUnixMs = new DateTimeOffset(info.LastWriteTimeUtc, TimeSpan.Zero).ToUnixTimeMilliseconds(),
            };
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
