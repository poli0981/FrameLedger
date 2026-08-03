using FluentAssertions;
using FrameLedger.Domain.Detection;
using static FrameLedger.Domain.Tests.DetectionFixtures;

namespace FrameLedger.Domain.Tests;

/// <summary>Every signal type, the ordered engine walk, and version extraction.</summary>
public sealed class RuleEvaluatorTests
{
    // ---- signal types -------------------------------------------------------

    [Fact]
    public void SiblingGlob_MatchesOnTheLeafAndOnTheRelativePath()
    {
        GameFileSnapshot s = Snapshot(files: ["Engine/Binaries/UnityPlayer.dll"]);

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "UnityPlayer.dll"), s)
            .Should().Be(SignalOutcome.Match);
        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "Engine/Binaries/UnityPlayer.dll"), s)
            .Should().Be(SignalOutcome.Match);
    }

    [Fact]
    public void SiblingGlob_HonoursWildcards_AndDoesNotMatchEverything()
    {
        GameFileSnapshot s = Snapshot(files: ["ffx_fsr2_api_x64.dll"]);

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "ffx_fsr2_*.dll"), s)
            .Should().Be(SignalOutcome.Match);
        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "libxess*.dll"), s)
            .Should().Be(SignalOutcome.NoMatch);
    }

    [Fact]
    public void Globbing_IsCaseInsensitive_BothWays()
    {
        GameFileSnapshot s = Snapshot(files: ["NVNGX_DLSS.dll"]);

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "nvngx_dlss.dll"), s)
            .Should().Be(SignalOutcome.Match);
        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "nvngx_dlssg.dll"), s)
            .Should().Be(SignalOutcome.NoMatch);
    }

    [Fact]
    public void DirExists_ExpandsExeName()
    {
        // `${ExeName}_Data` is how the Unity rule is written in the seed.
        GameFileSnapshot s = Snapshot(dirs: ["Game_Data"], exeName: "Game");

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.DirExists, "${ExeName}_Data"), s)
            .Should().Be(SignalOutcome.Match);

        GameFileSnapshot other = Snapshot(dirs: ["Game_Data"], exeName: "Other");
        RuleEvaluator.Evaluate(Signal(DetectionSignalType.DirExists, "${ExeName}_Data"), other)
            .Should().Be(SignalOutcome.NoMatch);
    }

    [Fact]
    public void PathContains_IsCaseInsensitiveAndSeparatorAgnostic()
    {
        GameFileSnapshot s = Snapshot(gameDir: "D:/SteamLibrary/steamapps/common/Example");

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.PathContains, @"steamapps\common"), s)
            .Should().Be(SignalOutcome.Match);
        RuleEvaluator.Evaluate(Signal(DetectionSignalType.PathContains, @"GOG Galaxy\Games"), s)
            .Should().Be(SignalOutcome.NoMatch);
    }

    [Fact]
    public void StringsContains_UsesTheUnexpandedNeedle_BecauseThatIsWhatTheProbeWasAsked()
    {
        GameFileSnapshot s = Snapshot(needles: ["Godot Engine v"]);

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.StringsContains, "Godot Engine v"), s)
            .Should().Be(SignalOutcome.Match);
        RuleEvaluator.Evaluate(Signal(DetectionSignalType.StringsContains, "CryEngine"), s)
            .Should().Be(SignalOutcome.NoMatch);
    }

    [Fact]
    public void ManifestField_IsUnknownThisPhase_NotFalse()
    {
        // The store-manifest extractors are out of scope, so every manifest_field
        // signal is Unknown. Asserted rather than left implicit: the day the
        // extractors land, this test should start failing and be revisited.
        GameFileSnapshot s = Snapshot();

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.ManifestField, "appid", field: "appid"), s)
            .Should().Be(SignalOutcome.Unknown);
    }

    // ---- the ordered engine walk -------------------------------------------

    [Fact]
    public void EngineWalk_IsFirstMatchWins_InListOrder()
    {
        // The Unity-markers-and-UE-structure case from 14_TESTING, as a unit
        // test. The corpus carries the same case as a real directory.
        DetectionRuleSet rules = Rules(engines:
        [
            Engine("unity", AnyOf(Signal(DetectionSignalType.SiblingGlob, "UnityPlayer.dll"))),
            Engine("unreal", AnyOf(Signal(DetectionSignalType.SiblingGlob, "*-Win64-Shipping.exe"))),
        ]);
        GameFileSnapshot both = Snapshot(files: ["UnityPlayer.dll", "Game-Win64-Shipping.exe"]);

        new RuleEvaluator(rules).MatchEngine(both).Rule!.Id.Should().Be("unity");

        // Reversing the rules reverses the answer — which is what makes the
        // array order load-bearing rather than incidental.
        DetectionRuleSet reversed = Rules(engines: [.. rules.Engines.Reverse()]);
        new RuleEvaluator(reversed).MatchEngine(both).Rule!.Id.Should().Be("unreal");
    }

    [Fact]
    public void EngineWalk_UnknownStopsTheWalk_RatherThanFallingThrough()
    {
        // If the first rule cannot be decided, a later match is NOT the first
        // match. Reporting it as one would be a wrong answer wearing the shape
        // of an ordered one.
        DetectionRuleSet rules = Rules(engines:
        [
            Engine("undecidable", AnyOf(Signal(DetectionSignalType.PeCompanyContains, "Whoever"))),
            Engine("unity", AnyOf(Signal(DetectionSignalType.SiblingGlob, "UnityPlayer.dll"))),
        ]);
        GameFileSnapshot s = Snapshot(files: ["UnityPlayer.dll"], company: null);

        EngineMatch m = new RuleEvaluator(rules).MatchEngine(s);

        m.IsUndetermined.Should().BeTrue();
        m.UndeterminedBy.Should().Be("undecidable");
        m.Rule.Should().BeNull("an undetermined walk must not report a later rule as the winner");
    }

    [Fact]
    public void EngineWalk_EveryRuleCleanlyMissing_IsARealAnswer()
    {
        DetectionRuleSet rules = Rules(engines:
        [
            Engine("unity", AnyOf(Signal(DetectionSignalType.SiblingGlob, "UnityPlayer.dll"))),
        ]);

        EngineMatch m = new RuleEvaluator(rules).MatchEngine(Snapshot(files: ["Game.exe"]));

        m.IsUndetermined.Should().BeFalse("a clean miss is knowledge, not absence of it");
        m.Rule.Should().BeNull();
    }

    // ---- platforms and capabilities ----------------------------------------

    [Fact]
    public void PlatformWalk_ReportsUndeterminedSeparatelyFromNoMatch()
    {
        DetectionRuleSet rules = Rules(platforms:
        [
            Platform("steam", AnyOf(Signal(DetectionSignalType.PeCompanyContains, "Valve"))),
        ]);
        var evaluator = new RuleEvaluator(rules);

        evaluator.MatchPlatform(Snapshot(company: null), out bool undetermined).Should().BeNull();
        undetermined.Should().BeTrue();

        evaluator.MatchPlatform(Snapshot(company: "Example Studios"), out bool clean).Should().BeNull();
        clean.Should().BeFalse();
    }

    [Fact]
    public void Capabilities_ReturnEveryMatch_NotTheFirst()
    {
        // A game ships DLSS and FSR routinely. Unlike engines there is no
        // precedence here, and an early exit would silently drop capabilities
        // from the "Supports" row.
        DetectionRuleSet rules = Rules(capabilities:
        [
            Capability("dlss", "nvngx_dlss.dll"),
            Capability("dlss_g", "nvngx_dlssg.dll"),
            Capability("fsr", "ffx_fsr2_*.dll"),
            Capability("xess", "libxess.dll"),
        ]);
        GameFileSnapshot s = Snapshot(files: ["nvngx_dlss.dll", "nvngx_dlssg.dll", "ffx_fsr2_api_x64.dll"]);

        new RuleEvaluator(rules).MatchCapabilities(s).Select(c => c.Id)
            .Should().BeEquivalentTo(["dlss", "dlss_g", "fsr"]);
    }

    [Fact]
    public void Capabilities_OnACleanDirectory_AreEmpty()
    {
        DetectionRuleSet rules = Rules(capabilities: [Capability("dlss", "nvngx_dlss.dll")]);

        new RuleEvaluator(rules).MatchCapabilities(Snapshot(files: ["Game.exe"])).Should().BeEmpty();
    }

    // ---- version extraction -------------------------------------------------

    [Fact]
    public void Version_NullExtractor_MeansNoVersion_NotAFailure()
    {
        DetectionRuleSet rules = Rules(engines:
        [
            Engine("gamemaker", AnyOf(Signal(DetectionSignalType.SiblingGlob, "data.win")), version: null),
        ]);

        EngineMatch m = new RuleEvaluator(rules).MatchEngine(Snapshot(files: ["data.win"]));

        m.Rule!.Id.Should().Be("gamemaker");
        m.Version.Should().BeNull();
    }

    [Fact]
    public void Version_PeProductVersionRegex_ReturnsTheFirstCaptureGroup()
    {
        DetectionRuleSet rules = Rules(engines:
        [
            Engine("unreal", AnyOf(Signal(DetectionSignalType.SiblingGlob, "*-Win64-Shipping.exe")),
                new VersionExtractor
                {
                    Type = VersionExtractorType.PeProductVersionRegex,
                    Value = @"\+\+UE(?:4|5)\+Release-(\d+\.\d+)",
                }),
        ]);
        GameFileSnapshot s = Snapshot(files: ["Game-Win64-Shipping.exe"], productVersion: "++UE5+Release-5.3");

        new RuleEvaluator(rules).MatchEngine(s).Version.Should().Be("5.3");
    }

    [Fact]
    public void Version_AnUnparseableOrRunawayPattern_YieldsNoVersionRatherThanThrowing()
    {
        // Rules are updatable DATA. A pattern that does not compile, or one that
        // backtracks forever, must cost a version field — never the Agent.
        DetectionRuleSet bad = Rules(engines:
        [
            Engine("broken", AnyOf(Signal(DetectionSignalType.SiblingGlob, "Game.exe")),
                new VersionExtractor { Type = VersionExtractorType.PeProductVersionRegex, Value = "(unclosed" }),
        ]);

        EngineMatch m = new RuleEvaluator(bad).MatchEngine(Snapshot(files: ["Game.exe"]));

        m.Rule!.Id.Should().Be("broken");
        m.Version.Should().BeNull();
    }

    [Fact]
    public void Version_StringsRegex_ComesFromThePrecomputedCapture()
    {
        DetectionRuleSet rules = Rules(engines:
        [
            Engine("godot", AnyOf(Signal(DetectionSignalType.SiblingGlob, "*.pck")),
                new VersionExtractor { Type = VersionExtractorType.StringsRegex, Value = @"Godot Engine v(\d+\.\d+)" }),
        ]);
        GameFileSnapshot s = Snapshot(
            files: ["Game.pck"],
            captures: new Dictionary<string, string>(StringComparer.Ordinal)
            {
                [@"Godot Engine v(\d+\.\d+)"] = "4.2",
            });

        new RuleEvaluator(rules).MatchEngine(s).Version.Should().Be("4.2");
    }

    [Fact]
    public void Version_PeFileVersion_ComesFromTheNamedSibling_NotTheExecutable()
    {
        // Unity's rule reads UnityPlayer.dll. Answering from the game exe would
        // report a version that is WRONG rather than merely missing.
        DetectionRuleSet rules = Rules(engines:
        [
            Engine("unity", AnyOf(Signal(DetectionSignalType.SiblingGlob, "UnityPlayer.dll")),
                new VersionExtractor { Type = VersionExtractorType.PeFileVersion, From = "UnityPlayer.dll" }),
        ]);
        GameFileSnapshot s = Snapshot(
            files: ["UnityPlayer.dll"],
            fileVersion: "9.9.9.9",    // the EXE's version — must not be reported
            siblingVersions: new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["UnityPlayer.dll"] = "2022.3.10.1",
            });

        new RuleEvaluator(rules).MatchEngine(s).Version.Should().Be("2022.3.10.1");
    }

    [Fact]
    public void Version_PeFileVersion_WithNoSiblingRead_IsNullRatherThanTheExecutables()
    {
        DetectionRuleSet rules = Rules(engines:
        [
            Engine("unity", AnyOf(Signal(DetectionSignalType.SiblingGlob, "UnityPlayer.dll")),
                new VersionExtractor { Type = VersionExtractorType.PeFileVersion, From = "UnityPlayer.dll" }),
        ]);

        new RuleEvaluator(rules).MatchEngine(Snapshot(files: ["UnityPlayer.dll"], fileVersion: "9.9.9.9"))
            .Version.Should().BeNull();
    }

    [Fact]
    public void ConstructorRejectsNullRules() =>
        ((Action)(() => _ = new RuleEvaluator(null!))).Should().Throw<ArgumentNullException>();
}
