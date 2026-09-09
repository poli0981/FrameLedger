namespace FrameLedger.Domain.Metrics;

/// <summary>
/// Finding the stretch of a drained stream a claim may be aggregated over.
/// </summary>
/// <remarks>
/// Generic over the element, because the same install-window rule governs the shared-memory record in
/// the capture host and the <see cref="FrameSample"/> here; the predicate is what says which claim.
/// </remarks>
public static class RecordWindow
{
    /// <summary>
    /// Index of the first record of the maximal SUFFIX on which <paramref name="claims"/> holds for every
    /// record; <c>stream.Count</c> when no such suffix exists.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>A suffix rather than a tolerance.</b> Feature hooks install lazily from the 1 Hz watchdog, so
    /// the opening of every session predates them — measured on Cyberpunk 2077, 292 of 10,169 records
    /// carry no upscaler bit — and a whole-stream <c>All</c> reported "no upscaler hook ran" about a
    /// session in which the hook was live for 97% of the presents. Excluding a leading prefix is safe
    /// because <c>hooksInstalledMask</c> is monotonic: a real install produces one clean boundary.
    /// </para>
    /// <para>
    /// <b>The boundary must be clean, or this is not an install window and nothing is aggregated.</b>
    /// Every record before the boundary must LACK the claim; a writer setting a bit intermittently would
    /// otherwise have its trailing run averaged as though it were a whole session.
    /// </para>
    /// </remarks>
    public static int ClaimedSuffixStart<T>(IReadOnlyList<T> stream, Func<T, bool> claims)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentNullException.ThrowIfNull(claims);

        int start = stream.Count;
        for (int i = stream.Count - 1; i >= 0 && claims(stream[i]); i--)
        {
            start = i;
        }

        for (int i = 0; i < start; i++)
        {
            if (claims(stream[i]))
            {
                return stream.Count;
            }
        }

        return start;
    }

    /// <summary>The samples from the first one claiming every bit of <paramref name="fields"/> to the end.</summary>
    public static int ClaimedSuffixStart(IReadOnlyList<FrameSample> stream, MeasuredFields fields) =>
        ClaimedSuffixStart(stream, s => s.Claims(fields));

    /// <summary>Seconds spanned by the intervals of <c>[start, stream.Count)</c>.</summary>
    /// <remarks>
    /// Non-positive intervals are skipped rather than summed. A drain that laps the ring can return an
    /// older record after a newer one — legitimately, because it resumes at the oldest survivor — and
    /// a negative delta is not a frame time in either direction.
    /// </remarks>
    public static double SecondsOf<T>(IReadOnlyList<T> stream, int start, long qpcFrequency, Func<T, ulong> qpcOf)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentNullException.ThrowIfNull(qpcOf);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(qpcFrequency);

        double seconds = 0;
        for (int i = start + 1; i < stream.Count; i++)
        {
            long delta = (long)qpcOf(stream[i]) - (long)qpcOf(stream[i - 1]);
            if (delta > 0)
            {
                seconds += (double)delta / qpcFrequency;
            }
        }

        return seconds;
    }

    /// <summary>Seconds spanned by the intervals of <c>[start, stream.Count)</c> over samples.</summary>
    public static double SecondsOf(IReadOnlyList<FrameSample> stream, int start, long qpcFrequency) =>
        SecondsOf(stream, start, qpcFrequency, static s => s.Qpc);
}
