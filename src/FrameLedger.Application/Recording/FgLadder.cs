using System.Globalization;
using FrameLedger.Application.Capture;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>
/// The frame-generation and upscaler identity rules over the raw records — ONE implementation, read by
/// the capture host's report (<c>MeasuredFacts</c>) and by the session row (<c>SessionAggregator</c>) alike
/// (P2 PR-D). Everything here was <c>MeasuredFacts</c>' private logic; the strings stayed there, the
/// decisions moved here.
/// </summary>
public static class FgLadder
{
    public const string SlInterposerFileName = "sl.interposer.dll";

    /// <summary>Streamline's DLSS Frame Generation plugin paces through DXGI from this interposer version (§H5).</summary>
    public static readonly Version StreamlineNoneWithheldFrom = new(2, 8, 0);

    /// <summary>
    /// Which frame-generation technology a hooked evaluation or dispatch named, or null. NEVER <c>None</c>:
    /// rung 0 puts N/A in front of the ladder's "otherwise none" when <see cref="FlMeasured.Fg"/> is clear,
    /// and a Streamline-only writer reports UNKNOWN on a title generating frames through XeFG or FSR3-FG —
    /// collapsing UNKNOWN to none would turn "our coverage is short" into a measured negative.
    /// </summary>
    /// <remarks>
    /// ANY record naming a technology wins over UNKNOWN, and that is not a preference — it is what the
    /// writer's own shape requires: <c>fgMode</c> is DLSS_G only on the presents that drained an evaluation;
    /// under frame generation the others are honestly UNKNOWN, so a modal or last-record reading would report
    /// UNKNOWN on a title running DLSS-G in three records out of four.
    /// </remarks>
    public static FlFgMode? Identity(IReadOnlyList<FlFrameRecord> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);
        int start = RecordWindow.ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Fg));
        for (int i = start; i < stream.Count; i++)
        {
            var value = (FlFgMode)stream[i].FgMode;
            if (value is not FlFgMode.NotReported and not FlFgMode.Unknown)
            {
                return value;
            }
        }

        return null;
    }

    /// <summary>Whether any record claimed a frame-generation measurement at all (rung 0's question).</summary>
    public static bool FgHookRan(IReadOnlyList<FlFrameRecord> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);
        return RecordWindow.ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Fg)) < stream.Count;
    }

    /// <summary>Whether any record claimed an upscaler identity.</summary>
    public static bool UpscalerHookRan(IReadOnlyList<FlFrameRecord> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);
        return RecordWindow.ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler)) < stream.Count;
    }

    /// <summary>
    /// The upscaler a hooked evaluation named, or null when no record named one — which covers "no hook
    /// ran" and "a hook ran and saw UNKNOWN" alike; <see cref="UpscalerHookRan"/> tells them apart.
    /// The retired ray-reconstruction value decodes as nothing, so the v3 conflation stays dropped.
    /// </summary>
    /// <remarks>
    /// ANY RECORD NAMING A TECHNOLOGY WINS, for the reason <see cref="Identity"/> gives: the writer publishes an
    /// identity only on the presents that drained an evaluation — one present in N under frame generation,
    /// measured 2,569 of 10,276 at ×4 — so the last record reports UNKNOWN with probability (N−1)/N.
    /// </remarks>
    public static FlUpscaler? UpscalerIdentity(IReadOnlyList<FlFrameRecord> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);
        int start = RecordWindow.ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler));
        for (int i = start; i < stream.Count; i++)
        {
            var candidate = (FlUpscaler)stream[i].Upscaler;
            if (candidate is not FlUpscaler.NotReported and not FlUpscaler.Unknown and not FlUpscaler.RetiredRayReconstruction)
            {
                return candidate;
            }
        }

        return null;
    }

    /// <summary>
    /// The one shape on which a counted 1.0 may not be published as <c>none</c> (§H5): Streamline ≥ 2.8.0
    /// with <c>sl.dlss_g.dll</c> loaded and DXGI's present counter NOT read. Null when <c>none</c> may stand.
    /// </summary>
    /// <remarks>
    /// THE DISCRIMINATOR, since Leg 0 (2026-09-06): DXGI's own counter on the hooked chain. The 2.8.0 pacer's
    /// generated presents ARE DXGI presents there (§H5 row P1-DXGI), so a counter that was read and saw
    /// nothing unseen is DXGI saying what the count says. A counter that was read and DID see presents while
    /// the count still sits at 1.0 is a contradiction between two words of the same writer, refused as such.
    /// </remarks>
    public static string? WithholdNone(FlRuntimeCensus census, RuntimeModuleSet? modules, FlWriterState writer)
    {
        if (!census.HasFlag(FlRuntimeCensus.Ran) || !census.HasFlag(FlRuntimeCensus.SlDlssG))
        {
            return null;
        }

        if (writer.DxgiPresentSamples > 0)
        {
            return writer.DxgiPresentsUnseen == 0
                ? null
                : $"DXGI's present counter read {writer.DxgiPresentsUnseen} present(s) this hook never saw over "
                  + $"{writer.DxgiPresentSamples} hooked present(s) while the records carry none of them — the writer "
                  + "state and the records disagree, and neither may be read as `none`";
        }

        const string notRead = "DXGI's present counter was not read this session, and on Streamline 2.8.0 the DLSS-G "
                               + "pacer's generated presents are DXGI presents this hook never sees (20_OPEN_QUESTIONS "
                               + "§H5 row P1-DXGI), so a 1.0 cannot be told from generation";
        Version? v = modules?.VersionOf(SlInterposerFileName);
        if (v is null)
        {
            return "sl.dlss_g.dll (Streamline's DLSS Frame Generation plugin) is loaded, sl.interposer.dll's file "
                   + "version could not be read, and " + notRead;
        }

        return v >= StreamlineNoneWithheldFrom
            ? $"sl.dlss_g.dll (Streamline's DLSS Frame Generation plugin) is loaded on sl.interposer.dll {v}; " + notRead
            : null;
    }

    /// <summary>DXGI's counter as a second witness beside a counted <c>none</c>; null when it was not read.</summary>
    public static string? DxgiBesideNone(FlWriterState writer)
    {
        if (writer.DxgiPresentSamples == 0)
        {
            return null;
        }

        string samples = writer.DxgiPresentSamples.ToString(CultureInfo.InvariantCulture);
        return writer.DxgiPresentsUnseen == 0
            ? $"DXGI's own present counter agrees: 0 unseen over {samples} hooked present(s)"
            : $"DXGI's own present counter read {writer.DxgiPresentsUnseen.ToString(CultureInfo.InvariantCulture)} "
              + $"unseen over {samples} hooked present(s), inside the `none` ceiling";
    }

    /// <summary>
    /// Identity and count, combined by one rule: <b>the count decides <c>none</c>, identity decides the name.</b>
    /// </summary>
    /// <remarks>
    /// <para>
    /// Until 2026-09-05 identity won outright, which was right for the identities the writer could produce:
    /// <c>FsrFg</c> lands only on a present that drained a FRAMEGENERATION dispatch (a generated batch, a fact
    /// about generation), and <c>DlssG</c> from a <c>kFeatureDLSS_G</c> evaluation never landed at all. Now
    /// <c>DlssG</c> also lands on a present that drained a HUD-less or UI tag — the title FEEDING frame
    /// generation, which a title may do with the feature switched off in its menu. So a counted 1.0 beside a
    /// <c>DlssG</c> mark is <c>none</c> with the inputs noted, never <c>DlssG</c>; an active count beside it is
    /// <c>DlssG</c>; and a withheld count (§H5) keeps its N/A.
    /// </para>
    /// <para><c>FsrFg</c> keeps precedence over the count: a generated batch drained is a generated batch.</para>
    /// </remarks>
    public static FgVerdict Resolve(FlFgMode? identity, FgWindow? fg, string? withheld)
    {
        bool dlssg = identity == FlFgMode.DlssG;
        if (fg?.IsNone == true && (identity is null || dlssg))
        {
            if (withheld is not null)
            {
                return FgVerdict.NoneWithheld;
            }

            return dlssg ? FgVerdict.NoneInputsTagged : FgVerdict.None;
        }

        if (identity is not null)
        {
            return FgVerdict.Named;
        }

        return fg?.IsActive == true ? FgVerdict.ActiveUnidentified : FgVerdict.NotMeasured;
    }
}
