using System.Globalization;
namespace FrameLedger.CaptureHost;

/// <summary>
/// The whole command surface, and what it deliberately cannot express.
/// </summary>
/// <remarks>
/// <para>
/// <b>There is no <c>--pid</c>, no <c>--payload</c>, no <c>--force</c> and no
/// <c>--yes</c>.</b> §S27's gap was a user-named pid on a binary with no consent
/// record; removing the pid argument entirely costs nothing here, because the
/// consent record is keyed on the executable path anyway and
/// <see cref="Capture.TargetResolver"/> resolves the process from it. The payload is
/// always the Overlay beside this binary, because §S22 refuses anything else.
/// </para>
/// <para>
/// <b><c>--diag</c> is already taken</b> and is not this. <c>10_LOGGING_AND_BUG_REPORTS</c>
/// assigns it to the App as a stdout capability report while <c>12_BUILD</c> lists
/// it as an Agent flag; §S27 records the collision.
/// </para>
/// </remarks>
internal sealed record CommandLine
{
    /// <summary>Every option this host accepts. A test pins it.</summary>
    public static readonly string[] AcceptedOptions = ["--exe", "--seconds"];

    public Verb Verb { get; init; }

    public string? ExePath { get; init; }

    /// <summary>
    /// A hard stop for <c>capture</c>, in seconds. Zero means "until the target exits", which
    /// is the default and the product behaviour.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>This is a duration bound, not a safety bypass.</b> The options this host refuses —
    /// <c>--pid</c>, <c>--payload</c>, <c>--force</c>, <c>--yes</c> — all widen WHAT may be
    /// injected or skip a check. This narrows HOW LONG an already-consented, already-guarded
    /// session runs, and it can only make a session shorter.
    /// </para>
    /// <para>
    /// It exists because a real-title measurement otherwise cannot end: <c>MaxDuration</c> was
    /// honoured by <c>CaptureLoop</c> and covered by tests, but nothing could set it, so
    /// <c>capture</c> against a running game ran until the game was closed and the report is
    /// only printed at the end. An operator taking a bounded measurement had no way to take one.
    /// </para>
    /// </remarks>
    public int Seconds { get; init; }

    public string? Error { get; init; }

    public static CommandLine Parse(string[] args)
    {
        ArgumentNullException.ThrowIfNull(args);

        Verb verb = args switch
        {
            ["consent", "list", ..] => Verb.ConsentList,
            ["consent", "grant", ..] => Verb.ConsentGrant,
            ["consent", "revoke", ..] => Verb.ConsentRevoke,
            ["capture", ..] => Verb.Capture,
            _ => Verb.None,
        };

        if (verb == Verb.None)
        {
            return new CommandLine { Error = "usage: consent list | consent grant --exe <path> | consent revoke --exe <path> | capture --exe <path> [--seconds <n>]" };
        }

        string? exe = null;
        int seconds = 0;
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i].StartsWith("--", StringComparison.Ordinal))
            {
                if (!AcceptedOptions.Contains(args[i], StringComparer.Ordinal))
                {
                    return new CommandLine { Error = $"unknown option '{args[i]}'" };
                }

                if (i + 1 >= args.Length)
                {
                    return new CommandLine { Error = $"'{args[i]}' needs a value" };
                }

                string option = args[i];
                string value = args[++i];
                if (string.Equals(option, "--seconds", StringComparison.Ordinal))
                {
                    // A non-numeric or non-positive value is an ERROR, never a silent 0. Zero
                    // means "run until the target exits", so accepting garbage as 0 would turn
                    // a mistyped bound into an unbounded session — the opposite of what the
                    // operator asked for, arrived at by leniency.
                    if (!int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out seconds)
                        || seconds <= 0)
                    {
                        return new CommandLine { Error = "'--seconds' needs a positive whole number" };
                    }
                }
                else
                {
                    exe = value;
                }
            }
        }

        if (verb != Verb.ConsentList && string.IsNullOrWhiteSpace(exe))
        {
            return new CommandLine { Error = "--exe <path> is required" };
        }

        return new CommandLine { Verb = verb, ExePath = exe, Seconds = seconds };
    }
}
