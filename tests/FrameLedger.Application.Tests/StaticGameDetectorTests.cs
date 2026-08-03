using FluentAssertions;
using FrameLedger.Application.Detection;
using FrameLedger.Domain.Detection;

namespace FrameLedger.Application.Tests;

/// <summary>
/// The use case that composes the rules source, the probe and the evaluator.
/// </summary>
public sealed class StaticGameDetectorTests
{
    private sealed class ScriptedRules(DetectionRuleSet rules) : IDetectionRulesSource
    {
        public int Calls { get; private set; }

        public ValueTask<DetectionRuleSet> LoadAsync(CancellationToken ct = default)
        {
            Calls++;
            return ValueTask.FromResult(rules);
        }
    }

    private sealed class ScriptedProbe(GameFileSnapshot snapshot) : IGameFileProbe
    {
        public DetectionRuleSet? SawRules { get; private set; }

        public ValueTask<GameFileSnapshot> SnapshotAsync(string exePath, DetectionRuleSet rules,
            CancellationToken ct = default)
        {
            SawRules = rules;
            return ValueTask.FromResult(snapshot);
        }
    }

    private static SignalGroup Any(params string[] globs) => new()
    {
        Combinator = SignalCombinator.Any,
        Signals = [.. globs.Select(g => new DetectionSignal
        {
            Type = DetectionSignalType.SiblingGlob,
            Value = g,
        })],
    };

    private static DetectionRuleSet Rules() => new()
    {
        SchemaVersion = 2,
        RulesVersion = "2026.08.1",
        Engines =
        [
            new EngineRule { Id = "unity", Name = "Unity", Signals = Any("UnityPlayer.dll"), Version = null },
            new EngineRule { Id = "godot", Name = "Godot", Signals = Any("*.pck"), Version = null },
        ],
        Platforms = [new PlatformRule { Id = "steam", Name = "Steam", Signals = Any("steam_api64.dll") }],
        Capabilities =
        [
            new CapabilityRule { Id = "dlss", Name = "DLSS", Signals = Any("nvngx_dlss.dll") },
            new CapabilityRule { Id = "fsr", Name = "FSR", Signals = Any("ffx_fsr2_*.dll") },
        ],
    };

    private static GameFileSnapshot Snapshot(params string[] files) => new()
    {
        ExePath = "C:/Games/Example/Game.exe",
        ExeNameWithoutExtension = "Game",
        GameDirectory = "C:/Games/Example",
        RelativeFiles = files,
        RelativeDirectories = [],
        FileListingComplete = true,
        SiblingFileVersions = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase),
        MatchedStringNeedles = new HashSet<string>(StringComparer.Ordinal),
        StringsRegexCaptures = new Dictionary<string, string>(StringComparer.Ordinal),
        ManifestFields = new Dictionary<string, string>(StringComparer.Ordinal),
        UncollectedFacts = new HashSet<DetectionSignalType> { DetectionSignalType.ManifestField },
    };

    [Fact]
    public async Task ItReportsEnginePlatformAndEveryCapability()
    {
        var detector = new StaticGameDetector(
            new ScriptedRules(Rules()),
            new ScriptedProbe(Snapshot("UnityPlayer.dll", "steam_api64.dll", "nvngx_dlss.dll", "ffx_fsr2_x64.dll")));

        StaticDetectionResult r = await detector.DetectAsync(@"C:\Games\Example\Game.exe", TestContext.Current.CancellationToken);

        r.EngineId.Should().Be("unity");
        r.PlatformId.Should().Be("steam");
        r.CapabilityIds.Should().BeEquivalentTo(["dlss", "fsr"]);
        r.RulesVersion.Should().Be("2026.08.1");
        r.EngineUndetermined.Should().BeFalse();
    }

    [Fact]
    public async Task ACleanDirectoryYieldsNothing_WhichIsAnAnswer()
    {
        var detector = new StaticGameDetector(new ScriptedRules(Rules()), new ScriptedProbe(Snapshot("Game.exe")));

        StaticDetectionResult r = await detector.DetectAsync(@"C:\Games\Example\Game.exe", TestContext.Current.CancellationToken);

        r.EngineId.Should().BeNull();
        r.PlatformId.Should().BeNull();
        r.CapabilityIds.Should().BeEmpty();
        r.EngineUndetermined.Should().BeFalse("every rule cleanly missed; that is knowledge, not a gap");
    }

    [Fact]
    public async Task TheProbeIsGivenTheRules_BecauseTheSnapshotDependsOnThem()
    {
        // The strings pass has to know its needles before it reads. If this ever
        // stops being true the evaluator could become genuinely pure — but until
        // then, pretending otherwise is how a needle silently never gets looked
        // for.
        var probe = new ScriptedProbe(Snapshot("Game.exe"));
        var detector = new StaticGameDetector(new ScriptedRules(Rules()), probe);

        await detector.DetectAsync(@"C:\Games\Example\Game.exe", TestContext.Current.CancellationToken);

        probe.SawRules.Should().NotBeNull();
        probe.SawRules!.RulesVersion.Should().Be("2026.08.1");
    }

    [Fact]
    public async Task AnUndeterminedEngineWalkIsReportedAsSuch_NotAsNoEngine()
    {
        DetectionRuleSet rules = Rules() with
        {
            Engines =
            [
                new EngineRule
                {
                    Id = "needs_pe",
                    Name = "Needs PE",
                    Signals = new SignalGroup
                    {
                        Combinator = SignalCombinator.Any,
                        Signals = [new DetectionSignal
                        {
                            Type = DetectionSignalType.PeCompanyContains,
                            Value = "Whoever",
                        }],
                    },
                    Version = null,
                },
                new EngineRule { Id = "unity", Name = "Unity", Signals = Any("UnityPlayer.dll"), Version = null },
            ],
        };
        var detector = new StaticGameDetector(
            new ScriptedRules(rules), new ScriptedProbe(Snapshot("UnityPlayer.dll")));

        StaticDetectionResult r = await detector.DetectAsync(@"C:\Games\Example\Game.exe", TestContext.Current.CancellationToken);

        r.EngineUndetermined.Should().BeTrue();
        r.EngineId.Should().BeNull("a later rule is not the first match when an earlier one could not be decided");
    }

    [Fact]
    public async Task ItRejectsAnEmptyExePathRatherThanProbingNothing()
    {
        var detector = new StaticGameDetector(new ScriptedRules(Rules()), new ScriptedProbe(Snapshot()));

        await ((Func<Task>)(() => detector.DetectAsync("  ", TestContext.Current.CancellationToken).AsTask()))
            .Should().ThrowAsync<ArgumentException>();
    }

    [Fact]
    public void ItRejectsNullCollaborators()
    {
        ((Action)(() => _ = new StaticGameDetector(null!, new ScriptedProbe(Snapshot()))))
            .Should().Throw<ArgumentNullException>();
        ((Action)(() => _ = new StaticGameDetector(new ScriptedRules(Rules()), null!)))
            .Should().Throw<ArgumentNullException>();
    }
}
