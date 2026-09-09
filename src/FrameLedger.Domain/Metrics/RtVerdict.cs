namespace FrameLedger.Domain.Metrics;

/// <summary>
/// <c>03_METRICS</c> §RT/PT/RR: the evidence-based tri-states. <c>Yes</c> needs evidence; <c>No</c> needs three
/// conjuncts; everything else is <see cref="Tri.NotApplicable"/>, never a fabricated negative.
/// </summary>
public static class RtVerdict
{
    private const RtEvidenceBits _evidence = RtEvidenceBits.AsBuildObserved | RtEvidenceBits.DispatchObserved;

    /// <summary>The counts the verdict is decided on, over ONE record set for both the evidence and its denominator.</summary>
    /// <remarks>
    /// Over the CLAIMING window, not every sample: the RT hooks install on a watchdog tick, so the first second
    /// of a session predates them and those samples can carry no evidence by construction. Measured 2026-08-20
    /// on Cyberpunk at ×4: 24.2% of all records and 25.0% of the claiming window — and 25.0% is the number the
    /// pre-registered falsifier is stated against.
    /// </remarks>
    public static RtSummary Summarise(IReadOnlyList<FrameSample> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        int start = RecordWindow.ClaimedSuffixStart(stream, MeasuredFields.Rt);
        int evidence = 0;
        int active = 0;
        int saturated = 0;
        long volume = 0;
        double pixelSum = 0;
        for (int i = start; i < stream.Count; i++)
        {
            FrameSample s = stream[i];
            if ((s.Rt & _evidence) != RtEvidenceBits.None)
            {
                evidence++;
            }

            if (s.DispatchRaysVolume > 0)
            {
                active++;
                volume += s.DispatchRaysVolume;
                pixelSum += (double)s.OutputW * s.OutputH;
                if (s.DispatchRaysVolume == uint.MaxValue)
                {
                    saturated++;
                }
            }
        }

        // rays_per_pixel over the RT-ACTIVE presents: the accumulator drains every present, so a frame's whole
        // volume lands on one present and the generated ones contribute nothing to either side. A saturated
        // volume is a floor, not a count, and is refused rather than averaged.
        double? perPixel = active > 0 && saturated == 0 && pixelSum > 0
            ? (volume / (double)active) / (pixelSum / active)
            : null;

        return new RtSummary
        {
            Claimed = stream.Count - start,
            Evidence = evidence,
            ActivePresents = active,
            TotalVolume = volume,
            VolumeSaturated = saturated,
            RaysPerPixel = perPixel,
        };
    }

    /// <summary>
    /// RT: <see cref="Tri.Yes"/> when an AS build or a dispatch was observed in ≥ 5% of the window's presents;
    /// <see cref="Tri.No"/> only when the device is RT-capable, the AS-build hook was INSTALLED, and nothing
    /// was observed all session; otherwise N/A.
    /// </summary>
    /// <remarks>
    /// Three conjuncts, and the middle one is the one that is easy to drop: a writer with only the DispatchRays
    /// hook sees nothing on an inline-RayQuery title, and its silence is indistinguishable from a real negative.
    /// </remarks>
    public static Tri RayTracing(IReadOnlyList<FrameSample> stream, WriterFacts writer)
    {
        ArgumentNullException.ThrowIfNull(writer);

        RtSummary summary = Summarise(stream);
        bool measured = summary.Claimed > 0;
        if (measured && summary.Evidence * 20 >= summary.Claimed)
        {
            return Tri.Yes;
        }

        bool asBuildInstalled = writer.HooksInstalled.HasFlag(HookFamilies.RtAsBuild);
        return measured && writer.RtCapable && asBuildInstalled && summary.Evidence == 0 ? Tri.No : Tri.NotApplicable;
    }

    /// <summary>
    /// Ray Reconstruction, decided over the presents that DRAINED a Streamline batch: none ⇒ N/A; any carrying
    /// the fact ⇒ <see cref="Tri.Yes"/>; batches and none carried it ⇒ <see cref="Tri.No"/>, the one RR negative
    /// a consumer may aggregate.
    /// </summary>
    /// <remarks>
    /// Gated on the in-band OBSERVED bit, not on the measured mask: <see cref="MeasuredFields.Upscaler"/> also
    /// covers FFX, XeSS and NIS, so a writer with FFX hooks and no NGX hooks knows nothing about RR. And NOT a
    /// suffix window: the bit is a per-present observation, intermittent by construction under frame generation
    /// (one batch spans ~4 presents), so demanding it on every sample is a condition that cannot hold at N &gt; 1.
    /// </remarks>
    public static Tri RayReconstruction(IReadOnlyList<FrameSample> stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        bool observedAny = false;
        foreach (FrameSample s in stream)
        {
            if (!s.Features.HasFlag(FeatureBits.RayReconstructionObserved))
            {
                continue;
            }

            observedAny = true;
            if (s.Features.HasFlag(FeatureBits.RayReconstruction))
            {
                return Tri.Yes;
            }
        }

        return observedAny ? Tri.No : Tri.NotApplicable;
    }
}
