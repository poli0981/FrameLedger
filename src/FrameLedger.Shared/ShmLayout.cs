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
    public const uint LayoutVersion = 3u;

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

/// <summary>
/// The technology actually executing. <b>Zero is "nobody said", not a fact</b> — layout v3's central
/// rule. A record is value-initialised, so whatever 0 means is what a writer publishes when it forgets;
/// until v3 that was <c>None</c>, a measured negative about a title nobody examined.
/// </summary>
/// <remarks>
/// Three distinct states, and all three are needed: <see cref="NotReported"/> (no hook was live — N/A),
/// <see cref="Unknown"/> (a hook ran and could not identify what it saw — also N/A, but it means our
/// coverage is short rather than that the question did not apply), and <see cref="None"/> (a hook ran
/// and there genuinely was no upscaler — the only one that may be aggregated as a negative).
/// </remarks>
public enum FlUpscaler : byte
{
    NotReported = 0,
    Dlss = 1,

    /// <summary>
    /// Retired in v3 and reserved rather than reused. Ray Reconstruction was a value here, which made
    /// it mutually exclusive with DLSS super-resolution — but they run together, and RR is an
    /// independent tri-state axis. It is now <see cref="FlFeatureFlags.RayReconstruction"/>.
    /// </summary>
    RetiredRayReconstruction = 2,

    Fsr2 = 3,
    Fsr3 = 4,
    Fsr4 = 5,
    XeSS = 6,
    Nis = 7,

    /// <summary>A hook ran and there was no upscaler. Moved from 0 in v3.</summary>
    None = 8,

    /// <summary>
    /// FSR, measured through the SDK 2.x upscaler DLL, version not named: that one module hosts
    /// FSR 3.1 and FSR 4 behind the same dispatch type, and the dispatch does not say which. The
    /// SDK 1.1.x monolith still decodes to <see cref="Fsr3"/>; <see cref="Fsr4"/> is reserved for a
    /// writer that can actually tell. An enumerator, not a field — no layout change.
    /// </summary>
    FsrUnversioned = 9,

    /// <summary>A hook ran and could not identify what it saw. Different from "not measured".</summary>
    Unknown = 0xFF,
}

public enum FlFgMode : byte
{
    NotReported = 0,
    DlssG = 1,
    FsrFg = 2,
    XeFg = 3,

    /// <summary>A hook ran and there was no frame generation. Moved from 0 in v3.</summary>
    None = 4,

    /// <summary>A hook ran and could not identify what it saw. Different from "not measured".</summary>
    Unknown = 0xFF,

    // No AFMF: driver-side frame generation happens after present and is invisible to an in-process hook.
}

/// <summary>
/// <see cref="FlFrameRecord.ColorSpace"/>. Was <c>hdr</c>, a bool — which had no third state.
/// </summary>
/// <remarks>
/// The only producer is a hook on <c>IDXGISwapChain3::SetColorSpace1</c>, and an SDR title never calls
/// it. So the writer initialises this to <see cref="Sdr"/> at swapchain identification rather than
/// leaving it at 0: DXGI documents G22/Rec.709 as the default for a swapchain nobody has called
/// SetColorSpace1 on, which makes SDR a measured default and not an affirmative negative. Without that,
/// HDR's definite "No" would be unreachable.
/// </remarks>
public enum FlColorSpace : byte
{
    NotReported = 0,
    Sdr = 1,
    Hdr10 = 2,
    ScRgb = 3,
}

/// <summary>
/// Bits in <see cref="FlFrameRecord.FeatureFlags"/> — per-frame boolean facts, each paired with an
/// OBSERVED companion four bits up.
/// </summary>
/// <remarks>
/// Self-describing on purpose. Per-frame bytes are persisted as their own blobs, so a byte whose "did
/// we look" lived in a different series could not express "not measured" after persistence without a
/// join. It also stops a specific over-claim: Ray Reconstruction is produced only by NGX/Streamline
/// while <see cref="FlMeasured.Upscaler"/> also covers FFX, XeSS and NIS, so a writer with FFX hooks
/// and no NGX hooks knows nothing about RR and must not publish "RR = No".
/// </remarks>
[Flags]
public enum FlFeatureFlags : byte
{
    None = 0,
    RayReconstruction = 1 << 0,
    ReflexEnabled = 1 << 1,

    /// <summary>
    /// A Streamline feature id the Overlay does not decode was evaluated this frame.
    /// </summary>
    /// <remarks>
    /// The only way to tell apart two sessions that otherwise look identical. Once the
    /// frame-generation count leaves the feature bitmask, a present carrying
    /// <c>kFeatureDLSS_G</c> alongside any id that falls to <c>FL_SL_SEEN_OTHER</c> — Reflex,
    /// PCL, DeepDVC, Latewarp, DirectSR, or a feature a newer Streamline adds — has
    /// <c>fgEvaluations &gt; 0</c> and is indistinguishable from a present that carried
    /// DLSS-G alone. Without it the "an id we do not decode" bucket reads ZERO on a title
    /// evaluating one every application frame, and a decision table keyed on that bucket
    /// would settle §S30 on a number that could not have been anything else.
    /// </remarks>
    SlUndecoded = 1 << 2,

    /// <summary>
    /// A super-resolution id — <c>kFeatureDLSS</c> or <c>kFeatureNIS</c> — was evaluated.
    /// </summary>
    /// <remarks>
    /// RAW, and that is the point: which id ARRIVED, independently of what the decode made of
    /// it. Added 2026-08-15 because the census had been contaminated by the thing it measures.
    /// Cyberpunk with Ray Reconstruction on evaluates <c>kFeatureDLSS_RR</c> and never
    /// <c>kFeatureDLSS</c>; correcting the decode to report DLSS for it — correctly, since RR
    /// performs the upscale — made a census derived from the decoded byte report 2,569 arrivals
    /// of an id that arrived zero times. A measurement that moves when the decode moves cannot
    /// be evidence about the decode.
    /// </remarks>
    SlSuperResolution = 1 << 3,

    RayReconstructionObserved = 1 << 4,
    ReflexObserved = 1 << 5,
    SlUndecodedObserved = 1 << 6,
}

/// <summary>Which hook families a writer installed. Bits in <see cref="FlWriterState.HooksInstalledMask"/>.</summary>
/// <remarks>
/// Monotonic — bits are only ever set. Ray tracing's definite "No" requires the AS-build hook to have
/// been <i>installed</i>, not merely for RT to have been "measured": a writer with only the DispatchRays
/// hook sees nothing on an inline-RayQuery title, which is exactly the case AS-build exists to catch.
/// </remarks>
[Flags]
public enum FlHookFamily : uint
{
    None = 0,
    Present = 1u << 0,
    UpscalerIdentity = 1u << 1,
    UpscalerParams = 1u << 2,
    FgEvaluations = 1u << 3,
    RtDispatch = 1u << 4,
    RtAsBuild = 1u << 5,
    RtPso = 1u << 6,
    Pso = 1u << 7,
    ColorSpace = 1u << 8,
    Reflex = 1u << 9,
    Vram = 1u << 10,
}

/// <summary>
/// Which vendor RUNTIME MODULES the loader reported in the game process. Bits in
/// <see cref="FlWriterState.RuntimeCensus"/>.
/// </summary>
/// <remarks>
/// <para>
/// <b>Not a hook and not a measurement.</b> Taken on the Overlay's watchdog once a second by
/// asking the loader for each name in <c>FL_RUNTIME_CENSUS</c>; OR-only, so monotonic. A set bit
/// says a module of that name was loaded. A clear bit says the loader had none of that name —
/// and cannot see a statically linked FSR, whose frame-generation proxy still calls the real
/// <c>Present</c>. <b>It therefore never produces <see cref="FlFgMode.None"/> or
/// <see cref="FlUpscaler.None"/></b>; it refines the reason for an N/A and warns when a
/// frame-generation runtime is present and nothing was observed
/// (<c>03_METRICS</c> §Frame Generation, rung 0's qualifier).
/// </para>
/// <para>
/// <see cref="Ran"/> is bit 0 so a writer that never took the census publishes 0 and decodes as
/// "nobody looked". A family bit without it is a writer defect.
/// </para>
/// </remarks>
[Flags]
public enum FlRuntimeCensus : uint
{
    None = 0,
    Ran = 1u << 0,

    SlDlssG = 1u << 1,
    NvngxDlssG = 1u << 2,
    LibXessFg = 1u << 3,
    FfxFrameInterpolation = 1u << 4,
    FfxFsr3 = 1u << 5,
    AmdFfxFrameGeneration = 1u << 6,

    SlInterposer = 1u << 8,
    SlDlss = 1u << 9,
    SlNis = 1u << 10,
    NvngxCore = 1u << 11,
    NvngxDlss = 1u << 12,
    NvngxDlssD = 1u << 13,
    LibXess = 1u << 14,
    FfxFsr2 = 1u << 15,
    FfxFsr3Upscaler = 1u << 16,
    AmdFfxUpscaler = 1u << 17,
    AmdFfxDx12 = 1u << 18,
}

/// <summary>
/// Mirror of <c>FlSlTagType</c>: the Streamline buffer types the tag detours and the local inputs walk record, as
/// bits within one route's field of <see cref="FlWriterState.SlTagCensus"/>.
/// </summary>
/// <remarks>
/// <b>The identity half of frame generation, from an argument already hooked.</b> Streamline's DLSS-G programming
/// guide §5.0 requires a title running DLSS Frame Generation to tag the HUD-less colour and UI buffers every frame
/// through <c>slSetTag</c> / <c>slSetTagForFrame</c> / <c>slEvaluateFeature</c>'s inputs; a title running
/// super-resolution alone never tags them. So <see cref="DlssgInputs"/> tagged says the title is FEEDING frame
/// generation — identity, never a count: whether frames were generated stays the count's verdict.
/// </remarks>
[Flags]
public enum FlSlTagType : uint
{
    None = 0,
    Depth = 1u << 0,
    MotionVectors = 1u << 1,
    Hudless = 1u << 2,
    ScalingInput = 1u << 3,
    ScalingOutput = 1u << 4,
    UiColorAlpha = 1u << 5,
    UiAlpha = 1u << 6,
    Backbuffer = 1u << 7,
    Other = 1u << 8,

    /// <summary>Mirror of <c>FL_SL_TAG_DLSSG_INPUTS</c>: the buffers only DLSS Frame Generation consumes.</summary>
    DlssgInputs = Hudless | UiColorAlpha | UiAlpha,
}

/// <summary>Mirror of the <c>FL_SL_TAG_ROUTE_*</c> shifts and the per-route width.</summary>
public static class FlSlTagRoute
{
    public const int TypeBits = 9;
    public const uint TypeMask = (1u << TypeBits) - 1u;
    public const int Global = 0;
    public const int Frame = TypeBits;
    public const int Local = 2 * TypeBits;

    /// <summary>The <see cref="FlSlTagType"/> bits one route contributed to a census word.</summary>
    public static FlSlTagType Of(uint census, int route) => (FlSlTagType)((census >> route) & TypeMask);

    /// <summary>The union of every route's bits.</summary>
    public static FlSlTagType Any(uint census) => Of(census, Global) | Of(census, Frame) | Of(census, Local);
}

/// <summary>The two groups of <see cref="FlRuntimeCensus"/>, mirroring <c>FL_CENSUS_FG_FAMILIES</c> and <c>FL_CENSUS_UPSCALER_FAMILIES</c>.</summary>
public static class FlRuntimeCensusFamilies
{
    /// <summary>
    /// Includes <see cref="FlRuntimeCensus.AmdFfxDx12"/>: the FidelityFX 3.1 facade dispatches upscaling AND frame
    /// generation, and Lies of P ships it alone while generating frames. A module that MAY generate frames is
    /// grouped with the ones that do, so its presence warns rather than reassures.
    /// </summary>
    public const FlRuntimeCensus Fg = FlRuntimeCensus.SlDlssG | FlRuntimeCensus.NvngxDlssG | FlRuntimeCensus.LibXessFg
        | FlRuntimeCensus.FfxFrameInterpolation | FlRuntimeCensus.FfxFsr3 | FlRuntimeCensus.AmdFfxFrameGeneration
        | FlRuntimeCensus.AmdFfxDx12;

    public const FlRuntimeCensus Upscaler = FlRuntimeCensus.SlInterposer | FlRuntimeCensus.SlDlss | FlRuntimeCensus.SlNis
        | FlRuntimeCensus.NvngxCore | FlRuntimeCensus.NvngxDlss | FlRuntimeCensus.NvngxDlssD | FlRuntimeCensus.LibXess
        | FlRuntimeCensus.FfxFsr2 | FlRuntimeCensus.FfxFsr3Upscaler | FlRuntimeCensus.AmdFfxUpscaler;
}

/// <summary>Bits in <see cref="FlFrameRecord.RtFlags"/>.</summary>
[Flags]
public enum FlRtFlags : byte
{
    None = 0,

    /// <summary>Catches inline RayQuery, which DispatchRays alone misses.</summary>
    AsBuildObserved = 1 << 0,
    DispatchObserved = 1 << 1,

    /// <summary>
    /// Renamed from <c>PsoAlive</c> in v3, because that claimed a present-tense fact the hook set
    /// cannot retract: creation is observed at <c>CreateStateObject</c>, destruction is COM Release,
    /// which is not in the hook inventory and must not be added. The bit latches on, so it says
    /// "created ever" and nothing about what is alive now.
    /// </summary>
    PsoCreatedEver = 1 << 2,

    // v3 flipped the polarity: every bit means "we OBSERVED this", so 0 says "no RT evidence seen" and
    // FlMeasured.Rt is what says whether anyone looked. The old opt-in NotMeasured bit is retired —
    // with the flip it would be a second statement of what the mask already says.
}

/// <summary>
/// Values for <see cref="FlWriterState.RtTier"/>. Not a flags enum: the tier is
/// <c>D3D12_RAYTRACING_TIER</c>'s own value, which is already "tier ×10" — measured against the
/// Windows SDK header, <c>NOT_SUPPORTED = 0</c>, <c>TIER_1_0 = 10</c>, <c>TIER_1_1 = 11</c>,
/// <c>TIER_1_2 = 12</c>. Nothing here names the individual tiers, so a tier newer than any build
/// still arrives intact.
/// </summary>
/// <remarks>
/// <para>
/// <c>D3D12_RAYTRACING_TIER_NOT_SUPPORTED</c> is 0 and 0 already meant NOT QUERIED, so storing the
/// vendor enum verbatim would have published "nobody looked" about every non-RT device — the
/// affirmative-negative collision layout v3 exists to prevent, reached by copying an enum. The
/// writer substitutes <see cref="Unsupported"/> for that one value.
/// </para>
/// </remarks>
public enum FlRtTier : uint
{
    /// <summary>No D3D12 device was identified, or the capability query failed.</summary>
    NotQueried = 0,

    /// <summary>D3D12 answered NOT_SUPPORTED. A measurement, not a silence.</summary>
    Unsupported = 1,

    /// <summary>
    /// The threshold <c>03_METRICS</c> §RT/PT/RR states for "an RT-capable device". Named so the
    /// consumer stops spelling it as a literal 10 beside a field whose units its type does not carry.
    /// </summary>
    CapableMin = 10,
}

/// <summary>
/// Which fields a frame actually MEASURED. A bit CLEAR means "not measured": render N/A and do not
/// aggregate. The zero-defaults elsewhere in the record are affirmative negatives, and a writer that has
/// not installed the corresponding hook is not entitled to make them.
/// </summary>
[Flags]
public enum FlMeasured : ushort
{
    None = 0,

    /// <summary>Upscaler IDENTITY only — the parameters are <see cref="UpscalerParams"/>.</summary>
    Upscaler = 1 << 0,

    /// <summary>FG IDENTITY only — the per-present counts are <see cref="FgCounts"/>.</summary>
    Fg = 1 << 1,
    Rt = 1 << 2,
    Pso = 1 << 3,
    Vram = 1 << 4,
    Latency = 1 << 5,

    /// <summary>From the swapchain description, not a feature hook.</summary>
    OutputRes = 1 << 6,
    Hdr = 1 << 7,

    /// <summary>
    /// <c>upscalerQuality</c> + <c>upscalerSharpness</c> + <c>renderW/H</c>. Split from
    /// <see cref="Upscaler"/> in v3: a Streamline-shimmed title exposes the NGX parameter accessors as
    /// exports and yields all four, while an NGX-direct title exports only the parameter-object
    /// factories — so a writer hooking CreateFeature knows <i>which</i> upscaler ran and nothing about
    /// quality, sharpness or render size. One bit for both published quality 0 ("DLSS Performance") as
    /// a measurement.
    /// </summary>
    UpscalerParams = 1 << 8,

    /// <summary>
    /// <c>syncInterval</c> + <c>presentFlags</c>. Two of the four planned present writers have no such
    /// arguments — <c>wglSwapBuffers</c> and <c>vkQueuePresentKHR</c> take neither — and would have
    /// published "vsync off, no flags" as measurement. <c>syncInterval</c> is the worse of the two:
    /// 0 is a real DXGI value, so no in-band sentinel exists and only a mask bit can carry it.
    /// </summary>
    PresentArgs = 1 << 9,

    /// <summary>
    /// <c>fgEvaluations</c>, split from <see cref="Fg"/> in v3. Identity and per-present counts are two
    /// hook rows. With this clear the Agent must treat <c>F_app</c> as a data gap — never as equal to
    /// <c>F_disp</c>, which would be <c>fg_factor 1.0</c> reached by a writer that counted nothing.
    /// </summary>
    FgCounts = 1 << 10,

    // Bits 11-15 reserved. Writers leave them zero; readers IGNORE them rather than validating them as
    // zero. That does NOT make a future field bump-free — recordSize and layoutVersion are compared
    // first, so an old reader refuses a new writer outright.
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

    /// <summary>
    /// Device ray-tracing tier, encoded as <see cref="FlRtTier"/>. Without it, 03_METRICS' definite
    /// RT "No" — "RT-capable device present, no AS builds and no dispatches for the whole session" —
    /// has no producer, so RT could reach Yes or N/A and never No. <b>Three states, not two:</b> a
    /// device that answered NOT_SUPPORTED is <see cref="FlRtTier.Unsupported"/>, never 0, because 0
    /// is reserved for "nobody looked".
    /// </summary>
    public uint RtTier;

    /// <summary><see cref="FlHookFamily"/> bits. Monotonic: set, never cleared.</summary>
    public uint HooksInstalledMask;

    /// <summary>03_METRICS' <c>rt_pso_count</c>, a session figure.</summary>
    public uint RtStateObjectsCreated;

    /// <summary>The raster denominator <c>pt_confidence</c> wanted.</summary>
    public uint RasterPsoCreated;

    /// <summary><see cref="FlRuntimeCensus"/> bits. Watchdog-published, OR-only. Took <c>reserved[0]</c> on 2026-09-03.</summary>
    public uint RuntimeCensus;

    /// <summary>
    /// <see cref="FlSlTagType"/> bits per route (<see cref="FlSlTagRoute"/>): which Streamline buffer types the
    /// title tagged, on which of the three tag routes. Watchdog-published, OR-only. Took <c>reserved[0]</c> on
    /// 2026-09-05 the way <see cref="RuntimeCensus"/> took it — additive, no layout bump.
    /// </summary>
    public uint SlTagCensus;

    /// <summary>Must be zero; room for additive fields.</summary>
    public fixed uint Reserved[4];

    // WHY THE COUNTERS ARE PUBLISHED AT 1 Hz AND NOT ACCUMULATED HERE PER FRAME: this struct is
    // region 2, which the Overlay writes on the present path, and the regions are separate cache lines
    // precisely so a cross-process write does not bounce that line. The hook keeps process-local
    // counters and the watchdog publishes them. NO CONSISTENCY GUARANTEE between these and any given
    // record — a rule needing them to agree with one frame cannot be stated per frame.

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

    /// <summary><see cref="FlColorSpace"/>. Was <c>Hdr</c>, a bool with no third state.</summary>
    public byte ColorSpace;

    /// <summary>
    /// Sum of W×H×D this frame — a VOLUME, not a call count, and 32-bit for a reason: one 3840×2160
    /// primary-ray dispatch is 8,294,400 rays, 126× a ushort's range.
    /// </summary>
    public uint DispatchRaysVolume;

    /// <summary>Compile COUNT, not a flag.</summary>
    public ushort PsoCreatedThisFrame;

    public byte MaxTraceRecursionDepth;

    /// <summary><see cref="FlFeatureFlags"/> — per-frame facts with their own OBSERVED bits.</summary>
    public byte FeatureFlags;

    /// <summary><see cref="FlMeasured"/> — which fields this frame actually measured. 16-bit since v3.</summary>
    public ushort MeasuredMask;

    /// <summary>
    /// Percent, 0-100. <c>0xFF</c> means a hook ran and this upscaler's API reports no sharpness —
    /// DLSS 3.x removed the parameter and XeSS exposes none — which is NOT the same as no hook running
    /// (<see cref="FlMeasured.UpscalerParams"/> clear).
    /// </summary>
    public byte UpscalerSharpness;

    /// <summary>
    /// FG feature evaluations drained by this present. <c>F_app = Σ fgEvaluations</c> and
    /// <c>fg_factor = presents / Σ</c>.
    /// </summary>
    /// <remarks>
    /// <b>This said <c>F_app = presents − Σ</c> and "3 at ×4" until the producer was written, and
    /// both halves were wrong.</b> The subtraction needs the count to be of GENERATED frames, and
    /// nothing can produce that in policy: <c>slEvaluateFeature(kFeatureDLSS_G)</c> fires once per
    /// APPLICATION frame and yields N−1 generated ones, where N lives in <c>sl::DLSSGOptions</c> —
    /// set out of band through the route <c>HANDOFF</c> §2b refused on five grounds. Owner ruling
    /// 2026-08-14: count the evaluations themselves, which needs no multiplier and no vendor
    /// header. So the value is 1 per application frame at every multiplier, not 3 at ×4, and the
    /// two forms differ by a factor of four on the one real title measured.
    /// <para>
    /// A byte, saturating at 255 rather than wrapping: a wrapped count reads LOW and is the
    /// DENOMINATOR here, so it would inflate the factor without bound. No configuration evaluates
    /// frame generation 255 times between two presents, so a consumer seeing 255 must refuse to
    /// publish a factor rather than divide by a floor.
    /// </para>
    /// </remarks>
    /// <remarks>
    /// <b>Producer changed 2026-09-03, name kept:</b> the byte counts APPLICATION-FRAME TOKENS —
    /// distinct <c>sl::FrameToken</c> objects the title obtained through <c>slGetNewFrameToken</c>
    /// since the previous present — which is the quantity <c>03_METRICS</c> always defined it as.
    /// The <c>kFeatureDLSS_G</c> evaluation count it used to carry read zero on five real titles.
    /// </remarks>
    public byte FgEvaluations;

    /// <summary>
    /// Per-process VRAM in MiB. Narrowed from <c>ulong</c> bytes in v3 — 03_METRICS exports
    /// <c>vram_mb</c>, 06_DATA_MODEL stores MiB, the value is a 1 Hz held sample, and
    /// <c>budget_exceeded_pct</c> compares it against <see cref="FlWriterState.VramBudgetMb"/>, which
    /// was already MiB. Must use the same truncating divisor, or that comparison gains a bias.
    /// </summary>
    public uint VramUsedMb;

    /// <summary>0 = unavailable.</summary>
    public uint ReflexLatencyUs;

    /// <summary>Must be zero. Slack, so the next addition does not start from none.</summary>
    public uint Reserved;

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
