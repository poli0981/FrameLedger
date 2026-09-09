using System.Globalization;
using FrameLedger.Domain.Metrics;

namespace FrameLedger.CaptureHost.Consume;

/// <summary>
/// The English for a <see cref="FgRefusal"/>. Domain carries the fact and its numbers; the wording is the
/// report's, and it is byte-identical to what the report printed while the arithmetic lived here.
/// </summary>
/// <remarks>
/// Operator-facing, en-only, like every other line this unshipped host prints. The <c>.resx</c> keys arrive
/// with the UI (P3); what this keeps is that the text never decides anything — every branch below reads
/// a <see cref="FgRefusalKind"/> the calculator already chose.
/// </remarks>
internal static class FgRefusalText
{
    /// <summary>The sentence for <paramref name="refusal"/>, or null when nothing was refused.</summary>
    public static string? Describe(FgRefusal? refusal)
    {
        if (refusal is null)
        {
            return null;
        }

        string count = refusal.Count.ToString(CultureInfo.InvariantCulture);
        return refusal.Kind switch
        {
            FgRefusalKind.NotCounted => "no record claims FL_MEASURED_FG_COUNTS, so nothing counted evaluations",
            FgRefusalKind.Unattributed => refusal.Subject == FgRefusalSubject.Factor
                ? $"{count} record(s) carry swapchainId 0, so the presents cannot be attributed and the ratio has no "
                  + "denominator anyone can name"
                : $"{count} record(s) carry swapchainId 0, so the presents cannot be attributed",
            FgRefusalKind.MultipleStreams => refusal.Subject == FgRefusalSubject.Factor
                ? $"{count} swapchains presented in the window; g_slSeen is one process-wide word, so an evaluation "
                  + "belonging to one stream can be drained by another's present"
                : $"{count} swapchains presented in the window; g_slSeen is one process-wide word, so a batch "
                  + "belonging to one stream can be drained by another's present",
            FgRefusalKind.CountSaturated =>
                $"{count} record(s) hit the fgEvaluations ceiling of 255, which is a saturation sentinel rather than "
                + "a count — dividing by it would report a floor",
            FgRefusalKind.DxgiSaturated =>
                $"{count} record(s) hit the dxgiUnseen ceiling of 255, which is a saturation sentinel rather than a "
                + "count — DXGI counted more presents than the byte can carry",
            FgRefusalKind.NoEvaluations =>
                "no application-frame token was counted in the window (slGetNewFrameToken, or an ffx-api PREPARE / "
                + "UPSCALE dispatch) — a data gap, and treating it as 'no frame generation' is how fg_factor becomes 1.0",
            FgRefusalKind.TooShortToCheck =>
                $"the window holds {count} record(s), below the "
                + $"{FgWindow.MinSamplesToCheck.ToString(CultureInfo.InvariantCulture)} needed to check whether "
                + $"{Subject(refusal.Subject)} changed during it",
            FgRefusalKind.NonUniform =>
                $"{Subject(refusal.Subject)} changed during the session — bucket "
                + $"{(refusal.BucketIndex + 1).ToString(CultureInfo.InvariantCulture)} of "
                + $"{refusal.BucketCount.ToString(CultureInfo.InvariantCulture)} measures {Fmt(refusal.BucketValue)} "
                + $"against {Fmt(refusal.Overall)} overall, and a session-level number would describe a configuration "
                + "that never existed",
            FgRefusalKind.AmbiguousBand =>
                $"presents/tokens = {refusal.Overall.ToString("0.00", CultureInfo.InvariantCulture)} sits between the "
                + $"`none` ceiling ({FgWindow.NoneCeiling.ToString("0.00", CultureInfo.InvariantCulture)}) and the "
                + $"cadence threshold ({FgWindow.ActiveThreshold.ToString("0.0", CultureInfo.InvariantCulture)}) — not "
                + "a configuration this consumer can name",
            FgRefusalKind.NoBatches => "no present drained a Streamline batch, so there is no ratio to read",
            _ => refusal.Kind.ToString(),
        };
    }

    private static string Subject(FgRefusalSubject subject) =>
        subject == FgRefusalSubject.Factor ? "the frame-generation state" : "the presents-per-batch ratio";

    private static string Fmt(double v) =>
        double.IsInfinity(v) ? "no evaluations" : v.ToString("0.##", CultureInfo.InvariantCulture);
}
