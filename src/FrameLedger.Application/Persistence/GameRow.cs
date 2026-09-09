using FrameLedger.Domain.Consent;

namespace FrameLedger.Application.Persistence;

/// <summary>
/// The non-consent face of a <c>games</c> row. The consent columns are reached only through
/// <c>IGameConsentStore</c>, so the reviewed consent surface stays exactly as it is.
/// </summary>
public sealed record GameRow
{
    public required long Id { get; init; }

    public required string Name { get; init; }

    /// <summary>The normalised full path, and what detects that the binary changed (null halves = not recorded).</summary>
    public required ExecutableFingerprint Fingerprint { get; init; }

    public required bool HookEnabled { get; init; }

    public string? HookBlockedReason { get; init; }

    public string? HookAutoDisabledReason { get; init; }

    public required int HookCrashCount { get; init; }

    public DateTimeOffset? HookLastInjectedAt { get; init; }

    public required DateTimeOffset AddedAt { get; init; }

    public required DateTimeOffset UpdatedAt { get; init; }
}
