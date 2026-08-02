namespace FrameLedger.Application.AntiCheat;

/// <summary>What the Agent knows about a game before asking the guard.</summary>
public sealed record HookRequest
{
    public required int TargetPid { get; init; }
    public required string PayloadPath { get; init; }

    /// <summary>Off by default for every newly added game (<c>19_SAFETY</c>).</summary>
    public required bool HookEnabled { get; init; }

    /// <summary>Stamped by the Agent, never supplied by a client (<c>07_IPC</c>).</summary>
    public DateTimeOffset? ConsentedAt { get; init; }

    /// <summary><c>games.hook_blocked_reason</c>; non-null means the toggle is disabled.</summary>
    public string? BlockedReason { get; init; }
}
