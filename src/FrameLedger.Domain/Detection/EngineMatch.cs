namespace FrameLedger.Domain.Detection;

/// <summary>The outcome of walking the engine rules in order.</summary>
/// <remarks>
/// Three outcomes, not two. <see cref="IsUndetermined"/> exists because a rule
/// that could not be evaluated must not fall through to a later one — the later
/// match would then be reported as first-match-wins when it was not.
/// </remarks>
public readonly record struct EngineMatch
{
    private EngineMatch(EngineRule? rule, string? version, string? undeterminedBy)
    {
        Rule = rule;
        Version = version;
        UndeterminedBy = undeterminedBy;
    }

    /// <summary>The engine that matched, or null.</summary>
    public EngineRule? Rule { get; }

    /// <summary>Extracted version, or null when there is no extractor or it yielded nothing.</summary>
    public string? Version { get; }

    /// <summary>Id of the rule that could not be decided, when undetermined.</summary>
    public string? UndeterminedBy { get; }

    /// <summary>True when the walk stopped on a rule it could not evaluate.</summary>
    public bool IsUndetermined => UndeterminedBy is not null;

    /// <summary>An engine was identified.</summary>
    public static EngineMatch Matched(EngineRule rule, string? version) => new(rule, version, null);

    /// <summary>Every rule cleanly missed. This is a real answer, and a writable one.</summary>
    public static EngineMatch NoEngine() => new(null, null, null);

    /// <summary>
    /// The walk stopped on a rule it could not evaluate. The caller must leave
    /// any stored engine alone — this is not "no engine", and writing
    /// <c>"unknown"</c> over a good value would be worse than writing nothing.
    /// </summary>
    public static EngineMatch Undetermined(string ruleId) => new(null, null, ruleId);
}
