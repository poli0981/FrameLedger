using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// Splits a drained ring into segments, stream first and settings second.
/// </summary>
/// <remarks>
/// <para>
/// <b>The order is not the obvious one, and reversing it manufactures a segment per
/// present.</b> Two different axes exist and neither document mentions the other:
/// <c>03_METRICS</c> §Upscaling segments on a mid-session settings change, while
/// <c>fl_shm.h</c> §<c>swapchainId</c> says the Agent "segments by this value and
/// reports the dominant stream". They compose in one order only. Patching a vtable
/// slot patches the shared <c>dxgi.dll</c> class vtable, so ONE hook sees EVERY
/// swapchain in the process — a title with a separate UI or video swapchain
/// interleaves two streams in one ring, and splitting on resolution first would cut
/// a new segment every time the two alternate.
/// </para>
/// <para>
/// <b><c>swapchainId == 0</c> is one undifferentiated stream, never a valid id.</b>
/// The writer publishes 0 when it could not identify the swapchain — the fixed
/// 16-slot table overflowed, or <c>GetDesc</c> failed — so grouping by it is
/// grouping "everything we could not tell apart", and it must never be reported as
/// the dominant stream while a real one exists.
/// </para>
/// </remarks>
internal static class StreamSegmenter
{
    /// <summary>Every segment, in drain order, grouped by stream then split on output size.</summary>
    public static IReadOnlyList<Segment> Segment(IReadOnlyList<FlFrameRecord> records)
    {
        ArgumentNullException.ThrowIfNull(records);

        List<Segment> segments = [];
        foreach (IGrouping<uint, FlFrameRecord> stream in records.GroupBy(r => r.SwapchainId))
        {
            List<FlFrameRecord> current = [];
            ushort w = 0;
            ushort h = 0;

            // `w`/`h` alone cannot distinguish "no baseline yet" from "the last measured size", because
            // 0 is both the initial value and a legal ushort. Without this flag, a stream whose first
            // records are UNMEASURED and whose later ones are measured splits at the first measured
            // record and emits a leading segment reported as 0x0 — cutting a segment on the writer's
            // silence, which is exactly what the comment below forbids.
            bool haveBaseline = false;

            foreach (FlFrameRecord r in stream)
            {
                // Only a MEASURED size may split a segment. FL_MEASURED_OUTPUT_RES clear means the
                // writer had no size to report — the swapchain table overflowed, or GetDesc failed —
                // and treating 0x0 as a resolution change would cut a segment on the writer's silence.
                bool haveSize = ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.OutputRes);
                if (haveSize && haveBaseline && current.Count > 0 && (r.OutputW != w || r.OutputH != h))
                {
                    segments.Add(new Segment { SwapchainId = stream.Key, OutputW = w, OutputH = h, Records = current });
                    current = [];
                }

                if (haveSize)
                {
                    w = r.OutputW;
                    h = r.OutputH;
                    haveBaseline = true;
                }

                current.Add(r);
            }

            if (current.Count > 0)
            {
                segments.Add(new Segment { SwapchainId = stream.Key, OutputW = w, OutputH = h, Records = current });
            }
        }

        return segments;
    }

    /// <summary>
    /// The stream with the most presents, preferring an identified one.
    /// </summary>
    /// <returns>Empty when there is nothing to report.</returns>
    public static IReadOnlyList<FlFrameRecord> DominantStream(IReadOnlyList<FlFrameRecord> records)
    {
        ArgumentNullException.ThrowIfNull(records);

        List<IGrouping<uint, FlFrameRecord>> streams = [.. records.GroupBy(r => r.SwapchainId)];
        if (streams.Count == 0)
        {
            return [];
        }

        // Identified streams first, THEN by size. A ring where the unidentified bucket happens to be
        // the biggest must not report "the dominant stream" about records the writer said it could not
        // tell apart; falling back to it only when there is nothing else is the honest ordering.
        IGrouping<uint, FlFrameRecord>? best = streams
            .Where(g => g.Key != 0)
            .OrderByDescending(g => g.Count())
            .FirstOrDefault();

        return [.. (best ?? streams.OrderByDescending(g => g.Count()).First())];
    }
}
