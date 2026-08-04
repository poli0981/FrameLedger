// FrameLedger shared-memory layout.
//
// NORMATIVE. This header and docs/07_IPC.md must agree; the static_asserts
// below are what enforce it. Mirrored in C# by FrameLedger.Shared, and a test
// in FrameLedger.Infrastructure.Tests compares both sides against the offsets
// emitted by tools/fl-layout-dump (CLAUDE.md §Struct mirroring).
//
// Header-only by design: the Overlay writes these structs from inside a game
// process and the Agent reads them from outside, so there must be exactly one
// definition and no link-time dependency between the two.
//
// Layout (docs/07_IPC.md §A + B):
//
//   [0x0000] FlShmHandshake  64 B  write-once by the Overlay at init
//   [0x0040] FlWriterState   64 B  Overlay-written (writeIndex every present)
//   [0x0080] FlControlBlock  64 B  Agent-written
//   [0x00C0] FlFrameRecord[capacity]
//
// The three header regions are separate cache lines on purpose. The Overlay
// writes region 2 every frame while the Agent writes region 3 every second;
// sharing a line would put a cross-process cache-line bounce on the hot path.
//
// A previous revision of the design declared a single 88-byte header while
// mapping the control block to 0x0040. In code, unhookRequested would have
// aliased faultCount — the safety stop would fire on any hook fault, and the
// Agent's heartbeat would clobber the fault counter. Hence the asserts.

#ifndef FRAMELEDGER_FL_SHM_H
#define FRAMELEDGER_FL_SHM_H

// <atomic> is included on purpose even though no declaration below needs it:
// every field marked "via std::atomic_ref" in this header is unusable without
// it, and a consumer following the documented protocol would otherwise get
// "'atomic_ref': is not a member of 'std'" from their own file. The contract
// this header defines should carry its own prerequisites.
#include <atomic>
#include <cstddef>
#include <cstdint>

// Bump whenever ANY struct below changes. The Agent refuses to attach on
// mismatch and tells the user to restart the game — the DLL lives inside a
// running process, so the two sides cannot be assumed to update in lockstep.
#define FL_SHM_LAYOUT_VERSION 2u

#define FL_SHM_HANDSHAKE_OFFSET 0x00u
#define FL_SHM_WRITER_OFFSET 0x40u
#define FL_SHM_CONTROL_OFFSET 0x80u
#define FL_SHM_RING_OFFSET 0xC0u

#define FL_SHM_DEFAULT_CAPACITY 8192u    // 8192 * 64 B = 512 KiB, ~16 s at 500 fps

// How long a capture side keeps observing without seeing guardTicks advance.
//
// 07_IPC §Supervision loss depended on this number and NEVER STATED IT — one
// grep over docs, src and tools returned exactly one line, the sentence that
// needs it. A rule with no value is not implementable, and the sentence had been
// read as delivered.
//
// 65 SECONDS = TWO MISSED SCANS. The guard re-scan runs every 30 s (19_SAFETY
// §During a session), so ~35 s would stop a session on a SINGLE late tick — and
// a tick is late whenever the machine is busy, which during a benchmark is
// always. Two consecutive misses is a real signal; one is noise.
//
// The cost is stated rather than hidden: it doubles the worst-case window in
// which an unsupervised hooked process keeps observing, from the 30 s the
// Disclaimer discloses to 65 s. legal/DISCLAIMER.md §2 says so in those words.
//
// NOT DRIVEN BY THE PRESENT HOOK. §S2 rejected that: the clock would stop when
// presents stop, which is the exact scenario this exists for — a game that has
// hung, or been alt-tabbed, while anti-cheat loads behind it.
#define FL_GUARD_TICK_DEADLINE_MS 65000u

namespace fl {

// status values published in FlWriterState::status
enum FlStatus : uint32_t {
    FL_STATUS_INIT = 0,
    FL_STATUS_READY = 1,
    FL_STATUS_SELF_DISABLED = 2,    // 3 hook faults; see 17_HOOK_ENGINE §Fault policy
    FL_STATUS_UNHOOKED = 3,
};

// bits in FlWriterState::apiMask and FlFrameRecord::api
enum FlApi : uint8_t {
    FL_API_UNKNOWN = 0,
    FL_API_D3D11 = 1,
    FL_API_D3D12 = 2,
    FL_API_VULKAN = 3,
    FL_API_OPENGL = 4,
    // No D3D9: those titles are almost entirely 32-bit and the Overlay is
    // x64-only, so they are Tier 2 in v1 (docs/20_OPEN_QUESTIONS.md §Scope).
};

enum FlUpscaler : uint8_t {
    FL_UPSCALER_NONE = 0,
    FL_UPSCALER_DLSS = 1,
    FL_UPSCALER_DLSS_RR = 2,
    FL_UPSCALER_FSR2 = 3,
    FL_UPSCALER_FSR3 = 4,
    FL_UPSCALER_FSR4 = 5,
    FL_UPSCALER_XESS = 6,
    FL_UPSCALER_NIS = 7,
    FL_UPSCALER_UNKNOWN = 0xFF,
};

enum FlFgMode : uint8_t {
    FL_FG_NONE = 0,
    FL_FG_DLSS_G = 1,
    FL_FG_FSR_FG = 2,
    FL_FG_XEFG = 3,
    FL_FG_UNKNOWN = 0xFF,
    // No AFMF: driver-side frame generation happens after present and is
    // invisible to an in-process hook (docs/03_METRICS.md §Frame Generation).
};

// bits in FlFrameRecord::rtFlags
enum FlRtFlags : uint8_t {
    FL_RT_AS_BUILD = 1u << 0,    // catches inline RayQuery, which DispatchRays misses
    FL_RT_DISPATCH_RAYS = 1u << 1,
    FL_RT_PSO_ALIVE = 1u << 2,

    // "We did not look", which rtFlags = 0 could not previously express.
    //
    // Zero means "no RT activity this frame" — a MEASURED negative — and
    // 03_METRICS turns a whole session of that into a definite `No`. A present
    // hook with no RT hooks installed writes zero every frame, so the first
    // Overlay would have asserted "this title does not ray-trace" 118 times a
    // second, about a title nobody asked. CLAUDE.md rule 7 forbids exactly that
    // fabrication in the other direction and the same reasoning applies here.
    //
    // When this bit is set the other three carry no information and the Agent
    // must map the frame to N/A, never to No.
    FL_RT_NOT_MEASURED = 1u << 3,
};

// bits in FlFrameRecord::measuredMask — which fields were MEASURED this frame.
//
// The zero-defaults elsewhere in the record are affirmative negatives:
// FL_UPSCALER_NONE, FL_FG_NONE and rtFlags = 0 all mean "we looked and there was
// none". A writer that has not installed the corresponding feature hook is not
// entitled to say that, and before this mask it had no way to say anything else
// — it would have produced `fg_factor 1.0` and `upscaler none` as measured facts
// on the exact title chosen to prove ADR-7.
//
// A bit CLEAR means "not measured": the Agent must render N/A and must not
// aggregate the field. The UNKNOWN sentinels (FL_UPSCALER_UNKNOWN, FL_FG_UNKNOWN)
// stay as the in-band form for a hook that ran and could not identify what it
// saw; this mask is for a hook that never ran at all. They are different states
// and 03_METRICS treats them differently.
enum FlMeasured : uint8_t {
    FL_MEASURED_UPSCALER = 1u << 0,      // upscaler + upscalerQuality + renderW/H
    FL_MEASURED_FG = 1u << 1,            // fgMode + fgEvaluations
    FL_MEASURED_RT = 1u << 2,            // rtFlags + dispatchRaysVolume + maxTraceRecursionDepth
    FL_MEASURED_PSO = 1u << 3,           // psoCreatedThisFrame
    FL_MEASURED_VRAM = 1u << 4,          // vramUsedBytes
    FL_MEASURED_LATENCY = 1u << 5,       // reflexLatencyUs
    FL_MEASURED_OUTPUT_RES = 1u << 6,    // outputW/H — from the swapchain desc, not a feature hook
};

// ---------------------------------------------------------------------------
// Region 1 — write-once at init by the Overlay.
// ---------------------------------------------------------------------------
struct alignas(64) FlShmHandshake {
    uint32_t layoutVersion;    // @0   must equal FL_SHM_LAYOUT_VERSION on both sides
    uint32_t recordSize;       // @4   64; belt-and-braces against struct drift
    uint32_t capacity;         // @8   power of two
    uint32_t pid;              // @12

    // @16 — FL_BUILD_ID, generated by CMake from the git description of the
    // commit the DLL was built from. It had NO PRODUCER: three references in the
    // whole tree, all of them the declaration, its offset assert and the layout
    // dump — while 07_IPC and 04_CAPTURE make a mismatch a hard refuse-to-attach
    // and 17_HOOK_ENGINE requires an FlGetBuildId() export the Overlay does not
    // have. A check whose input nobody writes compares "" with "" forever.
    char buildId[32];    // @16  see FL_BUILD_ID in src/native/CMakeLists.txt

    uint64_t qpcEpoch;    // @48  session time base, shared with sensor samples

    // @56 — WHICH GPU, and it is NOT known at init.
    //
    // This region is documented as "write-once by the Overlay at init", and
    // 17_HOOK_ENGINE writes the handshake at InitThread step 1 — two steps
    // BEFORE step 3 resolves which graphics modules exist. Our own throwaway
    // dummy device's adapter is not the game's, so anything written there would
    // have been a confident answer about the wrong GPU, and 0 is a valid-looking
    // LUID rather than an admission.
    //
    // Decided: this one field is published at FIRST PRESENT, when the swapchain
    // is known, and **0 means NOT YET KNOWN**. The Agent must not resolve
    // adapter-scoped telemetry against 0. Everything else in this region is
    // still write-once at init; the exception is here rather than in prose
    // because a reader of the struct is who needs it.
    uint64_t adapterLuid;    // @56  0 = not yet known; set at first present
};

// ---------------------------------------------------------------------------
// Region 2 — Overlay-written. writeIndex is touched every present.
//
// Fields shared across the process boundary are plain integers accessed
// through std::atomic_ref, NOT std::atomic<T> members: std::atomic<T> has no
// guaranteed layout, cannot be memcpy'd, and cannot be mirrored by a C#
// [StructLayout(LayoutKind.Sequential)] struct. Every offset below satisfies
// atomic_ref's alignment requirement (4 for uint32_t, 8 for uint64_t).
// ---------------------------------------------------------------------------
struct alignas(64) FlWriterState {
    uint64_t writeIndex;      // @0   monotonic; release-store
    uint32_t status;          // @8   FlStatus
    uint32_t apiMask;         // @12  which graphics APIs got hooked
    uint32_t faultCount;      // @16  17_HOOK_ENGINE §Fault policy
    uint32_t vramBudgetMb;    // @20  IDXGIAdapter3 Budget, refreshed at 1 Hz
    uint32_t reserved[10];    // @24..63  must be zero; room for additive fields

    // NOTE: there is deliberately no droppedRecords here. The writer has no
    // reader index and cannot know whether the slot it overwrites was ever
    // consumed — the field was unimplementable as originally specified. The
    // Agent computes drops from its own read index (docs/07_IPC.md).
};

// ---------------------------------------------------------------------------
// Region 3 — Agent-written.
// ---------------------------------------------------------------------------
// `guardTicks` is NOT a liveness heartbeat, and the difference is the whole
// point of the field.
//
// It was specified as "Agent bumps every second". A timer-driven tick attests
// that the Agent's PROCESS is alive, which is not the question anyone is
// asking: the guard loop can be dead — a swallowed exception, or blocked in a
// service query, or stalled on one unreadable process in the §S16 scan set —
// while a timer keeps bumping. A consumer reading "supervised" would then keep
// observing precisely because the thing supervising it had stopped. That is a
// gate incapable of going red for the reason it exists, which is the defect
// this project has now found eight times.
//
// So it counts COMPLETED GUARD EVALUATIONS for this ring. The Agent increments
// it at exactly one site: after `Evaluate` returns a verdict. A refusal still
// counts — the evaluation completed — but it also sets `unhookRequested`, so a
// consumer never has to infer the verdict from the counter.
struct alignas(64) FlControlBlock {
    uint32_t pauseRequested;     // @0
    uint32_t unhookRequested;    // @4   set when the guard fires mid-session
    uint32_t overlayEnabled;     // @8   in-game overlay draw toggle (v1.1)
    uint32_t guardTicks;         // @12  completed guard evaluations, NOT a timer
    uint32_t reserved[12];       // @16..63  must be zero
};

// ---------------------------------------------------------------------------
// Region 4 — the ring. 64 bytes exactly, no implicit padding.
// ---------------------------------------------------------------------------
struct alignas(64) FlFrameRecord {
    uint64_t qpc;                // @0   present entry timestamp
    uint32_t frameIndex;         // @8
    uint32_t presentFlags;       // @12
    uint16_t syncInterval;       // @16
    uint16_t renderW;            // @18  0 = unknown
    uint16_t renderH;            // @20
    uint16_t outputW;            // @22
    uint16_t outputH;            // @24
    uint8_t  api;                // @26  FlApi
    uint8_t  upscaler;           // @27  FlUpscaler
    uint8_t  upscalerQuality;    // @28  vendor enum, 0xFF unknown
    uint8_t  fgMode;             // @29  FlFgMode
    uint8_t  rtFlags;            // @30  FlRtFlags
    uint8_t  hdr;                // @31

    // Volume, not a call count, and 32-bit for a reason: one 3840x2160
    // primary-ray dispatch is 8,294,400 rays — 126x a uint16's range. A
    // narrower counter saturates on every RT title at 1080p or above, which
    // is exactly the regime the path-tracing heuristic reads.
    uint32_t dispatchRaysVolume;    // @32  sum of W*H*D this frame

    uint16_t psoCreatedThisFrame;       // @36  compile COUNT, not a flag
    uint8_t  maxTraceRecursionDepth;    // @38  from the live RT PSO config

    // @39 — was _pad0, an explicit hole kept only so the next field stayed
    // 8-aligned. It now carries FlMeasured. Spending it costs nothing: the byte
    // was already inside the record and already written every frame, so this is
    // not a layout change and needs no FL_SHM_LAYOUT_VERSION bump.
    uint8_t measuredMask;    // @39  FlMeasured — which fields this frame MEASURED

    uint64_t vramUsedBytes;      // @40
    uint32_t reflexLatencyUs;    // @48  0 = unavailable
    uint32_t fgEvaluations;      // @52  FG feature evaluations this frame

    // Seqlock counter. Monotonic per slot and NEVER reset, so a full lap of
    // the ring always changes it — otherwise a reader stalled exactly one lap
    // would validate a different frame as unchanged. The payload write must
    // cover [0,56) and [60,64) and must never touch this field.
    uint32_t seq;    // @56

    // @60 — was _pad1. A stable identity for the swapchain this present came
    // through, and it had to be decided BEFORE a writer exists.
    //
    // Patching a vtable slot patches the SHARED dxgi.dll class vtable, so one
    // hook sees EVERY IDXGISwapChain in the process — measured: five different
    // configurations, D3D11 and D3D12, WARP and hardware, composition and HWND,
    // all report the identical vtable, and a detour installed via one catches
    // presents made through another. A title with a separate UI or video-playback
    // swapchain therefore inflates F_disp, and 03_METRICS defines Displayed FPS
    // as count(F_disp)/D with no way to tell the streams apart.
    //
    // The Agent segments by this value and reports the dominant stream. Zero
    // means the writer could not identify the swapchain, which the Agent must
    // treat as "one undifferentiated stream", not as a valid id.
    //
    // FREE NOW, EXPENSIVE LATER, which is why it is here rather than in the PR
    // that writes it: 07_IPC §Protocol rules already requires the payload write
    // to cover [60,64), so these four bytes are written every frame regardless.
    // Once a C# mirror exists (§R10), changing the record costs a
    // FL_SHM_LAYOUT_VERSION bump, and fl_shm.h defines that as user-visible —
    // the Agent refuses to attach and tells the user to restart the game.
    uint32_t swapchainId;    // @60  stable per-swapchain id; 0 = unidentified
};

// ---------------------------------------------------------------------------
// Layout contract. If one of these fires, the C# mirror and every offset in
// docs/07_IPC.md are wrong too — fix all three together.
// ---------------------------------------------------------------------------
static_assert(sizeof(FlShmHandshake) == 64, "FlShmHandshake must be one cache line");
static_assert(sizeof(FlWriterState) == 64, "FlWriterState must be one cache line");
static_assert(sizeof(FlControlBlock) == 64, "FlControlBlock must be one cache line");
static_assert(sizeof(FlFrameRecord) == 64, "FlFrameRecord must be exactly 64 bytes");

static_assert(offsetof(FlShmHandshake, layoutVersion) == 0);
static_assert(offsetof(FlShmHandshake, recordSize) == 4);
static_assert(offsetof(FlShmHandshake, capacity) == 8);
static_assert(offsetof(FlShmHandshake, pid) == 12);
static_assert(offsetof(FlShmHandshake, buildId) == 16);
static_assert(offsetof(FlShmHandshake, qpcEpoch) == 48);
static_assert(offsetof(FlShmHandshake, adapterLuid) == 56);

static_assert(offsetof(FlWriterState, writeIndex) == 0);
static_assert(offsetof(FlWriterState, status) == 8);
static_assert(offsetof(FlWriterState, apiMask) == 12);
static_assert(offsetof(FlWriterState, faultCount) == 16);
static_assert(offsetof(FlWriterState, vramBudgetMb) == 20);

static_assert(offsetof(FlControlBlock, pauseRequested) == 0);
static_assert(offsetof(FlControlBlock, unhookRequested) == 4);
static_assert(offsetof(FlControlBlock, overlayEnabled) == 8);
static_assert(offsetof(FlControlBlock, guardTicks) == 12);

static_assert(offsetof(FlFrameRecord, qpc) == 0);
static_assert(offsetof(FlFrameRecord, frameIndex) == 8);
static_assert(offsetof(FlFrameRecord, presentFlags) == 12);
static_assert(offsetof(FlFrameRecord, syncInterval) == 16);
static_assert(offsetof(FlFrameRecord, renderW) == 18);
static_assert(offsetof(FlFrameRecord, renderH) == 20);
static_assert(offsetof(FlFrameRecord, outputW) == 22);
static_assert(offsetof(FlFrameRecord, outputH) == 24);
static_assert(offsetof(FlFrameRecord, api) == 26);
static_assert(offsetof(FlFrameRecord, upscaler) == 27);
static_assert(offsetof(FlFrameRecord, upscalerQuality) == 28);
static_assert(offsetof(FlFrameRecord, fgMode) == 29);
static_assert(offsetof(FlFrameRecord, rtFlags) == 30);
static_assert(offsetof(FlFrameRecord, hdr) == 31);
static_assert(offsetof(FlFrameRecord, dispatchRaysVolume) == 32);
static_assert(offsetof(FlFrameRecord, psoCreatedThisFrame) == 36);
static_assert(offsetof(FlFrameRecord, maxTraceRecursionDepth) == 38);
static_assert(offsetof(FlFrameRecord, vramUsedBytes) == 40);
static_assert(offsetof(FlFrameRecord, reflexLatencyUs) == 48);
static_assert(offsetof(FlFrameRecord, fgEvaluations) == 52);
static_assert(offsetof(FlFrameRecord, measuredMask) == 39);
static_assert(offsetof(FlFrameRecord, seq) == 56);
static_assert(offsetof(FlFrameRecord, swapchainId) == 60);

// The regions must not overlap, and the ring must stay 64-aligned.
static_assert(FL_SHM_HANDSHAKE_OFFSET + sizeof(FlShmHandshake) <= FL_SHM_WRITER_OFFSET);
static_assert(FL_SHM_WRITER_OFFSET + sizeof(FlWriterState) <= FL_SHM_CONTROL_OFFSET);
static_assert(FL_SHM_CONTROL_OFFSET + sizeof(FlControlBlock) <= FL_SHM_RING_OFFSET);
static_assert(FL_SHM_RING_OFFSET % 64 == 0);

constexpr size_t FlShmSizeForCapacity(uint32_t capacity) noexcept {
    return FL_SHM_RING_OFFSET + static_cast<size_t>(capacity) * sizeof(FlFrameRecord);
}

}    // namespace fl

#endif    // FRAMELEDGER_FL_SHM_H
