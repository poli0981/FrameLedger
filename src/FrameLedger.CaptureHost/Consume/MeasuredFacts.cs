using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// What a drained stream lets us say, and — much more of the time today — what it
/// does not.
/// </summary>
/// <remarks>
/// <para>
/// A throwaway consumer, deliberately here in the unshipped host and NOT in
/// <c>FrameLedger.Domain.Metrics.*</c>. P2 owns the real calculators, CLAUDE.md
/// records that nothing computes a frame time anywhere in the tree yet, and
/// <c>coverage-gate</c> carries a separate 95% floor for that namespace. A
/// throwaway must not be mistaken for the thing it stands in for.
/// </para>
/// <para>
/// <b>Every field that today's present-only writer cannot measure is nullable or
/// N/A, and none of them has a fallback.</b> That is the entire content of layout
/// v3 and of CLAUDE.md rules 6 and 7: <c>fg_factor 1.0</c> and a definite RT
/// <c>No</c> are the two numbers a forgetful consumer produces, and both are lies
/// about a title nobody looked at.
/// </para>
/// </remarks>
internal sealed record MeasuredFacts
{
    /// <summary>Presents observed on the dominant stream. The one number we may publish.</summary>
    public required int PresentsObserved { get; init; }

    /// <summary>Seconds spanned by the included intervals.</summary>
    public required double SecondsObserved { get; init; }

    /// <summary><c>F_disp</c> — count over duration. Null when there is no duration to divide by.</summary>
    public double? DisplayedFps =>
        SecondsObserved > 0 && PresentsObserved > 1 ? (PresentsObserved - 1) / SecondsObserved : null;

    /// <summary>
    /// <c>F_app</c>, and it is null until an FG-counting hook exists.
    /// </summary>
    /// <remarks>
    /// <c>03_METRICS</c>: <c>F_app = presents − Σ fgEvaluations</c>. With
    /// <see cref="FlMeasured.FgCounts"/> clear, <c>fgEvaluations</c> is the
    /// zero-default and the subtraction would make <c>F_app == F_disp</c> — i.e.
    /// <c>fg_factor 1.0</c>, reached by a consumer that counted nothing.
    /// <c>FL_MEASURED_FG_COUNTS</c> exists to make that a data gap instead.
    /// </remarks>
    public double? NativeFps { get; init; }

    /// <summary>Null, never 1.0, and never <c>—</c> dressed up as a measurement.</summary>
    public double? FgFactor { get; init; }

    /// <summary>Null when no FG-identity hook ran. NOT the string "none".</summary>
    /// <remarks>
    /// <c>03_METRICS</c> §Frame Generation's ladder ends "otherwise <c>fg_mode = none</c>",
    /// which applied to a present-only writer turns "nobody looked" into an
    /// affirmative negative. The ladder needs a rung 0 — mask bit clear ⇒ N/A —
    /// before rung 4 is reachable, and that correction lands in the same PR.
    /// </remarks>
    public string? FgMode { get; init; }

    /// <summary>Null when <see cref="FlMeasured.Upscaler"/> is clear or the value is UNKNOWN.</summary>
    public string? Upscaler { get; init; }

    /// <summary>Null, because <c>renderW/H</c> are always 0 and the ratio would divide by zero.</summary>
    public double? UpscaleRatio { get; init; }

    public Tri RayTracing { get; init; }

    public Tri RayReconstruction { get; init; }

    /// <summary>Always N/A. CLAUDE.md rule 7: path tracing has no API-level signature.</summary>
    /// <remarks>
    /// A field with a fixed value rather than a computed property, because there is
    /// nothing to compute: <c>03_METRICS</c>' heuristic needs rays-per-pixel,
    /// <c>MaxTraceRecursionDepth</c> and the RT state-object count, and it may only
    /// ever <i>suggest</i>. It never sets <c>Yes</c> on its own.
    /// </remarks>
    public Tri PathTracing { get; } = Tri.NotApplicable;

    public Tri Hdr { get; init; }

    /// <summary>
    /// Records whose <c>measuredMask</c> claimed something a present-only writer
    /// cannot know. Non-zero is a defect in the WRITER, and this consumer says so
    /// rather than averaging it.
    /// </summary>
    public int HonestyViolations { get; init; }

    /// <summary>True when the drain reported a torn slot or an overwrite.</summary>
    public bool HasDataGaps { get; init; }

    /// <summary>Builds the facts for one already-segmented stream.</summary>
    public static MeasuredFacts From(IReadOnlyList<FlFrameRecord> stream, FlWriterState writer,
        long qpcFrequency, long totalGaps, long totalDropped)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(qpcFrequency);

        double seconds = 0;
        for (int i = 1; i < stream.Count; i++)
        {
            // Within the grouped stream only. An earlier version excluded any interval whose
            // frameIndex did not advance by exactly one — which is WRONG here and would have excluded
            // every interval in any multi-swapchain title: dllmain.cpp assigns `g_frameIndex++` once
            // per accepted present for the WHOLE PROCESS, four lines before it assigns swapchainId, so
            // within one stream consecutive records differ by however many streams are interleaving.
            // Measured: the overflow harness interleaves 17, so the dominant stream's indices step by
            // ~17 and D would have been 0 — including for Displayed FPS, the one number we may publish.
            long delta = (long)stream[i].Qpc - (long)stream[i - 1].Qpc;
            if (delta > 0)
            {
                seconds += (double)delta / qpcFrequency;
            }
        }

        int violations = stream.Count(r => !IsHonest(r));

        return new MeasuredFacts
        {
            PresentsObserved = stream.Count,
            SecondsObserved = seconds,

            // Every one of these is a deliberate absence, not an oversight. See the property docs.
            NativeFps = null,
            FgFactor = null,
            FgMode = null,
            Upscaler = UpscalerOf(stream),
            UpscaleRatio = null,
            RayTracing = RayTracingOf(stream, writer),
            RayReconstruction = RayReconstructionOf(stream),
            Hdr = HdrOf(stream),
            HonestyViolations = violations,
            HasDataGaps = totalGaps > 0 || totalDropped > 0,
        };
    }

    /// <summary>
    /// What a present-only writer is entitled to claim, mirrored in managed code.
    /// </summary>
    /// <remarks>
    /// <c>guard_test.cpp</c> asserts this natively in the merge gate. Asserting it
    /// here too means that the day a feature hook lands, an over-claiming record is
    /// surfaced by the consumer rather than silently averaged into a number.
    /// </remarks>
    private static bool IsHonest(FlFrameRecord r)
    {
        var mask = (FlMeasured)r.MeasuredMask;
        const FlMeasured allowed = FlMeasured.OutputRes | FlMeasured.PresentArgs;
        return (mask & ~allowed) == FlMeasured.None
            && (FlRtFlags)r.RtFlags == FlRtFlags.None
            && r.Upscaler == (byte)FlUpscaler.NotReported
            && r.FgMode == (byte)FlFgMode.NotReported
            && (FlFeatureFlags)r.FeatureFlags == FlFeatureFlags.None;
    }

    private static string? UpscalerOf(IReadOnlyList<FlFrameRecord> stream)
    {
        if (stream.Count == 0 || !stream.All(r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler)))
        {
            return null;
        }

        var value = (FlUpscaler)stream[^1].Upscaler;
        return value switch
        {
            // A hook ran and could not identify what it saw. Still N/A, but a DIFFERENT N/A: it means
            // our coverage is short, not that the question did not apply.
            FlUpscaler.Unknown => null,

            // Retired in v3 and reserved rather than reused, because it made Ray Reconstruction
            // mutually exclusive with DLSS super-resolution. 03_METRICS §Upscaling listed `dlss_rr` as
            // an upscaler value until the PR that added this file removed it; decoding 2 as anything
            // would resurrect the conflation the record had already dropped.
            FlUpscaler.RetiredRayReconstruction => null,

            FlUpscaler.NotReported => null,
            _ => value.ToString(),
        };
    }

    private static Tri RayTracingOf(IReadOnlyList<FlFrameRecord> stream, FlWriterState writer)
    {
        if (stream.Count == 0)
        {
            return Tri.NotApplicable;
        }

        bool measured = stream.All(r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Rt));
        int evidence = stream.Count(r =>
            ((FlRtFlags)r.RtFlags & (FlRtFlags.AsBuildObserved | FlRtFlags.DispatchObserved)) != FlRtFlags.None);

        if (measured && evidence * 20 >= stream.Count)
        {
            return Tri.Yes;    // 03_METRICS: AS-build or DispatchRays in >= 5% of frames.
        }

        // THREE CONJUNCTS, and the middle one is the one that is easy to drop. A writer with only the
        // DispatchRays hook sees nothing on an inline-RayQuery title, and its silence is
        // indistinguishable from a real negative — so `No` needs the AS-BUILD hook to have been
        // INSTALLED, not merely for RT to have been "measured".
        //
        // Today rtTier and hooksInstalledMask have NO PRODUCER anywhere in the tree, so this reaches
        // NotApplicable on every session and both other branches are unreachable. §S29(f) recorded that
        // for `No` alone and from a stale premise (it said no record field carries the tier; layout v3
        // put it in FlWriterState @24). It is true of `Yes` as well, which is stronger.
        bool capable = writer.RtTier >= 10;
        bool asBuildInstalled = ((FlHookFamily)writer.HooksInstalledMask).HasFlag(FlHookFamily.RtAsBuild);
        return measured && capable && asBuildInstalled && evidence == 0 ? Tri.No : Tri.NotApplicable;
    }

    private static Tri RayReconstructionOf(IReadOnlyList<FlFrameRecord> stream)
    {
        // Gated on the in-band OBSERVED bit, NOT on measuredMask. FL_MEASURED_UPSCALER also covers FFX,
        // XeSS and NIS, so a writer with FFX hooks and no NGX hooks has "upscaler measured" and knows
        // nothing whatever about RR — sharing the mask bit would publish RR = No.
        if (stream.Count == 0
            || !stream.All(r => ((FlFeatureFlags)r.FeatureFlags).HasFlag(FlFeatureFlags.RayReconstructionObserved)))
        {
            return Tri.NotApplicable;
        }

        return stream.Any(r => ((FlFeatureFlags)r.FeatureFlags).HasFlag(FlFeatureFlags.RayReconstruction))
            ? Tri.Yes
            : Tri.No;
    }

    private static Tri HdrOf(IReadOnlyList<FlFrameRecord> stream)
    {
        if (stream.Count == 0 || !stream.All(r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Hdr)))
        {
            return Tri.NotApplicable;
        }

        return (FlColorSpace)stream[^1].ColorSpace is FlColorSpace.Hdr10 or FlColorSpace.ScRgb ? Tri.Yes : Tri.No;
    }
}
