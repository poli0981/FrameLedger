using FluentAssertions;
using FrameLedger.Domain.Detection;
using static FrameLedger.Domain.Tests.DetectionFixtures;

namespace FrameLedger.Domain.Tests;

/// <summary>
/// The combinator truth tables, one fact per row.
/// </summary>
/// <remarks>
/// The rows that matter are the Unknown ones. A two-valued evaluator passes
/// every other row in this file, which is precisely why the two-valued version
/// is the easy mistake to make.
/// </remarks>
public sealed class SignalOutcomeTriStateTests
{
    private static readonly DetectionSignal _match = Signal(DetectionSignalType.SiblingGlob, "there.dll");
    private static readonly DetectionSignal _noMatch = Signal(DetectionSignalType.SiblingGlob, "absent.dll");
    private static readonly DetectionSignal _unknown = Signal(DetectionSignalType.PeCompanyContains, "anything");

    private static GameFileSnapshot Snap() =>
        Snapshot(files: ["there.dll"], company: null);    // null company => that signal is Unknown

    [Fact]
    public void Unknown_IsZero_SoADefaultHasEstablishedNothing() =>
        ((int)SignalOutcome.Unknown).Should().Be(0);

    // ---- all ----------------------------------------------------------------

    [Fact]
    public void All_AnyNoMatch_IsNoMatch() =>
        RuleEvaluator.Evaluate(AllOf(_match, _noMatch), Snap()).Should().Be(SignalOutcome.NoMatch);

    [Fact]
    public void All_NoMatchBeatsUnknown_BecauseTheGroupIsAlreadyDecided() =>
        RuleEvaluator.Evaluate(AllOf(_unknown, _noMatch), Snap()).Should().Be(SignalOutcome.NoMatch);

    [Fact]
    public void All_UnknownWithoutNoMatch_IsUnknown() =>
        RuleEvaluator.Evaluate(AllOf(_match, _unknown), Snap()).Should().Be(SignalOutcome.Unknown);

    [Fact]
    public void All_EveryMatch_IsMatch() =>
        RuleEvaluator.Evaluate(AllOf(_match, _match), Snap()).Should().Be(SignalOutcome.Match);

    // ---- any ----------------------------------------------------------------

    [Fact]
    public void Any_AnyMatch_IsMatch() =>
        RuleEvaluator.Evaluate(AnyOf(_noMatch, _match), Snap()).Should().Be(SignalOutcome.Match);

    [Fact]
    public void Any_MatchBeatsUnknown_BecauseTheGroupIsAlreadyDecided() =>
        RuleEvaluator.Evaluate(AnyOf(_unknown, _match), Snap()).Should().Be(SignalOutcome.Match);

    [Fact]
    public void Any_UnknownWithoutMatch_IsUnknown() =>
        RuleEvaluator.Evaluate(AnyOf(_noMatch, _unknown), Snap()).Should().Be(SignalOutcome.Unknown);

    [Fact]
    public void Any_EveryNoMatch_IsNoMatch() =>
        RuleEvaluator.Evaluate(AnyOf(_noMatch, _noMatch), Snap()).Should().Be(SignalOutcome.NoMatch);

    // ---- the fact that makes the rest mean something ------------------------

    [Fact]
    public void AnUncollectedFact_IsUnknown_NotNoMatch()
    {
        // The probe never ran the strings pass. A needle missing from a scan
        // that did not happen is not evidence of absence — this is the same
        // collapse of "could not look" into "looked and it was clean" that the
        // native guard exists to prevent.
        GameFileSnapshot s = Snapshot(uncollected: [DetectionSignalType.StringsContains]);

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.StringsContains, "Godot Engine v"), s)
            .Should().Be(SignalOutcome.Unknown);
    }

    [Fact]
    public void AFailedPeRead_IsUnknown_NotNoMatch()
    {
        GameFileSnapshot s = Snapshot(company: null, product: null);

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.PeCompanyContains, "Valve"), s)
            .Should().Be(SignalOutcome.Unknown);
        RuleEvaluator.Evaluate(Signal(DetectionSignalType.PeProductContains, "Half-Life"), s)
            .Should().Be(SignalOutcome.Unknown);
    }

    [Fact]
    public void AnIncompleteListing_MakesAMissUnknown_ButNotAHit()
    {
        // The distinction the first version got wrong. A file the walk listed is
        // a file that is there, however early it stopped afterwards — only
        // ABSENCE is in doubt.
        //
        // Treating incompleteness as poisoning every file signal was measured
        // against three real games (depths 5, 6 and 9 against a cap of 4) and
        // made the detector useless: every engine came back Undetermined.
        // Failing safe is right; failing safe on every input is not working.
        GameFileSnapshot partial = Snapshot(files: ["there.dll"], listingComplete: false);

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "there.dll"), partial)
            .Should().Be(SignalOutcome.Match, "we saw it; a short walk does not unsee it");

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "absent.dll"), partial)
            .Should().Be(SignalOutcome.Unknown, "we did not finish looking, so absence proves nothing");
    }

    [Fact]
    public void ACompleteListing_MakesAMissDefinite()
    {
        // Without this the test above would pass against an evaluator that
        // answered Unknown for every miss, and a clean directory would never be
        // a usable answer.
        GameFileSnapshot complete = Snapshot(files: ["there.dll"], listingComplete: true);

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.SiblingGlob, "absent.dll"), complete)
            .Should().Be(SignalOutcome.NoMatch);
    }

    [Fact]
    public void APeReadThatSucceededAndMissed_IsNoMatch_NotUnknown()
    {
        // The other direction, without which the test above would pass against
        // an evaluator that answers Unknown for everything.
        GameFileSnapshot s = Snapshot(company: "Example Studios");

        RuleEvaluator.Evaluate(Signal(DetectionSignalType.PeCompanyContains, "Valve"), s)
            .Should().Be(SignalOutcome.NoMatch);
    }
}
