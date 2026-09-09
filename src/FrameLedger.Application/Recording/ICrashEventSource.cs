namespace FrameLedger.Application.Recording;

/// <summary>
/// The second witness for <c>crashed</c> (<c>04_CAPTURE</c> §Crash &amp; exit classification): an
/// Application Error (1000) or WER (1001) event naming the executable inside <c>[start, end + 30 s]</c>.
/// A process that was killed, or that returned 0 after an unhandled exception's handler, exits with a
/// code that says nothing; the event log still does.
/// </summary>
public interface ICrashEventSource
{
    /// <summary>Whether such an event exists; false when the log cannot be read — absence of evidence, not a crash.</summary>
    bool FoundCrash(string exeFileName, DateTimeOffset windowStart, DateTimeOffset windowEnd);
}
