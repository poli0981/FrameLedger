using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
using FrameLedger.Application.Detection;
using FrameLedger.Domain.Detection;

namespace FrameLedger.Infrastructure.Detection;

/// <summary>
/// Collects one snapshot of a game directory, under bounds.
/// </summary>
/// <remarks>
/// <para>
/// Every bound here records into
/// <see cref="GameFileSnapshot.UncollectedFacts"/> rather than truncating
/// silently. A walk that stopped early has not seen the directory, and a needle
/// missing from a scan that did not finish is not evidence of absence — the same
/// rule the native guard applies to module lists, applied to inference.
/// </para>
/// <para>
/// It reads files, never processes, and touches nothing outside the game
/// directory (<c>05_DETECTION</c> §Caching &amp; privacy constraints).
/// </para>
/// </remarks>
public sealed class GameFileProbe : IGameFileProbe
{
    /// <summary>The strings-scan bound from <c>05_DETECTION</c>.</summary>
    public const int StringsScanBytes = 8 * 1024 * 1024;

    /// <summary>How deep below the install root the walk goes.</summary>
    /// <remarks>
    /// Measured against three real installs before choosing: Deadly Heart Gambit
    /// is 6 deep, Alan Wake 2 is 5, Lies of P is 9. The first version capped at
    /// 4, which meant every real game came back incomplete.
    /// </remarks>
    public const int MaxDepth = 16;

    /// <summary>How many entries the walk will look at before giving up.</summary>
    /// <remarks>
    /// The real bound. Those three installs hold 254, 271 and 122 files; this
    /// leaves room for a very large title while still stopping a runaway walk.
    /// </remarks>
    public const int MaxEntries = 200_000;

    private static readonly TimeSpan _regexBudget = TimeSpan.FromMilliseconds(250);

    /// <inheritdoc />
    public ValueTask<GameFileSnapshot> SnapshotAsync(string exePath, DetectionRuleSet rules,
        CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(exePath);
        ArgumentNullException.ThrowIfNull(rules);
        ct.ThrowIfCancellationRequested();

        // The INSTALL ROOT, not the executable's own directory — an Unreal exe
        // sits three levels below everything that identifies the game.
        string dir = InstallRoot.Resolve(exePath);

        // manifest_field is uncollected from the start: the store-manifest
        // extractors are not built, so every such signal must evaluate Unknown
        // rather than false. The deferral costs answers, not correctness.
        HashSet<DetectionSignalType> uncollected = [DetectionSignalType.ManifestField];

        List<string> files = [];
        List<string> dirs = [];

        // An incomplete walk is NOT recorded as an uncollected fact. It makes a
        // miss unreliable, not a hit — see GameFileSnapshot.FileListingComplete.
        bool listingComplete = TryWalk(dir, files, dirs);

        VersionInfo pe = ReadVersionInfo(exePath);
        if (pe.IsEmpty)
        {
            uncollected.Add(DetectionSignalType.PeCompanyContains);
            uncollected.Add(DetectionSignalType.PeProductContains);
        }

        HashSet<string> needles = [];
        Dictionary<string, string> captures = new(StringComparer.Ordinal);
        if (!TryScanStrings(exePath, rules, needles, captures))
        {
            uncollected.Add(DetectionSignalType.StringsContains);
        }

        return ValueTask.FromResult(new GameFileSnapshot
        {
            ExePath = exePath.Replace('\\', '/'),
            ExeNameWithoutExtension = Path.GetFileNameWithoutExtension(exePath),
            GameDirectory = dir.Replace('\\', '/'),
            RelativeFiles = files,
            RelativeDirectories = dirs,
            FileListingComplete = listingComplete,
            PeCompanyName = pe.Company,
            PeProductName = pe.Product,
            PeFileVersion = pe.FileVersion,
            PeProductVersion = pe.ProductVersion,
            SiblingFileVersions = ReadSiblingVersions(dir, rules),
            MatchedStringNeedles = needles,
            StringsRegexCaptures = captures,
            ManifestFields = new Dictionary<string, string>(StringComparer.Ordinal),
            UncollectedFacts = uncollected,
        });
    }

    /// <summary>Breadth-first, bounded. Returns false if the walk did not complete.</summary>
    private static bool TryWalk(string root, List<string> files, List<string> dirs)
    {
        if (string.IsNullOrEmpty(root) || !Directory.Exists(root))
        {
            return false;
        }

        var queue = new Queue<(string Path, int Depth)>();
        queue.Enqueue((root, 0));
        int budget = MaxEntries;
        bool complete = true;

        while (queue.Count > 0)
        {
            (string current, int depth) = queue.Dequeue();

            string[] entries;
            try
            {
                entries = Directory.GetFileSystemEntries(current);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // A subdirectory we could not read is part of the tree we did
                // not see. Keep walking the rest, but say the picture is partial.
                complete = false;
                continue;
            }

            foreach (string entry in entries)
            {
                if (budget-- <= 0)
                {
                    return false;
                }

                if (!Visit(root, entry, depth, files, dirs, queue))
                {
                    complete = false;
                }
            }
        }

        return complete;
    }

    /// <summary>Records one entry. Returns false when it left a gap in the picture.</summary>
    private static bool Visit(string root, string entry, int depth, List<string> files, List<string> dirs,
        Queue<(string Path, int Depth)> queue)
    {
        string relative = Path.GetRelativePath(root, entry).Replace('\\', '/');

        FileAttributes attrs;
        try
        {
            attrs = new FileInfo(entry).Attributes;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return false;
        }

        bool isDir = attrs.HasFlag(FileAttributes.Directory);

        // Reparse points are recorded but NOT followed. A junction can point
        // back into this tree, and an unbounded symlink walk is the classic bug
        // here — so its contents are a gap we admit to.
        if (attrs.HasFlag(FileAttributes.ReparsePoint))
        {
            (isDir ? dirs : files).Add(relative);
            return false;
        }

        if (!isDir)
        {
            files.Add(relative);
            return true;
        }

        dirs.Add(relative);
        if (depth + 1 < MaxDepth)
        {
            queue.Enqueue((entry, depth + 1));
            return true;
        }

        return false;    // deeper than we look: another admitted gap
    }

    /// <summary>
    /// Version info for the sibling files that <c>pe_file_version</c> extractors
    /// name, keyed by the <c>from</c> value.
    /// </summary>
    /// <remarks>
    /// Unity's rule reads <c>UnityPlayer.dll</c>. Answering that from the game
    /// executable would report a version that is <em>wrong</em> rather than
    /// merely missing, which is the worse of the two failures.
    /// </remarks>
    private static Dictionary<string, string> ReadSiblingVersions(string dir, DetectionRuleSet rules)
    {
        Dictionary<string, string> versions = new(StringComparer.OrdinalIgnoreCase);
        if (string.IsNullOrEmpty(dir))
        {
            return versions;
        }

        foreach (string from in rules.Engines
                     .Select(e => e.Version)
                     .Where(v => v is { Type: VersionExtractorType.PeFileVersion, From: not null })
                     .Select(v => v!.From!)
                     .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            string candidate = Path.Combine(dir, from);
            if (!File.Exists(candidate))
            {
                continue;
            }

            VersionInfo v = ReadVersionInfo(candidate);
            if (v.FileVersion is not null)
            {
                versions[from] = v.FileVersion;
            }
        }

        return versions;
    }

    private static VersionInfo ReadVersionInfo(string exePath)
    {
        try
        {
            FileVersionInfo v = FileVersionInfo.GetVersionInfo(exePath);
            return new VersionInfo(v.CompanyName, v.ProductName, v.FileVersion, v.ProductVersion);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            // Null everywhere, so every pe_* signal evaluates Unknown. A PE we
            // could not read must not make "does the company contain Valve"
            // answer no.
            return new VersionInfo(null, null, null, null);
        }
    }

    /// <summary>
    /// One bounded pass over the executable, answering every strings question
    /// the rule set asks. Returns false if the pass could not be made.
    /// </summary>
    /// <remarks>
    /// The needles have to be known before the read, which is why the snapshot
    /// is rules-dependent and the evaluator is not quite the pure function it
    /// would be nicer to have.
    /// </remarks>
    private static bool TryScanStrings(string exePath, DetectionRuleSet rules,
        HashSet<string> foundNeedles, Dictionary<string, string> captures)
    {
        List<string> needles = [.. rules.Engines
            .SelectMany(e => e.Signals.Signals)
            .Concat(rules.Platforms.SelectMany(p => p.Signals.Signals))
            .Where(s => s.Type == DetectionSignalType.StringsContains)
            .Select(s => s.Value)
            .Distinct(StringComparer.Ordinal)];

        List<string> patterns = [.. rules.Engines
            .Select(e => e.Version)
            .Where(v => v is { Type: VersionExtractorType.StringsRegex, Value: not null })
            .Select(v => v!.Value!)
            .Distinct(StringComparer.Ordinal)];

        if (needles.Count == 0 && patterns.Count == 0)
        {
            return true;    // nothing asked, nothing to fail at
        }

        string? text = TryReadBounded(exePath);
        if (text is null)
        {
            return false;
        }

        foreach (string n in needles)
        {
            if (text.Contains(n, StringComparison.Ordinal))
            {
                foundNeedles.Add(n);
            }
        }

        foreach (string p in patterns)
        {
            string? captured = FirstGroup(text, p);
            if (captured is not null)
            {
                captures[p] = captured;
            }
        }

        return true;
    }

    private static string? TryReadBounded(string exePath)
    {
        try
        {
            using FileStream fs = File.OpenRead(exePath);
            int take = (int)Math.Min(fs.Length, StringsScanBytes);
            byte[] buffer = new byte[take];
            int read = fs.ReadAtLeast(buffer, take, throwOnEndOfStream: false);

            // Latin1 rather than UTF-8: this is a byte scan for ASCII markers in
            // a binary, and a decoder that replaced invalid sequences would
            // corrupt the very bytes being looked for.
            return Encoding.Latin1.GetString(buffer, 0, read);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static string? FirstGroup(string text, string pattern)
    {
        // Rules are updatable DATA, so a catastrophic or unparseable pattern is
        // in scope. Either costs one version field, never the Agent.
        try
        {
            Match m = Regex.Match(text, pattern, RegexOptions.None, _regexBudget);
            return m.Success && m.Groups.Count > 1 ? m.Groups[1].Value : null;
        }
        catch (RegexMatchTimeoutException)
        {
            return null;
        }
        catch (ArgumentException)
        {
            return null;
        }
    }

    private readonly record struct VersionInfo(string? Company, string? Product, string? FileVersion,
        string? ProductVersion)
    {
        public bool IsEmpty => Company is null && Product is null && FileVersion is null && ProductVersion is null;
    }
}
