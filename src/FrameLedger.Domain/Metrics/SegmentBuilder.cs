namespace FrameLedger.Domain.Metrics;

/// <summary>
/// Splits a drained ring into segments, stream first and settings second.
/// </summary>
/// <remarks>
/// <para>
/// <b>The order is not the obvious one, and reversing it manufactures a segment per present.</b>
/// <c>03_METRICS</c> §Upscaling segments on a mid-session settings change; the writer says the Agent
/// "segments by <c>swapchainId</c> and reports the dominant stream". They compose in one order only:
/// patching a vtable slot patches the shared <c>dxgi.dll</c> class vtable, so ONE hook sees EVERY
/// swapchain in the process — a title with a separate UI or video swapchain interleaves two streams in
/// one ring, and splitting on resolution first would cut a new segment every time the two alternate.
/// </para>
/// <para>
/// <b><c>swapchainId == 0</c> is one undifferentiated stream, never a valid id.</b> The writer publishes 0
/// when it could not identify the swapchain, so grouping by it is grouping "everything we could not tell
/// apart", and it must never be reported as the dominant stream while a real one exists.
/// </para>
/// </remarks>
public static class SegmentBuilder
{
    /// <summary>Every segment, in drain order, grouped by stream then split on a MEASURED output size.</summary>
    public static IReadOnlyList<Segment> Build(IReadOnlyList<FrameSample> samples)
    {
        ArgumentNullException.ThrowIfNull(samples);

        List<Segment> segments = [];
        foreach (IGrouping<uint, FrameSample> stream in samples.GroupBy(s => s.SwapchainId))
        {
            List<FrameSample> current = [];
            ushort w = 0;
            ushort h = 0;

            // `w`/`h` alone cannot distinguish "no baseline yet" from "the last measured size", because 0 is
            // both the initial value and a legal ushort. Without this flag a stream whose first samples are
            // UNMEASURED splits at the first measured one and emits a leading 0x0 segment — cutting a segment
            // on the writer's silence.
            bool haveBaseline = false;

            foreach (FrameSample s in stream)
            {
                // Only a MEASURED size may split a segment: OutputRes clear means the writer had no size to
                // report, and treating 0x0 as a resolution change would cut a segment on its silence.
                bool haveSize = s.Claims(MeasuredFields.OutputRes);
                if (haveSize && haveBaseline && current.Count > 0 && (s.OutputW != w || s.OutputH != h))
                {
                    segments.Add(new Segment { SwapchainId = stream.Key, OutputW = w, OutputH = h, Samples = current });
                    current = [];
                }

                if (haveSize)
                {
                    w = s.OutputW;
                    h = s.OutputH;
                    haveBaseline = true;
                }

                current.Add(s);
            }

            if (current.Count > 0)
            {
                segments.Add(new Segment { SwapchainId = stream.Key, OutputW = w, OutputH = h, Samples = current });
            }
        }

        return segments;
    }

    /// <summary>The stream with the most presents, preferring an identified one. Empty when there is nothing.</summary>
    public static IReadOnlyList<FrameSample> DominantStream(IReadOnlyList<FrameSample> samples) =>
        DominantStream(samples, static s => s.SwapchainId);

    /// <summary>
    /// The stream with the most elements, preferring an identified one (key ≠ 0). Generic so the capture
    /// host can apply the same rule to the shared-memory record it holds.
    /// </summary>
    public static IReadOnlyList<T> DominantStream<T>(IReadOnlyList<T> records, Func<T, uint> swapchainOf)
    {
        ArgumentNullException.ThrowIfNull(records);
        ArgumentNullException.ThrowIfNull(swapchainOf);

        List<IGrouping<uint, T>> streams = [.. records.GroupBy(swapchainOf)];
        if (streams.Count == 0)
        {
            return [];
        }

        // Identified streams first, THEN by size. A ring where the unidentified bucket happens to be the
        // biggest must not report "the dominant stream" about records the writer said it could not tell
        // apart; falling back to it only when there is nothing else is the honest ordering.
        IGrouping<uint, T>? best = streams
            .Where(g => g.Key != 0)
            .OrderByDescending(g => g.Count())
            .FirstOrDefault();

        return [.. (best ?? streams.OrderByDescending(g => g.Count()).First())];
    }
}
