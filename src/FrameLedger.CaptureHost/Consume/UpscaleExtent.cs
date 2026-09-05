using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The render → output extent a session ran at, taken as the MODAL tuple over the records
/// that claim <see cref="FlMeasured.UpscalerParams"/> and carry an output size.
/// </summary>
/// <remarks>
/// <para>
/// <b>Two measurements, one piece of arithmetic.</b> <c>renderW/H</c> come off the upscaler's own
/// dispatch (Streamline's scaling-input tag, or the ffx-api descriptor's <c>renderSize</c>) and
/// <c>outputW/H</c> off the swapchain the present hook read; the ratio is <c>03_METRICS</c>
/// §Upscaling's <c>sqrt((outW×outH)/(renW×renH))</c>, and the render scale is its reciprocal as
/// a percentage. Measured on Cyberpunk 2077 at <c>DLSS = Balanced</c>: 1485×835 against 2560×1440
/// is 1.72× and 58%, which is a number a writer that hardcoded a plausible resolution could not
/// have produced.
/// </para>
/// <para>
/// <b>Modal, not mean, and the count of distinct tuples is published beside it.</b> Averaging
/// across a settings change or a dynamic-resolution swing is the classic way a benchmark number
/// stops meaning anything (<c>03_METRICS</c> §Upscaling, segments); until P2's segmenter cuts the
/// session, the throwaway consumer reports the tuple most records ran at and says how many others
/// it saw.
/// </para>
/// </remarks>
internal sealed record UpscaleExtent
{
    public required ushort RenderW { get; init; }

    public required ushort RenderH { get; init; }

    public required ushort OutputW { get; init; }

    public required ushort OutputH { get; init; }

    /// <summary>Records that ran at exactly this tuple.</summary>
    public required int Records { get; init; }

    /// <summary>Records in the window that carried both sizes, whatever their tuple.</summary>
    public required int Measured { get; init; }

    /// <summary>Distinct (render, output) tuples in the window. More than 1 means the settings moved.</summary>
    public required int DistinctGroups { get; init; }

    /// <summary><c>sqrt((outW×outH)/(renW×renH))</c>.</summary>
    public double Ratio => Math.Sqrt((double)OutputW * OutputH / ((double)RenderW * RenderH));

    /// <summary>The per-axis render scale as a percentage — <c>100 / Ratio</c>.</summary>
    public double RenderScalePercent => 100.0 / Ratio;

    /// <summary>The dominant extent over the identity-claiming window, or null when no record in it carried both sizes.</summary>
    /// <remarks>
    /// <b>The window is the IDENTITY suffix, not the params suffix — measured 2026-09-05 on every
    /// frame-generation run.</b> The writer publishes <see cref="FlMeasured.UpscalerParams"/> only on
    /// the present that DRAINED a dispatch or tag, which under frame generation is one present in N;
    /// the other N−1 carry the identity bit and an honest zero. <see cref="RecordWindow.ClaimedSuffixStart"/>
    /// demands a clean suffix in which every record claims the bit, so keyed on params it returned
    /// "no window" for every FG-on capture — Cyberpunk at FSR 3 + FG printed <c>render -> output: N/A</c>
    /// two lines under its own raw block reading <c>render 1506x847 on 4332 record(s)</c>, and the
    /// 2026-09-04 build's "not computed yet" wording had hidden it. The identity bit IS per-record once
    /// its family is live (both routes set it on every present), so it is the install window; inside
    /// it, every record that carries both sizes counts, whichever present it landed on.
    /// </remarks>
    public static UpscaleExtent? From(IReadOnlyList<FlFrameRecord> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        int start = RecordWindow.ClaimedSuffixStart(
            stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler));
        if (start == stream.Count)
        {
            // No identity window: fall back to the params bit itself, which is what a
            // params-only writer would publish per record.
            start = RecordWindow.ClaimedSuffixStart(
                stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.UpscalerParams));
        }

        if (start == stream.Count)
        {
            return null;
        }

        var groups = new Dictionary<(ushort, ushort, ushort, ushort), int>();
        int measured = 0;
        for (int i = start; i < stream.Count; i++)
        {
            FlFrameRecord r = stream[i];
            var mask = (FlMeasured)r.MeasuredMask;
            // BOTH bits, not just the values: a value set while its bit is clear is the writer's
            // defect (IsHonest counts it), and this must not quietly average it in.
            if (!mask.HasFlag(FlMeasured.UpscalerParams) || !mask.HasFlag(FlMeasured.OutputRes)
                || r.RenderW == 0 || r.RenderH == 0 || r.OutputW == 0 || r.OutputH == 0)
            {
                continue;
            }

            measured++;
            (ushort, ushort, ushort, ushort) key = (r.RenderW, r.RenderH, r.OutputW, r.OutputH);
            groups[key] = groups.GetValueOrDefault(key) + 1;
        }

        if (measured == 0)
        {
            return null;
        }

        KeyValuePair<(ushort, ushort, ushort, ushort), int> modal = groups.MaxBy(static g => g.Value);
        return new UpscaleExtent
        {
            RenderW = modal.Key.Item1,
            RenderH = modal.Key.Item2,
            OutputW = modal.Key.Item3,
            OutputH = modal.Key.Item4,
            Records = modal.Value,
            Measured = measured,
            DistinctGroups = groups.Count,
        };
    }
}
