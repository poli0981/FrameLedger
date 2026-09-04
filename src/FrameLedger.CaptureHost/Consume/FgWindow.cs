using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The records a frame-generation factor may be computed from, and — far more often —
/// the reason it may not be.
/// </summary>
/// <remarks>
/// <para>
/// <b>ONE RECORD SET FOR BOTH SIDES OF THE RATIO.</b> <c>F_disp</c> is presents and
/// <c>F_app</c> is <c>Σ fgEvaluations</c>, and taking them over different sets is not a
/// rounding error: <c>g_slSeen</c> is one process-wide word drained by whichever present
/// arrives first, so an evaluation belonging to the game's frame can be consumed by a UI
/// swapchain's present. Counting Σ over the dominant stream while counting presents over
/// all of them undercounts the denominator and overstates the factor, with no diagnostic.
/// So this is built over <b>every</b> record in one QPC span, and where that span cannot
/// be attributed to a single stream it refuses instead.
/// </para>
/// <para>
/// <b>Every refusal here is a number this consumer would otherwise have published.</b>
/// CLAUDE.md rule 6 forbids a single inflated FPS number; the ways to reach one from
/// honest records are all arithmetic, and each has its own <see cref="Refusal"/>.
/// </para>
/// <para>
/// <b><see cref="NativeFps"/> is computed independently and not as
/// <c>DisplayedFps / Factor</c>.</b> Derived, the rule-6 trio is internally consistent by
/// construction and a reader can draw no conclusion from that consistency; computed
/// separately, <c>Native × Factor ≈ Displayed</c> becomes a property a test can check.
/// </para>
/// </remarks>
internal sealed record FgWindow
{
    /// <summary>Presents in the span — every record, not only the dominant stream.</summary>
    public required int Presents { get; init; }

    /// <summary><c>Σ fgEvaluations</c> over exactly those records.</summary>
    public required long Evaluations { get; init; }

    /// <summary>
    /// Presents that drained a non-empty Streamline word, from
    /// <see cref="FlFeatureFlags.RayReconstructionObserved"/>.
    /// </summary>
    /// <remarks>
    /// That bit is set by the writer under <c>seen != 0</c> and nothing else, so it is an
    /// EXACT indicator of "this present consumed a batch". The params bit is not: it
    /// carries a second conjunct (the tag being valid), which is why the 4.0134 figure in
    /// the ledger is an upper bound rather than an estimate.
    /// </remarks>
    public required int Batches { get; init; }

    /// <summary>Seconds spanned by the window's intervals.</summary>
    public required double Seconds { get; init; }

    /// <summary>Records the writer could not attribute to a swapchain.</summary>
    public required int Unidentified { get; init; }

    /// <summary>Distinct identified swapchains presenting inside the span.</summary>
    public required int Streams { get; init; }

    /// <summary>Records whose count hit the byte's ceiling.</summary>
    public required int Saturated { get; init; }

    /// <summary>How many records carried 0, 1, 2 … evaluations. Index is the value.</summary>
    public required IReadOnlyList<int> Histogram { get; init; }

    /// <summary>Per-bucket factors, in time order. Empty when the window is too short to bucket.</summary>
    public required IReadOnlyList<double> BucketFactors { get; init; }

    /// <summary>Per-bucket <c>presents / batches</c>, in time order. Same slicing as
    /// <see cref="BucketFactors"/>, a different weight.</summary>
    /// <remarks>
    /// <b>THIS EXISTS BECAUSE THE OTHER ONE CANNOT SEE THE CASE THAT OCCURRED.</b>
    /// <see cref="BucketFactors"/> divides by <c>Σ fgEvaluations</c>, which is <b>zero on every
    /// record</b> on the one route a real title has been measured on — so every bucket is
    /// identical, <see cref="NonUniform"/> passes vacuously, and a window that mixed two
    /// frame-generation states looks clean. Measured 2026-08-16: an alt-tab mid-capture on
    /// Cyberpunk 2077 produced an achieved <c>presents / batch</c> of 1.84 against a title
    /// configured for ×2, an 8% error with no diagnostic anywhere in the report. A guard keyed
    /// on a quantity that is zero on the route that runs is not a guard.
    /// </remarks>
    public required IReadOnlyList<double> BatchFactors { get; init; }

    /// <summary>Why no factor may be published, or null when one may.</summary>
    public required string? Refusal { get; init; }

    /// <summary><c>presents / Σ evaluations</c>, or null.</summary>
    public double? Factor =>
        Refusal is null && Evaluations > 0 ? Presents / (double)Evaluations : null;

    /// <summary><c>F_disp</c> over this window's own intervals.</summary>
    public double? DisplayedFps =>
        Refusal is null && Seconds > 0 && Presents > 1 ? (Presents - 1) / Seconds : null;

    /// <summary><c>F_app</c>, counted rather than derived.</summary>
    public double? NativeFps => Refusal is null && Seconds > 0 ? Evaluations / Seconds : null;

    /// <summary>
    /// Evaluations per drained batch — the oracle-free premise check, and it is published
    /// whether or not a factor is.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The quotient of the two ratios a run can compute: <c>(presents / batches)</c> ÷
    /// <c>(presents / Σ)</c> reduces to <c>Σ / batches</c>. HANDOFF item 3 rests on a
    /// premise nothing in this repository has verified — that
    /// <c>slEvaluateFeature(kFeatureDLSS_G)</c> fires <b>once</b> per application frame —
    /// and this is the measurement of it, needing no game settings and no second tool.
    /// </para>
    /// <para>
    /// <b>It is the only check that catches the k-per-frame case.</b> Three evaluations per
    /// application frame at ×4 yields a factor of 1.34: above 1.0, so an "over-counting"
    /// guard stays silent; not equal to 1.0, so a structurally-1.0 guard stays silent; and
    /// it still MOVES with the setting, so a three-point sweep passes too. Only this number
    /// says 3 instead of 1.
    /// </para>
    /// </remarks>
    public double? EvaluationsPerBatch => Batches > 0 ? Evaluations / (double)Batches : null;

    /// <summary>
    /// Presents per drained Streamline batch — a <b>PROXY</b>, and never
    /// <see cref="Factor"/>.
    /// </summary>
    /// <remarks>
    /// A batch is "a present that drained a Streamline evaluation", <b>not</b> an application
    /// frame. On the one title measured the two coincide only because Ray Reconstruction
    /// happens to be evaluated once per application frame there, which no independent oracle
    /// has confirmed. So this number is printed, named, and guarded by
    /// <see cref="BatchRefusal"/>; it is never published as <c>fg_factor</c>, and
    /// <see cref="Factor"/> stays keyed on counted evaluations.
    /// </remarks>
    public double? PresentsPerBatch => Batches > 0 ? Presents / (double)Batches : null;

    /// <summary>
    /// Why <see cref="PresentsPerBatch"/> may not be read as one configuration's number, or
    /// null when it may.
    /// </summary>
    /// <remarks>
    /// Separate from <see cref="Refusal"/> because the two answer different questions and, on
    /// the route that actually runs, <see cref="Refusal"/> is <b>always</b> non-null — it stops
    /// at "no frame-generation evaluation was counted" before uniformity is ever considered. A
    /// reader who took that as the last word would then read the proxy beneath it with nothing
    /// standing behind it at all.
    /// </remarks>
    public string? BatchRefusal => RefusalForBatches(this);

    /// <summary>Buckets the window is split into for the FG-state uniformity check.</summary>
    private const int _buckets = 8;

    /// <summary>Records a bucket needs before its factor means anything.</summary>
    private const int _minPerBucket = 8;

    /// <summary>How far a bucket's factor may sit from the window's before this refuses.</summary>
    private const double _bucketTolerance = 0.25;

    /// <summary>Builds the window over EVERY drained record, or the reason there is none.</summary>
    public static FgWindow From(IReadOnlyList<FlFrameRecord> all, long qpcFrequency)
    {
        ArgumentNullException.ThrowIfNull(all);

        int start = RecordWindow.ClaimedSuffixStart(
            all, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.FgCounts));
        if (start == all.Count)
        {
            return Nothing("no record claims FL_MEASURED_FG_COUNTS, so nothing counted evaluations");
        }

        FgWindow tallied = Tally(all, start, qpcFrequency);
        return tallied with { Refusal = RefusalFor(tallied) };
    }

    private static FgWindow Nothing(string refusal) => new()
    {
        Presents = 0,
        Evaluations = 0,
        Batches = 0,
        Seconds = 0,
        Unidentified = 0,
        Streams = 0,
        Saturated = 0,
        Histogram = [],
        BucketFactors = [],
        BatchFactors = [],
        Refusal = refusal,
    };

    /// <summary>Σ fgEvaluations, the weight <see cref="BucketFactors"/> divides by.</summary>
    private static long EvaluationWeight(FlFrameRecord r) => r.FgEvaluations;

    /// <summary>
    /// 1 on a present that drained a non-empty Streamline word, the weight
    /// <see cref="BatchFactors"/> divides by.
    /// </summary>
    private static long BatchWeight(FlFrameRecord r) =>
        ((FlFeatureFlags)r.FeatureFlags).HasFlag(FlFeatureFlags.RayReconstructionObserved) ? 1L : 0L;

    private static FgWindow Tally(IReadOnlyList<FlFrameRecord> all, int start, long qpcFrequency)
    {
        // Index is the value; the last slot lumps everything at or above it, which is where
        // a saturated 255 lands. `Saturated` counts those separately, because 255 is a
        // sentinel as much as a value and must not be averaged with a real 5.
        int[] histogram = new int[6];
        var streams = new HashSet<uint>();
        long sigma = 0;
        int batches = 0;
        int unidentified = 0;
        int saturated = 0;

        for (int i = start; i < all.Count; i++)
        {
            FlFrameRecord r = all[i];
            sigma += r.FgEvaluations;
            histogram[Math.Min((int)r.FgEvaluations, histogram.Length - 1)]++;
            if (r.FgEvaluations == byte.MaxValue)
            {
                saturated++;
            }

            // The EXACT indicator of "this present drained a non-empty word": the writer
            // sets it under `seen != 0` and nothing else.
            if (((FlFeatureFlags)r.FeatureFlags).HasFlag(FlFeatureFlags.RayReconstructionObserved))
            {
                batches++;
            }

            if (r.SwapchainId == 0)
            {
                unidentified++;
            }
            else
            {
                streams.Add(r.SwapchainId);
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
            Histogram = histogram,
            BucketFactors = BucketsOf(all, start, EvaluationWeight),
            BatchFactors = BucketsOf(all, start, BatchWeight),
            Refusal = null,
        };
    }

    /// <summary>
    /// Per-bucket <c>presents ÷ weight</c> in time order, or empty when the window is too
    /// short. One slicing, two weights.
    /// </summary>
    /// <remarks>
    /// <c>03_METRICS:133</c> — "averaging across a settings change is the classic way
    /// benchmark numbers become meaningless" — stated there for upscaling and true here for
    /// a reason with an arithmetic consequence: a session that runs half at ×4 and half with
    /// frame generation off yields a whole-window factor ABOVE the physically achievable 4,
    /// and <c>NativeFps</c> inherits the error. A bucket with no weight at all is infinite,
    /// so the uniformity check below cannot miss it.
    /// </remarks>
    private static List<double> BucketsOf(IReadOnlyList<FlFrameRecord> all, int start,
        Func<FlFrameRecord, long> weight)
    {
        int n = all.Count - start;
        if (n < _buckets * _minPerBucket)
        {
            return [];
        }

        var factors = new List<double>(_buckets);
        for (int b = 0; b < _buckets; b++)
        {
            int lo = start + (int)((long)n * b / _buckets);
            int hi = start + (int)((long)n * (b + 1) / _buckets);
            long sigma = 0;
            for (int i = lo; i < hi; i++)
            {
                sigma += weight(all[i]);
            }

            factors.Add(sigma > 0 ? (hi - lo) / (double)sigma : double.PositiveInfinity);
        }

        return factors;
    }

    /// <summary>The first reason a factor may not be published, or null.</summary>
    /// <remarks>
    /// Ordered so the most specific cause is named. Every one of these is a number this
    /// consumer would otherwise have printed as a measurement.
    /// </remarks>
    private static string? RefusalFor(FgWindow w)
    {
        // ATTRIBUTION FIRST, AND THE ORDER IS THE FIX. These two are facts about the record
        // set and hold whether or not anything was counted — but they used to sit BELOW the
        // zero-count check, so on the one session shape that actually occurred (a title whose
        // frame generation is driven off this route, so Evaluations == 0 on every record) they
        // were structurally unreachable and the report was SILENT about multi-stream rather
        // than clean. Four real-title captures went by with nobody able to say from the report
        // how many swapchains those presents came from, which is exactly §H5's third case.
        if (w.Unidentified > 0)
        {
            return $"{w.Unidentified} record(s) carry swapchainId 0, so the presents cannot be "
                   + "attributed and the ratio has no denominator anyone can name";
        }

        if (w.Streams > 1)
        {
            return $"{w.Streams} swapchains presented in the window; g_slSeen is one process-wide "
                   + "word, so an evaluation belonging to one stream can be drained by another's present";
        }

        if (w.Saturated > 0)
        {
            return $"{w.Saturated} record(s) hit the fgEvaluations ceiling of 255, which is a "
                   + "saturation sentinel rather than a count — dividing by it would report a floor";
        }

        if (w.Evaluations == 0)
        {
            return "no application-frame token was counted in the window (slGetNewFrameToken) — a data gap, "
                   + "and treating it as 'no frame generation' is how fg_factor becomes 1.0";
        }

        string? nonUniform = NonUniform(w.BucketFactors, w.Presents / (double)w.Evaluations, w.Presents,
                                        "the frame-generation state");
        if (nonUniform is not null)
        {
            return nonUniform;
        }

        // A RATIO NEAR 1 IS PUBLISHABLE SINCE 2026-09-04, and it took a measurement to make it
        // so. Until then this clause refused anything below the cadence threshold, because
        // "no frames were generated" and "the DLSS-G plugin requested a token for every frame
        // it generated" read the same 1.0 from inside the process. The owner's run landed on
        // row P1 of 20_OPEN_QUESTIONS §S31 — Cyberpunk 2077 off / ×3 / ×4 at 1.00 / 2.99 / 3.99
        // and Hell Is Us ×4 at 4.00 — so the second explanation is excluded on the title that
        // would have shown it, and 1.0 means what it says: every present carried an
        // application frame. That is `none`, the one negative 03_METRICS lets a consumer
        // aggregate, reached by COUNTING rather than by a hook that happened to see nothing.
        //
        // What is still refused is the band between: a steady 1.05–1.5 is not a configuration
        // any vendor ships, and a window that mixed states would usually have failed the
        // uniformity check above. Naming it would be guessing.
        double factor = w.Presents / (double)w.Evaluations;
        return factor > NoneCeiling && factor < ActiveThreshold
            ? $"presents/tokens = {factor.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture)} sits "
              + $"between the `none` ceiling ({NoneCeiling.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture)}) "
              + $"and the cadence threshold ({ActiveThreshold.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture)}) "
              + "— not a configuration this consumer can name"
            : null;
    }

    /// <summary>
    /// At or above this, frame generation is ACTIVE: <c>03_METRICS</c>' cadence threshold, and the
    /// value below which the one premise of the token producer could not be told apart from
    /// "none" until row P1 landed.
    /// </summary>
    public const double ActiveThreshold = 1.5;

    /// <summary>
    /// At or below this, frame generation is <c>none</c>: every present carried an application
    /// frame. Measured 1.00 on two off legs; the 5% is drain jitter at the window's edges.
    /// </summary>
    public const double NoneCeiling = 1.05;

    /// <summary><c>none</c>, reached by counting: a published factor at or below <see cref="NoneCeiling"/>.</summary>
    public bool IsNone => Factor is double f && f <= NoneCeiling;

    /// <summary>Frame generation is active: a published factor at or above <see cref="ActiveThreshold"/>.</summary>
    public bool IsActive => Factor is double f && f >= ActiveThreshold;

    /// <summary>The first reason <see cref="PresentsPerBatch"/> may not be read, or null.</summary>
    /// <remarks>
    /// <b>The proxy needs its own guard because <see cref="RefusalFor"/>'s never reaches it.</b>
    /// On the route measured, <c>Evaluations</c> is 0, so that method returns at the data-gap
    /// clause and uniformity is never assessed — while <c>presents/batch</c> is printed anyway
    /// and is the number a verification run actually reads. §S30 states the consequence as a
    /// prerequisite: "if <c>presents / batch</c> is ever published as <c>fg_factor</c>, it needs
    /// a uniformity guard OF ITS OWN, keyed on the per-bucket <c>presents / batch</c> rather
    /// than on <c>fgEvaluations</c>".
    /// </remarks>
    private static string? RefusalForBatches(FgWindow w)
    {
        if (w.Batches == 0)
        {
            return "no present drained a Streamline batch, so there is no ratio to read";
        }

        // The same two attribution facts the factor refuses on, for the same reason: one
        // process-wide drain word means a batch belonging to one stream can be consumed by
        // another stream's present, and the ratio then has a denominator nobody can name.
        if (w.Unidentified > 0)
        {
            return $"{w.Unidentified} record(s) carry swapchainId 0, so the presents cannot be attributed";
        }

        if (w.Streams > 1)
        {
            return $"{w.Streams} swapchains presented in the window; g_slSeen is one process-wide word, "
                   + "so a batch belonging to one stream can be drained by another's present";
        }

        return NonUniform(w.BatchFactors, w.Presents / (double)w.Batches, w.Presents,
                          "the presents-per-batch ratio");
    }

    /// <summary>Does any bucket depart from the whole by more than the tolerance?</summary>
    /// <remarks>
    /// A window too short to bucket refuses too: publishing a number whose uniformity was
    /// never checked is the same claim as publishing one that failed the check.
    /// </remarks>
    private static string? NonUniform(IReadOnlyList<double> buckets, double overall, int presents,
        string subject)
    {
        if (buckets.Count == 0)
        {
            return $"the window holds {presents} record(s), below the {_buckets * _minPerBucket} needed to "
                   + $"check whether {subject} changed during it";
        }

        for (int b = 0; b < buckets.Count; b++)
        {
            double f = buckets[b];
            if (double.IsInfinity(f) || Math.Abs(f - overall) > overall * _bucketTolerance)
            {
                return $"{subject} changed during the session — bucket "
                       + $"{b + 1} of {buckets.Count} measures {Fmt(f)} against {Fmt(overall)} overall, "
                       + "and a session-level number would describe a configuration that never existed";
            }
        }

        return null;
    }

    private static string Fmt(double v) =>
        double.IsInfinity(v) ? "no evaluations" : v.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture);
}
