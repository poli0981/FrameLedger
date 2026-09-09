namespace FrameLedger.Domain.Metrics;

/// <summary>
/// A refusal and the numbers its explanation needs. The English lives with the report, not here
/// (CLAUDE.md: user-visible strings come from <c>.resx</c>; Domain carries the fact).
/// </summary>
/// <param name="Kind">What was refused and why.</param>
/// <param name="Subject">Which ratio.</param>
/// <param name="Count">Samples, streams or presents, per <paramref name="Kind"/>.</param>
/// <param name="BucketIndex">Zero-based bucket that departed, for <see cref="FgRefusalKind.NonUniform"/>.</param>
/// <param name="BucketCount">How many buckets the window was split into.</param>
/// <param name="BucketValue">That bucket's ratio; infinity when it had no weight at all.</param>
/// <param name="Overall">The whole window's ratio.</param>
public sealed record FgRefusal(
    FgRefusalKind Kind,
    FgRefusalSubject Subject,
    int Count = 0,
    int BucketIndex = 0,
    int BucketCount = 0,
    double BucketValue = 0,
    double Overall = 0);
