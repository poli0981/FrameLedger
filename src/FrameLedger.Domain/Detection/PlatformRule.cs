namespace FrameLedger.Domain.Detection;

/// <summary>A store signature. No version — the schema gives platforms none.</summary>
public sealed record PlatformRule
{
    /// <summary>Stable id.</summary>
    public required string Id { get; init; }

    /// <summary>Display name.</summary>
    public required string Name { get; init; }

    /// <summary>What identifies this platform on disk.</summary>
    public required SignalGroup Signals { get; init; }
}
