using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// Finding the stretch of a drained stream a claim may be aggregated over.
/// </summary>
internal static class RecordWindow
{
    /// <summary>
    /// Index of the first record of the maximal SUFFIX on which <paramref name="claims"/>
    /// holds for every record; <c>stream.Count</c> when no such suffix exists.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>This replaces <c>stream.All(...)</c>, which was unsatisfiable by construction.</b>
    /// Feature hooks install lazily from the 1 Hz watchdog, so the opening of every session
    /// predates them — measured on Cyberpunk 2077, 292 of 10,169 records carry no
    /// <see cref="FlMeasured.Upscaler"/> bit (<c>spike-notes</c> §8). A whole-stream
    /// <c>All</c> therefore reported "no upscaler hook ran" about a session in which the
    /// hook was live for 97% of the presents.
    /// </para>
    /// <para>
    /// A SUFFIX rather than a tolerance, because the property being asserted has not
    /// changed: every record we aggregate must carry the bit. Excluding a leading prefix
    /// is safe precisely because <see cref="FlWriterState.HooksInstalledMask"/> is
    /// monotonic (<c>fl_shm.h</c> §FlHookFamily) — a real install produces one clean
    /// boundary, so a writer that sets the bit intermittently still fails to be
    /// aggregated instead of being averaged over its gaps.
    /// </para>
    /// <para>
    /// Takes a predicate and not an <see cref="FlMeasured"/> on purpose: the axes this has
    /// to cover do not all live in <c>measuredMask</c>. See
    /// <c>MeasuredFacts.RayReconstructionOf</c>, which is gated on
    /// <see cref="FlFeatureFlags"/> and is deliberately NOT a caller.
    /// </para>
    /// </remarks>
    public static int ClaimedSuffixStart(IReadOnlyList<FlFrameRecord> stream, Func<FlFrameRecord, bool> claims)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentNullException.ThrowIfNull(claims);

        int start = stream.Count;
        for (int i = stream.Count - 1; i >= 0 && claims(stream[i]); i--)
        {
            start = i;
        }

        // THE BOUNDARY MUST BE CLEAN, or this is not an install window and nothing is
        // aggregated. hooksInstalledMask is monotonic, so a real install sets the bit once
        // and never clears it: every record before the boundary must LACK it. Without this,
        // a writer setting the bit intermittently would have its trailing run averaged as
        // though it were a whole session — the same "a value nobody measured over that
        // interval" defect, reached from the other side, and silently, because a trailing
        // run always looks like a clean suffix on its own.
        for (int i = 0; i < start; i++)
        {
            if (claims(stream[i]))
            {
                return stream.Count;
            }
        }

        return start;
    }

    /// <summary>Seconds spanned by the intervals of <c>[start, stream.Count)</c>.</summary>
    /// <remarks>
    /// Non-positive intervals are skipped rather than summed. A drain that laps the ring
    /// can return an older record after a newer one — legitimately, because
    /// <c>Drain</c> resumes at the oldest survivor — and a negative delta is not a frame
    /// time in either direction.
    /// </remarks>
    public static double SecondsOf(IReadOnlyList<FlFrameRecord> stream, int start, long qpcFrequency)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(qpcFrequency);

        double seconds = 0;
        for (int i = start + 1; i < stream.Count; i++)
        {
            long delta = (long)stream[i].Qpc - (long)stream[i - 1].Qpc;
            if (delta > 0)
            {
                seconds += (double)delta / qpcFrequency;
            }
        }

        return seconds;
    }
}
