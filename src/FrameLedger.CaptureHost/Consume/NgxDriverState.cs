using System.Globalization;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The NVIDIA driver's own per-process NGX bookkeeping — <c>NvAPI_NGX_GetNGXOverrideState</c>, read out of
/// process by <c>fl-probe-nvapi --ngx-state</c> beside each module snapshot — merged over a session.
/// </summary>
/// <remarks>
/// <para>
/// <b>A driver-reported fact, and the report says so wherever it uses one.</b> It is neither a hooked argument
/// (CLAUDE.md rule 4) nor a static hint (<c>05_DETECTION</c>): the driver says a feature was created and
/// evaluated in this process, and this host repeats that with the attribution. What it may stand in for is
/// the super-resolution IDENTITY on a session where no hook saw an evaluation (<c>03_METRICS</c> §Upscaling,
/// the driver-reported rung, owner decision 2026-09-06). What it may not stand in for: a quality, a ratio, a
/// render size, a frame-generation mode or a multiplier — measured 2026-09-06 to be override fields or blind.
/// </para>
/// <para>
/// The last ANSWERED reading is the state; <see cref="Readings"/> counts probe runs, <see cref="Answered"/> the
/// ones the driver answered, and <see cref="Changed"/> whether two answered readings disagreed on a mask — a
/// title switching DLSS on mid-session would show as a change, and the report prints it rather than averaging.
/// </para>
/// </remarks>
internal sealed record NgxDriverState(
    NgxProbeOutcome Outcome,
    ulong Sr,
    ulong Rr,
    ulong Fg,
    double ScalingRatio,
    uint PerformanceMode,
    uint RenderPreset,
    uint FgCount,
    uint FgPreset,
    uint FgMode,
    uint Driver,
    int Readings,
    int Answered,
    bool Changed,
    string? Detail)
{
    /// <summary>The loop was built without a probe.</summary>
    public static readonly NgxDriverState NotRun = new(NgxProbeOutcome.NotRun, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, null);

    /// <summary>The one reading this host acts on: the driver says an NGX super-resolution feature ran here.</summary>
    public bool SrCreatedAndEvaluated =>
        Outcome == NgxProbeOutcome.Answered && (Sr & NgxOverrideFlags.CreatedAndEvaluated) == NgxOverrideFlags.CreatedAndEvaluated;

    /// <summary>One outcome with no masks — a probe that did not answer, and why.</summary>
    public static NgxDriverState Of(NgxProbeOutcome outcome, string? detail) =>
        NotRun with { Outcome = outcome, Readings = 1, Detail = detail };

    /// <summary>
    /// The machine line the probe prints (<c>NGXSTATE status=… sr=0x… …</c>), or a <see cref="NgxProbeOutcome.ProbeFailed"/>
    /// when nothing in <paramref name="output"/> is one. Every other line of the probe's output is for a human.
    /// </summary>
    public static NgxDriverState Parse(string output)
    {
        ArgumentNullException.ThrowIfNull(output);
        foreach (string raw in output.Split('\n'))
        {
            string line = raw.Trim();
            if (!line.StartsWith("NGXSTATE ", StringComparison.Ordinal))
            {
                continue;
            }

            Dictionary<string, string> kv = new(StringComparer.Ordinal);
            foreach (string token in line.Split(' ', StringSplitOptions.RemoveEmptyEntries).Skip(1))
            {
                int eq = token.IndexOf('=', StringComparison.Ordinal);
                if (eq > 0)
                {
                    kv[token[..eq]] = token[(eq + 1)..];
                }
            }

            return FromFields(kv, line);
        }

        return Of(NgxProbeOutcome.ProbeFailed, "the probe printed no NGXSTATE line");
    }

    private static NgxDriverState FromFields(Dictionary<string, string> kv, string line)
    {
        string status = kv.GetValueOrDefault("status", "");
        if (string.Equals(status, "ANSWERED", StringComparison.Ordinal))
        {
            try
            {
                return new NgxDriverState(NgxProbeOutcome.Answered, Hex(kv["sr"]), Hex(kv["rr"]), Hex(kv["fg"]),
                    double.Parse(kv["ratio"], CultureInfo.InvariantCulture), U(kv["mode"]), U(kv["preset"]),
                    U(kv["fgcount"]), U(kv["fgpreset"]), U(kv["fgmode"]), U(kv["driver"]), 1, 1, false, null);
            }
            catch (Exception e) when (e is KeyNotFoundException or FormatException or OverflowException)
            {
                return Of(NgxProbeOutcome.ProbeFailed, "an NGXSTATE line this host could not read: " + line);
            }
        }

        return status switch
        {
            "UNANSWERED" => Of(NgxProbeOutcome.Unanswered, "NvAPI " + kv.GetValueOrDefault("nvapi", "?")),
            "DEGRADED" => Of(NgxProbeOutcome.Degraded, "NvAPI_Initialize " + kv.GetValueOrDefault("nvapi", "?")),
            _ => Of(NgxProbeOutcome.ProbeFailed, "an NGXSTATE status this host does not know: " + status),
        };
    }

    private static ulong Hex(string v) =>
        ulong.Parse(v.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? v[2..] : v, NumberStyles.HexNumber,
            CultureInfo.InvariantCulture);

    private static uint U(string v) => uint.Parse(v, CultureInfo.InvariantCulture);

    /// <summary>Fold a later reading in: an answered one becomes the state, any one counts.</summary>
    public NgxDriverState Merge(NgxDriverState later)
    {
        ArgumentNullException.ThrowIfNull(later);
        if (Outcome == NgxProbeOutcome.NotRun)
        {
            return later;
        }

        int readings = Readings + later.Readings;
        int answered = Answered + later.Answered;
        if (later.Outcome != NgxProbeOutcome.Answered)
        {
            // An earlier answer outranks a later silence: the driver's words do not expire, and a
            // probe that failed once mid-session says nothing about what it had already read.
            return Outcome == NgxProbeOutcome.Answered
                ? this with { Readings = readings }
                : later with { Readings = readings, Answered = answered };
        }

        bool changed = Changed || (Outcome == NgxProbeOutcome.Answered && (Sr != later.Sr || Rr != later.Rr || Fg != later.Fg));
        return later with { Readings = readings, Answered = answered, Changed = changed };
    }

    /// <summary>The report's line: the three words decoded, the override fields named as such, the caveats measured.</summary>
    public string Describe()
    {
        string head = "  NVIDIA driver NGX state (NvAPI_NGX_GetNGXOverrideState, out of process): ";
        return Outcome switch
        {
            NgxProbeOutcome.NotRun => head + "not probed",
            NgxProbeOutcome.ProbeMissing => head + "probe not found - " + Detail,
            NgxProbeOutcome.ProbeFailed => head + "probe failed - " + Detail,
            NgxProbeOutcome.Degraded => head + "no usable NVIDIA driver on this machine (" + Detail + ")",
            NgxProbeOutcome.Unanswered => head + $"the driver did not answer for this process over {Readings} probe(s) ({Detail}) - "
                                          + "not an NVIDIA-rendered process, a driver older than R570, or the API refusing the caller",
            _ => head + $"answered {Answered} of {Readings} probe(s){(Changed ? ", and the words CHANGED between readings" : "")}"
                 + $"; driver {Driver / 100}.{Driver % 100:00}\n"
                 + $"    SR 0x{Sr:X} [{NgxOverrideFlags.Describe(Sr)}]  RR 0x{Rr:X} [{NgxOverrideFlags.Describe(Rr)}]  "
                 + $"FG 0x{Fg:X} [{NgxOverrideFlags.Describe(Fg)}]\n"
                 + $"    override fields: scalingRatio={ScalingRatio.ToString("0.0000", CultureInfo.InvariantCulture)} "
                 + $"performanceMode={PerformanceMode} renderPreset={RenderPreset} fgCount={FgCount} fgPreset={FgPreset} fgMode={FgMode} "
                 + "- the OVERRIDE's values when an NVIDIA-app override is set, zero otherwise (measured 2026-09-06); "
                 + "the FG word does not reflect Streamline DLSS-G (INITIALIZED DLL_EXISTS beside a x4 session) - frame generation stays with the tags and the count",
        };
    }
}
