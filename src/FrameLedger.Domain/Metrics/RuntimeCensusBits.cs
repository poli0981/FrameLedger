namespace FrameLedger.Domain.Metrics;

/// <summary>
/// Mirror of <c>FlRuntimeCensus</c>: which vendor RUNTIME MODULES the loader reported in the game process.
/// </summary>
/// <remarks>
/// <b>Not a hook and not a measurement.</b> A set bit says a module of that name was loaded; a clear bit
/// says the loader had none of that name — and cannot see a statically linked FSR. It therefore never
/// produces <see cref="FgKind.None"/> or <see cref="UpscalerKind.None"/>; it refines the reason for an N/A.
/// <see cref="Ran"/> is bit 0 so a writer that never took the census publishes 0 and decodes as
/// "nobody looked".
/// </remarks>
[Flags]
public enum RuntimeCensusBits
{
    None = 0,
    Ran = 1 << 0,

    SlDlssG = 1 << 1,
    NvngxDlssG = 1 << 2,
    LibXessFg = 1 << 3,
    FfxFrameInterpolation = 1 << 4,
    FfxFsr3 = 1 << 5,
    AmdFfxFrameGeneration = 1 << 6,

    SlInterposer = 1 << 8,
    SlDlss = 1 << 9,
    SlNis = 1 << 10,
    NvngxCore = 1 << 11,
    NvngxDlss = 1 << 12,
    NvngxDlssD = 1 << 13,
    LibXess = 1 << 14,
    FfxFsr2 = 1 << 15,
    FfxFsr3Upscaler = 1 << 16,
    AmdFfxUpscaler = 1 << 17,
    AmdFfxDx12 = 1 << 18,
}
