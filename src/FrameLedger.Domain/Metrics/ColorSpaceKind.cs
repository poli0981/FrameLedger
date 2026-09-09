namespace FrameLedger.Domain.Metrics;

/// <summary>Mirror of <c>FlColorSpace</c>. Was a bool, which had no third state.</summary>
public enum ColorSpaceKind
{
    NotReported = 0,
    Sdr = 1,
    Hdr10 = 2,
    ScRgb = 3,
}
