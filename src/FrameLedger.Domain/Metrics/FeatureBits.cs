namespace FrameLedger.Domain.Metrics;

/// <summary>
/// Mirror of <c>FlFeatureFlags</c>: per-frame boolean facts, each paired with an OBSERVED companion four
/// bits up. A fact bit without its OBSERVED bit says nothing.
/// </summary>
[Flags]
public enum FeatureBits
{
    None = 0,
    RayReconstruction = 1 << 0,
    ReflexEnabled = 1 << 1,

    /// <summary>A Streamline feature id the Overlay does not decode was evaluated this frame.</summary>
    SlUndecoded = 1 << 2,

    /// <summary>A super-resolution id was evaluated — raw, independent of what the decode made of it.</summary>
    SlSuperResolution = 1 << 3,

    /// <summary>
    /// The EXACT indicator of "this present drained a non-empty Streamline word": the writer sets it under
    /// <c>seen != 0</c> and nothing else, so it is the batch count's weight.
    /// </summary>
    RayReconstructionObserved = 1 << 4,
    ReflexObserved = 1 << 5,
    SlUndecodedObserved = 1 << 6,
}
