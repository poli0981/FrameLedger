namespace FrameLedger.Domain.Detection;

/// <summary>
/// Everything the evaluator is allowed to know about a game directory, collected
/// once by the probe.
/// </summary>
/// <remarks>
/// <para>
/// The evaluator does no I/O. That keeps <c>Domain</c> free of package
/// references and makes every rule testable against a literal, but the honest
/// caveat is that the snapshot is <strong>rules-dependent</strong>: the probe
/// has to know which needles and which regexes to look for before it walks, so
/// this is not a general-purpose picture of a directory. <c>05_DETECTION</c>
/// says so alongside the signal-type list.
/// </para>
/// <para>
/// <see cref="UncollectedFacts"/> is the load-bearing member. A PE read that
/// failed must make <c>pe_company_contains</c> evaluate
/// <see cref="SignalOutcome.Unknown"/>, never <see cref="SignalOutcome.NoMatch"/>
/// — the same collapse of "could not look" into "looked and it was clean" that
/// the guard exists to avoid, applied to the inference side.
/// </para>
/// </remarks>
public sealed record GameFileSnapshot
{
    /// <summary>Full path to the game executable.</summary>
    public required string ExePath { get; init; }

    /// <summary>The exe's base name without extension — what <c>${ExeName}</c> expands to.</summary>
    public required string ExeNameWithoutExtension { get; init; }

    /// <summary>The directory the exe lives in, normalised with forward slashes.</summary>
    public required string GameDirectory { get; init; }

    /// <summary>Files beneath <see cref="GameDirectory"/>, relative and forward-slashed.</summary>
    public required IReadOnlyList<string> RelativeFiles { get; init; }

    /// <summary>Directories beneath <see cref="GameDirectory"/>, relative and forward-slashed.</summary>
    public required IReadOnlyList<string> RelativeDirectories { get; init; }

    /// <summary>PE <c>CompanyName</c>, or null if it could not be read.</summary>
    public string? PeCompanyName { get; init; }

    /// <summary>PE <c>ProductName</c>, or null if it could not be read.</summary>
    public string? PeProductName { get; init; }

    /// <summary>The executable's PE <c>FileVersion</c>, or null if it could not be read.</summary>
    public string? PeFileVersion { get; init; }

    /// <summary>
    /// <c>FileVersion</c> of the sibling files that version extractors name in
    /// their <c>from</c>, keyed by that value.
    /// </summary>
    /// <remarks>
    /// Separate from <see cref="PeFileVersion"/> because they are different
    /// files. Unity's rule reads <c>UnityPlayer.dll</c>, not the game
    /// executable; answering from the executable would report a version that is
    /// wrong rather than merely missing, which is worse.
    /// </remarks>
    public required IReadOnlyDictionary<string, string> SiblingFileVersions { get; init; }

    /// <summary>PE <c>ProductVersion</c>, or null if it could not be read.</summary>
    public string? PeProductVersion { get; init; }

    /// <summary>
    /// The <c>strings_contains</c> needles the rule set named that were actually
    /// found, in one bounded pass. Absence here means "looked and did not find",
    /// which is why a failed scan reports through
    /// <see cref="UncollectedFacts"/> instead.
    /// </summary>
    public required IReadOnlySet<string> MatchedStringNeedles { get; init; }

    /// <summary>Regex source to first capture group, precomputed by the probe.</summary>
    public required IReadOnlyDictionary<string, string> StringsRegexCaptures { get; init; }

    /// <summary>
    /// Store-manifest fields by dotted path. <strong>Empty in this phase</strong>
    /// — the extractors are not built, so <c>manifest_field</c> is listed in
    /// <see cref="UncollectedFacts"/> and evaluates Unknown rather than false.
    /// </summary>
    public required IReadOnlyDictionary<string, string> ManifestFields { get; init; }

    /// <summary>
    /// Signal types the probe could not establish. Anything listed here
    /// evaluates <see cref="SignalOutcome.Unknown"/> no matter what the other
    /// members say.
    /// </summary>
    public required IReadOnlySet<DetectionSignalType> UncollectedFacts { get; init; }
}
