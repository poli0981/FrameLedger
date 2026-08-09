using System.Runtime.InteropServices;

namespace FrameLedger.Infrastructure.Io;

/// <summary>
/// Publish a file so a reader never observes a partial one, using the four
/// mechanisms <c>FileSystemRulesStore</c> measured the hard way.
/// </summary>
/// <remarks>
/// <para>
/// <b>Here rather than in a caller, because CLAUDE.md §Solution layout says so:
/// "The native layer is reachable only through <c>Infrastructure</c> — no P/Invoke
/// anywhere else."</b> The capture host owns a file-backed consent store and needs
/// this; it does not get to declare its own <c>ReplaceFileW</c>.
/// </para>
/// <para>
/// <b>It duplicates one <c>DllImport</c> rather than refactoring
/// <c>FileSystemRulesStore</c>, and that is deliberate.</b> That class writes the
/// hard gate's only input, and moving its publish path to share code with a
/// consent store would put an unrelated caller's requirements on the file the
/// anti-cheat guard reads. One duplicated declaration is the cheaper risk.
/// </para>
/// <para>
/// The four mechanisms, each chosen against an obvious wrong answer that was
/// shipped first somewhere in this tree:
/// </para>
/// <list type="bullet">
/// <item>
/// A reparse point anywhere in the ancestry refuses. A junction planted before
/// first run redirects the write, and a path-string comparison cannot see it.
/// </item>
/// <item>
/// The temp file goes in the DESTINATION directory, with a random name and
/// <c>FileShare.None</c>: <c>ReplaceFile</c> needs the same volume, <c>%TEMP%</c>
/// is both possibly elsewhere and the inherited-environment vector §S21 closed,
/// and a predictable name can be pre-created.
/// </item>
/// <item>
/// <c>Flush(flushToDisk: true)</c>, not <c>FlushAsync</c>, which only reaches the
/// OS cache. There is no async flush-to-disk in .NET, so the synchronous call is
/// the point rather than an oversight.
/// </item>
/// <item>
/// <c>ReplaceFileW</c> with a backup named, not
/// <c>MoveFileEx(MOVEFILE_REPLACE_EXISTING)</c>: measured against a handle opened
/// the way the guard opens one (<c>GENERIC_READ</c>,
/// <c>FILE_SHARE_READ|WRITE|DELETE</c>), <c>MoveFileEx</c> returns
/// <c>ERROR_ACCESS_DENIED</c> and <c>ReplaceFileW</c> succeeds. Delete sharing is
/// necessary and nowhere near sufficient.
/// </item>
/// </list>
/// </remarks>
public static class AtomicFileWrite
{
    /// <summary>
    /// Write <paramref name="content"/> to <paramref name="destination"/>, replacing
    /// whatever is there and keeping a <c>.bak</c> of it.
    /// </summary>
    /// <returns>True when the bytes are published; false on any IO refusal.</returns>
    public static async ValueTask<bool> PublishAsync(string destination, byte[] content,
        CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(destination);
        ArgumentNullException.ThrowIfNull(content);

        string dir = Path.GetDirectoryName(destination)
            ?? throw new InvalidOperationException($"'{destination}' has no directory");

        try
        {
            Directory.CreateDirectory(dir);
            if (AnyReparsePoint(dir))
            {
                return false;
            }

            string tmp = Path.Combine(dir,
                $"{Path.GetFileName(destination)}.{Guid.NewGuid():N}.tmp");
            try
            {
                var s = new FileStream(tmp, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096,
                    FileOptions.Asynchronous);
                await using (s.ConfigureAwait(false))
                {
                    await s.WriteAsync(content, ct).ConfigureAwait(false);
#pragma warning disable CA1849 // no async equivalent exists for flushToDisk
                    s.Flush(flushToDisk: true);
#pragma warning restore CA1849
                }

                if (!File.Exists(destination))
                {
                    // Nothing to replace. ReplaceFileW requires the destination to exist, so the first
                    // write is a move — and losing that race means somebody else got there, which the
                    // caller re-reads rather than treating as a failure.
                    File.Move(tmp, destination, overwrite: false);
                    return true;
                }

                return ReplaceFileW(destination, tmp, destination + ".bak", 0, IntPtr.Zero, IntPtr.Zero);
            }
            finally
            {
                TryDelete(tmp);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static void TryDelete(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // A leftover temp file is litter, not a failure of the write.
        }
    }

    private static bool AnyReparsePoint(string directory)
    {
        for (DirectoryInfo? d = new(directory); d is not null; d = d.Parent)
        {
            if (!d.Exists)
            {
                continue;
            }

            if (d.Attributes.HasFlag(FileAttributes.ReparsePoint))
            {
                return true;
            }
        }

        return false;
    }

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ReplaceFileW(string replacedFileName, string replacementFileName,
        string? backupFileName, uint replaceFlags, IntPtr exclude, IntPtr reserved);
}
