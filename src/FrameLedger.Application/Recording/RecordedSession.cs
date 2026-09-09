using FrameLedger.Application.Capture;
using FrameLedger.Application.Persistence;
using FrameLedger.Domain.Sessions;

namespace FrameLedger.Application.Recording;

/// <summary>What one recorded session came to: the loop's outcome, the row as built, and what was done with it.</summary>
public sealed record RecordedSession
{
    public required Guid SessionGuid { get; init; }

    public required CaptureOutcome Outcome { get; init; }

    /// <summary>The row as finalized (Tier 1: every measured column filled) — whether or not it was stored.</summary>
    public required SessionRow Row { get; init; }

    public required ExitStatus ExitStatus { get; init; }

    public required FinalizeOutcome Finalize { get; init; }

    public required CrashPolicyOutcome CrashPolicy { get; init; }

    /// <summary>Whether the event log named the executable in the crash window.</summary>
    public required bool CrashEventFound { get; init; }
}
