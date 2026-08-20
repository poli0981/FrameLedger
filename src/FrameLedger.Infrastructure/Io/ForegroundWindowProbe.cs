using System.Runtime.InteropServices;

namespace FrameLedger.Infrastructure.Io;

/// <summary>
/// Does a given process own the foreground window right now?
/// </summary>
/// <remarks>
/// <para>
/// <b>Why a performance tool cares.</b> Frame generation stops while a title is unfocused, and
/// an unfocused interval is not a measurement of the title's performance in any case. Measured
/// 2026-08-16 on Cyberpunk 2077: the operator alt-tabbed during a ×2 capture, so the window
/// mixed intervals at 2.00 with intervals near 1.00 and the achieved <c>presents / batch</c>
/// averaged <b>1.84</b>. The number was wrong by 8% and nothing anywhere said so.
/// </para>
/// <para>
/// <b>This is attribution, not the guard.</b> <c>FgWindow.BatchRefusal</c> is what CATCHES a
/// non-uniform window, from the records alone, and it does so whether or not anybody sampled
/// focus — a guard that can be defeated by a caller forgetting to wire an input is not a guard.
/// What focus adds is the CAUSE, which is the difference between "bucket 3 disagrees" and
/// "bucket 3 disagrees because you switched away from the game".
/// </para>
/// <para>
/// <b>Out of process, and it reads nothing belonging to the target.</b> Two documented Win32
/// calls about window ownership; no handle to the game, no memory, nothing that touches
/// CLAUDE.md rule 4. In <c>Infrastructure</c> because §Solution layout puts every P/Invoke on
/// this side of the line.
/// </para>
/// <para>
/// <b>False is not "the operator switched away".</b> A process that owns no top-level window at
/// all — <c>hook-harness</c> presents to a composition swapchain and has none — is false on
/// every tick, and so is a process on another desktop or behind a lock screen. Telling those
/// apart from an alt-tab needs the SESSION's history, not one sample, which is why
/// <c>CaptureResult</c> carries the foreground tick COUNT and the report refuses to call zero
/// an alt-tab.
/// </para>
/// </remarks>
public static class ForegroundWindowProbe
{
    /// <summary>
    /// The process owning the foreground window, or 0 when there is none.
    /// </summary>
    /// <remarks>
    /// <para>
    /// A zero <c>HWND</c> is a real state, not an error: there is no foreground window while the
    /// secure desktop is up, on a session with no interactive desktop at all, and during some
    /// window transitions.
    /// </para>
    /// <para>
    /// <b>Public because <see cref="IsForeground"/> alone cannot be tested without asserting
    /// against itself.</b> Every check phrased over <c>IsForeground</c> reduces to comparing the
    /// probe's answer with the probe's answer; asking it to NAME the owner makes one real claim
    /// a test can falsify — that the value it read is a live process id. Reading
    /// <c>GetWindowThreadProcessId</c>'s RETURN value (a thread id) instead of its out parameter
    /// compiles, type-checks, and is wrong, and nothing phrased over the boolean would see it.
    /// </para>
    /// </remarks>
    public static uint ForegroundProcessId()
    {
        nint hwnd = GetForegroundWindow();
        if (hwnd == 0)
        {
            return 0;
        }

        return GetWindowThreadProcessId(hwnd, out uint owner) != 0 ? owner : 0;
    }

    /// <summary>True when <paramref name="pid"/> owns the foreground window.</summary>
    public static bool IsForeground(int pid) => pid > 0 && ForegroundProcessId() == (uint)pid;

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("user32.dll")]
    private static extern nint GetForegroundWindow();

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetWindowThreadProcessId(nint hWnd, out uint processId);
}
