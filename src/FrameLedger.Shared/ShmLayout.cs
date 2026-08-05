// The C# half of the shared-memory ABI. The C++ half is
// src/native/FrameLedger.Shm/include/fl_shm.h and it is NORMATIVE: where the two
// disagree, that header is right and this file is the bug.
//
// WHY THESE ARE FIXED BUFFERS AND NOT [MarshalAs(ByValTStr)] string.
//
// The obvious idiom for a native `char[32]` is
// `[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string BuildId`,
// and this repository already uses it once (NativeAntiCheatGuard's FlGuardResult,
// which is fine there -- that struct crosses a P/Invoke boundary, where the
// marshaller runs). It would pass every offset assertion in the mirror test and
// still be wrong here, because a struct containing a `string` is NOT BLITTABLE:
// it cannot be read straight out of a MemoryMappedViewAccessor, which is the one
// thing this type exists to do. The test asserts blittability directly
// (RuntimeHelpers.IsReferenceOrContainsReferences) precisely because offsets
// cannot see the difference.
//
// Every struct is `Size = 64` to match `alignas(64)` on the native side. The
// three header regions are separate cache lines on purpose (fl_shm.h): the
// Overlay writes region 2 every frame while the Agent writes region 3 every
// second, and sharing a line would put a cross-process cache-line bounce on the
// hot path.

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace FrameLedger.Shared;

/// <summary>Constants mirrored from <c>fl_shm.h</c>. Drift here is caught by the mirror test.</summary>
public static class ShmLayout
{
    /// <summary>
    /// Bumped whenever ANY struct below changes. The Agent refuses to attach on mismatch and tells the
    /// user to restart the game — the DLL lives inside a running process, so the two sides cannot be
    /// assumed to update in lockstep.
    /// </summary>
    public const uint LayoutVersion = 2u;

    public const uint HandshakeOffset = 0x00u;
    public const uint WriterOffset = 0x40u;
    public const uint ControlOffset = 0x80u;
    public const uint RingOffset = 0xC0u;

    /// <summary>8192 × 64 B = 512 KiB, ≈16 s at 500 fps.</summary>
    public const uint DefaultCapacity = 8192u;

    /// <summary>
    /// How long a capture side keeps observing without seeing <c>guardTicks</c> advance: two missed
    /// 30 s scans, because one late tick is noise and a tick is late whenever the machine is busy.
    /// </summary>
    public const uint GuardTickDeadlineMs = 65000u;

    /// <summary>
    /// Total mapping size for a given ring capacity. Mirrors <c>FlShmSizeForCapacity</c>.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Derived from <see cref="FlFrameRecord"/> rather than from a literal 64. The literal was a
    /// fifth independent statement of the record size, and it feeds the
    /// <see cref="ShmAttachRefusal.CapacityExceedsMapping"/> bounds check — the one that stands
    /// between a hostile or stale <c>capacity</c> and raw pointer arithmetic over a mapped view.
    /// A record that grew while this stayed 64 would under-state the required size and let the
    /// refusal pass a mapping too small for the ring it describes.
    /// </para>
    /// </remarks>
    public static long SizeForCapacity(uint capacity) =>
        RingOffset + ((long)capacity * Unsafe.SizeOf<FlFrameRecord>());
}

/// <summary>Values published in <see cref="FlWriterState.Status"/>.</summary>
public enum FlStatus : uint
{
    Init = 0,
    Ready = 1,

    /// <summary>Three hook faults; see <c>17_HOOK_ENGINE</c> §Fault policy.</summary>
    SelfDisabled = 2,
    Unhooked = 3,
}

/// <summary>Bits in <see cref="FlWriterState.ApiMask"/>, and the value of <see cref="FlFrameRecord.Api"/>.</summary>
public enum FlApi : byte
{
    Unknown = 0,
    D3D11 = 1,
    D3D12 = 2,
    Vulkan = 3,
    OpenGL = 4,

    // No D3D9: those titles are almost entirely 32-bit and the Overlay is x64-only.
}

public enum FlUpscaler : byte
{
    None = 0,
    Dlss = 1,
    DlssRr = 2,
    Fsr2 = 3,
    Fsr3 = 4,
    Fsr4 = 5,
    XeSS = 6,
    Nis = 7,

    /// <summary>A hook ran and could not identify what it saw. Different from "not measured".</summary>
    Unknown = 0xFF,
}

public enum FlFgMode : byte
{
    None = 0,
    DlssG = 1,
    FsrFg = 2,
    XeFg = 3,

    /// <summary>A hook ran and could not identify what it saw. Different from "not measured".</summary>
    Unknown = 0xFF,

    // No AFMF: driver-side frame generation happens after present and is invisible to an in-process hook.
}

/// <summary>Bits in <see cref="FlFrameRecord.RtFlags"/>.</summary>
[Flags]
public enum FlRtFlags : byte
{
    None = 0,

    /// <summary>Catches inline RayQuery, which DispatchRays alone misses.</summary>
    AsBuild = 1 << 0,
    DispatchRays = 1 << 1,
    PsoAlive = 1 << 2,

    /// <summary>
    /// "We did not look." When this is set the other three carry no information and the Agent must map
    /// the frame to N/A, never to No — zero would otherwise be a MEASURED negative, and a present-only
    /// writer with no RT hooks would assert "this title does not ray-trace" ~118 times a second.
    /// </summary>
    NotMeasured = 1 << 3,
}

/// <summary>
/// Which fields a frame actually MEASURED. A bit CLEAR means "not measured": render N/A and do not
/// aggregate. The zero-defaults elsewhere in the record are affirmative negatives, and a writer that has
/// not installed the corresponding hook is not entitled to make them.
/// </summary>
[Flags]
public enum FlMeasured : byte
{
    None = 0,
    Upscaler = 1 << 0,
    Fg = 1 << 1,
    Rt = 1 << 2,
    Pso = 1 << 3,
    Vram = 1 << 4,
    Latency = 1 << 5,

    /// <summary>From the swapchain description, not a feature hook.</summary>
    OutputRes = 1 << 6,
    Hdr = 1 << 7,
}

/// <summary>Region 1 — write-once by the Overlay at init, except <see cref="AdapterLuid"/>.</summary>
[StructLayout(LayoutKind.Sequential, Size = 64)]
public unsafe struct FlShmHandshake
{
    public uint LayoutVersion;
    public uint RecordSize;
    public uint Capacity;
    public uint Pid;

    /// <summary>
    /// <c>FL_BUILD_ID</c>, from the git description of the commit the DLL was built from. NUL-terminated
    /// ASCII. Read it with <see cref="BuildIdString"/> rather than marshalling the struct.
    /// </summary>
    public fixed byte BuildId[32];

    /// <summary>Session time base, shared with sensor samples.</summary>
    public ulong QpcEpoch;

    /// <summary>
    /// 0 = NOT YET KNOWN, and it stays 0 until the first present, when the swapchain is known. The Agent
    /// must not resolve adapter-scoped telemetry against 0 — at init the Overlay is two steps before any
    /// graphics module is resolved, and a confident answer about the wrong GPU is worse than an admission.
    /// </summary>
    public ulong AdapterLuid;

    /// <summary>The build id as a string, stopping at the first NUL. Empty if the field is unset.</summary>
    public string BuildIdString()
    {
        fixed (byte* p = BuildId)
        {
            int n = 0;
            while (n < 32 && p[n] != 0)
            {
                n++;
            }

            return System.Text.Encoding.ASCII.GetString(p, n);
        }
    }
}

/// <summary>
/// Region 2 — Overlay-written; <see cref="WriteIndex"/> is touched every present. Plain integers, not
/// atomics: <c>std::atomic&lt;T&gt;</c> has no guaranteed layout and could not be mirrored here at all.
/// </summary>
[StructLayout(LayoutKind.Sequential, Size = 64)]
public unsafe struct FlWriterState
{
    /// <summary>Monotonic; release-stored by the writer. Read it with <c>Volatile.Read</c>.</summary>
    public ulong WriteIndex;

    public uint Status;
    public uint ApiMask;
    public uint FaultCount;

    /// <summary>
    /// <c>IDXGIAdapter3</c> Budget, refreshed at 1 Hz. NOTE: no producer exists yet — the Overlay
    /// installs no <c>QueryVideoMemoryInfo</c> call, so this reads 0 and 0 means "nobody wrote it".
    /// </summary>
    public uint VramBudgetMb;

    /// <summary>Must be zero; room for additive fields.</summary>
    public fixed uint Reserved[10];

    // There is deliberately NO droppedRecords here. The writer has no reader index and cannot know
    // whether the slot it overwrites was ever consumed; the Agent computes drops from its own read index.
}

/// <summary>Region 3 — Agent-written.</summary>
[StructLayout(LayoutKind.Sequential, Size = 64)]
public unsafe struct FlControlBlock
{
    public uint PauseRequested;

    /// <summary>Set when the guard fires mid-session. The capture side removes its hooks.</summary>
    public uint UnhookRequested;

    /// <summary>In-game overlay draw toggle (v1.1). No consumer yet.</summary>
    public uint OverlayEnabled;

    /// <summary>
    /// COMPLETED GUARD EVALUATIONS, not a timer, and the difference is the whole point of the field. A
    /// timer-driven tick attests that the Agent PROCESS is alive, which is not the question: the guard
    /// loop can be dead — a swallowed exception, a blocked service query, a stall on one unreadable
    /// process — while a timer keeps bumping, and the capture side would keep observing precisely
    /// because the thing supervising it had stopped. Increment at exactly one site, after a verdict
    /// returns.
    /// </summary>
    public uint GuardTicks;

    /// <summary>Must be zero.</summary>
    public fixed uint Reserved[12];
}

/// <summary>Region 4 — the ring. Exactly 64 bytes, no implicit padding.</summary>
[StructLayout(LayoutKind.Sequential, Size = 64)]
public struct FlFrameRecord
{
    /// <summary>Present entry timestamp.</summary>
    public ulong Qpc;

    public uint FrameIndex;
    public uint PresentFlags;
    public ushort SyncInterval;

    /// <summary>0 = unknown. Only meaningful when <see cref="FlMeasured.Upscaler"/> is set.</summary>
    public ushort RenderW;

    public ushort RenderH;
    public ushort OutputW;
    public ushort OutputH;
    public byte Api;
    public byte Upscaler;

    /// <summary>Vendor enum; 0xFF unknown.</summary>
    public byte UpscalerQuality;

    public byte FgMode;
    public byte RtFlags;
    public byte Hdr;

    /// <summary>
    /// Sum of W×H×D this frame — a VOLUME, not a call count, and 32-bit for a reason: one 3840×2160
    /// primary-ray dispatch is 8,294,400 rays, 126× a ushort's range.
    /// </summary>
    public uint DispatchRaysVolume;

    /// <summary>Compile COUNT, not a flag.</summary>
    public ushort PsoCreatedThisFrame;

    public byte MaxTraceRecursionDepth;

    /// <summary><see cref="FlMeasured"/> — which fields this frame actually measured.</summary>
    public byte MeasuredMask;

    public ulong VramUsedBytes;

    /// <summary>0 = unavailable.</summary>
    public uint ReflexLatencyUs;

    /// <summary>FG feature evaluations this frame. <c>F_app = presents − Σ fgEvaluations</c>.</summary>
    public uint FgEvaluations;

    /// <summary>
    /// Seqlock counter. Monotonic per slot and NEVER reset, so a full lap of the ring always changes it —
    /// otherwise a reader stalled exactly one lap would validate a different frame as unchanged. Odd
    /// means a write is in progress.
    /// </summary>
    public uint Seq;

    /// <summary>
    /// Stable per-swapchain id; 0 = unidentified, which the Agent must treat as "one undifferentiated
    /// stream" and never as a valid id. Patching a vtable slot patches the SHARED class vtable, so one
    /// hook sees every swapchain in the process and a title with a separate UI or video swapchain would
    /// otherwise inflate Displayed FPS.
    /// </summary>
    public uint SwapchainId;
}
