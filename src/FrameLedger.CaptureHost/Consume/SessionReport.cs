using System.Globalization;
using System.Text;

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
        if (facts.Fg?.Factor is not null)
        {
            FgWindow w = facts.Fg;
            sb.Append("  Native FPS: ").Append(Num(w.NativeFps))
              .Append(" -> Displayed FPS: ").Append(Num(w.DisplayedFps))
              .Append(" (x").Append(Num(w.Factor)).Append(" FG)")
              .Append("   over ").Append(Count(w.Presents)).AppendLine(" present(s)");
        }
        else
        {
            sb.Append("  Displayed FPS (presents observed; FG factor NOT measured): ")
              .AppendLine(Num(facts.DisplayedFps));
            if (facts.Fg?.Refusal is not null)
            {
                sb.Append("    no FG factor: ").AppendLine(facts.Fg.Refusal);
            }
        }

        AppendFgDiagnostics(sb, facts.Fg);
        AppendFacts(sb, facts);

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

        return sb.ToString().TrimEnd();
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
        sb.Append("  upscaler: ").AppendLine(facts.Upscaler
            ?? (facts.UpscalerHookRan
                ? "N/A (a hook ran and could not identify it — our coverage is short, not the title's)"
                : "N/A (no upscaler hook ran)"));
        sb.Append("  render -> output: ").AppendLine(facts.UpscaleRatio is null
            ? "N/A (the ratio is not computed yet; the raw sizes are above)"
            : Num(facts.UpscaleRatio));
        sb.Append("  frame generation: ").AppendLine(facts.FgMode
            ?? (facts.FgHookRan
                ? "N/A (a hook ran and saw no frame-generation evaluation — see the FG counts above)"
                : "N/A (no frame-generation hook ran)"));
        sb.Append("  ray tracing: ").AppendLine(facts.RayTracing.ToString());
        sb.Append("  ray reconstruction: ").AppendLine(facts.RayReconstruction.ToString());
        sb.Append("  path tracing: ").AppendLine(facts.PathTracing.ToString());
        sb.Append("  HDR: ").AppendLine(facts.Hdr.ToString());
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
          .Append(" evaluations=").Append(Count(w.Evaluations))
          .Append(" streams=").Append(Count(w.Streams))
          .Append(" unidentified=").Append(Count(w.Unidentified))
          .Append(" span=").Append(Num(w.Seconds > 0 ? w.Seconds : null)).Append('s')
          .Append("  presents/batch=").Append(Num(w.PresentsPerBatch))
          .Append("  evaluations/batch=").AppendLine(Num(w.EvaluationsPerBatch));

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

    private static string Num(double? v) =>
        v is null ? "N/A" : v.Value.ToString("0.##", CultureInfo.InvariantCulture);

    private static string Count(long v) => v.ToString(CultureInfo.InvariantCulture);
}
