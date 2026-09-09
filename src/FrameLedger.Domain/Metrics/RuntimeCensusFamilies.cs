namespace FrameLedger.Domain.Metrics;

/// <summary>The two groups of <see cref="RuntimeCensusBits"/>, mirroring <c>FlRuntimeCensusFamilies</c>.</summary>
public static class RuntimeCensusFamilies
{
    /// <summary>
    /// Includes <see cref="RuntimeCensusBits.AmdFfxDx12"/>: the FidelityFX 3.1 facade dispatches upscaling AND
    /// frame generation, so a module that MAY generate frames is grouped with the ones that do.
    /// </summary>
    public const RuntimeCensusBits Fg = RuntimeCensusBits.SlDlssG | RuntimeCensusBits.NvngxDlssG
        | RuntimeCensusBits.LibXessFg | RuntimeCensusBits.FfxFrameInterpolation | RuntimeCensusBits.FfxFsr3
        | RuntimeCensusBits.AmdFfxFrameGeneration | RuntimeCensusBits.AmdFfxDx12;

    public const RuntimeCensusBits Upscaler = RuntimeCensusBits.SlInterposer | RuntimeCensusBits.SlDlss
        | RuntimeCensusBits.SlNis | RuntimeCensusBits.NvngxCore | RuntimeCensusBits.NvngxDlss
        | RuntimeCensusBits.NvngxDlssD | RuntimeCensusBits.LibXess | RuntimeCensusBits.FfxFsr2
        | RuntimeCensusBits.FfxFsr3Upscaler | RuntimeCensusBits.AmdFfxUpscaler;
}
