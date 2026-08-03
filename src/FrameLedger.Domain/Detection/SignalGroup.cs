namespace FrameLedger.Domain.Detection;

/// <summary>
/// Exactly one of <c>any</c> / <c>all</c> over a flat list of signals.
/// </summary>
/// <remarks>
/// It holds signals, never groups. The schema forbids nesting in v2
/// (<c>maxProperties: 1</c> plus its own <c>$comment</c>), so that constraint is
/// expressed as a type here rather than as a validation rule someone has to
/// remember. It is also why two <c>05_DETECTION</c> engine rows — both RPG Maker
/// variants — cannot be expressed at all in this schema version.
/// </remarks>
public sealed record SignalGroup
{
    /// <summary>How the signals combine.</summary>
    public required SignalCombinator Combinator { get; init; }

    /// <summary>The signals. Never empty — the schema sets <c>minItems: 1</c>.</summary>
    public required IReadOnlyList<DetectionSignal> Signals { get; init; }
}
