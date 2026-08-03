namespace FrameLedger.Domain.Detection;

/// <summary>
/// A shipped-capability hint: files that say what the game <em>could</em> do.
/// </summary>
/// <remarks>
/// <para>
/// The schema carries these as a flat glob list rather than a signal group, and
/// calls unifying the two a schemaVersion-3 change. The reader normalises them
/// into an <c>any</c> group so the evaluator sees one shape; the difference is
/// kept where it belongs, at the data boundary.
/// </para>
/// <para>
/// These populate the "Supports" row and are <strong>never</strong> mixed with
/// measured per-session values. "Supports DLSS-G" and "Frame Generation: DLSS-G
/// ×1.9" are different claims, and conflating them is the confusion the old
/// design created.
/// </para>
/// </remarks>
public sealed record CapabilityRule
{
    /// <summary>Stable id, e.g. <c>dlss_g</c>.</summary>
    public required string Id { get; init; }

    /// <summary>Display name.</summary>
    public required string Name { get; init; }

    /// <summary>Sibling-file globs, normalised from the flat list.</summary>
    public required SignalGroup Signals { get; init; }
}
