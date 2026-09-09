using System.Diagnostics.Eventing.Reader;
using System.Globalization;
using FrameLedger.Application.Recording;

namespace FrameLedger.Infrastructure.Recording;

/// <summary>
/// The Application log's Application Error (1000) and Windows Error Reporting (1001) events, filtered to
/// the window and to records that name the executable (<c>04_CAPTURE</c> §Crash &amp; exit classification).
/// </summary>
/// <remarks>
/// Read-only, unprivileged (the Application log is readable by any interactive user), bounded by the
/// window in the query itself so a busy log is not walked. A log that cannot be read answers false —
/// absence of evidence — and the exit code still decides on its own.
/// </remarks>
public sealed class EventLogCrashSource : ICrashEventSource
{
    private const string _log = "Application";

    public bool FoundCrash(string exeFileName, DateTimeOffset windowStart, DateTimeOffset windowEnd)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(exeFileName);
        string from = windowStart.UtcDateTime.ToString("o", CultureInfo.InvariantCulture);
        string to = windowEnd.UtcDateTime.ToString("o", CultureInfo.InvariantCulture);
        string xpath = $"*[System[(EventID=1000 or EventID=1001) and TimeCreated[@SystemTime>='{from}' and @SystemTime<='{to}']]]";
        try
        {
            using var reader = new EventLogReader(new EventLogQuery(_log, PathType.LogName, xpath));
            for (EventRecord? record = reader.ReadEvent(); record is not null; record = reader.ReadEvent())
            {
                using (record)
                {
                    if (Names(record, exeFileName))
                    {
                        return true;
                    }
                }
            }

            return false;
        }
        catch (Exception ex) when (ex is EventLogException or UnauthorizedAccessException or InvalidOperationException)
        {
            return false;
        }
    }

    /// <summary>The executable appears among the event's properties (1000's first field is the faulting application's name).</summary>
    private static bool Names(EventRecord record, string exeFileName)
    {
        foreach (EventProperty property in record.Properties)
        {
            if (property.Value is string s && s.Contains(exeFileName, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }
}
