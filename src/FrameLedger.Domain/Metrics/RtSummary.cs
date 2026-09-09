namespace FrameLedger.Domain.Metrics;

/// <summary>What the ray-tracing evidence in a stream adds up to, over the claiming window.</summary>
public sealed record RtSummary
{
    /// <summary>Samples in the claiming window (<see cref="MeasuredFields.Rt"/> on every one to the end).</summary>
    public required int Claimed { get; init; }

    /// <summary>Samples in the window with an AS build or a dispatch observed.</summary>
    public required int Evidence { get; init; }

    /// <summary>Samples with any recorded dispatch volume.</summary>
    public required int ActivePresents { get; init; }

    /// <summary>Σ <c>dispatchRaysVolume</c> over the window.</summary>
    public required long TotalVolume { get; init; }

    /// <summary>Samples whose volume hit <see cref="uint.MaxValue"/> — the numerator's saturation sentinel.</summary>
    public required int VolumeSaturated { get; init; }

    /// <summary><c>rt_frame_pct</c>: evidence over PRESENTS in the window — diluted by the FG factor, by decision (<c>03_METRICS</c>).</summary>
    public double? FramePct => Claimed > 0 ? Evidence * 100.0 / Claimed : null;

    /// <summary><c>rays_per_pixel</c> over RT-ACTIVE presents, undiluted; null when refused.</summary>
    public required double? RaysPerPixel { get; init; }
}
