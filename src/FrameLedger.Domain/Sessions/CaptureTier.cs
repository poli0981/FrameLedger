namespace FrameLedger.Domain.Sessions;

/// <summary>
/// <c>04_CAPTURE</c> §Tier selection: two rungs, and the second is not a measurement. Owner decision 2026-08-28.
/// </summary>
public enum CaptureTier
{
    /// <summary>
    /// Nobody chose a tier. The zero value, so a default is not a claim; the schema's CHECK refuses it on a
    /// stored session (the rule <c>AntiCheatVerdict</c> and <c>ConsentProvenance</c> follow).
    /// </summary>
    NotRecorded = 0,

    /// <summary>Injected hooks, or the Vulkan layer: everything.</summary>
    Hooked = 1,

    /// <summary>Nothing hooked: session duration, whatever telemetry the machine provides, and the REASON.</summary>
    NotHooked = 2,
}
