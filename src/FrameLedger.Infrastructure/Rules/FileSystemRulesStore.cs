using System.Runtime.InteropServices;
using System.Security.Cryptography;
using FrameLedger.Application.Rules;
using FrameLedger.Infrastructure.AntiCheat;

namespace FrameLedger.Infrastructure.Rules;

/// <summary>
/// The real filesystem behind <see cref="IRulesStore"/> (§S20).
/// </summary>
/// <remarks>
/// Three mechanisms here were each chosen against an obvious wrong answer, and
/// the wrong answers are recorded because two of them were shipped first.
/// </remarks>
public sealed class FileSystemRulesStore : IRulesStore
{
    private const string _markerName = ".seeded";

    private readonly string _destination;
    private readonly string _packaged;

    /// <summary>Uses the product's one rules location and the packaged seed beside this assembly.</summary>
    public FileSystemRulesStore()
        : this(NativeAntiCheatGuard.NativeRulesFilePath(),
            Path.Combine(AppContext.BaseDirectory, "rules", "detection-rules.json"))
    {
    }

    /// <summary>Explicit paths. Tests only.</summary>
    public FileSystemRulesStore(string destination, string packagedSeed)
    {
        // The destination comes from the GUARD, not from DetectionRulesFile.
        // Writer and gate then cannot drift by construction rather than by
        // assertion — RulesPathAgreementTests becomes a second net instead of the
        // only one. A seeder that writes where the gate does not read reports
        // success, and its own test would agree with it.
        _destination = !string.IsNullOrEmpty(destination)
            ? destination
            : throw new ArgumentException("the guard could not resolve its rules path", nameof(destination));
        _packaged = packagedSeed ?? throw new ArgumentNullException(nameof(packagedSeed));
    }

    /// <summary>Where this store writes. Exposed so a test can assert it is the guard's own path.</summary>
    public string Destination => _destination;

    /// <inheritdoc />
    public async ValueTask<byte[]?> ReadInstalledAsync(CancellationToken ct = default)
    {
        try
        {
            // Delete sharing, matching fl_ac_rules.cpp. Denying it here would make
            // this reader the thing that blocks the guard's own replace.
            var s = new FileStream(_destination, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete, 4096, FileOptions.Asynchronous | FileOptions.SequentialScan);
            await using (s.ConfigureAwait(false))
            {
                using var ms = new MemoryStream();
                await s.CopyToAsync(ms, ct).ConfigureAwait(false);
                return ms.ToArray();
            }
        }
        catch (FileNotFoundException)
        {
            return null;
        }
        catch (DirectoryNotFoundException)
        {
            return null;
        }
    }

    /// <inheritdoc />
    public async ValueTask<byte[]> ReadPackagedSeedAsync(CancellationToken ct = default) =>
        await File.ReadAllBytesAsync(_packaged, ct).ConfigureAwait(false);

    /// <inheritdoc />
    public async ValueTask<string?> ReadInstalledMarkerAsync(CancellationToken ct = default)
    {
        string path = MarkerPath();
        try
        {
            string text = await File.ReadAllTextAsync(path, ct).ConfigureAwait(false);
            return text.Trim();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // No marker means "we have never installed here", which is the same
            // answer as an unreadable one for every decision above.
            return null;
        }
    }

    /// <inheritdoc />
    public bool IsUsableByTheGuard(byte[] candidate)
    {
        ArgumentNullException.ThrowIfNull(candidate);
        return NativeAntiCheatGuard.NativeCheckRules(candidate) == 0;    // fl::guard::ParseResult::kOk
    }

    /// <inheritdoc />
    public async ValueTask<RulesWriteOutcome> WriteAsync(byte[] content, bool replaceExisting,
        CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(content);
        string dir = Path.GetDirectoryName(_destination)
            ?? throw new InvalidOperationException($"'{_destination}' has no directory");

        try
        {
            Directory.CreateDirectory(dir);

            // A junction planted before first run redirects both this write and
            // the guard's read, and RulesPathAgreementTests cannot see it because
            // it compares path STRINGS. Check 4 already refuses reparse points for
            // exactly this reason — "a junction can hide an EasyAntiCheat/
            // beneath it" — and the read path is what is being defended here.
            if (AnyReparsePoint(dir))
            {
                return RulesWriteOutcome.Failed;
            }

            // Temp file in the DESTINATION directory: ReplaceFile and any rename
            // need the same volume, and %TEMP% is both possibly elsewhere and the
            // inherited-environment vector §S21 closed. Random name so it cannot
            // be pre-created; FileShare.None so nothing can touch the bytes
            // between validation and publication.
            string tmp = Path.Combine(dir, $"detection-rules.json.{Guid.NewGuid():N}.tmp");
            RulesWriteOutcome published;
            try
            {
                await WriteTempAsync(tmp, content, ct).ConfigureAwait(false);
                published = Publish(tmp, replaceExisting);
            }
            finally
            {
                TryDelete(tmp);
            }
            if (published != RulesWriteOutcome.Written)
            {
                return published;
            }

            await File.WriteAllTextAsync(MarkerPath(), Convert.ToHexString(SHA256.HashData(content)), ct)
                .ConfigureAwait(false);
            return RulesWriteOutcome.Written;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return RulesWriteOutcome.Failed;
        }
    }

    private static async ValueTask WriteTempAsync(string tmp, byte[] content, CancellationToken ct)
    {
        var s = new FileStream(tmp, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096,
            FileOptions.Asynchronous);
        await using (s.ConfigureAwait(false))
        {
            await s.WriteAsync(content, ct).ConfigureAwait(false);

            // Flush THROUGH TO DISK before the swap. FlushAsync only reaches the
            // OS cache, so a crash between the write and the rename could publish
            // a zero-length file at the hard gate's only input — which the guard
            // reads as kRulesMalformed, i.e. refuse every title. There is no async
            // flush-to-disk in .NET, so the synchronous call is the point rather
            // than an oversight.
#pragma warning disable CA1849 // no async equivalent exists for flushToDisk
            s.Flush(flushToDisk: true);
#pragma warning restore CA1849
        }
    }

    private RulesWriteOutcome Publish(string tmp, bool replaceExisting)
    {
        if (replaceExisting)
        {
            return ReplaceFileW(_destination, tmp, BackupPath(), 0, IntPtr.Zero, IntPtr.Zero)
                ? RulesWriteOutcome.Written
                : RulesWriteOutcome.Failed;
        }

        // Fails if the target appeared meanwhile, which is the whole point:
        // losing that race means somebody else seeded it.
        try
        {
            File.Move(tmp, _destination, overwrite: false);
            return RulesWriteOutcome.Written;
        }
        catch (IOException) when (File.Exists(_destination))
        {
            return RulesWriteOutcome.AlreadyExists;
        }
    }

    private string MarkerPath() => Path.Combine(Path.GetDirectoryName(_destination)!, _markerName);

    private string BackupPath() => _destination + ".bak";

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

    // MoveFileEx(MOVEFILE_REPLACE_EXISTING) is NOT usable here, and this comment
    // exists because §S21 prescribed it. Measured against a handle opened exactly
    // as the guard opens it (GENERIC_READ, FILE_SHARE_READ|WRITE|DELETE), it
    // returns ERROR_ACCESS_DENIED; ReplaceFileW with a backup file named succeeds.
    // Delete sharing is necessary and nowhere near sufficient.
    //
    // The backup is not incidental either: it is what makes 05_DETECTION's "the
    // last valid copy is kept" a mechanism rather than a sentence.
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ReplaceFileW(string replacedFileName, string replacementFileName,
        string? backupFileName, uint replaceFlags, IntPtr exclude, IntPtr reserved);
}
