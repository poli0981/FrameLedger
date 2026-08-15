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

        var entitled = EntitledBy((FlHookFamily)writer.HooksInstalledMask);
        int violations = stream.Count(r => !IsHonest(r, entitled));

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
    /// <summary>
    /// What a writer carrying <paramref name="hooks"/> is entitled to claim.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>Derived from <see cref="FlWriterState.HooksInstalledMask"/>, not a constant.</b>
    /// This used to be <c>OutputRes | PresentArgs</c> hardcoded, which was exactly
    /// right while the Overlay hooked only presents — and became wrong the moment
    /// the first feature hook landed, because an honest record claiming
    /// <see cref="FlMeasured.Upscaler"/> was then counted as a violation. A
    /// constant here says "the writer may claim what a present-only writer may
    /// claim", which is a statement about one particular build rather than about
    /// honesty.
    /// </para>
    /// <para>
    /// The property that actually matters, and that this keeps: a writer may
    /// claim a measurement <i>only</i> where it installed a hook capable of
    /// taking it. Both halves stay falsifiable — a writer that sets a mask bit
    /// with no hook family behind it is a violation, and so is one that sets a
    /// value field while the corresponding bit is clear.
    /// </para>
    /// </remarks>
    private static FlMeasured EntitledBy(FlHookFamily hooks)
    {
        // The present hook is what produced the record at all, so its two claims
        // ride along with it.
        var allowed = FlMeasured.None;
        if (hooks.HasFlag(FlHookFamily.Present))
        {
            allowed |= FlMeasured.OutputRes | FlMeasured.PresentArgs;
        }

        if (hooks.HasFlag(FlHookFamily.UpscalerIdentity))
        {
            // Identity only. FL_MEASURED_UPSCALER_PARAMS is a separate bit behind
            // a separate family precisely because an NGX-direct title yields
            // identity and nothing else (17_HOOK_ENGINE §The NGX parameter surface).
            allowed |= FlMeasured.Upscaler | FlMeasured.Fg;
        }

        if (hooks.HasFlag(FlHookFamily.UpscalerParams))
        {
            allowed |= FlMeasured.UpscalerParams;
        }

        if (hooks.HasFlag(FlHookFamily.FgEvaluations))
        {
            allowed |= FlMeasured.FgCounts;
        }

        if ((hooks & (FlHookFamily.RtDispatch | FlHookFamily.RtAsBuild | FlHookFamily.RtPso)) != FlHookFamily.None)
        {
            allowed |= FlMeasured.Rt;
        }

        if (hooks.HasFlag(FlHookFamily.Pso))
        {
            allowed |= FlMeasured.Pso;
        }

        if (hooks.HasFlag(FlHookFamily.ColorSpace))
        {
            allowed |= FlMeasured.Hdr;
        }

        if (hooks.HasFlag(FlHookFamily.Vram))
        {
            allowed |= FlMeasured.Vram;
        }

        if (hooks.HasFlag(FlHookFamily.Reflex))
        {
            allowed |= FlMeasured.Latency;
        }

        return allowed;
    }

    private static bool IsHonest(FlFrameRecord r, FlMeasured entitled)
    {
        var mask = (FlMeasured)r.MeasuredMask;
        if ((mask & ~entitled) != FlMeasured.None)
        {
            return false;    // claimed a measurement with no hook family behind it
        }

        // And the other direction: a VALUE set while its bit is clear is the same
        // defect seen from the record rather than from the mask. Layout v3 makes
        // the zero of every enum "nobody said", so these are the states a writer
        // publishes when it forgets.
        if (!mask.HasFlag(FlMeasured.Upscaler) && r.Upscaler != (byte)FlUpscaler.NotReported)
        {
            return false;
        }

        if (!mask.HasFlag(FlMeasured.Fg) && r.FgMode != (byte)FlFgMode.NotReported)
        {
            return false;
        }

        // fgEvaluations has no in-band sentinel — 0 is a real count — so only the mask bit
        // can say whether anyone counted. Added here to keep this in step with the native
        // twin in guard_test.cpp, which HANDOFF item 3 will make load-bearing.
        if (!mask.HasFlag(FlMeasured.FgCounts) && r.FgEvaluations != 0)
        {
            return false;
        }

        // featureFlags carries Ray Reconstruction's fact and OBSERVED bits, produced by the same
        // Streamline detour as the upscaler identity. A writer not entitled to claim an upscaler
        // is not entitled to say anything about RR either.
        if (!entitled.HasFlag(FlMeasured.Upscaler) && (FlFeatureFlags)r.FeatureFlags != FlFeatureFlags.None)
        {
            return false;
        }

        return mask.HasFlag(FlMeasured.Rt) || (FlRtFlags)r.RtFlags == FlRtFlags.None;
    }

    /// <summary>
    /// Index of the first record of the maximal SUFFIX on which <paramref name="claims"/>
    /// holds for every record; <c>stream.Count</c> when no such suffix exists.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>This replaces <c>stream.All(...)</c>, which was unsatisfiable by construction.</b>
    /// Feature hooks install lazily from the 1 Hz watchdog, so the opening of every session
    /// predates them — measured on Cyberpunk 2077, 292 of 10,169 records carry no
    /// <see cref="FlMeasured.Upscaler"/> bit (<c>spike-notes</c> §8). A whole-stream
    /// <c>All</c> therefore reported "no upscaler hook ran" about a session in which the
    /// hook was live for 97% of the presents: the report said the opposite of what
    /// happened, and <c>Program.cs</c> grew a per-bit record count to explain it in prose.
    /// </para>
    /// <para>
    /// A SUFFIX rather than a tolerance, because the property being asserted has not
    /// changed: every record we aggregate must carry the bit. Excluding a leading prefix
    /// is safe precisely because <see cref="FlWriterState.HooksInstalledMask"/> is
    /// monotonic (<c>fl_shm.h</c> §FlHookFamily) — a real install produces one clean
    /// boundary, so a writer that sets the bit intermittently still fails to be
    /// aggregated instead of being averaged over its gaps.
    /// </para>
    /// <para>
    /// Takes a predicate and not an <see cref="FlMeasured"/> on purpose: the axes this
    /// has to cover do not all live in <c>measuredMask</c>. See
    /// <see cref="RayReconstructionOf"/>, which is gated on <see cref="FlFeatureFlags"/>
    /// and is deliberately NOT a caller.
    /// </para>
    /// </remarks>
    private static int ClaimedSuffixStart(IReadOnlyList<FlFrameRecord> stream, Func<FlFrameRecord, bool> claims)
    {
        int start = stream.Count;
        for (int i = stream.Count - 1; i >= 0 && claims(stream[i]); i--)
        {
            start = i;
        }

        // THE BOUNDARY MUST BE CLEAN, or this is not an install window and nothing is aggregated.
        // hooksInstalledMask is monotonic, so a real install sets the bit once and never clears it:
        // every record before the boundary must LACK it. Without this, a writer setting the bit
        // intermittently would have its trailing run averaged as though it were a whole session —
        // the same "a value nobody measured over that interval" defect, reached from the other side,
        // and silently, because the trailing run always looks like a clean suffix on its own.
        for (int i = 0; i < start; i++)
        {
            if (claims(stream[i]))
            {
                return stream.Count;
            }
        }

        return start;
    }

    private static string? UpscalerOf(IReadOnlyList<FlFrameRecord> stream)
    {
        int start = ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler));
        if (start == stream.Count)
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
        // ONE RECORD SET FOR BOTH THE EVIDENCE AND ITS DENOMINATOR. Counting evidence over
        // the whole stream while `measured` is decided over a suffix would divide by records
        // the claim does not cover — a different number from either honest choice.
        int start = ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Rt));
        bool measured = start < stream.Count;
        int frames = stream.Count - start;

        int evidence = 0;
        for (int i = start; i < stream.Count; i++)
        {
            if (((FlRtFlags)stream[i].RtFlags & (FlRtFlags.AsBuildObserved | FlRtFlags.DispatchObserved))
                != FlRtFlags.None)
            {
                evidence++;
            }
        }

        if (measured && evidence * 20 >= frames)
        {
            return Tri.Yes;    // 03_METRICS: AS-build or DispatchRays in >= 5% of frames.
        }

        // THREE CONJUNCTS, and the middle one is the one that is easy to drop. A writer with only the
        // DispatchRays hook sees nothing on an inline-RayQuery title, and its silence is
        // indistinguishable from a real negative — so `No` needs the AS-BUILD hook to have been
        // INSTALLED, not merely for RT to have been "measured".
        //
        // Both conjuncts now have producers: hooksInstalledMask since the present hook, rtTier since
        // ResolveApi started asking the D3D12 device. What is still missing is the RT hooks themselves,
        // so FlMeasured.Rt is never set and `measured` is false — which is why this still reaches
        // NotApplicable on every session. The gap moved; it did not close.
        //
        // CapableMin, not a literal 10. rtTier holds D3D12_RAYTRACING_TIER's own value, and a device
        // that answered NOT_SUPPORTED is FlRtTier.Unsupported (1) rather than 0 — so `>=` correctly
        // excludes it while 0 keeps meaning nobody looked.
        bool capable = writer.RtTier >= (uint)FlRtTier.CapableMin;
        bool asBuildInstalled = ((FlHookFamily)writer.HooksInstalledMask).HasFlag(FlHookFamily.RtAsBuild);
        return measured && capable && asBuildInstalled && evidence == 0 ? Tri.No : Tri.NotApplicable;
    }

    private static Tri RayReconstructionOf(IReadOnlyList<FlFrameRecord> stream)
    {
        // Gated on the in-band OBSERVED bit, NOT on measuredMask. FL_MEASURED_UPSCALER also covers FFX,
        // XeSS and NIS, so a writer with FFX hooks and no NGX hooks has "upscaler measured" and knows
        // nothing whatever about RR — sharing the mask bit would publish RR = No.
        //
        // AND THAT IS WHY THIS IS NOT A ClaimedSuffixStart CALLER, THOUGH IT LOOKS LIKE ONE.
        // The other three axes are gated on a HOOK-LIVENESS bit, which is monotonic, so excluding the
        // install prefix is the whole fix. This one is gated on a PER-PRESENT OBSERVATION: dllmain.cpp
        // sets RayReconstructionObserved under `seen != 0`, deliberately, so an NGX-direct title running
        // DLSS-RR does not get a fabricated `No`. Under frame generation that bit is intermittent by
        // construction — on the Cyberpunk stream one Streamline batch spans ~4 presents, so ~24% of
        // records carry it — and the maximal claiming suffix is then ONE record. Feeding that to the
        // `Any` below would publish a whole-session Yes/No from a single frame, which is worse than the
        // N/A this returns today.
        //
        // So the honest state is: RR is N/A on every frame-generating title, for a reason that is not
        // the install window and is not fixed by sweeping it. Fixing it needs the application-frame
        // unit that HANDOFF item 3 introduces; until then this stays as it is, and says so.
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
        int start = ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Hdr));
        if (start == stream.Count)
        {
            return Tri.NotApplicable;
        }

        return (FlColorSpace)stream[^1].ColorSpace is FlColorSpace.Hdr10 or FlColorSpace.ScRgb ? Tri.Yes : Tri.No;
    }
}
