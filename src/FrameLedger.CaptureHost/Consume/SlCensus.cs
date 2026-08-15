using System.Globalization;
using System.Text;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// Which Streamline feature ids actually reached the writer, counted rather than assumed.
/// </summary>
/// <remarks>
/// <para>
/// <b>This exists to answer §S30, and it is the step that has to come before the fix.</b>
/// On Cyberpunk 2077 every one of 2,461 params-carrying records decoded to
/// <c>FL_UPSCALER_UNKNOWN</c> while the title was demonstrably running DLSS. The batches
/// that reached a present held <c>DLSS_G</c> / <c>DLSS_RR</c> / an undecoded id and not
/// <c>kFeatureDLSS</c> — but WHICH ids arrive was never printed, so both candidate fixes
/// (accumulate identity across presents to an application-frame boundary, or prefer a
/// super-resolution id over a frame-generation one) are guesses, and they give different
/// answers for a title that switches upscaler mid-session.
/// <c>docs/HANDOFF.md</c> forbids the decode change before this measurement by name:
/// it would turn a wrong answer into a confident wrong answer.
/// </para>
/// <para>
/// <b>Derived from the record, and only possible because of the bit PR 3.2 added.</b>
/// Once frame generation is carried as a count rather than a feature bit, a present that
/// drained <c>kFeatureDLSS_G</c> together with any id falling to <c>FL_SL_SEEN_OTHER</c>
/// — Reflex, PCL, DeepDVC, Latewarp, DirectSR — is byte-identical to one that drained
/// DLSS-G alone. <see cref="FlFeatureFlags.SlUndecoded"/> is what keeps them apart, so
/// <see cref="WithUndecoded"/> can be non-zero on a title that also generates frames.
/// Without it this count would read ZERO on a title evaluating an undecoded id every
/// application frame, and a decision table keyed on it would close §S30 on a number that
/// could not have been anything else.
/// </para>
/// </remarks>
internal sealed record SlCensus
{
    /// <summary>Presents that drained a non-empty Streamline word.</summary>
    public required int Batches { get; init; }

    /// <summary>Records in the window, whether or not they drained anything.</summary>
    public required int Records { get; init; }

    public required int WithDlss { get; init; }

    public required int WithNis { get; init; }

    public required int WithRayReconstruction { get; init; }

    public required int WithFrameGeneration { get; init; }

    /// <summary>Batches that carried a feature id the Overlay does not decode.</summary>
    public required int WithUndecoded { get; init; }

    /// <summary>
    /// Batches that generated frames without any super-resolution id — §S30's exact shape.
    /// </summary>
    public required int FgWithoutSuperResolution { get; init; }

    /// <summary>Builds the census over the window the identity hook governs.</summary>
    public static SlCensus From(IReadOnlyList<FlFrameRecord> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        int start = RecordWindow.ClaimedSuffixStart(
            stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler));
        return start == stream.Count ? Empty : Tally(stream, start);
    }

    /// <summary>Bucket indices into the tally array, in the order <see cref="Describe"/> prints them.</summary>
    private const int _idxDlss = 0;
    private const int _idxNis = 1;
    private const int _idxRr = 2;
    private const int _idxFg = 3;
    private const int _idxUndecoded = 4;
    private const int _idxFgOnly = 5;

    private static SlCensus Tally(IReadOnlyList<FlFrameRecord> stream, int start)
    {
        int[] n = new int[6];
        int batches = 0;
        for (int i = start; i < stream.Count; i++)
        {
            FlFrameRecord r = stream[i];
            var flags = (FlFeatureFlags)r.FeatureFlags;
            if (!flags.HasFlag(FlFeatureFlags.RayReconstructionObserved))
            {
                continue;    // nothing was drained into this present
            }

            batches++;
            var upscaler = (FlUpscaler)r.Upscaler;
            bool superResolution = upscaler is FlUpscaler.Dlss or FlUpscaler.Nis;
            n[_idxDlss] += upscaler == FlUpscaler.Dlss ? 1 : 0;
            n[_idxNis] += upscaler == FlUpscaler.Nis ? 1 : 0;
            n[_idxRr] += flags.HasFlag(FlFeatureFlags.RayReconstruction) ? 1 : 0;
            n[_idxUndecoded] += flags.HasFlag(FlFeatureFlags.SlUndecoded) ? 1 : 0;
            n[_idxFg] += r.FgEvaluations > 0 ? 1 : 0;
            n[_idxFgOnly] += r.FgEvaluations > 0 && !superResolution ? 1 : 0;
        }

        return new SlCensus
        {
            Batches = batches,
            Records = stream.Count - start,
            WithDlss = n[_idxDlss],
            WithNis = n[_idxNis],
            WithRayReconstruction = n[_idxRr],
            WithFrameGeneration = n[_idxFg],
            WithUndecoded = n[_idxUndecoded],
            FgWithoutSuperResolution = n[_idxFgOnly],
        };
    }

    private static SlCensus Empty => new()
    {
        Batches = 0,
        Records = 0,
        WithDlss = 0,
        WithNis = 0,
        WithRayReconstruction = 0,
        WithFrameGeneration = 0,
        WithUndecoded = 0,
        FgWithoutSuperResolution = 0,
    };

    /// <summary>One line per bucket, in the shape §S30's decision table reads.</summary>
    public string Describe()
    {
        if (Records == 0)
        {
            return "  Streamline id census: no record claimed FL_MEASURED_UPSCALER, so no id was seen";
        }

        var sb = new StringBuilder();
        sb.Append("  Streamline id census over ").Append(N(Records)).Append(" record(s): ")
          .Append(N(Batches)).AppendLine(" drained a batch");
        sb.Append("    kFeatureDLSS=").Append(N(WithDlss))
          .Append("  kFeatureNIS=").Append(N(WithNis))
          .Append("  kFeatureDLSS_RR=").Append(N(WithRayReconstruction))
          .Append("  kFeatureDLSS_G=").Append(N(WithFrameGeneration))
          .Append("  UNDECODED=").AppendLine(N(WithUndecoded));

        // The §S30 shape, named rather than left for a reader to compute.
        sb.Append("    batches generating frames with NO super-resolution id: ")
          .AppendLine(N(FgWithoutSuperResolution));
        return sb.ToString().TrimEnd();
    }

    private static string N(int v) => v.ToString(CultureInfo.InvariantCulture);
}
