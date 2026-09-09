namespace FrameLedger.Domain.Metrics;

/// <summary>
/// Which fields a frame actually MEASURED — Domain's mirror of <c>FlMeasured</c> in
/// <c>FrameLedger.Shared</c>, numerically identical and pinned both ways by
/// <c>MetricEnumMirrorTests</c>.
/// </summary>
/// <remarks>
/// A bit CLEAR means "not measured": render N/A and do not aggregate. The zero defaults elsewhere in
/// a sample are affirmative negatives, and a writer that has not installed the corresponding hook is
/// not entitled to make them. Domain carries its own copy because it references nothing
/// (CLAUDE.md §Solution layout); the mapper in <c>FrameLedger.Application.Metrics</c> is the one
/// place a shared-memory record becomes a <see cref="FrameSample"/>.
/// </remarks>
[Flags]
public enum MeasuredFields
{
    None = 0,

    /// <summary>Upscaler IDENTITY only — the parameters are <see cref="UpscalerParams"/>.</summary>
    Upscaler = 1 << 0,

    /// <summary>FG IDENTITY only — the per-present counts are <see cref="FgCounts"/>.</summary>
    Fg = 1 << 1,
    Rt = 1 << 2,
    Pso = 1 << 3,
    Vram = 1 << 4,
    Latency = 1 << 5,

    /// <summary>From the swapchain description, not a feature hook.</summary>
    OutputRes = 1 << 6,
    Hdr = 1 << 7,

    /// <summary><c>upscalerQuality</c> + <c>upscalerSharpness</c> + <c>renderW/H</c>.</summary>
    UpscalerParams = 1 << 8,

    /// <summary><c>syncInterval</c> + <c>presentFlags</c>.</summary>
    PresentArgs = 1 << 9,

    /// <summary><c>fgEvaluations</c>. With this clear, <c>F_app</c> is a data gap — never equal to <c>F_disp</c>.</summary>
    FgCounts = 1 << 10,

    /// <summary><c>dxgiUnseen</c> is a measurement on this sample, a zero included.</summary>
    DxgiPresents = 1 << 11,
}
