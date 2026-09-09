namespace FrameLedger.Domain.Metrics;

/// <summary>
/// The samples a frame-generation factor may be computed from, and — far more often — the reason it
/// may not be.
/// </summary>
/// <remarks>
/// <para>
/// <b>ONE RECORD SET FOR BOTH SIDES OF THE RATIO.</b> <c>F_disp</c> is presents and <c>F_app</c> is
/// <c>Σ fgEvaluations</c>, and taking them over different sets is not a rounding error: the writer's
/// drain word is one process-wide word drained by whichever present arrives first, so an evaluation
/// belonging to the game's frame can be consumed by a UI swapchain's present. This is built over
/// <b>every</b> sample in one QPC span, and where that span cannot be attributed to a single stream
/// it refuses instead.
/// </para>
/// <para>
/// <b>Every refusal here is a number a consumer would otherwise have published.</b> CLAUDE.md rule 6
/// forbids a single inflated FPS number; the ways to reach one from honest samples are all arithmetic,
/// and each has its own <see cref="FgRefusalKind"/>.
/// </para>
/// <para>
/// <b><see cref="NativeFps"/> is computed independently and not as <c>DisplayedFps / Factor</c>.</b>
/// Derived, the rule-6 trio is internally consistent by construction and a reader can draw no
/// conclusion from that consistency; computed separately, <c>Native × Factor ≈ Displayed</c> becomes a
/// property a test can check.
/// </para>
/// </remarks>
public sealed record FgWindow
{
    /// <summary>Presents in the span — every sample, not only the dominant stream.</summary>
    public required int Presents { get; init; }

    /// <summary><c>Σ fgEvaluations</c> over exactly those samples.</summary>
    public required long Evaluations { get; init; }

    /// <summary>Presents that drained a non-empty Streamline word (<see cref="FeatureBits.RayReconstructionObserved"/>).</summary>
    public required int Batches { get; init; }

    /// <summary>Seconds spanned by the window's intervals.</summary>
    public required double Seconds { get; init; }

    /// <summary>Samples the writer could not attribute to a swapchain.</summary>
    public required int Unidentified { get; init; }

    /// <summary>Distinct identified swapchains presenting inside the span.</summary>
    public required int Streams { get; init; }

    /// <summary>Samples whose count hit the byte's ceiling.</summary>
    public required int Saturated { get; init; }

    /// <summary>
    /// Σ <c>dxgiUnseen</c> over the samples that claim <see cref="MeasuredFields.DxgiPresents"/>: presents DXGI
    /// counted on the hooked chain that this hook never saw.
    /// </summary>
    public required long DxgiUnseen { get; init; }

    /// <summary>Samples claiming <see cref="MeasuredFields.DxgiPresents"/> — the byte's own denominator.</summary>
    public required int DxgiClaiming { get; init; }

    /// <summary>Samples whose <c>dxgiUnseen</c> hit 255, the saturation sentinel; any means a refusal.</summary>
    public required int DxgiSaturated { get; init; }

    /// <summary>
    /// The presents the window counts as DISPLAYED: the ones this hook timed plus the ones DXGI counted on
    /// the same chain and this hook never saw. The numerator of <see cref="Factor"/>, <see cref="DisplayedFps"/>
    /// and <see cref="PresentsPerBatch"/> — never of a frame-time distribution, which has no timestamps for them.
    /// </summary>
    public long DisplayedPresents => Presents + DxgiUnseen;

    /// <summary>True when Displayed is a count DXGI made rather than this hook — the report must say so.</summary>
    public bool DxgiCounted => DxgiUnseen > 0;

    /// <summary>How many samples carried 0, 1, 2 … evaluations. Index is the value; the last slot lumps everything above it.</summary>
    public required IReadOnlyList<int> Histogram { get; init; }

    /// <summary>Per-bucket factors, in time order. Empty when the window is too short to bucket.</summary>
    public required IReadOnlyList<double> BucketFactors { get; init; }

    /// <summary>Per-bucket <c>presents / batches</c>, in time order. Same slicing as <see cref="BucketFactors"/>, a different weight.</summary>
    /// <remarks>
    /// <b>This exists because the other one cannot see the case that occurred.</b> <see cref="BucketFactors"/>
    /// divides by <c>Σ fgEvaluations</c>, which is zero on every sample on one route a real title has been
    /// measured on — so every bucket is identical, the uniformity check passes vacuously, and a window
    /// that mixed two frame-generation states looks clean. Measured 2026-08-16: an alt-tab mid-capture on
    /// Cyberpunk 2077 produced an achieved <c>presents / batch</c> of 1.84 against a title configured for ×2.
    /// </remarks>
    public required IReadOnlyList<double> BatchFactors { get; init; }

    /// <summary>Why no factor may be published, or null when one may.</summary>
    public required FgRefusal? Refusal { get; init; }

    /// <summary><c>presents / Σ evaluations</c>, or null.</summary>
    public double? Factor =>
        Refusal is null && Evaluations > 0 ? DisplayedPresents / (double)Evaluations : null;

    /// <summary><c>F_disp</c> over this window's own intervals.</summary>
    public double? DisplayedFps =>
        Refusal is null && Seconds > 0 && DisplayedPresents > 1 ? (DisplayedPresents - 1) / Seconds : null;

    /// <summary><c>F_app</c>, counted rather than derived.</summary>
    public double? NativeFps => Refusal is null && Seconds > 0 ? Evaluations / Seconds : null;

    /// <summary>
    /// Evaluations per drained batch — the oracle-free premise check, published whether or not a factor is.
    /// It is the only check that catches the k-per-frame case: three evaluations per application frame at
    /// ×4 yields a factor of 1.34, which every other guard passes.
    /// </summary>
    public double? EvaluationsPerBatch => Batches > 0 ? Evaluations / (double)Batches : null;

    /// <summary>
    /// Presents per drained Streamline batch — a <b>PROXY</b>, and never <see cref="Factor"/>. A batch is "a present
    /// that drained a Streamline evaluation", not an application frame.
    /// </summary>
    public double? PresentsPerBatch => Batches > 0 ? DisplayedPresents / (double)Batches : null;

    /// <summary>Why <see cref="PresentsPerBatch"/> may not be read as one configuration's number, or null when it may.</summary>
    /// <remarks>
    /// Separate from <see cref="Refusal"/> because, on the route that actually runs, <see cref="Refusal"/> is
    /// <b>always</b> non-null — it stops at "no evaluation was counted" before uniformity is ever considered —
    /// and a reader who took that as the last word would read the proxy beneath it with nothing behind it.
    /// </remarks>
    public FgRefusal? BatchRefusal => RefusalForBatches(this);

    /// <summary>Buckets the window is split into for the FG-state uniformity check.</summary>
    public const int Buckets = 8;

    /// <summary>Samples a bucket needs before its factor means anything.</summary>
    public const int MinPerBucket = 8;

    /// <summary>The window length below which uniformity cannot be checked and nothing is published.</summary>
    public const int MinSamplesToCheck = Buckets * MinPerBucket;

    /// <summary>How far a bucket's factor may sit from the window's before this refuses.</summary>
    public const double BucketTolerance = 0.25;

    /// <summary>
    /// At or above this, frame generation is ACTIVE: <c>03_METRICS</c>' cadence threshold, and the value below
    /// which the one premise of the token producer could not be told apart from "none" until row P1 landed.
    /// </summary>
    public const double ActiveThreshold = 1.5;

    /// <summary>
    /// At or below this, frame generation is <c>none</c>: every present carried an application frame.
    /// Measured 1.00 on two off legs; the 5% is drain jitter at the window's edges.
    /// </summary>
    public const double NoneCeiling = 1.05;

    /// <summary><c>none</c>, reached by counting: a published factor at or below <see cref="NoneCeiling"/>.</summary>
    public bool IsNone => Factor is double f && f <= NoneCeiling;

    /// <summary>Frame generation is active: a published factor at or above <see cref="ActiveThreshold"/>.</summary>
    public bool IsActive => Factor is double f && f >= ActiveThreshold;

    /// <summary>Builds the window over EVERY drained sample, or the reason there is none.</summary>
    public static FgWindow From(IReadOnlyList<FrameSample> all, long qpcFrequency)
    {
        ArgumentNullException.ThrowIfNull(all);

        int start = RecordWindow.ClaimedSuffixStart(all, MeasuredFields.FgCounts);
        if (start == all.Count)
        {
            return Nothing(new FgRefusal(FgRefusalKind.NotCounted, FgRefusalSubject.Factor));
        }

        FgWindow tallied = Tally(all, start, qpcFrequency);
        return tallied with { Refusal = RefusalFor(tallied) };
    }

    private static FgWindow Nothing(FgRefusal refusal) => new()
    {
        Presents = 0,
        Evaluations = 0,
        Batches = 0,
        Seconds = 0,
        Unidentified = 0,
        Streams = 0,
        Saturated = 0,
        DxgiUnseen = 0,
        DxgiClaiming = 0,
        DxgiSaturated = 0,
        Histogram = [],
        BucketFactors = [],
        BatchFactors = [],
        Refusal = refusal,
    };

    private static bool DrainedBatch(in FrameSample s) => s.Features.HasFlag(FeatureBits.RayReconstructionObserved);

    private static FgWindow Tally(IReadOnlyList<FrameSample> all, int start, long qpcFrequency)
    {
        // Index is the value; the last slot lumps everything at or above it, which is where a saturated 255
        // lands. `Saturated` counts those separately, because 255 is a sentinel as much as a value and must
        // not be averaged with a real 5.
        int[] histogram = new int[6];
        var streams = new HashSet<uint>();
        long sigma = 0;
        int batches = 0;
        int unidentified = 0;
        int saturated = 0;
        (long dxgiUnseen, int dxgiClaiming, int dxgiSaturated) = TallyDxgi(all, start);

        for (int i = start; i < all.Count; i++)
        {
            FrameSample s = all[i];
            sigma += s.FgEvaluations;
            histogram[Math.Min(s.FgEvaluations, histogram.Length - 1)]++;
            if (s.FgEvaluations == byte.MaxValue)
            {
                saturated++;
            }

            if (DrainedBatch(s))
            {
                batches++;
            }

            if (s.SwapchainId == 0)
            {
                unidentified++;
            }
            else
            {
                streams.Add(s.SwapchainId);
            }
        }

        return new FgWindow
        {
            Presents = all.Count - start,
            Evaluations = sigma,
            Batches = batches,
            Seconds = RecordWindow.SecondsOf(all, start, qpcFrequency),
            Unidentified = unidentified,
            Streams = streams.Count,
            Saturated = saturated,
            DxgiUnseen = dxgiUnseen,
            DxgiClaiming = dxgiClaiming,
            DxgiSaturated = dxgiSaturated,
            Histogram = histogram,
            BucketFactors = BucketsOf(all, start, static s => s.FgEvaluations),
            BatchFactors = BucketsOf(all, start, static s => DrainedBatch(s) ? 1L : 0L),
            Refusal = null,
        };
    }

    /// <summary>
    /// Σ <c>dxgiUnseen</c>, the samples claiming it, and the ones at the 255 sentinel — the DXGI half of the
    /// tally, in its own pass because it is its own measurement.
    /// </summary>
    private static (long Unseen, int Claiming, int Saturated) TallyDxgi(IReadOnlyList<FrameSample> all, int start)
    {
        long unseen = 0;
        int claiming = 0;
        int saturated = 0;
        for (int i = start; i < all.Count; i++)
        {
            FrameSample s = all[i];
            if (!s.Claims(MeasuredFields.DxgiPresents))
            {
                continue;
            }

            claiming++;
            unseen += s.DxgiUnseen;
            if (s.DxgiUnseen == byte.MaxValue)
            {
                saturated++;
            }
        }

        return (unseen, claiming, saturated);
    }

    /// <summary>Per-bucket <c>presents ÷ weight</c> in time order, or empty when the window is too short. One slicing, two weights.</summary>
    /// <remarks>
    /// A session that runs half at ×4 and half with frame generation off yields a whole-window factor ABOVE
    /// the physically achievable 4, and <c>NativeFps</c> inherits the error. A bucket with no weight at all is
    /// infinite, so the uniformity check cannot miss it.
    /// </remarks>
    private static List<double> BucketsOf(IReadOnlyList<FrameSample> all, int start, Func<FrameSample, long> weight)
    {
        int n = all.Count - start;
        if (n < MinSamplesToCheck)
        {
            return [];
        }

        var factors = new List<double>(Buckets);
        for (int b = 0; b < Buckets; b++)
        {
            int lo = start + (int)((long)n * b / Buckets);
            int hi = start + (int)((long)n * (b + 1) / Buckets);
            long sigma = 0;
            long displayed = 0;
            for (int i = lo; i < hi; i++)
            {
                sigma += weight(all[i]);
                displayed += 1 + Unseen(all[i]);
            }

            // The SAME numerator the window publishes: a DXGI-counted present belongs to the bucket of the
            // hooked present that read it, so a session where the pacer stopped mid-way fails uniformity
            // exactly as one where tokens stopped would.
            factors.Add(sigma > 0 ? displayed / (double)sigma : double.PositiveInfinity);
        }

        return factors;
    }

    /// <summary><c>dxgiUnseen</c> where the sample claims it, else 0 — an unclaimed byte is nobody's count.</summary>
    private static long Unseen(in FrameSample s) => s.Claims(MeasuredFields.DxgiPresents) ? s.DxgiUnseen : 0L;

    /// <summary>The first reason a factor may not be published, or null. Ordered so the most specific cause is named.</summary>
    private static FgRefusal? RefusalFor(FgWindow w)
    {
        if (RefusalForRecordSet(w, FgRefusalSubject.Factor) is FgRefusal aboutTheRecords)
        {
            return aboutTheRecords;
        }

        if (w.Evaluations == 0)
        {
            return new FgRefusal(FgRefusalKind.NoEvaluations, FgRefusalSubject.Factor);
        }

        return RefusalForRatio(w);
    }

    /// <summary>
    /// Facts about the record SET that refuse a ratio before anything is divided: attribution first, then the
    /// two saturation sentinels (factor only).
    /// </summary>
    /// <remarks>
    /// Attribution comes FIRST, and the order is the fix: these hold whether or not anything was counted,
    /// and below the zero-count check they were structurally unreachable on the one session shape that
    /// actually occurred — four real-title captures went by with the report silent about multi-stream.
    /// </remarks>
    private static FgRefusal? RefusalForRecordSet(FgWindow w, FgRefusalSubject subject)
    {
        if (w.Unidentified > 0)
        {
            return new FgRefusal(FgRefusalKind.Unattributed, subject, Count: w.Unidentified);
        }

        if (w.Streams > 1)
        {
            return new FgRefusal(FgRefusalKind.MultipleStreams, subject, Count: w.Streams);
        }

        if (subject != FgRefusalSubject.Factor)
        {
            return null;
        }

        if (w.Saturated > 0)
        {
            return new FgRefusal(FgRefusalKind.CountSaturated, subject, Count: w.Saturated);
        }

        if (w.DxgiSaturated > 0)
        {
            return new FgRefusal(FgRefusalKind.DxgiSaturated, subject, Count: w.DxgiSaturated);
        }

        return null;
    }

    /// <summary>The refusals that need the ratio itself: uniformity, then the unnameable band.</summary>
    private static FgRefusal? RefusalForRatio(FgWindow w)
    {
        double factor = w.DisplayedPresents / (double)w.Evaluations;
        FgRefusal? nonUniform = NonUniform(w.BucketFactors, factor, w.Presents, FgRefusalSubject.Factor);
        if (nonUniform is not null)
        {
            return nonUniform;
        }

        // A ratio near 1 is publishable since 2026-09-04 (20_OPEN_QUESTIONS §S31 row P1): 1.0 means every
        // present carried an application frame — `none`, reached by COUNTING. What is still refused is the
        // band between: a steady 1.05–1.5 is not a configuration any vendor ships, and naming it would be
        // guessing.
        return factor > NoneCeiling && factor < ActiveThreshold
            ? new FgRefusal(FgRefusalKind.AmbiguousBand, FgRefusalSubject.Factor, Overall: factor)
            : null;
    }

    /// <summary>The first reason <see cref="PresentsPerBatch"/> may not be read, or null.</summary>
    /// <remarks>
    /// The proxy needs its own guard because <see cref="RefusalFor"/>'s never reaches it: on the route
    /// measured, <c>Evaluations</c> is 0, so that path returns at the data-gap clause and uniformity is never
    /// assessed — while <c>presents/batch</c> is the number a verification run actually reads (§S30).
    /// </remarks>
    private static FgRefusal? RefusalForBatches(FgWindow w)
    {
        if (w.Batches == 0)
        {
            return new FgRefusal(FgRefusalKind.NoBatches, FgRefusalSubject.PresentsPerBatch);
        }

        if (RefusalForRecordSet(w, FgRefusalSubject.PresentsPerBatch) is FgRefusal aboutTheRecords)
        {
            return aboutTheRecords;
        }

        return NonUniform(w.BatchFactors, w.DisplayedPresents / (double)w.Batches, w.Presents,
                          FgRefusalSubject.PresentsPerBatch);
    }

    /// <summary>Does any bucket depart from the whole by more than the tolerance?</summary>
    /// <remarks>
    /// A window too short to bucket refuses too: publishing a number whose uniformity was never checked is
    /// the same claim as publishing one that failed the check.
    /// </remarks>
    private static FgRefusal? NonUniform(IReadOnlyList<double> buckets, double overall, int presents,
        FgRefusalSubject subject)
    {
        if (buckets.Count == 0)
        {
            return new FgRefusal(FgRefusalKind.TooShortToCheck, subject, Count: presents);
        }

        for (int b = 0; b < buckets.Count; b++)
        {
            double f = buckets[b];
            if (double.IsInfinity(f) || Math.Abs(f - overall) > overall * BucketTolerance)
            {
                return new FgRefusal(FgRefusalKind.NonUniform, subject,
                    BucketIndex: b, BucketCount: buckets.Count, BucketValue: f, Overall: overall);
            }
        }

        return null;
    }
}
