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
        CommandLine.AcceptedOptions.Should().BeEquivalentTo(["--exe"]);
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
        text.Should().Contain("Tier-2");

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
