// FrameLedger shared-memory layout.
//
// NORMATIVE. This header and docs/07_IPC.md must agree; the static_asserts
// below are what enforce it. Mirrored in C# by FrameLedger.Shared's
// ShmLayout.cs, and ShmLayoutMirrorTests compares both sides against the
// offsets emitted by tools/fl-layout-dump (CLAUDE.md §Struct mirroring).
//
// That sentence was present tense and FALSE from the day this file was written
// until 2026-08-05 -- there was no mirror and no test. It is true now, and the
// reason it is worth a note is where the false claim sat: in the normative
// header, where a reader is most entitled to trust it. build.ps1:324-329
// enumerates the other eight sites.
//
// SO: A FIELD CHANGED HERE MUST BE CHANGED IN ShmLayout.cs, and the build tells
// you. Add it to tools/fl-layout-dump too -- the test walks the dump's field
// list in BOTH directions, so a field this header gains and the dump does not
// report is a field the mirror silently stops checking.
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
#define FL_SHM_LAYOUT_VERSION 3u

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

// THE ZERO VALUE OF EVERY ENUM IN THE RECORD IS "NOBODY SAID", NOT A FACT.
//
// This is the layout-version-3 rule and it is the one idea the whole revision is
// built on. `FlFrameRecord rec{}` zero-initialises, so whatever 0 means is what a
// writer publishes when it FORGETS. Until v3, 0 meant FL_UPSCALER_NONE and
// FL_FG_NONE -- "we looked and there was none" -- so a forgetful writer asserted
// a measured negative about a title nobody had examined, and 03_METRICS turns a
// session of that into `upscaler none` and `fg_factor 1.0`, the single inflated
// number CLAUDE.md rule 6 exists to forbid.
//
// measuredMask made that safe by CONVENTION: the value was only to be read when
// its bit was set. Convention is what this project keeps finding broken. v3 makes
// it safe by CONSTRUCTION -- a writer that forgets everything publishes a record
// that decodes to "nothing was measured", and the mask becomes corroboration
// rather than the sole defence.
//
// FL_API_UNKNOWN = 0 was already this shape and is the worked example.
//
// THE THREE STATES ARE DISTINCT AND ALL THREE ARE NEEDED:
//   NOT_REPORTED (0) -- no hook capable of answering was live. Agent: N/A.
//   UNKNOWN (0xFF)   -- a hook ran and could not identify what it saw. Agent: N/A,
//                       but it is a DIFFERENT N/A: it means our coverage is short,
//                       not that the question did not apply.
//   NONE             -- a hook ran and there was genuinely no upscaler. A real
//                       measurement, and the only one of the three that may be
//                       aggregated as a negative.
enum FlUpscaler : uint8_t {
    FL_UPSCALER_NOT_REPORTED = 0,

    FL_UPSCALER_DLSS = 1,

    // RETIRED, and the slot is reserved rather than reused. DLSS Ray
    // Reconstruction was a value here, which made it MUTUALLY EXCLUSIVE with
    // DLSS super-resolution -- but they run together, and 03_METRICS §RT/PT/RR
    // makes RR an independent tri-state axis ("Yes / No if DLSS is active
    // without it"), which is also CLAUDE.md rule 7's trio. The record was the
    // only place the two were conflated; 06_DATA_MODEL already stored them
    // apart. RR is now FL_FEAT_RAY_RECONSTRUCTION in featureFlags.
    //
    // Reserved, not reused: a stale writer emitting 2 must not silently read as
    // FSR2. The layout-version bump already refuses such a writer, so this is
    // belt-and-braces -- and it costs one line.
    FL_UPSCALER_RETIRED_RAY_RECONSTRUCTION = 2,

    FL_UPSCALER_FSR2 = 3,
    FL_UPSCALER_FSR3 = 4,
    FL_UPSCALER_FSR4 = 5,
    FL_UPSCALER_XESS = 6,
    FL_UPSCALER_NIS = 7,

    // Moved from 0. The values above keep their v2 numbers deliberately: the
    // widening is already a layout change, and renumbering seven live values
    // across two languages is transcription risk for a cosmetic gain.
    FL_UPSCALER_NONE = 8,

    FL_UPSCALER_UNKNOWN = 0xFF,
};

enum FlFgMode : uint8_t {
    FL_FG_NOT_REPORTED = 0,
    FL_FG_DLSS_G = 1,
    FL_FG_FSR_FG = 2,
    FL_FG_XEFG = 3,
    FL_FG_NONE = 4,    // moved from 0, same reason as FL_UPSCALER_NONE
    FL_FG_UNKNOWN = 0xFF,
    // No AFMF: driver-side frame generation happens after present and is
    // invisible to an in-process hook (docs/03_METRICS.md §Frame Generation).
};

// FlFrameRecord::colorSpace @31. WAS `hdr`, a bool.
//
// Same byte, and the change is not decoration: 03_METRICS and 06_DATA_MODEL want
// a tri-state, and a bool has no third state.
//
// THE DEFAULT IS THE SUBTLE PART. The only producer is a hook on
// IDXGISwapChain3::SetColorSpace1, and an SDR title NEVER CALLS IT -- so
// "hook live, no call observed" would sit at 0 forever and HDR's definite `No`
// would be unreachable, which is the same defect as the missing RT tier being
// fixed in the same revision. It is avoided by initialising the field to SDR at
// swapchain identification rather than leaving it at 0: DXGI documents
// DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 as the default for a swapchain nobody
// has called SetColorSpace1 on, so SDR is a MEASURED default and not an
// affirmative negative. The writer must do that, and say why where it does it.
enum FlColorSpace : uint8_t {
    FL_COLOR_SPACE_NOT_REPORTED = 0,    // no SetColorSpace1 hook was live
    FL_COLOR_SPACE_SDR = 1,             // G22 / Rec.709 -- DXGI's documented default
    FL_COLOR_SPACE_HDR10 = 2,           // ST.2084 / Rec.2020
    FL_COLOR_SPACE_SCRGB = 3,           // linear FP16
};

// bits in FlFrameRecord::featureFlags @39 — per-frame boolean facts.
//
// SELF-DESCRIBING, low nibble facts and high nibble their OBSERVED companions.
// That costs four bits that were going to be reserved anyway and buys the
// property rtFlags is being flipped for: 06_DATA_MODEL persists per-frame bytes
// as their own blobs, so a byte whose "did we look" lives in a DIFFERENT series
// cannot express "not measured" after persistence without a join.
//
// It also answers a specific over-claim. Ray Reconstruction is produced only by
// NGX/Streamline, while FL_MEASURED_UPSCALER also covers FFX, XeSS and NIS -- so
// a writer with FFX hooks and no NGX hooks has "upscaler measured" and knows
// nothing whatever about RR. Sharing the mask bit would publish RR = No. The
// design panel asked for a separate mask bit; an in-band OBSERVED bit is the
// same guarantee, cheaper, and survives the blob.
enum FlFeatureFlags : uint8_t {
    FL_FEAT_RAY_RECONSTRUCTION = 1u << 0,
    FL_FEAT_REFLEX_ENABLED = 1u << 1,

    // A Streamline feature id we do not decode was evaluated this frame.
    //
    // NOT AN ERROR, AND NOT DECORATION -- it is the only way the consumer can tell
    // apart two sessions that otherwise look identical. Once the frame-generation
    // count leaves the feature bitmask, a present carrying kFeatureDLSS_G together
    // with any id that falls to FL_SL_SEEN_OTHER -- Reflex, PCL, DeepDVC, Latewarp,
    // DirectSR, or a feature a newer Streamline adds -- has fgEvaluations > 0 and is
    // indistinguishable from a present that carried DLSS-G alone. §S30's question is
    // exactly "which ids actually arrive", and without this bit the bucket for
    // "an id we do not decode" reads ZERO on a title evaluating one every
    // application frame, so a decision table keyed on that bucket would close the
    // item on a number that could not have been anything else.
    //
    // AN ENUMERATOR, NOT A FIELD: no struct changes, so no FL_SHM_LAYOUT_VERSION
    // bump, no ShmLayout.cs struct edit and no fl-layout-dump entry. The byte and
    // its offset are unchanged.
    FL_FEAT_SL_UNDECODED = 1u << 2,

    // A SUPER-RESOLUTION id -- kFeatureDLSS or kFeatureNIS -- was evaluated this frame.
    //
    // RAW, AND THAT IS THE ENTIRE POINT: it records which id ARRIVED, independently of
    // what the decode made of it. Measured 2026-08-15 and added because the instrument
    // had been contaminated by the thing it measures. Cyberpunk 2077 with Ray
    // Reconstruction on evaluates kFeatureDLSS_RR and never kFeatureDLSS, and the decode
    // was corrected to report DLSS for it -- correctly, since RR performs the upscale and
    // carries the scaling-input tag. But the census derived "kFeatureDLSS arrived" from
    // the decoded `upscaler` byte, so the same correction made it report 2,569 arrivals
    // of an id that arrived zero times. A measurement that moves when the decode moves
    // cannot be evidence ABOUT the decode, which is the whole job §S30 gave it.
    //
    // NO SEPARATE OBSERVED COMPANION, deliberately, against this enum's own convention.
    // All three Streamline facts in this byte are published under one condition -- an
    // evaluation was drained for this present -- so their companions would be provably
    // equal bit for bit, and FL_FEAT_RAY_RECONSTRUCTION_OBSERVED already carries it. A
    // redundant bit is not more honest than a shared one; it is just another thing to
    // keep in step. Bit 7 stays free.
    FL_FEAT_SL_SUPER_RESOLUTION = 1u << 3,

    FL_FEAT_RAY_RECONSTRUCTION_OBSERVED = 1u << 4,
    FL_FEAT_REFLEX_OBSERVED = 1u << 5,
    FL_FEAT_SL_UNDECODED_OBSERVED = 1u << 6,
    // bit 7 free, and must pair with the fact bit four places below
};

// bits in FlFrameRecord::rtFlags — POLARITY FLIPPED IN v3.
//
// Every bit now means "we OBSERVED this", so rtFlags == 0 says "no RT evidence
// was seen" and says nothing about whether anyone looked. Whether anyone looked
// is FL_MEASURED_RT in the mask, and WHICH RT hooks were live is
// FlWriterState::hooksInstalledMask. v2 had it the other way round -- three
// evidence bits plus an opt-in FL_RT_NOT_MEASURED -- which made a forgetful
// writer publish a measured absence.
enum FlRtFlags : uint8_t {
    FL_RT_AS_BUILD_OBSERVED = 1u << 0,    // catches inline RayQuery, which DispatchRays misses
    FL_RT_DISPATCH_OBSERVED = 1u << 1,

    // RENAMED from FL_RT_PSO_ALIVE, because that claimed a present-tense fact
    // the hook set cannot retract. Creation is observed at
    // ID3D12Device5::CreateStateObject; destruction is COM Release, which is NOT
    // in the hook inventory and must not be added (CLAUDE.md rule 4, and the hot
    // path). So the bit latches on and would have asserted "an RT state object
    // is currently alive" for the rest of a session after the game released the
    // last one. The name now says the thing that is actually observable.
    FL_RT_PSO_CREATED_EVER = 1u << 2,

    // bits 3-7 free. FL_RT_NOT_MEASURED is RETIRED: with the polarity flipped it
    // would be a second, redundant statement of what FL_MEASURED_RT already says,
    // and two statements of one fact is what this file exists to complain about.
};

// values for FlWriterState::rtTier — and the one value D3D12 does NOT give us.
//
// The tier is stored as D3D12_RAYTRACING_TIER's own value, because that enum is
// already "tier x10": measured against the Windows SDK 10.0.26100.0 header,
// NOT_SUPPORTED = 0, TIER_1_0 = 10, TIER_1_1 = 11, TIER_1_2 = 12. So no
// arithmetic is done on it and a tier this build has never heard of still
// arrives intact -- which is why nothing below names 1_0/1_1/1_2.
//
// THE COLLISION THIS EXISTS TO RESOLVE. D3D12_RAYTRACING_TIER_NOT_SUPPORTED is
// 0, and 0 already means NOT QUERIED here. Storing the enum verbatim would make
// "we asked and this device cannot ray-trace" byte-identical to "nobody looked"
// -- the affirmative-negative conflation the whole of layout v3 exists to make
// impossible, arrived at by copying a vendor enum instead of by a guess.
//
// So a queried-and-incapable device stores FL_RT_TIER_UNSUPPORTED. 1 is chosen
// because no D3D12 tier value is 1 and none can be: the enum's own step is 10,
// and 03_METRICS' `No` branch tests `rtTier >= 10`, so an incapable device
// correctly fails that conjunct and the session reads N/A rather than a
// confident negative about a machine that could never have produced one.
enum FlRtTier : uint32_t {
    FL_RT_TIER_NOT_QUERIED = 0,    // no D3D12 device was identified, or the query failed
    FL_RT_TIER_UNSUPPORTED = 1,    // D3D12 answered NOT_SUPPORTED -- a measurement, not a silence

    // The threshold 03_METRICS §RT/PT/RR states for "an RT-capable device". Named
    // here so the consumer stops spelling it as a literal 10 next to a field whose
    // units are not obvious from its type.
    FL_RT_TIER_CAPABLE_MIN = 10,
};

// bits in FlWriterState::hooksInstalledMask — which hook FAMILIES are live.
//
// Writer-level, not per-frame, and it exists because "we looked and saw nothing"
// is not one question but several. 03_METRICS defines ray tracing's definite `No`
// as "RT-capable device present, no AS builds and no dispatches for the WHOLE
// session" -- and a writer that installed only the DispatchRays hook sees no
// evidence on an inline-RayQuery title, which is precisely the case
// BuildRaytracingAccelerationStructure exists to catch. Without knowing which
// hooks were live, that writer's silence is indistinguishable from a real
// negative and the Agent would publish `No` about a title that ray-traces every
// frame.
//
// So `No` requires AS-build to have been INSTALLED, not merely RT to have been
// "measured". docs/03_METRICS.md §RT/PT/RR states the conjuncts.
enum FlHookFamily : uint32_t {
    FL_HOOK_PRESENT = 1u << 0,
    FL_HOOK_UPSCALER_IDENTITY = 1u << 1,    // NGX/SL/FFX/XeSS CreateFeature-class
    FL_HOOK_UPSCALER_PARAMS = 1u << 2,      // the parameter accessors (Streamline-only today)
    FL_HOOK_FG_EVALUATIONS = 1u << 3,
    FL_HOOK_RT_DISPATCH = 1u << 4,
    FL_HOOK_RT_AS_BUILD = 1u << 5,
    FL_HOOK_RT_PSO = 1u << 6,
    FL_HOOK_PSO = 1u << 7,
    FL_HOOK_COLOR_SPACE = 1u << 8,
    FL_HOOK_REFLEX = 1u << 9,
    FL_HOOK_VRAM = 1u << 10,
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
enum FlMeasured : uint16_t {
    FL_MEASURED_UPSCALER = 1u << 0,      // upscaler IDENTITY only — see bit 8
    FL_MEASURED_FG = 1u << 1,            // fgMode IDENTITY only — see bit 10
    FL_MEASURED_RT = 1u << 2,            // rtFlags + dispatchRaysVolume + maxTraceRecursionDepth
    FL_MEASURED_PSO = 1u << 3,           // psoCreatedThisFrame
    FL_MEASURED_VRAM = 1u << 4,          // vramUsedMb
    FL_MEASURED_LATENCY = 1u << 5,       // reflexLatencyUs
    FL_MEASURED_OUTPUT_RES = 1u << 6,    // outputW/H — from the swapchain desc, not a feature hook

    // `hdr` @31 had no "not measured" state, and #36 missed it while fixing
    // exactly this class everywhere else.
    //
    // HDR output is only knowable by hooking IDXGISwapChain3::SetColorSpace1
    // (17_HOOK_ENGINE §Hook inventory). A present-only writer does not install
    // it, so `hdr = 0` would assert "we looked and this title is not HDR" about a
    // title nobody asked -- the same affirmative negative measuredMask exists to
    // prevent, and 06_DATA_MODEL persists the value in sessions.hdr.
    //
    // Spending bit 7 costs nothing: the byte is already inside the record and
    // already written every frame, so this is not a layout change and needs no
    // FL_SHM_LAYOUT_VERSION bump. It is claimed NOW because after the C# mirror
    // exists (§R10) the identical edit becomes user-visible -- the Agent refuses
    // to attach and tells the user to restart the game -- and 11_UPDATER makes
    // FL_SHM_LAYOUT_VERSION a SemVer MAJOR. Same argument #36 made for the other
    // five decisions; this is the last field it left unclaimed.
    FL_MEASURED_HDR = 1u << 7,    // colorSpace

    // --- v3, and each of these three splits a bit that covered TWO HOOK CLASSES.
    //
    // The shape of the defect is the same every time: one bit governing values
    // from producers that do not arrive together, so a writer with half the
    // hooks publishes the other half's zero-default as measured fact.

    // upscalerQuality + upscalerSharpness + renderW/H.
    //
    // 17_HOOK_ENGINE §The NGX parameter surface measured the split this corrects:
    // a Streamline-shimmed title exposes the parameter accessors as ordinary
    // exports on sl.common.dll and yields all four, while an NGX-DIRECT title
    // exports only the parameter-object FACTORIES -- so a writer hooking
    // CreateFeature/EvaluateFeature knows WHICH upscaler ran and nothing about
    // quality, sharpness or render size. One bit for both published
    // upscalerQuality = 0 -- NGX MaxPerf, "DLSS Performance" -- as a measurement.
    FL_MEASURED_UPSCALER_PARAMS = 1u << 8,

    // syncInterval + presentFlags.
    //
    // Two of the four planned present writers have no such arguments to report:
    // wglSwapBuffers(HDC) takes neither (the interval is wglSwapIntervalEXT
    // context state) and vkQueuePresentKHR takes neither (FIFO/MAILBOX/IMMEDIATE
    // is a swapchain property). Both would have published "syncInterval 0,
    // presentFlags 0" -- vsync off, no flags -- as measurement. syncInterval is
    // the worse of the two: 0 is a REAL DXGI value, so there is no in-band
    // sentinel available and only a mask bit can carry it.
    FL_MEASURED_PRESENT_ARGS = 1u << 9,

    // fgEvaluations, split from fgMode.
    //
    // 17_HOOK_ENGINE lists them as two hook classes: a CreateFeature-class call
    // tells you a FrameGeneration feature exists, and the per-present evaluation
    // count is a different capability. A writer with identity and no counts would
    // publish fgEvaluations = 0 on every record, and a consumer reading that as a
    // measurement gets F_app == 0 or, worse, folds it back to F_app == F_disp --
    // i.e. fg_factor 1.0, CLAUDE.md rule 6's forbidden number, reached by a writer
    // that never counted anything. With this bit clear the Agent must treat F_app
    // as a DATA GAP; with it set, a zero on one record is a real measurement of
    // that present and must not be filtered out.
    //
    // THE STREAMLINE WRITER SETS THIS AND FL_MEASURED_FG TOGETHER, from one detour
    // on slEvaluateFeature, so on that writer the two bits carry the same
    // information. They stay separate because the split is about HOOK CLASSES and
    // not about this writer: an NGX-direct or FFX title yields identity through a
    // CreateFeature-class call with no evaluation count behind it, and that writer
    // must be able to say so.
    FL_MEASURED_FG_COUNTS = 1u << 10,

    // bits 11-15 reserved. Writers leave them zero; readers must IGNORE them
    // rather than validate them as zero.
    //
    // BE PRECISE ABOUT WHAT THAT BUYS, because the tempting claim is false: it
    // does NOT mean a future field can be added without a layout bump. Any new
    // field also needs a byte, and every byte of this record is allocated except
    // `reserved` @52 -- and FlShmHandshake::recordSize plus the layout version are
    // compared before a reader looks at anything, so an old reader refuses a new
    // writer outright and never gets as far as an unknown bit. What the reserve
    // actually buys is that existing offsets do not move, which keeps the diff,
    // the fl-layout-dump edit and the C# mirror edit small and reviewable.
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

    // --- v3. Four SESSION facts, deliberately here and not in the record.
    //
    // Paying 64 bytes a frame at 500 fps to restate a constant is the wrong
    // trade, and 03_METRICS already treats two of these as session-scoped:
    // rt_pso_count is listed under "Derived extras", not per frame.
    //
    // NO CONSISTENCY GUARANTEE BETWEEN THESE AND ANY RECORD. They are read by the
    // Agent at drain time, not sampled with a frame. A rule that needs them to
    // agree with a particular record cannot be stated per frame; state it over
    // the session (see hooksInstalledMask).

    // Device ray-tracing tier, encoded as FlRtTier. Without this, 03_METRICS'
    // definite RT `No` -- "RT-capable device present, no AS builds and no
    // dispatches for the whole session" -- has no producer at all, so item 6
    // could reach Yes or N/A and never No. Written at device identification --
    // once per swapchain the writer newly sees, not once per session, because it
    // is a property of the adapter and every query answers the same thing.
    //
    // THREE STATES, NOT TWO, and this comment said two until the producer was
    // written. "0 = NOT QUERIED" is right, and D3D12's own
    // D3D12_RAYTRACING_TIER_NOT_SUPPORTED is ALSO 0 -- so the obvious
    // implementation (store the enum) would have published "nobody looked" about
    // every non-RT device. FlRtTier above carries the resolution.
    uint32_t rtTier;    // @24

    // FlHookFamily bits. Which hook families this writer actually installed.
    // MONOTONIC: bits are only ever set, never cleared, because hooks install
    // lazily as vendor modules appear and a bit that could clear would make a
    // session-level check race the frame it read.
    uint32_t hooksInstalledMask;    // @28

    // Session counts, and the reason they live here rather than being derived
    // from a per-frame field is a CACHE-LINE argument, not a space one.
    //
    // This struct is region 2, which the Overlay writes on the present path;
    // fl_shm.h's own header explains that the regions are separate cache lines so
    // a cross-process write does not bounce that line. Accumulating from the hook
    // would put a bursty read-modify-write on it every frame. So the hook keeps
    // process-local counters and the 1 Hz watchdog publishes them here -- the
    // Agent drains at 10 Hz and these are session facts, so 1 Hz is ample.
    uint32_t rtStateObjectsCreated;    // @32  03_METRICS' rt_pso_count
    uint32_t rasterPsoCreated;         // @36  the denominator pt_confidence wanted

    uint32_t reserved[6];    // @40..63  must be zero; room for additive fields

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
    uint8_t  upscaler;           // @27  FlUpscaler — 0 = NOT_REPORTED in v3
    uint8_t  upscalerQuality;    // @28  vendor enum; 0xFF = a hook ran and could not tell
    uint8_t  fgMode;             // @29  FlFgMode — 0 = NOT_REPORTED in v3
    uint8_t  rtFlags;            // @30  FlRtFlags — v3 bits are *_OBSERVED
    uint8_t  colorSpace;         // @31  FlColorSpace — was `hdr`, a bool with no third state

    // Volume, not a call count, and 32-bit for a reason: one 3840x2160
    // primary-ray dispatch is 8,294,400 rays — 126x a uint16's range. A
    // narrower counter saturates on every RT title at 1080p or above, which
    // is exactly the regime the path-tracing heuristic reads.
    uint32_t dispatchRaysVolume;    // @32  sum of W*H*D this frame

    uint16_t psoCreatedThisFrame;       // @36  compile COUNT, not a flag
    uint8_t  maxTraceRecursionDepth;    // @38  from the live RT PSO config

    // @39 — was _pad0, then measuredMask, now FlFeatureFlags. The mask outgrew a
    // byte in v3 and moved to 40 where a uint16 is naturally aligned; this byte
    // did not go back to being padding.
    uint8_t featureFlags;    // @39  FlFeatureFlags — facts + their OBSERVED bits

    uint16_t measuredMask;    // @40  FlMeasured, WIDENED 8 -> 16 bits in v3

    // @42 — NEW in v3. Percent, 0-100. 0xFF means a hook ran and this upscaler's
    // API reports no sharpness: DLSS 3.x removed the parameter and XeSS exposes
    // none, so "the hook ran and there is nothing to report" is a real state that
    // is NOT the same as "no hook ran" (measuredMask bit 8 clear). Governed with
    // quality and render size, because it comes from the same accessor.
    uint8_t upscalerSharpness;    // @42

    // @43 — NARROWED uint32 -> uint8 and moved from 52.
    //
    // FG *EVALUATIONS* IN ONE PRESENT, NOT GENERATED FRAMES, and this comment said
    // the second thing until the producer was written. Owner ruling 2026-08-14:
    // slEvaluateFeature(kFeatureDLSS_G) fires ONCE PER APPLICATION FRAME and yields
    // N-1 generated ones, where N lives in sl::DLSSGOptions -- set out of band, by
    // the route HANDOFF §2b refused. So the value here is 0 or 1 on a DLSS-G title
    // (1 on the present that drained the batch, 0 on the presents the vendor's
    // swapchain emitted from it), and 03_METRICS computes F_app = Σ fgEvaluations
    // rather than presents − Σ. The old reading ("3 at x4") described a number no
    // in-policy hook can produce.
    //
    // SATURATES AT 255, NEVER WRAPS. It is the DENOMINATOR of fg_factor, so a
    // wrapped count reads low and inflates the factor without bound -- CLAUDE.md
    // rule 6's forbidden direction. 255 is therefore a sentinel as much as a value:
    // no configuration evaluates frame generation 255 times between two presents, so
    // a consumer seeing it must refuse to publish a factor rather than divide.
    uint8_t fgEvaluations;    // @43

    // @44 — NARROWED uint64 bytes -> uint32 MiB, renamed, moved from 40. This is
    // where v3's four spare bytes came from, and the narrowing is a correction
    // rather than a sacrifice: 03_METRICS exports the value as `vram_mb`,
    // 06_DATA_MODEL stores `vram_proc_avg_mb`, 17_HOOK_ENGINE refreshes it at 1 Hz
    // so it is a held sample and not a per-frame measurement, and
    // `budget_exceeded_pct` compares it against vramBudgetMb -- which was ALREADY
    // uint32 MiB in FlWriterState. The record was carrying 64 bits of
    // byte-precision that every consumer divided away, to feed a comparison that
    // was unit-mismatched at the point of use.
    //
    // MUST use the same divisor as vramBudgetMb (1024*1024, truncating), or
    // budget_exceeded_pct gains a systematic bias. Residual: a flip within 1 MiB
    // of the budget, 0.004% of a 24 GiB card. uint32 MiB reaches 4 PiB.
    uint32_t vramUsedMb;    // @44

    uint32_t reflexLatencyUs;    // @48  0 = unavailable; NOT narrowed, see below

    // @52 — NEW in v3, MUST BE ZERO. The record hit zero slack once and four
    // answers had nowhere to go; shipping it at zero slack again would repeat
    // that exactly.
    //
    // What it does NOT buy is a future field without a layout bump — recordSize
    // and layoutVersion are compared before a reader looks at anything, so an old
    // reader refuses a new writer and never reaches an unknown field. What it buys
    // is that existing offsets do not move, which keeps the next diff small.
    uint32_t reserved;    // @52

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
static_assert(offsetof(FlWriterState, rtTier) == 24);
static_assert(offsetof(FlWriterState, hooksInstalledMask) == 28);
static_assert(offsetof(FlWriterState, rtStateObjectsCreated) == 32);
static_assert(offsetof(FlWriterState, rasterPsoCreated) == 36);
static_assert(offsetof(FlWriterState, reserved) == 40);

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
static_assert(offsetof(FlFrameRecord, colorSpace) == 31);
static_assert(offsetof(FlFrameRecord, dispatchRaysVolume) == 32);
static_assert(offsetof(FlFrameRecord, psoCreatedThisFrame) == 36);
static_assert(offsetof(FlFrameRecord, maxTraceRecursionDepth) == 38);
static_assert(offsetof(FlFrameRecord, featureFlags) == 39);
static_assert(offsetof(FlFrameRecord, measuredMask) == 40);
static_assert(offsetof(FlFrameRecord, upscalerSharpness) == 42);
static_assert(offsetof(FlFrameRecord, fgEvaluations) == 43);
static_assert(offsetof(FlFrameRecord, vramUsedMb) == 44);
static_assert(offsetof(FlFrameRecord, reflexLatencyUs) == 48);
static_assert(offsetof(FlFrameRecord, reserved) == 52);
static_assert(offsetof(FlFrameRecord, seq) == 56);
static_assert(offsetof(FlFrameRecord, swapchainId) == 60);

// The two fl_ring.h pins, restated here because v3 moved seven fields and these
// are the two that must NOT have moved: the seqlock's payload spans are derived
// from them, and 07_IPC states them in prose.
static_assert(offsetof(FlFrameRecord, seq) == 56, "07_IPC pins seq at 56; the payload write steps over it");
static_assert(offsetof(FlFrameRecord, swapchainId) == 60, "07_IPC pins the payload tail at [60,64)");

// EVERY FIELD NATURALLY ALIGNED, asserted rather than eyeballed. std::atomic_ref
// requires it for the shared fields, and a misaligned uint16 introduced by a
// future edit would be a tearing bug nobody could see in a diff.
static_assert(offsetof(FlFrameRecord, qpc) % 8 == 0);
static_assert(offsetof(FlFrameRecord, measuredMask) % 2 == 0);
static_assert(offsetof(FlFrameRecord, vramUsedMb) % 4 == 0);
static_assert(offsetof(FlFrameRecord, reserved) % 4 == 0);
static_assert(offsetof(FlFrameRecord, seq) % 4 == 0);

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
