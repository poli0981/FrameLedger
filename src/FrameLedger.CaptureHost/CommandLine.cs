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
    public static readonly string[] AcceptedOptions = ["--exe"];

    public Verb Verb { get; init; }

    public string? ExePath { get; init; }

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
            return new CommandLine { Error = "usage: consent list | consent grant --exe <path> | consent revoke --exe <path> | capture --exe <path>" };
        }

        string? exe = null;
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

                exe = args[++i];
            }
        }

        if (verb != Verb.ConsentList && string.IsNullOrWhiteSpace(exe))
        {
            return new CommandLine { Error = "--exe <path> is required" };
        }

        return new CommandLine { Verb = verb, ExePath = exe };
    }
}
