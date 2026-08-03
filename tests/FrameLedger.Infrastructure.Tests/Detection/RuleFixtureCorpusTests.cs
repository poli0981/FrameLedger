using System.Text.Json;
using FluentAssertions;
using FrameLedger.Application.Detection;
using FrameLedger.Domain.Detection;
using FrameLedger.Infrastructure.Detection;

namespace FrameLedger.Infrastructure.Tests.Detection;

/// <summary>
/// Evaluates the SHIPPED rules against real directory trees, through the REAL
/// probe.
/// </summary>
/// <remarks>
/// <para>
/// It lives in Infrastructure.Tests and drives <see cref="GameFileProbe"/>
/// rather than a fake, because a corpus run against a test double proves the
/// double behaves — which is not the question.
/// </para>
/// <para>
/// The evaluator's own unit tests are in Domain.Tests, deliberately:
/// <c>coverage-gate.ps1</c> takes the best line-rate per assembly across reports
/// and never merges them, so Domain code exercised only from here could not
/// reach its floor no matter how thorough this file got.
/// </para>
/// </remarks>
public sealed class RuleFixtureCorpusTests
{
    /// <summary>
    /// The first two engines, in the order the seed must keep them.
    /// </summary>
    /// <remarks>
    /// A game carrying both Unity and Unreal markers reports whichever comes
    /// first, so this pair is the load-bearing part of the array's order.
    /// </remarks>
    private static readonly string[] _precedenceHead = ["unity", "unreal"];

    private sealed record Expected(string? Engine, string? Platform, IReadOnlyList<string> Capabilities);

    /// <summary>Walks up from the test binary to the repository root.</summary>
    private static string RepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null && !Directory.Exists(Path.Combine(dir.FullName, "rules")))
        {
            dir = dir.Parent;
        }

        dir.Should().NotBeNull("the corpus cannot run without the repository it describes");
        return dir!.FullName;
    }

    private static string CorpusRoot() => Path.Combine(RepoRoot(), "tests", "fixtures", "rules");

    private static string SeedRules() => Path.Combine(RepoRoot(), "rules", "detection-rules.json");

    public static TheoryData<string> Fixtures()
    {
        var data = new TheoryData<string>();
        foreach (string dir in Directory.EnumerateDirectories(CorpusRoot(), "*", SearchOption.AllDirectories))
        {
            if (File.Exists(Path.Combine(dir, "expected.json")))
            {
                data.Add(Path.GetRelativePath(CorpusRoot(), dir).Replace('\\', '/'));
            }
        }

        return data;
    }

    /// <summary>
    /// The canary that stops the theory below being green by construction.
    /// </summary>
    /// <remarks>
    /// A <c>[Theory]</c> whose <c>MemberData</c> yields zero cases is a passing
    /// suite that tested nothing — the exact shape of <c>fl_proxy_swapchain</c>,
    /// which shipped green and watched nothing. If the corpus root moves or a
    /// glob stops matching, this fails and the theory's silence is explained.
    /// </remarks>
    [Fact]
    public async Task TheCorpusIsNotEmptyAndCoversEveryRuleId()
    {
        TheoryData<string> fixtures = Fixtures();
        fixtures.Should().NotBeEmpty("a theory with no cases is a green suite that tested nothing");

        DetectionRuleSet rules =
            await new DetectionRulesFile(SeedRules()).LoadAsync(TestContext.Current.CancellationToken);

        List<string> dirs = [.. Directory
            .EnumerateDirectories(CorpusRoot(), "*", SearchOption.AllDirectories)
            .Select(Path.GetFileName)
            .OfType<string>()];

        foreach (string id in rules.Engines.Select(e => e.Id))
        {
            dirs.Should().Contain(id, $"engine '{id}' has no fixture; a rule nobody evaluates is a rule nobody checks");
        }

        foreach (string id in rules.Platforms.Select(p => p.Id))
        {
            dirs.Should().Contain(id, $"platform '{id}' has no fixture");
        }
    }

    [Theory]
    [MemberData(nameof(Fixtures))]
    public async Task FixtureEvaluatesToItsExpectation(string relative)
    {
        ArgumentNullException.ThrowIfNull(relative);

        string dir = Path.Combine(CorpusRoot(), relative.Replace('/', Path.DirectorySeparatorChar));
        Expected expected = Read(Path.Combine(dir, "expected.json"));

        string exe = Directory.EnumerateFiles(dir, "*.exe").FirstOrDefault()
                     ?? throw new InvalidOperationException($"fixture '{relative}' has no .exe to detect from");

        var detector = new StaticGameDetector(new DetectionRulesFile(SeedRules()), new GameFileProbe());
        StaticDetectionResult r = await detector.DetectAsync(exe, TestContext.Current.CancellationToken);

        r.EngineId.Should().Be(expected.Engine, $"fixture '{relative}'");
        r.PlatformId.Should().Be(expected.Platform, $"fixture '{relative}'");
        r.CapabilityIds.Should().BeEquivalentTo(expected.Capabilities, $"fixture '{relative}'");
    }

    /// <summary>The ordering rule, asserted on the data rather than only through a fixture.</summary>
    /// <remarks>
    /// The corpus catches a reorder too, but this fails with a message that says
    /// what happened instead of "expected unity, found unreal".
    /// </remarks>
    [Fact]
    public async Task TheEngineArrayOrderIsThePrecedence()
    {
        DetectionRuleSet rules =
            await new DetectionRulesFile(SeedRules()).LoadAsync(TestContext.Current.CancellationToken);

        rules.Engines.Select(e => e.Id).Should().ContainInOrder(
            _precedenceHead,
            "the JSON array order IS the precedence (05_DETECTION); reordering it changes which engine a game " +
            "carrying both markers reports, and the ordering fixture is what notices");
    }

    private static Expected Read(string path)
    {
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path));
        JsonElement root = doc.RootElement;
        return new Expected(
            root.GetProperty("engine").ValueKind == JsonValueKind.Null
                ? null
                : root.GetProperty("engine").GetString(),
            root.GetProperty("platform").ValueKind == JsonValueKind.Null
                ? null
                : root.GetProperty("platform").GetString(),
            [.. root.GetProperty("capabilities").EnumerateArray().Select(e => e.GetString()!)]);
    }
}
