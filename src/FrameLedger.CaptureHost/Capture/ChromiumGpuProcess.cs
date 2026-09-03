namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// The one discriminator the resolver may use when several processes share the consented
/// image: Chromium's own <c>--type=gpu-process</c> flag.
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
/// <b>Exactly one, or nothing.</b> Two GPU processes (two instances of the title), none, or
/// any candidate whose command line could not be read all return null — an unreadable
/// candidate must not narrow the set, the same rule <c>TargetResolver</c> applies to an
/// unreadable image path. The token is matched whole: <c>--type=gpu-process-foo</c> is not a
/// GPU process.
/// </para>
/// </remarks>
internal static class ChromiumGpuProcess
{
    public const string Marker = "--type=gpu-process";

    /// <summary>The single candidate that is the GPU process, or null.</summary>
    public static int? Pick(IReadOnlyList<(int Pid, string? CommandLine)> candidates)
    {
        ArgumentNullException.ThrowIfNull(candidates);

        int? found = null;
        foreach ((int pid, string? commandLine) in candidates)
        {
            if (commandLine is null)
            {
                return null;
            }

            if (!HasMarker(commandLine))
            {
                continue;
            }

            if (found is not null)
            {
                return null;
            }

            found = pid;
        }

        return found;
    }

    /// <summary>Whether the flag appears as a whole argument.</summary>
    public static bool HasMarker(string commandLine)
    {
        ArgumentNullException.ThrowIfNull(commandLine);
        foreach (string token in commandLine.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (string.Equals(token.Trim('"'), Marker, StringComparison.Ordinal))
            {
                return true;
            }
        }

        return false;
    }
}
