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
/// <b>Two matches refuse</b> — with one named exception. Picking one would be a guess about
/// which process the record was for, and a guess that resolves to an injection is the wrong
/// kind. The exception (2026-09-03) is a Chromium-based title: NW.js, Electron, RPG Maker
/// MV/MZ run several processes from ONE image path, every one of them is the consented
/// binary, and Chromium marks the one that owns the swapchain with its own
/// <c>--type=gpu-process</c> flag. That is not a guess; it is the vendor's label, read
/// through a kernel query (<see cref="ProcessCommandLine"/>), and it resolves only when
/// exactly one candidate carries it (<see cref="ChromiumGpuProcess"/>). Measured on
/// <i>Flower in Us</i>: three <c>Game.exe</c>, one path, <c>TargetAmbiguous</c>.
/// </para>
/// </remarks>
internal sealed class TargetResolver : ITargetResolver
{
    private readonly Func<int, string?> _commandLineOf;

    public TargetResolver() : this(ProcessCommandLine.TryRead)
    {
    }

    /// <summary>Test seam: how a candidate's command line is read.</summary>
    public TargetResolver(Func<int, string?> commandLineOf)
    {
        _commandLineOf = commandLineOf ?? throw new ArgumentNullException(nameof(commandLineOf));
    }

    public int? Resolve(string normalisedExePath, out SessionEndReason reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(normalisedExePath);

        List<int> matches = CollectMatches(normalisedExePath, out int unreadable);

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

        // THE ONE DISCRIMINATOR. Several readable processes of the consented image, and exactly
        // one of them is Chromium's GPU process by Chromium's own flag. An unreadable sibling
        // (unreadable > 0) never reaches this, because "could not look" must not narrow the set.
        if (unreadable == 0 && TryPickGpuProcess(matches) is int gpu)
        {
            reason = SessionEndReason.Running;
            return gpu;
        }

        // More than one match, or one match beside a process sharing the executable's NAME that we
        // could not identify. Both are ambiguity, and picking is a guess that resolves to an injection.
        reason = SessionEndReason.TargetAmbiguous;
        return null;
    }

    /// <summary>Every readable process whose image is the path; unreadable candidates are counted, never dropped.</summary>
    private static List<int> CollectMatches(string normalisedExePath, out int unreadable)
    {
        string name = Path.GetFileNameWithoutExtension(normalisedExePath);
        List<int> matches = [];

        // COUNTED, NOT JUST SKIPPED, and the difference is the whole invariant. Skipping alone is what
        // NARROWS: unreadable candidates vanish, so `matches.Count == 1` could not tell "one candidate
        // exists" from "one candidate is readable and the others were invisible" — and the second is
        // the case where we would inject into the wrong instance of the right game. The comment here
        // used to claim the skip prevented that; it caused it.
        unreadable = 0;

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

        return matches;
    }

    private int? TryPickGpuProcess(List<int> matches)
    {
        var candidates = new List<(int Pid, string? CommandLine)>(matches.Count);
        foreach (int pid in matches)
        {
            candidates.Add((pid, _commandLineOf(pid)));
        }

        int? gpu = ChromiumGpuProcess.Pick(candidates);
        if (gpu is not null)
        {
            HostConsole.Line($"target: {matches.Count} processes share the image; pid {gpu} is Chromium's "
                             + $"{ChromiumGpuProcess.Marker} and owns the swapchain, so it is the target");
        }

        return gpu;
    }
}
