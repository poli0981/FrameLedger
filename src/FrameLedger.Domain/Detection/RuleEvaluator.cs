using System.Text.RegularExpressions;

namespace FrameLedger.Domain.Detection;

/// <summary>
/// Evaluates the static-hint rules against a collected snapshot.
/// </summary>
/// <remarks>
/// <para>
/// Pure: no I/O, no clock, no ambient state. Everything it can know is in the
/// <see cref="GameFileSnapshot"/> it is handed, which is what lets the whole
/// tri-state table be tested against literals rather than a directory somebody
/// has to build.
/// </para>
/// <para>
/// <strong>A static hint may never set a runtime fact</strong>
/// (<c>05_DETECTION</c>). Nothing here produces an upscaler, a frame-generation
/// mode or an RT flag; a test asserts the result type has no such member.
/// </para>
/// </remarks>
public sealed class RuleEvaluator
{
    private static readonly TimeSpan _regexBudget = TimeSpan.FromMilliseconds(100);

    private readonly DetectionRuleSet _rules;

    /// <summary>Creates an evaluator over one rule set.</summary>
    /// <param name="rules">The parsed rules. Its list order is the precedence.</param>
    public RuleEvaluator(DetectionRuleSet rules) =>
        _rules = rules ?? throw new ArgumentNullException(nameof(rules));

    /// <summary>Evaluates one signal against the snapshot.</summary>
    /// <remarks>
    /// Static because a signal's answer depends only on the snapshot — the rule
    /// set decides which signals are asked, never what any one of them means.
    /// </remarks>
    public static SignalOutcome Evaluate(DetectionSignal signal, GameFileSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(signal);
        ArgumentNullException.ThrowIfNull(snapshot);

        // The probe could not establish this class of fact at all. That outranks
        // everything below: a needle missing from a scan that never ran is not
        // evidence of absence.
        if (snapshot.UncollectedFacts.Contains(signal.Type))
        {
            return SignalOutcome.Unknown;
        }

        string needle = Expand(signal.Value, snapshot);

        return signal.Type switch
        {
            // A hit survives an incomplete walk; a miss does not. We saw what we
            // saw — it is only absence that a short walk casts doubt on.
            DetectionSignalType.FileExists or DetectionSignalType.SiblingGlob =>
                Found(snapshot.RelativeFiles, needle, snapshot.FileListingComplete),
            DetectionSignalType.DirExists =>
                Found(snapshot.RelativeDirectories, needle, snapshot.FileListingComplete),
            DetectionSignalType.PathContains => Outcome(
                snapshot.GameDirectory.Contains(needle.Replace('\\', '/'), StringComparison.OrdinalIgnoreCase)),
            DetectionSignalType.PeCompanyContains => Contains(snapshot.PeCompanyName, needle),
            DetectionSignalType.PeProductContains => Contains(snapshot.PeProductName, needle),
            DetectionSignalType.StringsContains => Outcome(snapshot.MatchedStringNeedles.Contains(signal.Value)),

            // The extractors are not built this phase, so this is always
            // Unknown via UncollectedFacts above. Kept explicit so that adding
            // them is a change here rather than a silent behaviour flip.
            DetectionSignalType.ManifestField => signal.Field is not null &&
                                                 snapshot.ManifestFields.ContainsKey(signal.Field)
                ? SignalOutcome.Match
                : SignalOutcome.Unknown,
            _ => SignalOutcome.Unknown,
        };
    }

    /// <summary>Combines a group's signals.</summary>
    /// <remarks>
    /// <c>all</c>: any NoMatch ⇒ NoMatch; else any Unknown ⇒ Unknown; else Match.
    /// <c>any</c>: any Match ⇒ Match; else any Unknown ⇒ Unknown; else NoMatch.
    /// <para>
    /// Both put the decisive answer first and let Unknown win over the
    /// remainder. A group that cannot be fully evaluated is Unknown even when
    /// the signals it did read would have decided it the other way.
    /// </para>
    /// </remarks>
    public static SignalOutcome Evaluate(SignalGroup group, GameFileSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(group);
        ArgumentNullException.ThrowIfNull(snapshot);

        bool sawUnknown = false;
        foreach (DetectionSignal s in group.Signals)
        {
            SignalOutcome o = Evaluate(s, snapshot);
            if (group.Combinator == SignalCombinator.All && o == SignalOutcome.NoMatch)
            {
                return SignalOutcome.NoMatch;
            }

            if (group.Combinator == SignalCombinator.Any && o == SignalOutcome.Match)
            {
                return SignalOutcome.Match;
            }

            sawUnknown |= o == SignalOutcome.Unknown;
        }

        if (sawUnknown)
        {
            return SignalOutcome.Unknown;
        }

        return group.Combinator == SignalCombinator.All ? SignalOutcome.Match : SignalOutcome.NoMatch;
    }

    /// <summary>
    /// Walks the engines in order and returns the first that matches.
    /// </summary>
    /// <remarks>
    /// <para>
    /// First match wins, and the JSON array order <em>is</em> the precedence
    /// (<c>05_DETECTION</c>). Nothing else in the repository notices if that
    /// array is reordered, which is why the fixture corpus carries a case with
    /// Unity markers <em>and</em> Unreal structure.
    /// </para>
    /// <para>
    /// <strong>A rule that evaluates Unknown stops the walk.</strong> Falling
    /// through would let a later rule be reported as the first match when it was
    /// not — a wrong answer dressed as an ordered one. The caller gets
    /// <see cref="EngineMatch.Undetermined"/> naming the rule that could not be
    /// decided, and must leave any stored engine alone rather than writing
    /// "unknown" over it.
    /// </para>
    /// </remarks>
    public EngineMatch MatchEngine(GameFileSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        foreach (EngineRule rule in _rules.Engines)
        {
            switch (Evaluate(rule.Signals, snapshot))
            {
                case SignalOutcome.Match:
                    return EngineMatch.Matched(rule, ExtractVersion(rule.Version, snapshot));
                case SignalOutcome.Unknown:
                    return EngineMatch.Undetermined(rule.Id);
                default:
                    continue;
            }
        }

        return EngineMatch.NoEngine();
    }

    /// <summary>First platform whose signals match, or null. Same Unknown rule as engines.</summary>
    public PlatformRule? MatchPlatform(GameFileSnapshot snapshot, out bool undetermined)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        undetermined = false;
        foreach (PlatformRule rule in _rules.Platforms)
        {
            switch (Evaluate(rule.Signals, snapshot))
            {
                case SignalOutcome.Match:
                    return rule;
                case SignalOutcome.Unknown:
                    undetermined = true;
                    return null;
                default:
                    continue;
            }
        }

        return null;
    }

    /// <summary>
    /// Every capability whose signals match — all of them, not the first.
    /// </summary>
    /// <remarks>
    /// A game ships DLSS <em>and</em> FSR routinely, so unlike engines there is
    /// no precedence here and no early exit. These populate the "Supports" row
    /// and are never mixed with measured per-session values.
    /// </remarks>
    public IReadOnlyList<CapabilityRule> MatchCapabilities(GameFileSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        List<CapabilityRule> hits = [];
        foreach (CapabilityRule rule in _rules.Capabilities)
        {
            if (Evaluate(rule.Signals, snapshot) == SignalOutcome.Match)
            {
                hits.Add(rule);
            }
        }

        return hits;
    }

    private static string? ExtractVersion(VersionExtractor? extractor, GameFileSnapshot snapshot)
    {
        // An explicit null means "this engine has no version extractor" — a
        // statement, not a gap. Nothing to run, nothing to report.
        if (extractor is null)
        {
            return null;
        }

        return extractor.Type switch
        {
            // From the NAMED sibling, not the executable. Unity's rule reads
            // UnityPlayer.dll; answering from the game exe would report a
            // version that is wrong rather than absent.
            VersionExtractorType.PeFileVersion => extractor.From is not null &&
                                                  snapshot.SiblingFileVersions.TryGetValue(extractor.From, out string? fv)
                ? fv
                : null,
            VersionExtractorType.PeProductVersionRegex =>
                FirstGroup(extractor.Value, snapshot.PeProductVersion),
            VersionExtractorType.StringsRegex => extractor.Value is not null &&
                                                 snapshot.StringsRegexCaptures.TryGetValue(extractor.Value, out string? c)
                ? c
                : null,
            VersionExtractorType.ManifestField => extractor.Field is not null &&
                                                  snapshot.ManifestFields.TryGetValue(extractor.Field, out string? v)
                ? v
                : null,
            _ => null,
        };
    }

    private static string? FirstGroup(string? pattern, string? text)
    {
        if (pattern is null || text is null)
        {
            return null;
        }

        // Rules are updatable DATA, so a hostile or merely catastrophic pattern
        // is in scope. A timeout yields no version rather than hanging the
        // Agent; NonBacktracking is not used because the seed's patterns need
        // captures it does not support in every form.
        try
        {
            Match m = Regex.Match(text, pattern, RegexOptions.None, _regexBudget);
            return m.Success && m.Groups.Count > 1 ? m.Groups[1].Value : null;
        }
        catch (RegexMatchTimeoutException)
        {
            return null;
        }
        catch (ArgumentException)
        {
            return null;    // an unparseable pattern is bad data, not a crash
        }
    }

    private static string Expand(string value, GameFileSnapshot snapshot) =>
        value.Replace("${ExeName}", snapshot.ExeNameWithoutExtension, StringComparison.Ordinal);

    private static SignalOutcome Outcome(bool hit) => hit ? SignalOutcome.Match : SignalOutcome.NoMatch;

    private static SignalOutcome Contains(string? haystack, string needle) =>
        haystack is null
            ? SignalOutcome.Unknown
            : Outcome(haystack.Contains(needle, StringComparison.OrdinalIgnoreCase));

    private static SignalOutcome Found(IReadOnlyList<string> entries, string pattern, bool listingComplete)
    {
        foreach (string e in entries)
        {
            if (GlobMatch(pattern, e) || GlobMatch(pattern, LeafOf(e)))
            {
                return SignalOutcome.Match;
            }
        }

        // Not found. Whether that means "it is not there" depends entirely on
        // whether we finished looking.
        return listingComplete ? SignalOutcome.NoMatch : SignalOutcome.Unknown;
    }

    private static string LeafOf(string path)
    {
        int slash = path.LastIndexOf('/');
        return slash < 0 ? path : path[(slash + 1)..];
    }

    /// <summary>
    /// Case-insensitive glob supporting <c>*</c> only.
    /// </summary>
    /// <remarks>
    /// Deliberately not a regex. The capability signals are shapes like
    /// <c>ffx_fsr2_*.dll</c>, the seed contains no other metacharacter, and a
    /// regex engine fed updatable data is a bigger surface than the job needs.
    /// </remarks>
    private static bool GlobMatch(string pattern, string text)
    {
        int p = 0;
        int t = 0;
        int star = -1;
        int back = 0;

        while (t < text.Length)
        {
            if (p < pattern.Length && pattern[p] == '*')
            {
                star = p++;
                back = t;
            }
            else if (p < pattern.Length &&
                     char.ToLowerInvariant(pattern[p]) == char.ToLowerInvariant(text[t]))
            {
                p++;
                t++;
            }
            else if (star >= 0)
            {
                p = star + 1;
                t = ++back;
            }
            else
            {
                return false;
            }
        }

        while (p < pattern.Length && pattern[p] == '*')
        {
            p++;
        }

        return p == pattern.Length;
    }
}
