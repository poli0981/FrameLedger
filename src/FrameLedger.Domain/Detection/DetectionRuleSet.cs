namespace FrameLedger.Domain.Detection;

/// <summary>
/// The detection half of <c>detection-rules.json</c>.
/// </summary>
/// <remarks>
/// There is deliberately no <c>anticheat</c> member anywhere in this model, and
/// a test asserts it stays that way. The blocklist has exactly one matcher and
/// it is native (§S15 item 1); a managed type that merely <em>held</em> the data
/// would be one refactor away from being a second one.
/// </remarks>
public sealed record DetectionRuleSet
{
    /// <summary>Always 2. A file declaring another version is refused, not coerced.</summary>
    public required int SchemaVersion { get; init; }

    /// <summary><c>YYYY.MM.N</c>. Part of the detection cache key.</summary>
    public required string RulesVersion { get; init; }

    /// <summary>
    /// Engine signatures. <strong>The list order is the precedence</strong> —
    /// first match wins (<c>05_DETECTION</c>).
    /// </summary>
    /// <remarks>
    /// Nothing else in the repository notices if this array is reordered, which
    /// is why the fixture corpus carries a directory with Unity markers
    /// <em>and</em> Unreal structure.
    /// </remarks>
    public required IReadOnlyList<EngineRule> Engines { get; init; }

    /// <summary>Platform signatures.</summary>
    public required IReadOnlyList<PlatformRule> Platforms { get; init; }

    /// <summary>Shipped-capability hints.</summary>
    public required IReadOnlyList<CapabilityRule> Capabilities { get; init; }
}
