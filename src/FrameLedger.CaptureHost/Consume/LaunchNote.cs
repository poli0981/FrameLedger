using System.Globalization;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// Launch mode's cost, as the report reads it: how long the guard waited before it injected, and how many
/// presents DXGI had already counted on the first hooked chain when the hook saw its first one — the number
/// <c>20_OPEN_QUESTIONS</c> §S1 said nobody had.
/// </summary>
internal static class LaunchNote
{
    /// <summary>One line when the session was launched, and one line always.</summary>
    public static IEnumerable<string> Describe(TimeSpan? launchWait, FlWriterState writer)
    {
        if (launchWait is TimeSpan wait)
        {
            yield return "  launch: the guard injected "
                         + ((long)wait.TotalMilliseconds).ToString(CultureInfo.InvariantCulture)
                         + " ms after the process was started (it waited for a presentation runtime to map, then ran every check)";
        }

        yield return writer.DxgiPresentsBeforeHook == FlWriterState.DxgiPresentsBeforeHookNotRead
            ? "  presents before the first hooked present: not read (the hook saw no present)"
            : "  presents before the first hooked present: "
              + writer.DxgiPresentsBeforeHook.ToString(CultureInfo.InvariantCulture)
              + " (DXGI's count on the first hooked chain; 0 means nothing ran unhooked on it -- §S1's number)";
    }
}
