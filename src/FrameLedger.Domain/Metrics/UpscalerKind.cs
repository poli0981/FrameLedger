namespace FrameLedger.Domain.Metrics;

/// <summary>
/// Mirror of <c>FlUpscaler</c>: the technology actually executing. <b>Zero is "nobody said", not a fact.</b>
/// </summary>
/// <remarks>
/// Three distinct states, and all three are needed: <see cref="NotReported"/> (no hook was live — N/A),
/// <see cref="Unknown"/> (a hook ran and could not identify what it saw — also N/A, but our coverage is
/// short rather than the question not applying), and <see cref="None"/> (a hook ran and there genuinely
/// was no upscaler — the only one that may be aggregated as a negative).
/// </remarks>
public enum UpscalerKind
{
    NotReported = 0,
    Dlss = 1,

    /// <summary>Retired in layout v3 and reserved rather than reused; Ray Reconstruction is a feature flag.</summary>
    RetiredRayReconstruction = 2,

    Fsr2 = 3,
    Fsr3 = 4,
    Fsr4 = 5,
    XeSS = 6,
    Nis = 7,

    /// <summary>A hook ran and there was no upscaler.</summary>
    None = 8,

    /// <summary>FSR through the SDK 2.x upscaler DLL, which hosts FSR 3.1 and FSR 4 behind one dispatch type.</summary>
    FsrUnversioned = 9,

    /// <summary>A hook ran and could not identify what it saw. Different from "not measured".</summary>
    Unknown = 0xFF,
}
