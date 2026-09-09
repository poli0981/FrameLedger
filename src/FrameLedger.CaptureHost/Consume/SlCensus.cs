using System.Globalization;
using System.Text;
using FrameLedger.Domain.Metrics;
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
    /// <b><see cref="WithFrameGeneration"/> IS NOT AN ID OBSERVATION, and must never be quoted
    /// as one beside <c>fgEvaluations</c>.</b>
    /// </summary>
    /// <remarks>
    /// <para>
    /// It is <c>fgEvaluations &gt; 0</c> restated — the same byte read twice — because
    /// <c>FL_SL_SEEN_RETIRED_DLSS_G</c> was retired deliberately so frame generation travels
    /// only as the count (<c>dllmain.cpp</c> §FlSlSeen), and <see cref="FlFeatureFlags"/> has
    /// no free fact bit left to carry a raw one. So "the census says zero" and "every record
    /// says zero" are ONE measurement with one provenance, and a ledger entry citing them as
    /// two corroborating lines would be manufacturing agreement out of a single fact.
    /// </para>
    /// <para>
    /// What genuinely corroborates a zero here is <see cref="WithUndecoded"/>: an id matching
    /// none of the four decoded constants falls to <c>FL_SL_SEEN_OTHER</c> and lights that
    /// bucket, so <c>UNDECODED == 0</c> excludes the most likely silent failure — a vendored
    /// <c>sl::kFeatureDLSS_G</c> constant that does not match the runtime id. That is a
    /// different bit on a different path, and it is the one to cite.
    /// </para>
    /// </remarks>
    public static string FrameGenerationProvenance =>
        "fgEvaluations>0 restated — not an independent id observation";

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

            // THE RAW FACT, NOT THE DECODED BYTE. Deriving "a super-resolution id arrived"
            // from `upscaler == Dlss` made this census move when the DECODE moved: correcting
            // the decode to report DLSS for kFeatureDLSS_RR — right, because RR performs the
            // upscale — made the census claim 2,569 arrivals of kFeatureDLSS on a title that
            // evaluates it zero times. An instrument that tracks its own subject cannot be
            // evidence about it, and this census exists precisely to be that evidence (§S30).
            bool superResolution = ((FlFeatureFlags)r.FeatureFlags).HasFlag(FlFeatureFlags.SlSuperResolution);
            var upscaler = (FlUpscaler)r.Upscaler;
            n[_idxDlss] += superResolution && upscaler == FlUpscaler.Dlss ? 1 : 0;
            n[_idxNis] += superResolution && upscaler == FlUpscaler.Nis ? 1 : 0;
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
          .Append("  UNDECODED=").AppendLine(N(WithUndecoded));

        // NOT ON THE ID LINE. Every other column there is a distinct bit decoded from the
        // drain word; this one is the fgEvaluations byte restated, and printing it in the
        // same row invites citing it as a second witness to its own value.
        sb.Append("    records with fgEvaluations>0: ").Append(N(WithFrameGeneration))
          .Append("  (").Append(FrameGenerationProvenance).AppendLine(")");

        // The §S30 shape, named rather than left for a reader to compute.
        sb.Append("    batches generating frames with NO super-resolution id: ")
          .AppendLine(N(FgWithoutSuperResolution));
        return sb.ToString().TrimEnd();
    }

    private static string N(int v) => v.ToString(CultureInfo.InvariantCulture);
}
