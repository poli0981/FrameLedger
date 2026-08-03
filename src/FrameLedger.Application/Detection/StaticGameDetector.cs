using FrameLedger.Domain.Detection;

namespace FrameLedger.Application.Detection;

/// <summary>
/// Runs the static-hint rules over one game and reports what they established.
/// </summary>
/// <remarks>
/// <para>
/// Constructed by its caller, like <c>HookedCaptureGate</c>. There is no DI
/// container in this repository yet and this is the wrong consumer to introduce
/// one from; composition lands with the Generic Host.
/// </para>
/// <para>
/// It writes nothing. The result is a value the caller decides what to do with,
/// and <see cref="StaticDetectionResult.ShouldWrite"/> is the rule it must apply
/// before persisting any of it.
/// </para>
/// </remarks>
public sealed class StaticGameDetector(IDetectionRulesSource rulesSource, IGameFileProbe probe)
{
    private readonly IDetectionRulesSource _rulesSource =
        rulesSource ?? throw new ArgumentNullException(nameof(rulesSource));

    private readonly IGameFileProbe _probe = probe ?? throw new ArgumentNullException(nameof(probe));

    /// <summary>Detects engine, platform and shipped capabilities for one executable.</summary>
    public async ValueTask<StaticDetectionResult> DetectAsync(string exePath, CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(exePath);

        DetectionRuleSet rules = await _rulesSource.LoadAsync(ct).ConfigureAwait(false);
        GameFileSnapshot snapshot = await _probe.SnapshotAsync(exePath, rules, ct).ConfigureAwait(false);

        var evaluator = new RuleEvaluator(rules);
        EngineMatch engine = evaluator.MatchEngine(snapshot);
        PlatformRule? platform = evaluator.MatchPlatform(snapshot, out bool platformUndetermined);

        return new StaticDetectionResult
        {
            EngineId = engine.Rule?.Id,
            EngineVersion = engine.Version,
            EngineUndetermined = engine.IsUndetermined,
            PlatformId = platform?.Id,
            PlatformUndetermined = platformUndetermined,
            CapabilityIds = [.. evaluator.MatchCapabilities(snapshot).Select(c => c.Id)],
            RulesVersion = rules.RulesVersion,
        };
    }
}
