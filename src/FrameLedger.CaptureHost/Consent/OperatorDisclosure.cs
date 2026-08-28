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
    /// <remarks>
    /// <para>
    /// <b>Bumped to /2 on 2026-08-28, and the reason is that /1 contained two FALSE
    /// sentences rather than differently-phrased ones.</b> It told an operator that a
    /// no-injection mode would capture for them, and that nothing here was required to
    /// measure frame times. The ladder collapsed to two rungs and hooking became the only
    /// frame-time path, so both stopped being true. Two different texts sharing one version
    /// would defeat the only property this field has.
    /// </para>
    /// <para>
    /// <b>NOTHING RE-PROMPTS ON A VERSION MISMATCH, and this field is provenance rather
    /// than a control.</b> Traced 2026-08-28: it is written by <c>Program</c>, merged
    /// forward by the store, cleared on revoke, and read by exactly one non-test caller —
    /// the status line in <c>consent list</c>. <c>HookRequest.FromConsent</c> never sees it,
    /// so a record stamped /1 still yields consent today. Whether that is acceptable is an
    /// owner decision, not a coding one: the realistic population is one developer's own
    /// build tree, which <c>git clean</c> removes. If a wording change is ever material
    /// enough to require re-consent, <b>that re-prompt has to be built — it does not exist</b>,
    /// and saying so here is what stops the next reader assuming the stamp does something.
    /// </para>
    /// </remarks>
    public const string Version = "unshipped-host-operator/2";

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
        "  If you do not enable hooking, this host still records the session: its start,",
        "  end and duration, plus whatever hardware telemetry this machine provides. Every",
        "  frame-derived value — frame times, resolution, upscaler, frame generation, ray",
        "  tracing — reads N/A. That is Tier 2. It needs no elevation, and it is what every",
        "  game gets until it is individually enabled here.",
        "",
        "  Anti-cheat systems may flag or ban accounts. FrameLedger refuses to inject where",
        "  it detects one, and it CANNOT GUARANTEE IT KNOWS EVERY ANTI-CHEAT. If anti-cheat",
        "  appears after injection, capture stops WITHIN 30 SECONDS of it being detected.",
        "",
        "  You are responsible for the terms of service of the games you play.",
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
