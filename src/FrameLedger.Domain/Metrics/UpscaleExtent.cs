namespace FrameLedger.Domain.Metrics;

/// <summary>
/// The render → output extent a session ran at, taken as the MODAL tuple over the samples that claim
/// <see cref="MeasuredFields.UpscalerParams"/> and carry an output size.
/// </summary>
/// <remarks>
/// <para>
/// <b>Two measurements, one piece of arithmetic.</b> <c>renderW/H</c> come off the upscaler's own dispatch
/// and <c>outputW/H</c> off the swapchain the present hook read; the ratio is <c>03_METRICS</c> §Upscaling's
/// <c>sqrt((outW×outH)/(renW×renH))</c>, and the render scale is its reciprocal as a percentage. Measured
/// on Cyberpunk 2077 at <c>DLSS = Balanced</c>: 1485×835 against 2560×1440 is 1.72× and 58%.
/// </para>
/// <para>
/// <b>Modal, not mean, and the count of distinct tuples is published beside it.</b> Averaging across a
/// settings change is the classic way a benchmark number stops meaning anything.
/// </para>
/// </remarks>
public sealed record UpscaleExtent
{
    public required ushort RenderW { get; init; }

    public required ushort RenderH { get; init; }

    public required ushort OutputW { get; init; }

    public required ushort OutputH { get; init; }

    /// <summary>Samples that ran at exactly this tuple.</summary>
    public required int Records { get; init; }

    /// <summary>Samples in the window that carried both sizes, whatever their tuple.</summary>
    public required int Measured { get; init; }

    /// <summary>Distinct (render, output) tuples in the window. More than 1 means the settings moved.</summary>
    public required int DistinctGroups { get; init; }

    /// <summary><c>sqrt((outW×outH)/(renW×renH))</c>.</summary>
    public double Ratio => Math.Sqrt((double)OutputW * OutputH / ((double)RenderW * RenderH));

    /// <summary>The per-axis render scale as a percentage — <c>100 / Ratio</c>.</summary>
    public double RenderScalePercent => 100.0 / Ratio;

    /// <summary>The dominant extent over the identity-claiming window, or null when no sample in it carried both sizes.</summary>
    /// <remarks>
    /// <b>The window is the IDENTITY suffix, not the params suffix.</b> The writer publishes
    /// <see cref="MeasuredFields.UpscalerParams"/> only on the present that DRAINED a dispatch or tag, which under
    /// frame generation is one present in N; the identity bit is per-sample once its family is live, so it is the
    /// install window. Inside it, every sample that carries both sizes counts, whichever present it landed on.
    /// </remarks>
    public static UpscaleExtent? From(IReadOnlyList<FrameSample> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        int start = RecordWindow.ClaimedSuffixStart(stream, MeasuredFields.Upscaler);
        if (start == stream.Count)
        {
            // No identity window: fall back to the params bit itself, which is what a params-only writer
            // would publish per sample.
            start = RecordWindow.ClaimedSuffixStart(stream, MeasuredFields.UpscalerParams);
        }

        if (start == stream.Count)
        {
            return null;
        }

        var groups = new Dictionary<(ushort, ushort, ushort, ushort), int>();
        int measured = 0;
        for (int i = start; i < stream.Count; i++)
        {
            FrameSample s = stream[i];
            // BOTH bits, not just the values: a value set while its bit is clear is the writer's defect, and
            // this must not quietly average it in.
            if (!s.Claims(MeasuredFields.UpscalerParams | MeasuredFields.OutputRes)
                || s.RenderW == 0 || s.RenderH == 0 || s.OutputW == 0 || s.OutputH == 0)
            {
                continue;
            }

            measured++;
            (ushort, ushort, ushort, ushort) key = (s.RenderW, s.RenderH, s.OutputW, s.OutputH);
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
