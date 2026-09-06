using System.Globalization;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The Overlay's native log for a session (<c>17_HOOK_ENGINE</c> §Native logging), as the report reads it: where
/// it is, how many events it holds, and the lines a bug report needs first — faults, the stop, and every
/// unhook decision (<c>10_LOGGING</c>: "hook fault details … are what make injection bugs diagnosable at all").
/// </summary>
internal static class OverlayLog
{
    /// <summary><c>%LOCALAPPDATA%\FrameLedger\logs</c> — where the Overlay writes, resolved the same way.</summary>
    public static string DefaultDirectory => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FrameLedger", "logs");

    /// <summary>The newest <c>overlay-&lt;pid&gt;-*.log</c> in <paramref name="directory"/>, or null.</summary>
    public static string? Find(int pid, string directory)
    {
        if (pid <= 0 || !Directory.Exists(directory))
        {
            return null;
        }

        try
        {
            return Directory.EnumerateFiles(directory, "overlay-" + pid.ToString(CultureInfo.InvariantCulture) + "-*.log")
                .OrderByDescending(File.GetLastWriteTimeUtc)
                .FirstOrDefault();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    /// <summary>The report lines: the path and count, then the lines worth reading without opening the file.</summary>
    public static IEnumerable<string> Describe(int pid, string directory)
    {
        string? path = Find(pid, directory);
        if (path is null)
        {
            yield return "  overlay log: none found for this pid (the Overlay writes it at init, on request and on the stop)";
            yield break;
        }

        string[]? lines = TryReadLines(path, out string? problem);
        if (lines is null)
        {
            yield return $"  overlay log: {path} (could not be read: {problem})";
            yield break;
        }

        int events = lines.Count(l => l.StartsWith('+'));
        yield return $"  overlay log: {path} ({events.ToString(CultureInfo.InvariantCulture)} event(s))";
        foreach (string line in lines.Where(IsWorthPrinting))
        {
            yield return "    " + line.Trim();
        }
    }

    private static string[]? TryReadLines(string path, out string? problem)
    {
        try
        {
            problem = null;
            return File.ReadAllLines(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            problem = e.GetType().Name;
            return null;
        }
    }

    /// <summary>Faults, the stop, unhook decisions, a missing symbol, supervision loss — never the routine installs.</summary>
    private static bool IsWorthPrinting(string line) =>
        line.StartsWith('+')
        && (line.Contains(" FAULT ", StringComparison.Ordinal)
            || line.Contains(" STOP ", StringComparison.Ordinal)
            || line.Contains(" UNHOOK_", StringComparison.Ordinal)
            || line.Contains(" SYMBOL_MISSING ", StringComparison.Ordinal)
            || line.Contains(" SUPERVISION_LOST ", StringComparison.Ordinal));
}
