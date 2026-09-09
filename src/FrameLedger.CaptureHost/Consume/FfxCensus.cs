using System.Globalization;
using System.Text;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// What the ffx-api rows saw: two independent per-frame counts and their ratio — the AMD
/// twin of <c>tokens/batch</c>.
/// </summary>
/// <remarks>
/// <para>
/// <b>The count that becomes <c>fg_factor</c> needs a second count beside it, or a doubled
/// writer is green everywhere.</b> On the Streamline route <c>tokens/batch</c> is that second
/// count, and it is what answered §S31. On the AMD route the writer produces two per-frame
/// counts from two different dispatch types — PREPARE's <c>frameID</c> (what <c>fgEvaluations</c>
/// carries once a title has ever prepared) and the UPSCALE dispatches (what it carries
/// otherwise, and what names the identity byte on the presents that drained one). A title
/// that generates frames issues exactly one of each per application frame, so
/// <see cref="FramesPerUpscale"/> reads <c>1.00</c> when the two agree, <c>2.00</c> when one of
/// them is doubled — a row for the loader as well as the leaf, or a title with two upscale
/// contexts per frame — and nothing else can see either.
/// </para>
/// <para>
/// <b>Derived from the record, and only the record.</b> An UPSCALE drained is a present whose
/// identity byte names an FSR value (the writer names it only on such a present); a
/// FRAMEGENERATION drained is a present whose <c>fgMode</c> is <c>FSR_FG</c>. Both are the
/// decoded bytes rather than raw fact bits, because <see cref="FlFeatureFlags"/> has no free
/// bit for a raw AMD fact and adding one is a layout change — so unlike <see cref="SlCensus"/>
/// this census MOVES if the decode moves, and is evidence about the counts, not about which
/// descriptor type arrived.
/// </para>
/// <para>
/// <b>On a title with both vendors loaded</b> (UE5: <c>sl.interposer.dll</c> beside the two AMD
/// leaves) <c>fgEvaluations</c> is Streamline's token count whenever Streamline has ever
/// issued one, so <see cref="FramesPerUpscale"/> is then tokens against UPSCALE dispatches — a
/// cross-vendor agreement, and printed as such.
/// </para>
/// </remarks>
internal sealed record FfxCensus
{
    /// <summary>Records in the window, whether or not they drained anything.</summary>
    public required int Records { get; init; }

    /// <summary>Presents that drained an UPSCALE dispatch: the identity byte names an FSR value.</summary>
    public required int UpscaleDrained { get; init; }

    /// <summary>Presents that drained a FRAMEGENERATION dispatch: <c>fgMode == FSR_FG</c>.</summary>
    public required int FgDispatchDrained { get; init; }

    /// <summary><c>Σ fgEvaluations</c> over the same records — application frames, by whichever count the writer chose.</summary>
    public required long Frames { get; init; }

    /// <summary>False when no record in the window carries an FSR identity or an FSR_FG mode.</summary>
    public bool SawAnything => UpscaleDrained > 0 || FgDispatchDrained > 0;

    /// <summary><c>presents / frames</c> — the factor, before the guards <see cref="FgWindow"/> applies.</summary>
    public double? PresentsPerFrame => Frames > 0 ? Records / (double)Frames : null;

    /// <summary>The agreement check: <c>1.00</c> when the two per-frame counts agree.</summary>
    public double? FramesPerUpscale => UpscaleDrained > 0 ? Frames / (double)UpscaleDrained : null;

    /// <summary>≈ 1 while frame generation is on, 0 while it is off.</summary>
    public double? FgDispatchesPerFrame => Frames > 0 ? FgDispatchDrained / (double)Frames : null;

    /// <summary>Builds the census over the window the identity hook governs.</summary>
    public static FfxCensus From(IReadOnlyList<FlFrameRecord> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        int start = RecordWindow.ClaimedSuffixStart(
            stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler));
        if (start == stream.Count)
        {
            return new FfxCensus { Records = 0, UpscaleDrained = 0, FgDispatchDrained = 0, Frames = 0 };
        }

        int upscale = 0;
        int fg = 0;
        long frames = 0;
        for (int i = start; i < stream.Count; i++)
        {
            FlFrameRecord r = stream[i];
            frames += r.FgEvaluations;
            upscale += IsFsr((FlUpscaler)r.Upscaler) ? 1 : 0;
            fg += (FlFgMode)r.FgMode == FlFgMode.FsrFg ? 1 : 0;
        }

        return new FfxCensus
        {
            Records = stream.Count - start,
            UpscaleDrained = upscale,
            FgDispatchDrained = fg,
            Frames = frames,
        };
    }

    /// <summary>Every value the ffx-api rows can put in the identity byte.</summary>
    private static bool IsFsr(FlUpscaler u) =>
        u is FlUpscaler.Fsr2 or FlUpscaler.Fsr3 or FlUpscaler.Fsr4 or FlUpscaler.FsrUnversioned;

    /// <summary>Two counts and their ratio, with the reading guide on the same line.</summary>
    public string Describe()
    {
        if (Records == 0)
        {
            return "  ffx dispatch census: no record claimed FL_MEASURED_UPSCALER, so no ffx-api dispatch could have been seen";
        }

        if (!SawAnything)
        {
            return "  ffx dispatch census over " + N(Records) + " record(s): no UPSCALE or FRAMEGENERATION dispatch "
                   + "reached an ffx-api leaf or the FSR 3.0 host facade (no FSR identity, no FSR_FG mode)";
        }

        var sb = new StringBuilder();
        sb.Append("  ffx dispatch census over ").Append(N(Records)).Append(" record(s): upscale-drained=")
          .Append(N(UpscaleDrained)).Append("  fg-dispatch-drained=").Append(N(FgDispatchDrained))
          .Append("  frames(Σ fgEvaluations)=").AppendLine(N(Frames));
        sb.Append("    presents/frame=").Append(F(PresentsPerFrame))
          .Append("  frames/upscale-drained=").Append(F(FramesPerUpscale))
          .Append("  fg-dispatch/frame=").Append(F(FgDispatchesPerFrame))
          .Append("  (frames/upscale-drained: 1.00 = the two per-frame counts agree; 2.00 = one of them is "
                  + "doubled — a hook on the loader as well as the leaf, or two upscale contexts per frame)");
        return sb.ToString();
    }

    private static string N(long v) => v.ToString(CultureInfo.InvariantCulture);

    private static string F(double? v) => v is null ? "—" : v.Value.ToString("F2", CultureInfo.InvariantCulture);
}
