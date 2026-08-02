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
};

// ---------------------------------------------------------------------------
// Region 1 — write-once at init by the Overlay.
// ---------------------------------------------------------------------------
struct alignas(64) FlShmHandshake {
    uint32_t layoutVersion;    // @0   must equal FL_SHM_LAYOUT_VERSION on both sides
    uint32_t recordSize;       // @4   64; belt-and-braces against struct drift
    uint32_t capacity;         // @8   power of two
    uint32_t pid;              // @12
    char     buildId[32];      // @16  native DLL build id
    uint64_t qpcEpoch;         // @48  session time base, shared with sensor samples
    uint64_t adapterLuid;      // @56  which GPU the swapchain is on
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
    uint8_t  _pad0;                     // @39  explicit: keeps the next field 8-aligned

    uint64_t vramUsedBytes;      // @40
    uint32_t reflexLatencyUs;    // @48  0 = unavailable
    uint32_t fgEvaluations;      // @52  FG feature evaluations this frame

    // Seqlock counter. Monotonic per slot and NEVER reset, so a full lap of
    // the ring always changes it — otherwise a reader stalled exactly one lap
    // would validate a different frame as unchanged. The payload write must
    // cover [0,56) and [60,64) and must never touch this field.
    uint32_t seq;      // @56
    uint32_t _pad1;    // @60  explicit
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
static_assert(offsetof(FlFrameRecord, seq) == 56);

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
