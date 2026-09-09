namespace FrameLedger.Infrastructure.Capture;

/// <summary>
/// The discriminators the resolver may use when several processes share the consented
/// image: Chromium's own process-type flags.
/// </summary>
/// <remarks>
/// <para>
/// <b>This does not weaken §S27.</b> Consent is still keyed on the path, every candidate IS
/// that path, and the guard still scans the pid that is chosen. What it removes is a refusal
/// that had no correct alternative: the presenting process of an NW.js / Electron / RPG
/// Maker title is Chromium's GPU process, which owns no window, so "the one with the window"
/// and "the parent" would both pick the wrong process. The flag is Chromium's, documented,
/// and set on exactly one process per browser instance.
/// </para>
/// <para>
/// <b>Two shapes, measured 2026-09-03.</b> A Chromium with a GPU process marks it
/// <c>--type=gpu-process</c>. <b>NW.js does not have one</b>: <i>Flower in Us</i> runs six
/// processes — browser, crashpad-handler, three utility, renderer — and the GPU work is
/// in-process in the <i>browser</i>, which is the one process that carries <b>no</b>
/// <c>--type=</c> at all (Chromium's convention: children are typed, the browser is not).
/// So the rule is: exactly one <c>--type=gpu-process</c> wins; otherwise exactly one
/// untyped candidate wins <i>provided every other candidate is typed</i>, because two
/// untyped candidates are two browsers, i.e. two instances.
/// </para>
/// <para>
/// <b>Exactly one, or nothing.</b> Two GPU processes, two browsers, none, or any candidate
/// whose command line could not be read all return null — an unreadable candidate must not
/// narrow the set, the same rule <c>TargetResolver</c> applies to an unreadable image path.
/// Tokens are matched whole: <c>--type=gpu-process-foo</c> is not a GPU process.
/// </para>
/// </remarks>
public static class ChromiumGpuProcess
{
    public const string Marker = "--type=gpu-process";
    private const string _typePrefix = "--type=";

    /// <summary>What the pick was, for the report line.</summary>
    public enum Kind
    {
        None = 0,
        GpuProcess,
        BrowserWithInProcessGpu,
    }

    /// <summary>The single candidate that presents, or null.</summary>
    public static int? Pick(IReadOnlyList<(int Pid, string? CommandLine)> candidates) => Pick(candidates, out _);

    /// <summary>The single candidate that presents, or null, and which rule chose it.</summary>
    public static int? Pick(IReadOnlyList<(int Pid, string? CommandLine)> candidates, out Kind kind)
    {
        ArgumentNullException.ThrowIfNull(candidates);
        kind = Kind.None;

        int? gpu = null;
        int? untyped = null;
        int gpuCount = 0;
        int untypedCount = 0;
        foreach ((int pid, string? commandLine) in candidates)
        {
            if (commandLine is null)
            {
                return null;
            }

            string? type = TypeOf(commandLine);
            if (type is null)
            {
                untypedCount++;
                untyped = pid;
            }
            else if (string.Equals(type, "gpu-process", StringComparison.Ordinal))
            {
                gpuCount++;
                gpu = pid;
            }
        }

        if (gpuCount == 1)
        {
            kind = Kind.GpuProcess;
            return gpu;
        }

        if (gpuCount == 0 && untypedCount == 1 && candidates.Count > 1)
        {
            kind = Kind.BrowserWithInProcessGpu;
            return untyped;
        }

        return null;
    }

    /// <summary>Whether the GPU-process flag appears as a whole argument.</summary>
    public static bool HasMarker(string commandLine) =>
        string.Equals(TypeOf(commandLine), "gpu-process", StringComparison.Ordinal);

    /// <summary>Chromium's <c>--type=</c> value as a whole argument, or null for the browser process.</summary>
    public static string? TypeOf(string commandLine)
    {
        ArgumentNullException.ThrowIfNull(commandLine);
        foreach (string raw in commandLine.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            string token = raw.Trim('"');
            if (token.StartsWith(_typePrefix, StringComparison.Ordinal))
            {
                return token[_typePrefix.Length..];
            }
        }

        return null;
    }

    /// <summary>One line naming what each candidate is, for the refusal the report prints.</summary>
    public static string Describe(IReadOnlyList<(int Pid, string? CommandLine)> candidates)
    {
        ArgumentNullException.ThrowIfNull(candidates);
        var counts = new SortedDictionary<string, int>(StringComparer.Ordinal);
        foreach ((_, string? commandLine) in candidates)
        {
            string key = commandLine is null ? "unreadable" : TypeOf(commandLine) ?? "browser";
            counts[key] = counts.TryGetValue(key, out int n) ? n + 1 : 1;
        }

        return string.Join(", ", counts.Select(static kv => $"{kv.Key}={kv.Value}"));
    }
}
