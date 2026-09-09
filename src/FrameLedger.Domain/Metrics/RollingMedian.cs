namespace FrameLedger.Domain.Metrics;

/// <summary>
/// The centered rolling median <c>03_METRICS</c>' stutter rule divides by, with the edge rule it specifies.
/// </summary>
/// <remarks>
/// <b>Rolling-median edges.</b> The window is centered and measured in <i>frames</i>, so the first and last
/// <c>(window − 1) / 2</c> frames have no full window. They use a <b>truncated symmetric window</b> — the largest
/// centered odd window that fits — not a padded or partial-shifted one: the alternatives either invent data or
/// silently bias the first samples. Golden tests pin both edges.
/// </remarks>
public static class RollingMedian
{
    /// <summary><c>03_METRICS</c>' window: 19 frames.</summary>
    public const int DefaultWindow = 19;

    /// <summary>The rolling median at every index, or null when the series is shorter than the window.</summary>
    public static IReadOnlyList<double>? Of(IReadOnlyList<double> series, int window = DefaultWindow)
    {
        ArgumentNullException.ThrowIfNull(series);
        ArgumentOutOfRangeException.ThrowIfLessThan(window, 1);
        if (window % 2 == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(window), window, "The window is centered, so it must be odd.");
        }

        int n = series.Count;
        if (n < window)
        {
            return null;
        }

        int maxHalf = (window - 1) / 2;
        var result = new double[n];
        double[] scratch = new double[window];
        for (int i = 0; i < n; i++)
        {
            // The largest centered odd window that fits: shrink symmetrically toward the edges.
            int half = Math.Min(maxHalf, Math.Min(i, n - 1 - i));
            int count = (2 * half) + 1;
            for (int k = 0; k < count; k++)
            {
                scratch[k] = series[i - half + k];
            }

            Array.Sort(scratch, 0, count);
            result[i] = scratch[half];
        }

        return result;
    }
}
