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
        sb.Append("  Displayed FPS (presents observed; FG state not measured): ")
          .AppendLine(Num(facts.DisplayedFps));

        // Rule 6: Native and the factor are shown WITH Displayed or not at all. Both are null here, and
        // printing "Native: N/A" beside a real Displayed figure would invite reading Displayed as
        // Native — which is the single inflated number the rule forbids, reached by a renderer instead
        // of by a calculator.
        if (facts.NativeFps is not null && facts.FgFactor is not null)
        {
            sb.Append("  Native FPS: ").Append(Num(facts.NativeFps))
              .Append("  FG factor: ").AppendLine(Num(facts.FgFactor));
        }

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

    private static string Num(double? v) =>
        v is null ? "N/A" : v.Value.ToString("0.##", CultureInfo.InvariantCulture);
}
