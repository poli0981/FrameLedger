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
    /// The records a frame-generation factor may be computed from, or the reason there
    /// are none. Null when no FG-counting hook was live at all.
    /// </summary>
    /// <remarks>
    /// Carried whole rather than flattened into two doubles, because CLAUDE.md rule 6 is
    /// about the three numbers appearing TOGETHER and over ONE window: Displayed taken
    /// over the dominant stream while the factor is taken over a post-install suffix
    /// produces a trio in which no member describes the same records as its neighbours.
    /// <see cref="SessionReport"/> renders from this or renders none of it.
    /// </remarks>
    public FgWindow? Fg { get; init; }

    /// <summary>
    /// <c>F_app = Σ fgEvaluations</c> — counted, never derived from the other two.
    /// </summary>
    /// <remarks>
    /// <c>03_METRICS</c> §Frame Generation, as ruled 2026-08-14. Deriving this as
    /// <c>DisplayedFps / FgFactor</c> would make the rule-6 trio internally consistent by
    /// construction, so a reader could conclude nothing from that consistency; computed
    /// independently, <c>Native × Factor ≈ Displayed</c> is a property a test can check.
    /// </remarks>
    public double? NativeFps => Fg?.NativeFps;

    /// <summary>Null, never 1.0, and never <c>—</c> dressed up as a measurement.</summary>
    public double? FgFactor => Fg?.Factor;

    /// <summary>
    /// The technology named by the identity hook, else what the COUNT says: <c>None</c> when a
    /// published factor sits at 1, <c>Active (technology not identified)</c> when it clears the
    /// cadence threshold, null when neither is measured.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <c>03_METRICS</c>'s ladder ends "otherwise <c>fg_mode = none</c>", and rung 0 keeps a
    /// present-only writer from turning "nobody looked" into that negative. <b><c>none</c> is
    /// reachable since 2026-09-04</b>, by counting: the token producer's off leg read 1.00 on the
    /// title whose ×3 / ×4 legs read 2.99 / 3.99 (§S31 row P1), so a factor of 1 is the measured
    /// statement that every present carried an application frame.
    /// </para>
    /// <para>
    /// Identity wins when present. A factor above the cadence threshold with no identity is
    /// <c>03_METRICS</c>' rung 3 — frame generation is happening and this writer cannot name the
    /// vendor, which on a UE5 title with Streamline and FidelityFX both loaded is the honest
    /// answer.
    /// </para>
    /// </remarks>
    public string? FgMode { get; init; }

    /// <summary>The string <see cref="FgMode"/> carries for a counted negative.</summary>
    public const string FgNone = "None";

    /// <summary>The string <see cref="FgMode"/> carries for rung 3: active, vendor not identified.</summary>
    public const string FgActiveUnidentified = "Active (technology not identified)";

    /// <summary>Null when <see cref="FlMeasured.Upscaler"/> is clear or the value is UNKNOWN.</summary>
    public string? Upscaler { get; init; }

    /// <summary>Whether a hook capable of naming the upscaler was live for the aggregated window.</summary>
    /// <remarks>
    /// <b>Two different N/As, and the report said the wrong one on the first real title.</b>
    /// <c>fl_shm.h</c> spends a section on the distinction — <c>NOT_REPORTED</c> is "no hook
    /// capable of answering was live", <c>UNKNOWN</c> is "a hook ran and could not identify
    /// what it saw, so our coverage is short" — and a renderer that prints "no upscaler hook
    /// ran" for both throws it away at the last step. Measured on Cyberpunk 2077: the hook was
    /// live for 9,990 of 10,088 records and the report said it never ran.
    /// </remarks>
    public bool UpscalerHookRan { get; init; }

    /// <summary>Whether a hook capable of naming the frame-generation mode was live.</summary>
    public bool FgHookRan { get; init; }

    /// <summary>
    /// <see cref="FlWriterState.RuntimeCensus"/>: which vendor runtime modules the loader
    /// reported in the process. Not a measurement — see the enum's remarks.
    /// </summary>
    /// <remarks>
    /// <b>What it is for.</b> Until 2026-09-03 a 2D title with no upscaler and a DLSS-G title
    /// on the route this writer does not hook printed the same line. The census cannot tell
    /// what the title DID, but it can tell whether a frame-generation runtime was even
    /// present — which is the difference between "this present count cannot include
    /// in-process generated frames" and "it may". <b>It never promotes an N/A to
    /// <c>none</c></b>: a statically linked FSR3-FG has no module to see, and a census-derived
    /// <c>none</c> there would print the inflated number CLAUDE.md rule 6 forbids.
    /// </remarks>
    public FlRuntimeCensus RuntimeCensus { get; init; }

    /// <summary>False means the watchdog never took the census, and nothing below may be inferred.</summary>
    public bool CensusRan => RuntimeCensus.HasFlag(FlRuntimeCensus.Ran);

    public FlRuntimeCensus FgRuntimesLoaded => RuntimeCensus & FlRuntimeCensusFamilies.Fg;

    public FlRuntimeCensus UpscalerRuntimesLoaded => RuntimeCensus & FlRuntimeCensusFamilies.Upscaler;

    /// <summary>A family bit without <see cref="FlRuntimeCensus.Ran"/>: the writer's defect, reported rather than read.</summary>
    public bool CensusInconsistent =>
        !CensusRan && (RuntimeCensus & ~FlRuntimeCensus.Ran) != FlRuntimeCensus.None;

    /// <summary>
    /// The dominant render → output extent over the records that claim <see cref="FlMeasured.UpscalerParams"/>
    /// and carry an output size, or null when no record carried both.
    /// </summary>
    /// <remarks>
    /// <b>The ratio is <c>03_METRICS</c> §Upscaling's own formula — <c>sqrt((outW×outH)/(renW×renH))</c>
    /// — and nothing more.</b> A preset NAME derived from it ("58% ≈ Balanced") is HANDOFF item 7a's
    /// owner decision and is deliberately not printed here: the quality byte stays what the writer
    /// measured (<c>0xFF</c> on every title so far), and this line says what the title rendered at,
    /// which is a measurement, beside the ratio, which is arithmetic on two measurements. The modal
    /// tuple rather than a mean, because averaging across a settings change is the classic way a
    /// benchmark number stops meaning anything; <see cref="UpscaleExtent.DistinctGroups"/> says
    /// whether one happened.
    /// </remarks>
    public UpscaleExtent? Extent { get; init; }

    /// <summary><c>sqrt((outW×outH)/(renW×renH))</c> of the dominant extent, or null.</summary>
    public double? UpscaleRatio => Extent?.Ratio;

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
    /// <remarks>
    /// <paramref name="fg"/> is built over EVERY drained record rather than over
    /// <paramref name="stream"/>, because <c>g_slSeen</c> is one process-wide word: an
    /// evaluation belonging to the game's frame can be drained by a UI swapchain's present,
    /// so summing the denominator over one stream while counting presents over all of them
    /// overstates the factor. <see cref="FgWindow"/> owns that decision and its refusals.
    /// </remarks>
    public static MeasuredFacts From(IReadOnlyList<FlFrameRecord> stream, FlWriterState writer,
        long qpcFrequency, long totalGaps, long totalDropped, FgWindow? fg = null)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(qpcFrequency);

        // Within the grouped stream only, and non-positive intervals are skipped. An earlier
        // version excluded any interval whose frameIndex did not advance by exactly one — which
        // is WRONG here and would have excluded every interval in any multi-swapchain title:
        // dllmain.cpp assigns `g_frameIndex++` once per accepted present for the WHOLE PROCESS,
        // four lines before it assigns swapchainId, so within one stream consecutive records
        // differ by however many streams are interleaving. Measured: the overflow harness
        // interleaves 17, so the dominant stream's indices step by ~17 and D would have been 0.
        double seconds = RecordWindow.SecondsOf(stream, 0, qpcFrequency);

        var entitled = EntitledBy((FlHookFamily)writer.HooksInstalledMask);
        int violations = stream.Count(r => !IsHonest(r, entitled));

        return new MeasuredFacts
        {
            PresentsObserved = stream.Count,
            SecondsObserved = seconds,

            Fg = fg,
            FgMode = FgModeOf(stream) ?? (fg?.IsNone == true ? FgNone : fg?.IsActive == true ? FgActiveUnidentified : null),
            UpscalerHookRan = RecordWindow.ClaimedSuffixStart(
                stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler)) < stream.Count,
            FgHookRan = RecordWindow.ClaimedSuffixStart(
                stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Fg)) < stream.Count,
            RuntimeCensus = (FlRuntimeCensus)writer.RuntimeCensus,

            Extent = UpscaleExtent.From(stream),
            Upscaler = UpscalerOf(stream),
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

        // BOTH RT FIELDS, not just the flags. dispatchRaysVolume has no in-band sentinel — 0 is a
        // real measurement of a frame that recorded no dispatch — so only the mask bit can say
        // whether anyone looked, exactly as with fgEvaluations. A writer that summed a volume
        // without claiming the measurement is contradicting itself, and it is the shape a drain
        // that cleared one word and not the other would produce.
        return mask.HasFlag(FlMeasured.Rt) || ((FlRtFlags)r.RtFlags == FlRtFlags.None && r.DispatchRaysVolume == 0);
    }

    /// <summary>Which frame-generation technology ran, or null. NEVER the string "none".</summary>
    /// <remarks>
    /// <c>03_METRICS</c>'s ladder ends "otherwise <c>fg_mode = none</c>", and rung 0 — added
    /// 2026-08-06 — puts <c>N/A</c> in front of it when <see cref="FlMeasured.Fg"/> is clear.
    /// Both matter here: a Streamline-only writer cannot see XeFG or FSR3-FG, so it reports
    /// <c>UNKNOWN</c> on a title generating frames through either, and a consumer that
    /// collapsed UNKNOWN to "none" would turn "our coverage is short" into a measured
    /// negative about the title.
    /// </remarks>
    private static string? FgModeOf(IReadOnlyList<FlFrameRecord> stream)
    {
        int start = RecordWindow.ClaimedSuffixStart(
            stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Fg));
        if (start == stream.Count)
        {
            return null;
        }

        // ANY record naming a technology wins over UNKNOWN, and that is not a preference —
        // it is what the writer's own shape requires. fgMode is DLSS_G only on the presents
        // that drained an evaluation; under frame generation the others are honestly
        // UNKNOWN, so a modal or last-record reading would report UNKNOWN on a title running
        // DLSS-G in three records out of four.
        for (int i = start; i < stream.Count; i++)
        {
            var value = (FlFgMode)stream[i].FgMode;
            if (value is not FlFgMode.NotReported and not FlFgMode.Unknown)
            {
                return value.ToString();
            }
        }

        return null;
    }

    private static string? UpscalerOf(IReadOnlyList<FlFrameRecord> stream)
    {
        int start =
            RecordWindow.ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Upscaler));
        if (start == stream.Count)
        {
            return null;
        }

        // ANY RECORD NAMING A TECHNOLOGY WINS, exactly as FgModeOf does, and for the same
        // reason: the writer publishes an identity only on the presents that DRAINED an
        // evaluation. Under frame generation that is one present in N — measured, 2,569 of
        // 10,276 at ×4 — so reading the LAST record reports UNKNOWN about a title whose every
        // batch said DLSS, and it does so with probability (N−1)/N. It reported exactly that
        // on Cyberpunk 2077 on 2026-08-15, three lines below its own raw block printing
        // `upscaler=Dlss on 2561 record(s)`.
        var value = FlUpscaler.NotReported;
        for (int i = start; i < stream.Count; i++)
        {
            var candidate = (FlUpscaler)stream[i].Upscaler;
            if (candidate is not FlUpscaler.NotReported and not FlUpscaler.Unknown)
            {
                value = candidate;
                break;
            }
        }

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

            // FSR through the SDK 2.x upscaler DLL, which hosts FSR 3.1 and FSR 4 behind one dispatch
            // type. The enum member's name is a token; the report gets the sentence, because a reader
            // who sees "FsrUnversioned" will ask what it means and the answer is the whole point.
            FlUpscaler.FsrUnversioned => UpscalerFsrUnversioned,
            _ => value.ToString(),
        };
    }

    /// <summary>What <see cref="Upscaler"/> carries for <see cref="FlUpscaler.FsrUnversioned"/>.</summary>
    public const string UpscalerFsrUnversioned =
        "FSR (3.1 or 4 — the SDK 2.x upscaler DLL hosts both, and the dispatch does not name the version)";

    private static Tri RayTracingOf(IReadOnlyList<FlFrameRecord> stream, FlWriterState writer)
    {
        // ONE RECORD SET FOR BOTH THE EVIDENCE AND ITS DENOMINATOR. Counting evidence over
        // the whole stream while `measured` is decided over a suffix would divide by records
        // the claim does not cover — a different number from either honest choice.
        int start = RecordWindow.ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Rt));
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
        // records carry it.
        //
        // THE POPULATION IS THE PRESENTS THAT DRAINED A BATCH, NOT EVERY RECORD, and requiring it on
        // every record is what made this N/A on every frame-generating title. Demanding a bit the
        // writer only ever sets on one present in N is a condition that cannot hold at N > 1, so the
        // answer was decided by the frame-generation setting rather than by whether Ray Reconstruction
        // ran. An earlier note here reasoned that the alternative was a whole-session verdict from a
        // single frame and that fixing it needed the application-frame unit HANDOFF item 3 would
        // introduce. Both halves were wrong: the natural population for "did RR run" is the presents
        // that carry a Streamline observation at all, which on the Cyberpunk stream is 2,461 batches
        // rather than one frame, and it needs no application-frame unit — which is fortunate, because
        // item 3 could not produce one.
        //
        // The three answers this now yields:
        //
        //   no present carries OBSERVED  -> N/A. Nothing looked, including the whole install prefix,
        //                                   which drops out for free instead of forcing N/A.
        //   some batch carried RR        -> Yes.
        //   batches, and none carried RR -> No, and this is the branch that was unreachable.
        //
        // `Any` rather than `All` for Yes is the pre-existing semantics and is kept: a title that turns
        // RR off mid-session still ran it, and splitting that belongs to 03_METRICS §Upscaling's
        // segment list rather than to a session-level tri-state.
        bool observedAny = false;
        foreach (FlFrameRecord r in stream)
        {
            var flags = (FlFeatureFlags)r.FeatureFlags;
            if (!flags.HasFlag(FlFeatureFlags.RayReconstructionObserved))
            {
                continue;
            }

            observedAny = true;
            if (flags.HasFlag(FlFeatureFlags.RayReconstruction))
            {
                return Tri.Yes;
            }
        }

        return observedAny ? Tri.No : Tri.NotApplicable;
    }

    private static Tri HdrOf(IReadOnlyList<FlFrameRecord> stream)
    {
        int start = RecordWindow.ClaimedSuffixStart(stream, static r => ((FlMeasured)r.MeasuredMask).HasFlag(FlMeasured.Hdr));
        if (start == stream.Count)
        {
            return Tri.NotApplicable;
        }

        return (FlColorSpace)stream[^1].ColorSpace is FlColorSpace.Hdr10 or FlColorSpace.ScRgb ? Tri.Yes : Tri.No;
    }
}
