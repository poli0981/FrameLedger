using System.Globalization;
using System.Text;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// Renders <see cref="MeasuredFacts"/> without inventing anything it does not hold.
/// </summary>
/// <remarks>
/// <para>
/// <b>CLAUDE.md rule 6 is a rule about the RENDERER as much as about the maths.</b>
/// "Wherever FPS is shown and Frame Generation is active, show Native FPS,
/// Displayed FPS and the FG factor together — never a single inflated number." The
/// present-only writer has no FG hook, so it does not know whether FG is active;
/// the honest rendering is therefore one labelled number and an explicit statement
/// that the FG state was not measured, not <c>×1.0</c> and not a bare FPS figure
/// that a reader would take for Native.
/// </para>
/// <para>
/// A test asserts the rendered text matches no <c>×N</c> pattern while
/// <see cref="MeasuredFacts.FgFactor"/> is null, because "we never format it" is
/// the kind of claim that survives a refactor by accident.
/// </para>
/// </remarks>
internal static class SessionReport
{
    public static string Render(MeasuredFacts facts)
    {
        ArgumentNullException.ThrowIfNull(facts);

        var sb = new StringBuilder();

        // RULE 6: the three numbers appear TOGETHER, over ONE window, or not at all.
        //
        // Over one window is the half that is easy to lose. `facts.DisplayedFps` spans the
        // whole dominant stream while the factor spans the post-install suffix, so printing
        // the first beside the other two would produce a trio in which no member describes
        // the same records as its neighbours — and, because Native × factor == Displayed
        // would then be false, a reader could not even check it. So when there is a factor,
        // all three come off the window; when there is not, one labelled number and the
        // reason are all that may be said.
        if (facts.Fg?.IsActive == true)
        {
            FgWindow w = facts.Fg;
            sb.Append("  Native FPS: ").Append(Num(w.NativeFps))
              .Append(" -> Displayed FPS: ").Append(Num(w.DisplayedFps))
              .Append(" (x").Append(Num(w.Factor)).Append(" FG)")
              .Append("   over ").Append(Count(w.Presents)).AppendLine(" present(s)");
        }
        else if (facts.Fg?.IsNone == true && facts.NoneWithheld is null)
        {
            // THE THIRD SHAPE, reachable since 2026-09-04: 08_UI's bare `144 FPS`. No pair, no
            // factor, no qualifier — the count measured that every present carried an
            // application frame, which is the one negative this report may state outright.
            //
            // UNLESS IT IS WITHHELD. On Streamline 2.8 with sl.dlss_g.dll loaded the same count read
            // 1.00 five times while the title was generating (§H5 case 3), so that shape falls
            // through to the Presented line and its qualifier says why. MeasuredFacts owns the
            // decision; this branch only honours it.
            FgWindow w = facts.Fg;
            sb.Append("  FPS: ").Append(Num(w.NativeFps))
              .Append("   over ").Append(Count(w.Presents)).AppendLine(" present(s)");
            sb.Append("    frame generation: none — ").Append(Count(w.Evaluations))
              .Append(" application frame(s) counted (slGetNewFrameToken, or an ffx-api PREPARE / UPSCALE dispatch) against ")
              .Append(Count(w.Presents))
              .AppendLine(" present(s); every present carried an application frame");
        }
        else
        {
            AppendPresented(sb, facts);
        }

        AppendFgDiagnostics(sb, facts.Fg);
        AppendFacts(sb, facts);
        AppendWarnings(sb, facts);

        return sb.ToString().TrimEnd();
    }

    /// <summary>
    /// PRESENTED FPS, the name for the one number that stands alone (03_METRICS §Core definitions,
    /// 2026-09-03). Numerically it is Displayed FPS; the name changes because "Displayed" is half of
    /// a pair, and printing half a pair invites the reader to supply the other half. The qualifier
    /// under it is mandatory, and which one is printed is the census's whole job.
    /// </summary>
    private static void AppendPresented(StringBuilder sb, MeasuredFacts facts)
    {
        sb.Append("  Presented FPS: ").Append(Num(facts.DisplayedFps))
          .Append("   over ").Append(Count(facts.PresentsObserved)).AppendLine(" present(s)");
        sb.Append("    ").AppendLine(FgQualifier(facts));
        if (facts.NoneWithheld is not null)
        {
            sb.AppendLine("    frame generation: NOT stated — the FG counts below are the record; the discriminator "
                          + "is the title's own counter beside this line (§H5)");
        }

        if (facts.Fg?.Refusal is not null)
        {
            sb.Append("    no FG factor: ").AppendLine(facts.Fg.Refusal);
        }
    }

    /// <summary>Defects in the DATA, each attributed to where it came from, never averaged in.</summary>
    private static void AppendWarnings(StringBuilder sb, MeasuredFacts facts)
    {
        if (facts.HasDataGaps)
        {
            sb.AppendLine("  WARNING: the drain reported torn or overwritten records; intervals spanning "
                          + "them are not frame times (04_CAPTURE §Ring draining)");
        }

        if (facts.HonestyViolations > 0)
        {
            sb.Append("  WARNING: ").Append(facts.HonestyViolations.ToString(CultureInfo.InvariantCulture))
              .AppendLine(" record(s) claimed a measurement this writer cannot make — that is a defect in "
                          + "the Overlay, not in this report");
        }

        if (facts.CensusInconsistent)
        {
            sb.AppendLine("  WARNING: the runtime census names a module while saying it never ran — a defect in "
                          + "the Overlay's watchdog, and nothing above was read from it");
        }
    }

    /// <summary>
    /// The line under Presented FPS. Three shapes, and the census decides which — never a
    /// fourth that says <c>none</c>.
    /// </summary>
    /// <remarks>
    /// The clear-census sentence names its own two holes because they are the reason the
    /// census may not say <c>none</c>: a statically linked FSR3-FG has no module to see, and
    /// driver-level AFMF happens after present. Both are outside what an in-process module
    /// list can know, and the sentence is only honest with them in it.
    /// </remarks>
    private static string FgQualifier(MeasuredFacts facts)
    {
        // THE WITHHELD SHAPE COMES FIRST, because the WARNING below it reads in the wrong
        // direction for this case. "MAY include generated frames; read it as Displayed" is right
        // when nothing was counted; here the count says the presents ARE the application frames
        // and the generated ones are the number nobody has. Displayed is what is unknown.
        if (facts.NoneWithheld is not null)
        {
            return "WARNING: the count read presents = application frames (1.00) and `none` is WITHHELD — "
                   + facts.NoneWithheld
                   + ". 20_OPEN_QUESTIONS §H5 case 3: on this shape the generated presents may never reach the "
                   + "Present bodies this hook patches, so this number counts APPLICATION frames; whether frames were "
                   + "generated, and the Displayed rate, are unknown. Read it as the application-frame rate, never "
                   + "as Displayed"
                   + (facts.DlssgInputsTagged
                       ? ". The title tagged DLSS-G inputs (HUD-less / UI) through Streamline this session — it is "
                         + "FEEDING frame generation, which is the identity; the count is still what it is"
                       : "");
        }

        if (!facts.CensusRan)
        {
            return "frame generation: NOT measured, and the runtime census did not run — this number is presents, "
                   + "and whether they include generated frames is unknown";
        }

        if (facts.FgRuntimesLoaded != FlRuntimeCensus.None)
        {
            return "WARNING: a frame-generation runtime was loaded (" + CensusNames.Describe(facts.FgRuntimesLoaded)
                   + ") and no evaluation was observed — this number MAY include generated frames; read it as "
                   + "Displayed, not Native";
        }

        return "frame generation: not measured — no known frame-generation runtime was loaded in this process, "
               + "so this number cannot include in-process generated frames (statically linked FSR3-FG and "
               + "driver-level AFMF are outside what this can see)";
    }

    /// <summary>The per-session facts, each with the reason behind its N/A.</summary>
    /// <remarks>
    /// <b>THE TWO N/As ARE NOT THE SAME N/A</b>, and printing one wording for both discards the
    /// distinction <c>fl_shm.h</c> exists to preserve. "No hook capable of answering was live"
    /// and "a hook ran and could not identify what it saw" mean different things about our
    /// COVERAGE, and only the second is a lead worth following. Measured on Cyberpunk 2077,
    /// 2026-08-15: the upscaler hook was live for 9,990 of 10,088 records and this line said it
    /// never ran — the report asserting the opposite of what the line above it had just printed.
    /// </remarks>
    private static void AppendFacts(StringBuilder sb, MeasuredFacts facts)
    {
        // THREE CAUSES, NOT ONE. This line used to say "our coverage is short, not the title's" for
        // every UNKNOWN, and that is false in the commonest case: a Streamline title with upscaling
        // switched off in its settings never calls slEvaluateFeature either. Nothing in this writer
        // can tell that apart from a title driving DLSS through a path we do not hook — Black Myth:
        // Wukong loads sl.interposer.dll, runs DLSS-G, and calls it never — so the line names all
        // three rather than picking the one that flatters the hook.
        sb.Append("  upscaler: ").AppendLine(facts.Upscaler
            ?? (facts.UpscalerHookRan
                ? "N/A (an upscaler hook ran — Streamline's slEvaluateFeature and/or an ffx-api leaf's ffxDispatch — "
                  + "and no evaluation this build decodes arrived: "
                  + "upscaling is off in this title's settings, or it runs through a path this build does not "
                  + "hook — measured on 3 of 5 Streamline titles — or a vendor this build does not decode)"
                : NoHookRan("upscaler", facts.CensusRan, facts.UpscalerRuntimesLoaded)));
        sb.Append("  render -> output: ").AppendLine(RenderToOutput(facts.Extent));
        sb.Append("  frame generation: ").AppendLine(facts.FgMode
            ?? (facts.FgHookRan
                ? "N/A (a hook ran and saw no frame-generation evaluation — see the FG counts above)"
                : NoHookRan("frame-generation", facts.CensusRan, facts.FgRuntimesLoaded)));
        sb.Append("  ray tracing: ").AppendLine(facts.RayTracing.ToString());
        sb.Append("  ray reconstruction: ").AppendLine(facts.RayReconstruction.ToString());
        sb.Append("  path tracing: ").AppendLine(facts.PathTracing.ToString());
        sb.Append("  HDR: ").AppendLine(facts.Hdr.ToString());
    }

    /// <summary>
    /// "No hook ran", refined by the census: was there even a runtime of that kind to hook?
    /// </summary>
    private static string NoHookRan(string kind, bool censusRan, FlRuntimeCensus loaded)
    {
        if (!censusRan)
        {
            return $"N/A (no {kind} hook ran)";
        }

        string article = kind[0] is 'a' or 'e' or 'i' or 'o' or 'u' ? "an" : "a";
        return loaded == FlRuntimeCensus.None
            ? $"N/A (no {kind} hook ran, and no known {kind} runtime was loaded in this process)"
            : $"N/A (no {kind} hook ran, though {article} {kind} runtime is loaded: {CensusNames.Describe(loaded)} — "
              + "an ABI this build refuses, or a vendor it does not hook)";
    }

    /// <summary>
    /// The numbers a verification run reads, printed whether or not a factor was published.
    /// </summary>
    private static void AppendFgDiagnostics(StringBuilder sb, FgWindow? w)
    {
        if (w is null || w.Presents == 0)
        {
            return;
        }

        // streams= AND unidentified= ARE PRINTED UNCONDITIONALLY, because a refusal that never
        // fires says nothing and the reader cannot tell silence from cleanliness. On a title
        // whose frame generation is driven off this route the zero-count refusal wins, so the
        // attribution refusals never speak — and presents/batch, which IS printed, is only
        // meaningful over one stream. Print the provenance beside the ratio or the ratio is a
        // number with an unnamed denominator.
        //
        // span= LIKEWISE, and it was computed and never printed. §S30's draft had to
        // reconstruct it from Displayed FPS — a DIFFERENT window, since MeasuredFacts runs from
        // record 0 while this one starts after the lazy-install prefix — and the resulting rate
        // moved across 78.6-83 on window choice alone, ten times the residual that draft quoted.
        sb.Append("    FG counts: presents=").Append(Count(w.Presents))
          .Append(" batches=").Append(Count(w.Batches))
          .Append(" tokens=").Append(Count(w.Evaluations))
          .Append(" streams=").Append(Count(w.Streams))
          .Append(" unidentified=").Append(Count(w.Unidentified))
          .Append(" span=").Append(Num(w.Seconds > 0 ? w.Seconds : null)).Append('s')
          .Append("  presents/batch=").Append(Num(w.PresentsPerBatch))
          .Append("  tokens/batch=").AppendLine(Num(w.EvaluationsPerBatch));

        AppendProxyVerdict(sb, w);
        AppendFgAnomalies(sb, w);
    }

    /// <summary>
    /// The proxy's own uniformity verdict, on the line under the proxy, always.
    /// </summary>
    /// <remarks>
    /// <b>A number printed without its guard is a number a reader will take.</b>
    /// <c>presents/batch</c> above is the sharpest figure this report produces and the one a
    /// verification run reads — and until now nothing checked whether the window it averages
    /// was one configuration. <c>FgWindow.BucketFactors</c> could not: it divides by
    /// <c>Σ fgEvaluations</c>, zero on this route, so every bucket matched and the check passed
    /// vacuously. Measured 2026-08-16, an alt-tab mid-capture produced 1.84 against a title
    /// configured for ×2 and the report said nothing at all.
    /// </remarks>
    private static void AppendProxyVerdict(StringBuilder sb, FgWindow w)
    {
        sb.Append("    presents/batch is a PROXY — a batch is a drained Streamline evaluation, "
                  + "NOT an application frame: ");
        sb.AppendLine(w.BatchRefusal is null
            ? "the window is uniform across every bucket, so the ratio describes one configuration"
            : "NOT READABLE — " + w.BatchRefusal);
    }

    /// <summary>The three things worth saying only when they happened.</summary>
    /// <remarks>
    /// <para>
    /// <b><c>EvaluationsPerBatch</c> is the premise under test, and it needs no oracle.</b>
    /// HANDOFF item 3 assumes <c>slEvaluateFeature(kFeatureDLSS_G)</c> fires once per
    /// application frame, and nothing in this repository has verified it. Three per frame at
    /// ×4 gives a factor of 1.34 — above 1.0, so an over-counting guard is silent; not equal
    /// to 1.0, so a structurally-1.0 guard is silent; and it still moves with the setting, so
    /// a three-point sweep passes. This is the one number that says 3 instead of 1.
    /// </para>
    /// <para>
    /// <b>A factor of exactly 1.0 is NOT reported as §H5 case 3.</b> At least three causes
    /// produce it and this data cannot tell them apart: generated presents never reaching the
    /// vtable we patch; frame generation configured off while the title still evaluates the
    /// feature; and an evaluation that FAILED — <c>Hook_SlEvaluateFeature</c> increments
    /// before forwarding and ignores the <c>sl::Result</c>, so a refused evaluation is counted.
    /// Naming one of them here is how the item gets routed down the wrong branch.
    /// </para>
    /// </remarks>
    private static void AppendFgAnomalies(StringBuilder sb, FgWindow w)
    {
        // GATED ON HAVING COUNTED SOMETHING, and the first version was not — which a real
        // title caught within minutes. With Σ = 0 the quotient is 0, "0 evaluations per
        // batch" trivially differs from 1, and the line fired claiming the premise was
        // violated and that "the FG factor is that many times too small". Neither is true:
        // nothing was counted, the refusal above says exactly that, and a factor that does
        // not exist cannot be scaled. A premise about the RATIO of two counts says nothing
        // when one of them is zero.
        if (w.Evaluations > 0 && w.EvaluationsPerBatch is { } perBatch && Math.Abs(perBatch - 1.0) > 0.05)
        {
            sb.Append("    PREMISE VIOLATED: ").Append(Num(perBatch))
              .AppendLine(" evaluations per drained batch, not 1 — the FG factor is wrong by that "
                          + "factor, and item 3's once-per-application-frame premise does not hold here");
        }

        // AND THE CASE THAT ACTUALLY OCCURRED: batches arrived, evaluations did not. That is
        // a statement about the TITLE, not about our arithmetic, and it is the sharpest
        // single number this report produces — presents/batch near N on a title configured
        // for N-times frame generation says the generated presents DO reach our hook while
        // the frame-generation feature is not evaluated through slEvaluateFeature at all.
        if (w.Evaluations == 0 && w.Batches > 0)
        {
            sb.Append("    NO FG EVALUATION IN ").Append(Count(w.Batches))
              .AppendLine(" batch(es): Streamline was evaluated and kFeatureDLSS_G was never among "
                          + "the ids. Read presents/batch above against the title's own FG setting — "
                          + "if it is near that setting, the feature is driven somewhere this hook "
                          + "does not see, and the counter cannot measure it on this title");
        }

        sb.Append("    fgEvaluations histogram (0,1,2,3,4,5+): ")
          .AppendLine(string.Join(',', w.Histogram.Select(v => Count(v))));

        if (w.Factor is 1.0)
        {
            sb.AppendLine("    factor is exactly 1.0 with evaluations observed — AT LEAST THREE causes and "
                          + "this data cannot separate them: generated presents missing our vtable (§H5 "
                          + "case 3), FG configured off while the feature is still evaluated, or evaluations "
                          + "that FAILED and were counted anyway (the hook ignores sl::Result)");
        }
    }

    /// <summary>
    /// The extent line: two measured sizes, the ratio <c>03_METRICS</c> defines over them, and
    /// whether the window ran at one tuple. Never a preset name — that is HANDOFF 7a's owner call.
    /// </summary>
    private static string RenderToOutput(UpscaleExtent? e)
    {
        if (e is null)
        {
            return "N/A (no record carried both a render size and an output size)";
        }

        string line = $"{e.RenderW}x{e.RenderH} -> {e.OutputW}x{e.OutputH} = {Num(e.Ratio)}x "
                      + $"({e.RenderScalePercent.ToString("0", CultureInfo.InvariantCulture)}% render scale) "
                      + $"on {Count(e.Records)} of {Count(e.Measured)} record(s)";
        return e.DistinctGroups > 1
            ? line + $" — SETTINGS MOVED: {Count(e.DistinctGroups)} distinct extents in the window, this is the dominant one"
            : line;
    }

    private static string Num(double? v) =>
        v is null ? "N/A" : v.Value.ToString("0.##", CultureInfo.InvariantCulture);

    private static string Count(long v) => v.ToString(CultureInfo.InvariantCulture);
}
