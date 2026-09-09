using System.Runtime.InteropServices;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>Mirror of <c>FlNvNgxState</c> (<c>fl_nvapi_bridge.h</c>): the driver's NGX words for one process.</summary>
// CA1815 is the same call FrameLedger.Shared makes for its ring mirrors: this is a window onto a native struct, not
// a value anyone compares, and an Equals over it would be ceremony that hides the layout the file exists to state.
[System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1815:Override equals and operator equals on value types", Justification = "P/Invoke mirror of a native struct; never compared.")]
[StructLayout(LayoutKind.Sequential)]
public struct NvapiNgxWords
{
    public const int Answered = 0;

    public const int Unanswered = 1;

    public const int Degraded = 2;

    public uint Size;

    public int Status;

    public int NvapiStatus;

    public uint Driver;

    public ulong Sr;

    public ulong Rr;

    public ulong Fg;

    public float ScalingRatio;

    public uint PerformanceMode;

    public uint RenderPreset;

    public uint FrameGenerationCount;

    public uint FrameGenerationPreset;

    public uint FrameGenerationMode;

    public uint Reserved0;

    public uint Reserved1;
}
