using FrameLedger.Application.Capture;
using FrameLedger.Domain.Sessions;

namespace FrameLedger.Application.Recording;

/// <summary>
/// <c>04_CAPTURE</c> §Crash &amp; exit classification, as one function of what the session saw:
/// how it ended, the process's exit code, and whether the event log names the executable.
/// </summary>
/// <remarks>
/// <para>
/// The crash-witness window is <c>[start, end + 30 s]</c>; the caller asks the
/// <see cref="ICrashEventSource"/> with exactly that and hands the answer in. The precedence is
/// the document's: our own safety stop is <c>unhooked_safety</c> whatever the process did next; the
/// capture side stopping on its own is <c>degraded</c>; a non-zero exit code or an event is
/// <c>crashed</c>; a target that is still running when the session ends (a bounded capture, a
/// supervision fault) is <c>normal</c> — nothing about the game went wrong.
/// </para>
/// <para>
/// <c>interrupted</c> is never produced here: it is the recovery path's, for a session whose Agent
/// died (<c>PartialRecovery</c>).
/// </para>
/// </remarks>
public static class ExitStatusMapper
{
    /// <summary>How long after the session's end an event may still be attributed to it.</summary>
    public static readonly TimeSpan CrashWitnessGrace = TimeSpan.FromSeconds(30);

    public static ExitStatus Map(SessionEndReason reason, int? exitCode, bool crashEventFound)
    {
        switch (reason)
        {
            case SessionEndReason.SafetyUnhook:
                return ExitStatus.UnhookedSafety;
            case SessionEndReason.WriterSelfDisabled:
            case SessionEndReason.WriterStoppedBlocklisted:
            case SessionEndReason.SupervisionLost:
            case SessionEndReason.WriterNeverInstalledHooks:
                return ExitStatus.Degraded;
            default:
                return exitCode is { } code && code != 0 || crashEventFound ? ExitStatus.Crashed : ExitStatus.Normal;
        }
    }

    /// <summary>
    /// What <c>sessions.capture_notes</c> carries about the end: the fine reason the four statuses
    /// collapse, the exit code when there was one, the witness when there was one.
    /// </summary>
    public static string Describe(SessionEndReason reason, int? exitCode, bool crashEventFound)
    {
        string note = "end=" + reason;
        if (exitCode is { } code)
        {
            note += "; exit_code=" + code.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }

        if (crashEventFound)
        {
            note += "; crash_event=application_log";
        }

        return note;
    }
}
