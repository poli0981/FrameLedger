namespace FrameLedger.Domain.Metrics;

/// <summary>Why <see cref="FgWindow"/> may not publish a number. Every kind is a number a consumer would otherwise have printed.</summary>
public enum FgRefusalKind
{
    /// <summary>Nothing refused — the value <see cref="FgWindow.Refusal"/> carries as <c>null</c>, present so a default is not a claim.</summary>
    None = 0,

    /// <summary>No sample claims <see cref="MeasuredFields.FgCounts"/>, so nothing counted evaluations.</summary>
    NotCounted,

    /// <summary><see cref="FgRefusal.Count"/> samples carry <c>swapchainId</c> 0, so the presents cannot be attributed.</summary>
    Unattributed,

    /// <summary><see cref="FgRefusal.Count"/> swapchains presented in the window; the drain word is process-wide.</summary>
    MultipleStreams,

    /// <summary><see cref="FgRefusal.Count"/> samples hit the <c>fgEvaluations</c> ceiling of 255 — a sentinel, not a count.</summary>
    CountSaturated,

    /// <summary><see cref="FgRefusal.Count"/> samples hit the <c>dxgiUnseen</c> ceiling of 255.</summary>
    DxgiSaturated,

    /// <summary>No application-frame token was counted in the window — a data gap, never <c>fg_factor 1.0</c>.</summary>
    NoEvaluations,

    /// <summary>The window holds <see cref="FgRefusal.Count"/> samples, below what the uniformity check needs.</summary>
    TooShortToCheck,

    /// <summary>Bucket <see cref="FgRefusal.BucketIndex"/> departs from the whole: the state changed mid-session.</summary>
    NonUniform,

    /// <summary>The ratio sits between the <c>none</c> ceiling and the cadence threshold — not a configuration anyone ships.</summary>
    AmbiguousBand,

    /// <summary>No present drained a Streamline batch, so there is no presents-per-batch ratio to read.</summary>
    NoBatches,
}
