using System.Diagnostics;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// Resolves a target by image path only.
/// </summary>
/// <remarks>
/// <para>
/// <b>The absence of a pid parameter is the point.</b> §S27's gap was a
/// user-named pid on a binary with no consent record — "never automatic",
/// automatically. Resolving the target from the same normalised path the consent
/// record is keyed on makes the class of bug where consent is granted for binary A
/// and injection aimed at pid B inexpressible, rather than merely discouraged.
/// </para>
/// <para>
/// <b>Two matches refuse.</b> Picking one would be a guess about which process the
/// record was for, and a guess that resolves to an injection is the wrong kind.
/// </para>
/// </remarks>
internal sealed class TargetResolver : ITargetResolver
{
    public int? Resolve(string normalisedExePath, out SessionEndReason reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);

        string name = Path.GetFileNameWithoutExtension(normalisedExePath);
        List<int> matches = [];

        // COUNTED, NOT JUST SKIPPED, and the difference is the whole invariant. Skipping alone is what
        // NARROWS: unreadable candidates vanish, so `matches.Count == 1` could not tell "one candidate
        // exists" from "one candidate is readable and the others were invisible" — and the second is
        // the case where we would inject into the wrong instance of the right game. The comment here
        // used to claim the skip prevented that; it caused it.
        int unreadable = 0;

        foreach (Process p in Process.GetProcessesByName(name))
        {
            try
            {
                // MainModule needs rights we may not have for another user's or an elevated process.
                // A process we cannot read is NOT a match — "could not look" must not widen the set —
                // but it is counted below so it cannot narrow one either.
                string? image = p.MainModule?.FileName;
                if (image is not null
                    && string.Equals(ExecutableIdentity.Normalise(image), normalisedExePath,
                        StringComparison.OrdinalIgnoreCase))
                {
                    matches.Add(p.Id);
                }
            }
            catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception)
            {
                // Exited between enumeration and the read, or not ours to read. Either way we did not
                // establish whether it is our target.
                unreadable++;
            }
            finally
            {
                p.Dispose();
            }
        }

        if (matches.Count == 0)
        {
            reason = unreadable > 0 ? SessionEndReason.TargetAmbiguous : SessionEndReason.TargetNotRunning;
            return null;
        }

        if (matches.Count == 1 && unreadable == 0)
        {
            reason = SessionEndReason.Running;
            return matches[0];
        }

        // More than one match, or one match beside a process sharing the executable's NAME that we
        // could not identify. Both are ambiguity, and picking is a guess that resolves to an injection.
        reason = SessionEndReason.TargetAmbiguous;
        return null;
    }
}
