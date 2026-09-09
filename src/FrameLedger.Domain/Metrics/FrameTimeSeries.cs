namespace FrameLedger.Domain.Metrics;

/// <summary>
/// <c>03_METRICS</c> §Frame times: <c>ft[i] = (qpc[i] − qpc[i−1]) / qpcFreq × 1000</c> ms, measured at present
/// entry, with the intervals that are not frame times left out rather than counted as long frames.
/// </summary>
/// <remarks>
/// <para>
/// <b>Data gaps are EXCLUDED, not merged.</b> Where the drain recorded a gap (a torn or overwritten slot,
/// <c>07_IPC</c> §Protocol rules), the interval spanning it is dropped from the series entirely. Counting it
/// as one long frame would fabricate a stutter — the exact artifact these metrics exist to detect honestly.
/// A golden test asserts the gap does not appear as a stutter.
/// </para>
/// <para>
/// Non-positive intervals are dropped for the reason <see cref="RecordWindow.SecondsOf{T}"/> gives: a drain
/// that laps the ring can return an older record after a newer one, and a negative delta is not a frame
/// time in either direction.
/// </para>
/// </remarks>
public sealed record FrameTimeSeries
{
    /// <summary>The kept intervals, in ms, in time order.</summary>
    public required IReadOnlyList<double> FrameTimesMs { get; init; }

    /// <summary>For each kept interval, the index of the sample that ENDS it — the frame the interval belongs to.</summary>
    public required IReadOnlyList<int> EndingSample { get; init; }

    /// <summary>Intervals excluded because a gap sat inside them.</summary>
    public required int ExcludedForGaps { get; init; }

    /// <summary>Intervals excluded because the clock did not advance across them.</summary>
    public required int ExcludedNonPositive { get; init; }

    /// <summary><c>D</c>: first present to last present, in seconds — the time base of every average.</summary>
    public required double DurationSeconds { get; init; }

    /// <summary>Presents in the stream.</summary>
    public required int Presents { get; init; }

    public int Count => FrameTimesMs.Count;

    /// <summary>
    /// The time-based average: <c>presents / D</c>, NOT the mean of instantaneous FPS values, which weights fast
    /// frames more than slow ones. Null without a duration to divide by.
    /// </summary>
    public double? AverageFps => DurationSeconds > 0 && Presents > 1 ? (Presents - 1) / DurationSeconds : null;

    /// <summary>
    /// Builds the series over one stream. <paramref name="gapBefore"/> holds the indices of samples that follow
    /// a gap: the interval ending at each of them is excluded.
    /// </summary>
    public static FrameTimeSeries From(IReadOnlyList<FrameSample> stream, IReadOnlySet<int>? gapBefore, long qpcFrequency)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(qpcFrequency);

        var times = new List<double>(Math.Max(0, stream.Count - 1));
        var ending = new List<int>(times.Capacity);
        int gaps = 0;
        int nonPositive = 0;
        for (int i = 1; i < stream.Count; i++)
        {
            if (gapBefore?.Contains(i) == true)
            {
                gaps++;
                continue;
            }

            long delta = (long)stream[i].Qpc - (long)stream[i - 1].Qpc;
            if (delta <= 0)
            {
                nonPositive++;
                continue;
            }

            times.Add(delta * 1000.0 / qpcFrequency);
            ending.Add(i);
        }

        double duration = stream.Count > 1
            ? Math.Max(0, ((long)stream[^1].Qpc - (long)stream[0].Qpc) / (double)qpcFrequency)
            : 0;

        return new FrameTimeSeries
        {
            FrameTimesMs = times,
            EndingSample = ending,
            ExcludedForGaps = gaps,
            ExcludedNonPositive = nonPositive,
            DurationSeconds = duration,
            Presents = stream.Count,
        };
    }
}
