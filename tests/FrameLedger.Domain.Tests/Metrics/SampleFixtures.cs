using FrameLedger.Domain.Metrics;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>
/// Builders for the metric goldens. Literal samples, no ring and no mapper: the calculators are pure
/// over <see cref="FrameSample"/>, which is what makes every row in <c>03_METRICS</c> testable without
/// a game.
/// </summary>
internal static class SampleFixtures
{
    /// <summary>A stand-in QPC frequency: one tick per microsecond.</summary>
    public const long Frequency = 1_000_000;

    /// <summary>Ticks per present at 240 Hz on <see cref="Frequency"/>.</summary>
    public const long Step = Frequency / 240;

    /// <summary>A present-only sample, as today's writer publishes before any feature hook is live.</summary>
    public static FrameSample Present(ulong qpc, uint swapchain = 1, ushort w = 3840, ushort h = 2160,
        MeasuredFields extra = MeasuredFields.None) => new()
        {
            Qpc = qpc,
            SwapchainId = swapchain,
            OutputW = w,
            OutputH = h,
            Api = FrameApi.D3D11,
            Measured = MeasuredFields.OutputRes | MeasuredFields.PresentArgs | extra,
        };

    /// <summary><paramref name="n"/> presents on one stream, <paramref name="step"/> ticks apart.</summary>
    public static List<FrameSample> Stream(int n, long step = Step, uint swapchain = 1, MeasuredFields extra = MeasuredFields.None)
    {
        var list = new List<FrameSample>(n);
        ulong qpc = 1_000_000;
        for (int i = 0; i < n; i++)
        {
            list.Add(Present(qpc, swapchain, extra: extra));
            qpc += (ulong)step;
        }

        return list;
    }

    /// <summary>
    /// The writer's frame-generation shape, reproduced exactly: <paramref name="appFrames"/> × <paramref name="k"/>
    /// samples, <paramref name="evalsPerFrame"/> evaluations drained by each group's FIRST present, and only
    /// the draining present carrying <see cref="FeatureBits.RayReconstructionObserved"/>.
    /// </summary>
    public static List<FrameSample> FgStream(int appFrames, int k, byte evalsPerFrame = 1, uint swapchain = 1,
        bool counted = true, ulong startQpc = 1_000_000)
    {
        List<FrameSample> stream = [];
        ulong qpc = startQpc;
        for (int f = 0; f < appFrames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                bool drains = p == 0;
                stream.Add(new FrameSample
                {
                    Qpc = qpc,
                    SwapchainId = swapchain,
                    Measured = MeasuredFields.OutputRes | MeasuredFields.PresentArgs
                               | (counted ? MeasuredFields.Fg | MeasuredFields.FgCounts : MeasuredFields.None),
                    FgEvaluations = drains ? evalsPerFrame : (byte)0,
                    FgMode = drains ? FgKind.DlssG : FgKind.Unknown,
                    Features = drains ? FeatureBits.RayReconstructionObserved : FeatureBits.None,
                });
                qpc += (ulong)Step;
            }
        }

        return stream;
    }

    /// <summary>Two runs back to back, on one monotonic clock across the join.</summary>
    public static List<FrameSample> Concat(List<FrameSample> first, List<FrameSample> second)
    {
        List<FrameSample> all = [.. first];
        ulong qpc = all[^1].Qpc;
        foreach (FrameSample s in second)
        {
            qpc += (ulong)Step;
            all.Add(s with { Qpc = qpc });
        }

        return all;
    }

    /// <summary>Frame times in ms → samples whose QPC deltas reproduce them exactly on <see cref="Frequency"/>.</summary>
    public static List<FrameSample> FromFrameTimes(IEnumerable<double> frameTimesMs)
    {
        List<FrameSample> stream = [Present(1_000_000)];
        ulong qpc = 1_000_000;
        foreach (double ms in frameTimesMs)
        {
            qpc += (ulong)Math.Round(ms * Frequency / 1000.0);
            stream.Add(Present(qpc));
        }

        return stream;
    }
}
