using FluentAssertions;
using FrameLedger.CaptureHost;
using FrameLedger.CaptureHost.Consent;

namespace FrameLedger.CaptureHost.Tests;

/// <summary>
/// What the host can be ASKED to do, pinned.
/// </summary>
/// <remarks>
/// §S27's gap was a user-named pid on a binary carrying no consent record. The
/// surface is the place that stays closed, so it is the place that gets an
/// assertion — a reviewer scanning a diff for a new flag is the mechanism this
/// replaces.
/// </remarks>
public sealed class CommandLineSurfaceTests
{
    [Fact]
    public void TheAcceptedOptionsAreExactlyThese()
    {
        // TWO, and adding the second was a deliberate, reviewed act -- this assertion is what
        // made it one. `--seconds` bounds how long an already-consented, already-guarded
        // session runs; it cannot widen WHAT is injected or skip a check, which is the
        // property that separates it from the --pid / --payload / --force / --yes this host
        // refuses. It exists because CaptureLoop honoured MaxDuration and nothing could set
        // it, so a bounded real-title measurement was impossible to take.
        CommandLine.AcceptedOptions.Should().BeEquivalentTo(["--exe", "--seconds"]);
    }

    [Fact]
    public void ASecondsValueThatIsNotAPositiveNumberIsAnErrorAndNeverASilentZero()
    {
        // ZERO MEANS "UNTIL THE TARGET EXITS". So leniency here does not produce a shorter
        // session than asked for — it produces an UNBOUNDED one, which is the opposite of what
        // an operator typing --seconds wants. Every rejected form is listed, because "it parsed
        // as 0" and "it was refused" are indistinguishable from the exit code alone.
        foreach (string bad in new[] { "abc", "0", "-5", "", "1.5", "2s", "+9", " 9" })
        {
            CommandLine.Parse(["capture", "--exe", "game.exe", "--seconds", bad]).Error
                .Should().NotBeNull($"'--seconds {bad}' must be refused rather than read as unbounded");
        }

        CommandLine parsed = CommandLine.Parse(["capture", "--exe", "game.exe", "--seconds", "45"]);
        parsed.Error.Should().BeNull();
        parsed.Seconds.Should().Be(45);
        parsed.ExePath.Should().Be("game.exe", "--seconds must not be mistaken for the --exe value");

        // Absent is the product default, and it is 0 = run until the target exits.
        CommandLine.Parse(["capture", "--exe", "game.exe"]).Seconds.Should().Be(0);
    }

    [Fact]
    public void NoVerbOrOptionNamesAPidAPayloadOrAnOverride()
    {
        // --pid is §S27's gap. --payload would defeat §S22, which compares the payload's directory
        // against the guard's own by file id. --force and --yes are the "I understand, continue anyway"
        // button CLAUDE.md rule 2 forbids. --diag is already taken by the App (10_LOGGING).
        string[] forbidden = ["pid", "payload", "force", "yes", "diag", "override"];
        IEnumerable<string> surface = [.. CommandLine.AcceptedOptions, .. Enum.GetNames<Verb>()];

        foreach (string token in surface)
        {
            foreach (string bad in forbidden)
            {
                token.Should().NotContainEquivalentOf(bad);
            }
        }
    }

    [Theory]
    [InlineData("--pid")]
    [InlineData("--force")]
    [InlineData("--payload")]
    public void AnUnknownOptionIsRefusedRatherThanIgnored(string option)
    {
        CommandLine.Parse(["capture", option, "1234"]).Error.Should().NotBeNull();
    }

    [Fact]
    public void CaptureNeedsAnExecutable()
    {
        CommandLine.Parse(["capture"]).Error.Should().NotBeNull();
        CommandLine.Parse(["capture", "--exe", @"C:\a\game.exe"]).Should().BeEquivalentTo(
            new CommandLine { Verb = Verb.Capture, ExePath = @"C:\a\game.exe" });
    }

    [Fact]
    public void AnEmptyCommandLineIsAUsageError()
    {
        CommandLine.Parse([]).Error.Should().NotBeNull();
        CommandLine.Parse(["nonsense"]).Error.Should().NotBeNull();
    }

    [Fact]
    public void RedirectedInputCannotAcknowledgeAnything()
    {
        // An acknowledgement a script can supply is not the explicit human action HANDOFF licenses as
        // "not synthesis". Passing null is exactly what Program does when Console.IsInputRedirected.
        using var output = new StringWriter();

        OperatorDisclosure.Confirm(output, input: null).Should().BeFalse();
        output.ToString().Should().Contain("stdin is redirected");
    }

    [Fact]
    public void OnlyTheExactPhraseAcknowledges()
    {
        using var output = new StringWriter();

        using var wrongCase = new StringWriter();
        using var exact = new StringWriter();

        OperatorDisclosure.Confirm(output, new StringReader("yes")).Should().BeFalse();
        OperatorDisclosure.Confirm(wrongCase, new StringReader("i accept the injection risk"))
            .Should().BeFalse("the comparison is ordinal, so case is part of the phrase");
        OperatorDisclosure.Confirm(exact, new StringReader("I ACCEPT THE INJECTION RISK"))
            .Should().BeTrue();
    }

    [Fact]
    public void TheDisclosureSaysWhatItIsNotBeforeItSaysAnythingElse()
    {
        // 19_SAFETY §User-facing consent's four statements, plus the one this text has to make that a
        // real consent dialog does not: that it is NOT that dialog.
        using var output = new StringWriter();
        OperatorDisclosure.Confirm(output, input: null);
        string text = output.ToString();

        text.Should().Contain("DEVELOPER TOOL");
        text.Should().Contain("NOT the per-game consent dialog");
        text.Should().Contain("may flag or ban accounts");
        text.Should().Contain("CANNOT GUARANTEE IT KNOWS EVERY ANTI-CHEAT");
        text.Should().Contain("terms of service");
        // THE FOURTH STATEMENT, AND THIS ASSERTION USED TO BE UNABLE TO FAIL FOR IT.
        // It was `Contain("Tier-2")`. When the ladder collapsed to two rungs on 2026-08-28 the
        // claim behind that token changed completely -- Tier 2 stopped being a no-injection
        // MEASUREMENT and became "nothing was measured, and here is why" -- and any rewrite
        // keeping the literal token would still have passed. A gate that cannot fail for the one
        // thing that changed is this project's own recurring defect.
        //
        // So pin the SUBSTANCE: that declining is stated, and stated in terms of what it yields.
        text.Should().Contain("N/A", "the fourth statement is what a session yields WITHOUT hooking");
        text.Should().Contain("Tier 2");
        text.Should().NotContain("Tier 3", "the ladder has two rungs");

        // The two sentences this text carried until 2026-08-28, both false by then. The first told
        // an operator that a no-injection mode would capture for them; the second told them the
        // injection risk was optional, when hooking is now the only frame-time path there is.
        text.Should().NotContainEquivalentOf("required to measure");
        text.Should().NotContainEquivalentOf("is the default for anything the guard");

        // 19_SAFETY: "Consent and disclaimer wording must say 'within 30 seconds', never 'immediately'."
        //
        // The word is banned OUTRIGHT rather than only in a promising sense, and the first draft of
        // this text found out why: it read "stops WITHIN 30 SECONDS — not immediately", which is
        // honest, denies the promise, and still puts the word in front of a reader who may take away
        // only the emphasised part. A rule a test can enforce beats one that needs a reader to parse a
        // negation, and the sentence loses nothing by saying "of it being detected" instead.
        text.Should().Contain("WITHIN 30 SECONDS");
        text.Should().NotContainEquivalentOf("immediately");
    }
}
