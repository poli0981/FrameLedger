using FrameLedger.Domain.Detection;

namespace FrameLedger.Domain.Tests;

/// <summary>
/// Builders for the detection tests. Literals rather than a filesystem: the
/// evaluator does no I/O, which is exactly what makes the tri-state table
/// testable without building a directory tree for every row.
/// </summary>
internal static class DetectionFixtures
{
    public static GameFileSnapshot Snapshot(
        IEnumerable<string>? files = null,
        IEnumerable<string>? dirs = null,
        string exe = @"C:/Games/Example/Game.exe",
        string exeName = "Game",
        string gameDir = "C:/Games/Example",
        string? company = "Example Studios",
        string? product = "Example Game",
        string? fileVersion = "1.2.3.4",
        string? productVersion = "++UE5+Release-5.3",
        IReadOnlyDictionary<string, string>? siblingVersions = null,
        IEnumerable<string>? needles = null,
        IReadOnlyDictionary<string, string>? captures = null,
        IReadOnlyDictionary<string, string>? manifest = null,
        bool listingComplete = true,
        IEnumerable<DetectionSignalType>? uncollected = null) =>
        new()
        {
            ExePath = exe,
            ExeNameWithoutExtension = exeName,
            GameDirectory = gameDir,
            RelativeFiles = files?.ToList() ?? [],
            RelativeDirectories = dirs?.ToList() ?? [],
            FileListingComplete = listingComplete,
            PeCompanyName = company,
            PeProductName = product,
            PeFileVersion = fileVersion,
            PeProductVersion = productVersion,
            SiblingFileVersions = siblingVersions ?? new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase),
            MatchedStringNeedles = new HashSet<string>(needles ?? [], StringComparer.Ordinal),
            StringsRegexCaptures = captures ?? new Dictionary<string, string>(StringComparer.Ordinal),
            ManifestFields = manifest ?? new Dictionary<string, string>(StringComparer.Ordinal),

            // manifest_field is uncollected by default because the store-manifest
            // extractors are not built this phase. Stating it here means a test
            // that expects a manifest answer has to say so deliberately.
            UncollectedFacts = new HashSet<DetectionSignalType>(
                uncollected ?? [DetectionSignalType.ManifestField]),
        };

    public static DetectionSignal Signal(DetectionSignalType type, string value, string? field = null) =>
        new() { Type = type, Value = value, Field = field };

    public static SignalGroup Group(SignalCombinator how, params DetectionSignal[] signals) =>
        new() { Combinator = how, Signals = signals };

    public static SignalGroup AnyOf(params DetectionSignal[] signals) =>
        Group(SignalCombinator.Any, signals);

    public static SignalGroup AllOf(params DetectionSignal[] signals) =>
        Group(SignalCombinator.All, signals);

    public static EngineRule Engine(string id, SignalGroup signals, VersionExtractor? version = null) =>
        new() { Id = id, Name = id, Signals = signals, Version = version };

    public static PlatformRule Platform(string id, SignalGroup signals) =>
        new() { Id = id, Name = id, Signals = signals };

    public static CapabilityRule Capability(string id, params string[] globs) =>
        new()
        {
            Id = id,
            Name = id,
            Signals = AnyOf([.. globs.Select(g => Signal(DetectionSignalType.SiblingGlob, g))]),
        };

    public static DetectionRuleSet Rules(
        IEnumerable<EngineRule>? engines = null,
        IEnumerable<PlatformRule>? platforms = null,
        IEnumerable<CapabilityRule>? capabilities = null) =>
        new()
        {
            SchemaVersion = 2,
            RulesVersion = "2026.08.1",
            Engines = engines?.ToList() ?? [],
            Platforms = platforms?.ToList() ?? [],
            Capabilities = capabilities?.ToList() ?? [],
        };
}
