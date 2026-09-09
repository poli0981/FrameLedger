using System.Globalization;
using FrameLedger.Application.Capture;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The report's lines for <see cref="NgxDriverState"/> and its words. Text stays with the report,
/// beside <see cref="FgRefusalText"/>; the state itself moved to <c>Application.Capture</c> (P2 PR-C).
/// </summary>
internal static class NgxDriverStateText
{
    private static readonly (ulong Bit, string Name)[] _names =
    [
        (NgxOverrideFlags.Initialized, "INITIALIZED"), (NgxOverrideFlags.Enabled, "ENABLED"), (NgxOverrideFlags.DllExists, "DLL_EXISTS"),
        (NgxOverrideFlags.DllLoaded, "DLL_LOADED"), (NgxOverrideFlags.DllSelected, "DLL_SELECTED"), (NgxOverrideFlags.Preset, "PRESET"),
        (NgxOverrideFlags.PerfMode, "PERF_MODE"), (NgxOverrideFlags.ScalingRatio, "SCALING_RATIO"),
        (NgxOverrideFlags.OptimalSettings, "OPTIMAL_SETTINGS"), (NgxOverrideFlags.Created, "CREATED"), (NgxOverrideFlags.Evaluate, "EVALUATE"),
        (NgxOverrideFlags.FgMode, "FG_MODE"), (NgxOverrideFlags.SrDlaaMode, "SR_DLAA_MODE"), (NgxOverrideFlags.FgMultiFrame, "FG_MULTI_FRAME"),
        (NgxOverrideFlags.ErrFailed, "ERR_FAILED"), (NgxOverrideFlags.ErrDenied, "ERR_DENIED"), (NgxOverrideFlags.ErrDrs, "ERR_DRS"),
        (NgxOverrideFlags.ErrNotFound, "ERR_NOT_FOUND"), (NgxOverrideFlags.ErrDllLoad, "ERR_DLL_LOAD"),
    ];

    /// <summary>The probe's own spelling of a word's bits, unknown bits printed as hex so nothing is dropped.</summary>
    public static string DescribeFlags(ulong mask)
    {
        if (mask == 0)
        {
            return "-";
        }

        List<string> parts = [];
        ulong seen = 0;
        foreach ((ulong bit, string name) in _names)
        {
            if ((mask & bit) != 0)
            {
                parts.Add(name);
                seen |= bit;
            }
        }

        ulong unknown = mask & ~seen;
        if (unknown != 0)
        {
            parts.Add("0x" + unknown.ToString("X", CultureInfo.InvariantCulture));
        }

        return string.Join(' ', parts);
    }

    /// <summary>The report's line: the three words decoded, the override fields named as such, the caveats measured.</summary>
    public static string Describe(this NgxDriverState s)
    {
        ArgumentNullException.ThrowIfNull(s);
        string head = "  NVIDIA driver NGX state (NvAPI_NGX_GetNGXOverrideState, out of process): ";
        return s.Outcome switch
        {
            NgxProbeOutcome.NotRun => head + "not probed",
            NgxProbeOutcome.ProbeMissing => head + "probe not found - " + s.Detail,
            NgxProbeOutcome.ProbeFailed => head + "probe failed - " + s.Detail,
            NgxProbeOutcome.Degraded => head + "no usable NVIDIA driver on this machine (" + s.Detail + ")",
            NgxProbeOutcome.Unanswered => head + $"the driver did not answer for this process over {s.Readings} probe(s) ({s.Detail}) - "
                                          + "not an NVIDIA-rendered process, a driver older than R570, or the API refusing the caller",
            _ => head + $"answered {s.Answered} of {s.Readings} probe(s){(s.Changed ? ", and the words CHANGED between readings" : "")}"
                 + $"; driver {s.Driver / 100}.{s.Driver % 100:00}\n"
                 + $"    SR 0x{s.Sr:X} [{DescribeFlags(s.Sr)}]  RR 0x{s.Rr:X} [{DescribeFlags(s.Rr)}]  "
                 + $"FG 0x{s.Fg:X} [{DescribeFlags(s.Fg)}]\n"
                 + $"    override fields: scalingRatio={s.ScalingRatio.ToString("0.0000", CultureInfo.InvariantCulture)} "
                 + $"performanceMode={s.PerformanceMode} renderPreset={s.RenderPreset} fgCount={s.FgCount} fgPreset={s.FgPreset} fgMode={s.FgMode} "
                 + "- the OVERRIDE's values when an NVIDIA-app override is set, zero otherwise (measured 2026-09-06); "
                 + "the SR and FG words name a feature the driver saw created and evaluated - identity, never a count or a multiplier "
                 + "(FG_MODE / FG_MULTI_FRAME stay clear and fgCount is 0 at x4)",
        };
    }
}
