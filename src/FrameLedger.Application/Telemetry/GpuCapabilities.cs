namespace FrameLedger.Application.Telemetry;

/// <summary>
/// Which <see cref="GpuSample"/> fields a source has actually produced on this machine.
/// </summary>
/// <remarks>
/// <para>
/// <c>18_GPU_VENDOR_APIS</c> §Abstraction: <i>"Every field nullable — Capabilities says what
/// to trust, and the UI shows N/A rather than zero."</i> A bit here is a claim that the field
/// has carried a real value at least once in this session, never a claim that the vendor
/// path <i>should</i> provide it. A sensor that exists in the tree and never reports is not
/// a capability — that is the one direction a source must never get wrong, because a
/// capability bit over a null field is exactly the "zero presented as a measurement" the
/// tier table forbids.
/// </para>
/// </remarks>
[Flags]
public enum GpuCapabilities
{
    None = 0,
    TempCore = 1 << 0,
    TempHotspot = 1 << 1,
    TempMemory = 1 << 2,
    Load = 1 << 3,
    VramAdapter = 1 << 4,
    CoreClock = 1 << 5,
    MemClock = 1 << 6,
    Power = 1 << 7,
    Fan = 1 << 8,
    ThrottleReasons = 1 << 9,
    Pcie = 1 << 10,
}
