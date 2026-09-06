using FluentAssertions;
using FrameLedger.CaptureHost.Capture;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>The loader detour's two words as the report reads them, and the family index against a rules file.</summary>
public sealed class EarlyStopTests : IDisposable
{
    private readonly string _rules = Path.Combine(Path.GetTempPath(), "fl-rules-" + Guid.NewGuid().ToString("N") + ".json");

    public EarlyStopTests() => File.WriteAllText(_rules,
        """{"anticheat":{"modules":[{"family":"Easy Anti-Cheat","match":"prefix","values":["EasyAntiCheat"]},{"family":"BattlEye","match":"prefix","values":["BEClient"]}]}}""");

    public void Dispose()
    {
        try
        {
            File.Delete(_rules);
        }
        catch (IOException)
        {
        }
    }

    [Fact]
    public void TheFamilyIndexIsOneBasedInRulesOrderAndNeverAGuess()
    {
        EarlyStop.FamilyName(1, _rules).Should().Be("Easy Anti-Cheat");
        EarlyStop.FamilyName(2, _rules).Should().Be("BattlEye");
        EarlyStop.FamilyName(3, _rules).Should().BeNull("outside the file");
        EarlyStop.FamilyName(0, _rules).Should().BeNull();
        EarlyStop.FamilyName(1, Path.Combine(Path.GetTempPath(), "nope.json")).Should().BeNull("unreadable is null, not a throw");
    }

    [Fact]
    public void AnInstalledDetourWithNoStopIsOneLine()
    {
        var writer = new FlWriterState { LoaderSignals = 0x8003, Status = (uint)FlStatus.Ready };

        List<string> lines = [.. EarlyStop.Describe(writer, _rules)];

        lines.Should().ContainSingle().Which.Should().Contain("installed").And.Contain("woke the watchdog 3 time(s)");
    }

    [Fact]
    public void AMissingDetourSaysWhatIsLost()
    {
        List<string> lines = [.. EarlyStop.Describe(new FlWriterState { LoaderSignals = 0 }, _rules)];

        lines.Should().ContainSingle().Which.Should().Contain("NOT installed").And.Contain("host's 30 s scan is the only stop");
    }

    [Fact]
    public void AStopNamesTheFamilyAndTheStatus()
    {
        var writer = new FlWriterState { LoaderSignals = 0x8000, EarlyStopFamily = 2, Status = (uint)FlStatus.StoppedBlocklisted };

        List<string> lines = [.. EarlyStop.Describe(writer, _rules)];

        lines.Should().HaveCount(2);
        lines[1].Should().Contain("EARLY STOP").And.Contain("family #2 (BattlEye)").And.Contain("status=StoppedBlocklisted")
            .And.Contain("fragment-and-signer tier is the host's scan");
    }

    [Fact]
    public void TheClassifierMapsTheNewStatusToItsOwnReasonAndAnExitedTargetStillWins()
    {
        SessionEndClassifier.Classify(targetExited: false, (uint)FlStatus.StoppedBlocklisted, weLatchedTheUnhook: false, attachSettled: true)
            .Should().Be(SessionEndReason.WriterStoppedBlocklisted);
        SessionEndClassifier.Classify(targetExited: true, (uint)FlStatus.StoppedBlocklisted, weLatchedTheUnhook: false, attachSettled: true)
            .Should().Be(SessionEndReason.TargetExited);
    }
}
