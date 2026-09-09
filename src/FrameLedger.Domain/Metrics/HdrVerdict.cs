namespace FrameLedger.Domain.Metrics;

/// <summary>HDR as a tri-state: the last colour space in the claiming window, or N/A when no hook was live.</summary>
public static class HdrVerdict
{
    public static Tri Of(IReadOnlyList<FrameSample> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        int start = RecordWindow.ClaimedSuffixStart(stream, MeasuredFields.Hdr);
        if (start == stream.Count)
        {
            return Tri.NotApplicable;
        }

        return stream[^1].ColorSpace is ColorSpaceKind.Hdr10 or ColorSpaceKind.ScRgb ? Tri.Yes : Tri.No;
    }
}
