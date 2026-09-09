using System.Runtime.InteropServices;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// Mirror of <c>FlNvSample</c> (<c>fl_nvapi_bridge.h</c>). <see cref="Size"/> is set by the caller and checked
/// by the bridge, so a drift between the two is refused (<c>FL_NV_BAD_SIZE</c>) rather than read; <c>NvapiBridgeLayoutTests</c>
/// compares <c>Marshal.SizeOf</c> against <c>FlNvSampleSize()</c> as well.
/// </summary>
// CA1815 is the same call FrameLedger.Shared makes for its ring mirrors: this is a window onto a native struct, not
// a value anyone compares, and an Equals over it would be ceremony that hides the layout the file exists to state.
[System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1815:Override equals and operator equals on value types", Justification = "P/Invoke mirror of a native struct; never compared.")]
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct NvapiSample
{
    public uint Size;

    public uint Present;

    public float TempCoreC;

    public float TempMemoryC;

    public float LoadPct;

    public float VramUsedMb;

    public float CoreClockMhz;

    public float MemClockMhz;

    public float FanRpm;

    public uint ThrottleReasons;

    public uint PcieWidth;

    public uint Reserved0;

    public uint Reserved1;

    public uint Reserved2;

    public uint Reserved3;

    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string Name;

    public readonly bool Has(NvapiField field) => (Present & (uint)field) != 0;
}
