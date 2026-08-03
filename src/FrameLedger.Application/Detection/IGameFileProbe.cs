using FrameLedger.Domain.Detection;

namespace FrameLedger.Application.Detection;

/// <summary>Collects everything the evaluator is allowed to know about a game.</summary>
/// <remarks>
/// Takes the rule set because the snapshot is <strong>rules-dependent</strong>:
/// the strings pass has to know which needles and regexes to look for before it
/// walks, so this is not a general-purpose picture of a directory. Anything the
/// probe could not establish must come back in
/// <see cref="GameFileSnapshot.UncollectedFacts"/> rather than as an absence.
/// </remarks>
public interface IGameFileProbe
{
    /// <summary>Walks the game directory once and returns what it found.</summary>
    ValueTask<GameFileSnapshot> SnapshotAsync(string exePath, DetectionRuleSet rules, CancellationToken ct = default);
}
