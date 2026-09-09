namespace FrameLedger.Application.Persistence;

/// <summary>One <c>session_segments</c> row: a contiguous run of presents with one set of settings, on one stream.</summary>
public sealed record SegmentRow
{
    public required long SwapchainId { get; init; }

    public required long StartFrame { get; init; }

    public required long EndFrame { get; init; }

    public int? RenderW { get; init; }

    public int? RenderH { get; init; }

    public int? OutputW { get; init; }

    public int? OutputH { get; init; }

    public string? Upscaler { get; init; }

    public string? UpscalerQuality { get; init; }

    public string? FgMode { get; init; }

    public double? NativeFps { get; init; }

    public double? DisplayedFps { get; init; }

    public double? P1LowFps { get; init; }
}
