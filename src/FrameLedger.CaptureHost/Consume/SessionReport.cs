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

        sb.Append("  upscaler: ").AppendLine(facts.Upscaler ?? "N/A (no upscaler hook ran)");
        sb.Append("  render -> output: ").AppendLine(
            facts.UpscaleRatio is null ? "N/A (render resolution is not measured)" : Num(facts.UpscaleRatio));
        sb.Append("  frame generation: ").AppendLine(facts.FgMode ?? "N/A (no frame-generation hook ran)");
        sb.Append("  ray tracing: ").AppendLine(facts.RayTracing.ToString());
        sb.Append("  ray reconstruction: ").AppendLine(facts.RayReconstruction.ToString());
        sb.Append("  path tracing: ").AppendLine(facts.PathTracing.ToString());
        sb.Append("  HDR: ").AppendLine(facts.Hdr.ToString());

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

    /// <summary>
    /// The numbers a verification run reads, printed whether or not a factor was published.
    /// </summary>
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
    private static void AppendFgDiagnostics(StringBuilder sb, FgWindow? w)
    {
        if (w is null || w.Presents == 0)
        {
            return;
        }

        sb.Append("    FG counts: presents=").Append(Count(w.Presents))
          .Append(" batches=").Append(Count(w.Batches))
          .Append(" evaluations=").Append(Count(w.Evaluations))
          .Append("  presents/batch=").Append(Num(w.Batches > 0 ? w.Presents / (double)w.Batches : null))
          .Append("  evaluations/batch=").AppendLine(Num(w.EvaluationsPerBatch));

        if (w.EvaluationsPerBatch is { } perBatch && Math.Abs(perBatch - 1.0) > 0.05)
        {
            sb.Append("    PREMISE VIOLATED: ").Append(Num(perBatch))
              .AppendLine(" evaluations per drained batch, not 1 — the FG factor below is that many "
                          + "times too small, and item 3's once-per-application-frame premise is wrong");
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
