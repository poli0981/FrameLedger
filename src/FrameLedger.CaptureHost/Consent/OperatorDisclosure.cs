namespace FrameLedger.CaptureHost.Consent;

/// <summary>
/// What an operator is told before a consent record may be written here, and the
/// exact act that writes one.
/// </summary>
/// <remarks>
/// <para>
/// <b>This is NOT FR-2.1's consent dialog, and the text says so in its first
/// line.</b> That dialog needs reviewed <c>Safety_*</c> wording in en, vi and ja —
/// <c>09_I18N</c> reviews safety strings as legal text and fails the <c>ja</c>
/// build until a human signs off — and no <c>.resx</c> file exists anywhere in this
/// tree. Drafting it English-only here would manufacture the reviewed artifact
/// without the review, which is why
/// <c>ConsentProvenance</c> has no FR-2.1 member at all.
/// </para>
/// <para>
/// What it does do is satisfy HANDOFF's actual requirement: "a record written by an
/// explicit user action is not synthesis". It states, in substance, the four things
/// <c>19_SAFETY</c> §User-facing consent requires; it says "within 30 seconds" and
/// never "immediately", because a 30 s poll means anti-cheat can be loaded for up
/// to 30 s before we react; and it requires an exact typed phrase.
/// </para>
/// <para>
/// <b>It refuses outright when stdin is redirected.</b> An acknowledgement a script
/// can supply is not an explicit human action, and this is the one place the
/// difference is enforceable.
/// </para>
/// </remarks>
internal static class OperatorDisclosure
{
    /// <summary>
    /// Stamped onto every record this path writes, so a later change to the wording
    /// does not silently re-interpret acknowledgements already made.
    /// </summary>
    public const string Version = "unshipped-host-operator/1";

    private const string _phrase = "I ACCEPT THE INJECTION RISK";

    // A string[] printed in a foreach, not a run of Console.WriteLine("literal"):
    // AnalysisLevel latest-all + TreatWarningsAsErrors turns CA1303 into a build error.
    private static readonly string[] _lines =
    [
        "FrameLedger capture host — a DEVELOPER TOOL. This is not the FrameLedger app.",
        "",
        "  This is NOT the per-game consent dialog FR-2.1 specifies. That dialog does not",
        "  exist yet: its wording has to be reviewed as legal text in en, vi and ja, and no",
        "  such text has been written. What you are about to record is an OPERATOR",
        "  ACKNOWLEDGEMENT at an unshipped binary, and the record says exactly that.",
        "",
        "  What gets injected, and why: FrameLedger.Overlay.dll, into the game process, to",
        "  measure the real render resolution, upscaler, frame generation and ray tracing",
        "  state. Passive measurement cannot do this accurately.",
        "",
        "  Anti-cheat systems may flag or ban accounts. FrameLedger refuses to inject where",
        "  it detects one, and it CANNOT GUARANTEE IT KNOWS EVERY ANTI-CHEAT. If anti-cheat",
        "  appears after injection, capture stops WITHIN 30 SECONDS of it being detected.",
        "",
        "  You are responsible for the terms of service of the games you play.",
        "",
        "  Tier-2 capture, which injects nothing, is the default for anything the guard is",
        "  unsure about. Nothing here is required to measure frame times.",
        "",
    ];

    /// <summary>Show the disclosure and read the confirmation. False means nothing may be written.</summary>
    /// <param name="output">Where the disclosure goes.</param>
    /// <param name="input">The operator's reply, or null when stdin is redirected.</param>
    public static bool Confirm(TextWriter output, TextReader? input)
    {
        ArgumentNullException.ThrowIfNull(output);

        foreach (string line in _lines)
        {
            output.WriteLine(line);
        }

        if (input is null)
        {
            output.WriteLine("Refusing: stdin is redirected, and an acknowledgement a script can supply is not");
            output.WriteLine("an explicit human action. Run this from a console.");
            return false;
        }

        output.WriteLine($"Type exactly '{_phrase}' to enable hooking for this game, or anything else to stop:");
        string? typed = input.ReadLine();
        return string.Equals(typed?.Trim(), _phrase, StringComparison.Ordinal);
    }
}
