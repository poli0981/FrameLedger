using System.Globalization;
using System.Text;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The longest present-to-present intervals in a stream, each placed against the moments this host
/// touched the target — so "the game dropped to 1 FPS for a second" can be read as ours or not ours.
/// </summary>
/// <remarks>
/// <para>
/// <b>Why this exists.</b> The owner saw Cyberpunk 2077 fall to 1 FPS for about a second under capture
/// (2026-09-05) and the report had nothing to say about it: no frame-time statistic is computed by this
/// throwaway (P2's calculators own them), and nothing correlated a gap with what FrameLedger was doing.
/// FrameLedger touches the target at known moments — the guard's 30 s re-scan enumerates its modules,
/// and the module snapshot reads the same list beside it — and the records carry QPC, the same clock
/// <see cref="System.Diagnostics.Stopwatch.GetTimestamp"/> reads. So a stall can be placed: within a
/// touch window it is a suspect; far from every touch it is the title's own (a shader compile, a
/// level stream, a menu).
/// </para>
/// <para>
/// <b>Not a stutter metric.</b> <c>03_METRICS</c> defines stutter against a rolling median with edge
/// rules and a sufficiency floor, and this prints none of that. It prints the few longest raw
/// intervals and where they sit, which is a diagnostic, and it says so.
/// </para>
/// </remarks>
internal sealed record StallReport
{
    /// <summary>One long interval: where it starts in the session, how long it is, and the nearest touch.</summary>
    internal sealed record Stall(double AtSeconds, double Milliseconds, double? NearestTouchSeconds)
    {
        /// <summary>A touch inside the interval or within <see cref="TouchWindowSeconds"/> of it is a suspect.</summary>
        public bool NearATouch => NearestTouchSeconds is double d && Math.Abs(d) <= TouchWindowSeconds;
    }

    /// <summary>How far a host touch may be from a stall and still be named beside it.</summary>
    public const double TouchWindowSeconds = 1.0;

    /// <summary>Intervals at or above this are counted as stalls — ten presents' worth at 100 FPS.</summary>
    public const double StallThresholdMs = 100.0;

    public required IReadOnlyList<Stall> Longest { get; init; }

    public required int OverThreshold { get; init; }

    public required int Intervals { get; init; }

    public required int Touches { get; init; }

    public static StallReport From(IReadOnlyList<FlFrameRecord> stream, long qpcFrequency, IReadOnlyList<long> touchQpc,
        int keep = 3)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentNullException.ThrowIfNull(touchQpc);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(qpcFrequency);

        var stalls = new List<Stall>();
        int over = 0;
        int intervals = 0;
        if (stream.Count > 1)
        {
            long origin = (long)stream[0].Qpc;
            var candidates = new List<(long StartQpc, long DeltaQpc)>();
            for (int i = 1; i < stream.Count; i++)
            {
                long delta = (long)stream[i].Qpc - (long)stream[i - 1].Qpc;
                if (delta <= 0)
                {
                    continue;    // a torn or reordered record; the drain reports gaps separately
                }

                intervals++;
                if (delta * 1000.0 / qpcFrequency >= StallThresholdMs)
                {
                    over++;
                }

                candidates.Add(((long)stream[i - 1].Qpc, delta));
            }

            foreach ((long startQpc, long deltaQpc) in candidates.OrderByDescending(c => c.DeltaQpc).Take(keep))
            {
                double at = (startQpc - origin) / (double)qpcFrequency;
                double ms = deltaQpc * 1000.0 / qpcFrequency;
                double? nearest = null;
                foreach (long t in touchQpc)
                {
                    // Distance from the touch to the interval [start, start + delta]: 0 when inside.
                    double d = t < startQpc ? (t - startQpc) / (double)qpcFrequency
                             : t > startQpc + deltaQpc ? (t - (startQpc + deltaQpc)) / (double)qpcFrequency
                             : 0.0;
                    if (nearest is null || Math.Abs(d) < Math.Abs(nearest.Value))
                    {
                        nearest = d;
                    }
                }

                stalls.Add(new Stall(at, ms, nearest));
            }
        }

        return new StallReport { Longest = stalls, OverThreshold = over, Intervals = intervals, Touches = touchQpc.Count };
    }

    /// <summary>The report lines. A diagnostic, and it names itself as one.</summary>
    public string Describe()
    {
        if (Intervals == 0)
        {
            return "  stalls: no interval to read (fewer than two presents)";
        }

        var sb = new StringBuilder();
        sb.Append("  stalls (diagnostic, not 03_METRICS' stutter): ").Append(Num(OverThreshold))
          .Append(" interval(s) of ").Append(Num(Intervals)).Append(" at or over ")
          .Append(Ms(StallThresholdMs)).Append("; host touches (guard scan + module snapshot): ").Append(Num(Touches));
        foreach (Stall s in Longest)
        {
            sb.AppendLine();
            sb.Append("    longest: ").Append(Ms(s.Milliseconds)).Append(" at t=").Append(Sec(s.AtSeconds)).Append(" — ");
            if (s.NearestTouchSeconds is not double d)
            {
                sb.Append("no host touch recorded");
            }
            else if (s.NearATouch)
            {
                sb.Append("A HOST TOUCH ").Append(d == 0 ? "INSIDE the interval" : (Math.Abs(d).ToString("0.00", CultureInfo.InvariantCulture) + " s "
                          + (d < 0 ? "before" : "after") + " it"))
                  .Append(" — FrameLedger is a SUSPECT for this one; re-run with a longer --seconds and see whether it recurs on the 30 s cadence");
            }
            else
            {
                sb.Append("nearest host touch ").Append(Math.Abs(d).ToString("0.0", CultureInfo.InvariantCulture)).Append(" s away — not ours");
            }
        }

        return sb.ToString();
    }

    private static string Num(long v) => v.ToString(CultureInfo.InvariantCulture);

    private static string Ms(double v) => v.ToString("0.#", CultureInfo.InvariantCulture) + " ms";

    private static string Sec(double v) => v.ToString("0.00", CultureInfo.InvariantCulture) + " s";
}
