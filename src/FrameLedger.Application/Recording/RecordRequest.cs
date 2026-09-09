using FrameLedger.Domain.Consent;
using FrameLedger.Domain.Sessions;

namespace FrameLedger.Application.Recording;

/// <summary>One session to record: which executable, how to reach it, and what to inject.</summary>
public sealed record RecordRequest
{
    public required string NormalisedExePath { get; init; }

    /// <summary>The executable as read from disk now; null when it could not be read (the session refuses, and says so).</summary>
    public required ExecutableFingerprint? Observed { get; init; }

    public required string PayloadPath { get; init; }

    public required CaptureMode Mode { get; init; }

    /// <summary>Launch mode: the game's own command line, verbatim.</summary>
    public string Arguments { get; init; } = string.Empty;

    /// <summary>What the <c>games</c> row is called when it has to be created; defaults to the file name.</summary>
    public string? GameName { get; init; }
}
