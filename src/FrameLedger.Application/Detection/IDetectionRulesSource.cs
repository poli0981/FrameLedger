using FrameLedger.Domain.Detection;

namespace FrameLedger.Application.Detection;

/// <summary>Supplies the parsed detection rules.</summary>
/// <remarks>
/// <para>
/// <strong>No path parameter, deliberately.</strong> §S3 removed the rules
/// source as a parameter from the hard gate, on the grounds that letting a
/// caller name the rules file is a documented override of it. The detection
/// half is not a gate and could not be overridden into unsafety — but two
/// answers to "where do rules live" is how the guard and the layer end up
/// reading different files, so the shape is kept.
/// </para>
/// </remarks>
public interface IDetectionRulesSource
{
    /// <summary>Loads and validates the rule set.</summary>
    /// <exception cref="InvalidOperationException">The rules are absent, unparseable, or not schema version 2.</exception>
    ValueTask<DetectionRuleSet> LoadAsync(CancellationToken ct = default);
}
