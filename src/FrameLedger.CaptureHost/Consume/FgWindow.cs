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
        Refusal = refusal,
    };

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
            BucketFactors = BucketsOf(all, start),
            Refusal = null,
        };
    }

    /// <summary>Per-bucket factors in time order, or empty when the window is too short.</summary>
    /// <remarks>
    /// <c>03_METRICS:133</c> — "averaging across a settings change is the classic way
    /// benchmark numbers become meaningless" — stated there for upscaling and true here for
    /// a reason with an arithmetic consequence: a session that runs half at ×4 and half with
    /// frame generation off yields a whole-window factor ABOVE the physically achievable 4,
    /// and <c>NativeFps</c> inherits the error. An FG-off bucket has no evaluations at all,
    /// so its factor is infinite and the uniformity check below cannot miss it.
    /// </remarks>
    private static List<double> BucketsOf(IReadOnlyList<FlFrameRecord> all, int start)
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
                sigma += all[i].FgEvaluations;
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
            return "no frame-generation evaluation was counted in the window — a data gap, "
                   + "and treating it as 'no frame generation' is how fg_factor becomes 1.0";
        }

        return NonUniform(w);
    }

    private static string? NonUniform(FgWindow w)
    {
        if (w.BucketFactors.Count == 0)
        {
            return $"the window holds {w.Presents} record(s), below the {_buckets * _minPerBucket} needed to "
                   + "check whether the frame-generation state changed during it";
        }

        double overall = w.Presents / (double)w.Evaluations;
        for (int b = 0; b < w.BucketFactors.Count; b++)
        {
            double f = w.BucketFactors[b];
            if (double.IsInfinity(f) || Math.Abs(f - overall) > overall * _bucketTolerance)
            {
                return "the frame-generation state changed during the session — bucket "
                       + $"{b + 1} of {w.BucketFactors.Count} measures {Fmt(f)} against {Fmt(overall)} overall, "
                       + "and a session-level factor would describe a configuration that never existed";
            }
        }

        return null;
    }

    private static string Fmt(double v) =>
        double.IsInfinity(v) ? "no evaluations" : v.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture);
}
