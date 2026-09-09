namespace FrameLedger.Domain.Metrics;

/// <summary>
/// Mirror of <c>FlRtTier</c>: the device ray-tracing tier as <c>D3D12_RAYTRACING_TIER</c>'s own value
/// (already tier ×10), with the two states a copied vendor enum would have collapsed kept apart.
/// </summary>
public enum RtTierValue
{
    /// <summary>No D3D12 device was identified, or the capability query failed.</summary>
    NotQueried = 0,

    /// <summary>D3D12 answered NOT_SUPPORTED. A measurement, not a silence.</summary>
    Unsupported = 1,

    /// <summary>The threshold <c>03_METRICS</c> §RT/PT/RR states for "an RT-capable device".</summary>
    CapableMin = 10,
}
