namespace FrameLedger.Domain.Metrics;

/// <summary>
/// <c>03_METRICS</c> §Core definitions: a stutter is a frame more than twice the centered 19-frame rolling
/// median around it; stutter time is the share of the session those frames took.
/// </summary>
public static class StutterDetector
{
    /// <summary>The verdict, or null when the series is shorter than the window — <c>stutter_count = N/A</c>.</summary>
    /// <remarks>
    /// A session shorter than 19 application frames is already below the 30 s minimum session length (FR-3.6)
    /// and would be discarded anyway; reporting 0 for it would be a measured negative about nothing.
    /// </remarks>
    public static StutterResult? Detect(IReadOnlyList<double> frameTimesMs, int window = RollingMedian.DefaultWindow)
    {
        ArgumentNullException.ThrowIfNull(frameTimesMs);

        IReadOnlyList<double>? median = RollingMedian.Of(frameTimesMs, window);
        if (median is null)
        {
            return null;
        }

        var flags = new bool[frameTimesMs.Count];
        double total = 0;
        double stuttered = 0;
        for (int i = 0; i < flags.Length; i++)
        {
            double ft = frameTimesMs[i];
            total += ft;
            if (ft > 2 * median[i])
            {
                flags[i] = true;
                stuttered += ft;
            }
        }

        return new StutterResult
        {
            IsStutter = flags,
            TimePct = total > 0 ? stuttered / total * 100.0 : 0,
        };
    }

    /// <summary>
    /// <c>pso_stutter_pct</c>: the share of stutter frames whose ending sample compiled at least one pipeline
    /// state. Null when nothing stuttered, or when no sample claims <see cref="MeasuredFields.Pso"/>.
    /// </summary>
    public static double? PsoStutterPct(StutterResult stutter, FrameTimeSeries series, IReadOnlyList<FrameSample> stream)
    {
        ArgumentNullException.ThrowIfNull(stutter);
        ArgumentNullException.ThrowIfNull(series);
        ArgumentNullException.ThrowIfNull(stream);

        int stutters = 0;
        int withPso = 0;
        bool measured = false;
        for (int i = 0; i < stutter.IsStutter.Count; i++)
        {
            if (!stutter.IsStutter[i])
            {
                continue;
            }

            FrameSample ending = stream[series.EndingSample[i]];
            stutters++;
            if (ending.Claims(MeasuredFields.Pso))
            {
                measured = true;
                if (ending.PsoCreated > 0)
                {
                    withPso++;
                }
            }
        }

        return stutters > 0 && measured ? withPso * 100.0 / stutters : null;
    }
}
