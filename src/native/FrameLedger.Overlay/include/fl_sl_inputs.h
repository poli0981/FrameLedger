// Walking the chained structures a title passes to slEvaluateFeature.
//
// WHY THIS IS A HEADER AND NOT A STATIC FUNCTION IN dllmain.cpp. Everything here
// dereferences pointers supplied by somebody else's process, under bounds we
// chose. That is exactly the code that has to be exercised against malformed
// input -- a cycle, a lying count, a null element -- and a function buried in an
// anonymous namespace inside the injected DLL can only be reached by injecting
// it. Here, hook-harness calls it directly and drives the hostile cases in
// microseconds (ctest fl_sl_inputs).
//
// RULE 4, AND WHY THIS IS INSIDE IT. `inputs` is an ARGUMENT to an API we
// hooked, which CLAUDE.md rule 4 permits in as many words. sl_core_api.h:251
// documents it as "the chained structures providing the input data (viewport,
// tags, constants etc)". We read the vendor's own declared layout and nothing
// else: no pattern scanning, no game state, no memory we were not handed.
//
// RULE 5, AND WHAT THAT COSTS. A fault in here lands in FL_HOOK_GUARD and burns
// one of the three that self-disable the Overlay. A hostile input that faults is
// therefore a BUG, not acceptable degradation, which is why every bound below is
// a constant and every dereference is preceded by a null check.

#ifndef FRAMELEDGER_FL_SL_INPUTS_H
#define FRAMELEDGER_FL_SL_INPUTS_H

#include <cstdint>
#include <fl_shm.h>
#include <sl.h>
// AFTER sl.h, deliberately: sl_dlss.h has NO includes of its own -- verified on
// the upstream file, its closure is empty -- and relies on sl.h having been
// pulled in first for SL_STRUCT_BEGIN, Boolean and INVALID_UINT.
#include <sl_dlss.h>

namespace fl::slinputs {

// Bounds, chosen to be far above any plausible real value and cheap to honour.
//
// numInputs is the CALLER'S number. On a healthy title it is a handful; a wrong
// one -- from a version mismatch, a bug, or a struct we misread -- walks off the
// end of an array we do not own, inside a game.
//
// BE PRECISE ABOUT WHAT THIS BUYS, because "bounded" reads as safer than it is.
// The cap turns numInputs = 4 billion into 32 reads instead of a guaranteed
// access violation. It does NOT protect against a caller that passes 5 with a
// 2-element array: nothing in the ABI carries the allocation's length, so the
// count is the only statement of it and a wrong one in that direction is
// unprotectable from here. What the cap removes is the catastrophic case; the
// residual is the vendor's contract to keep.
inline constexpr uint32_t kMaxInputs = 32;

// `next` forms a linked chain (sl_struct.h). A cycle would hang the present
// thread, which is worse than a fault: no exception, no self-disable, just a
// frozen game with our DLL in it.
inline constexpr uint32_t kMaxChain = 8;

// Which FlSlTagType bit a Streamline BufferType maps to. Every type maps to SOMETHING
// -- FL_SL_TAG_OTHER for the ones the census does not name -- so a tag list of any
// content leaves a mark, and "no bits" can only mean "no tags".
inline constexpr uint32_t TagTypeBit(sl::BufferType type) noexcept {
    switch (type) {
    case sl::kBufferTypeDepth:
        return fl::FL_SL_TAG_DEPTH;
    case sl::kBufferTypeMotionVectors:
        return fl::FL_SL_TAG_MOTION_VECTORS;
    case sl::kBufferTypeHUDLessColor:
        return fl::FL_SL_TAG_HUDLESS;
    case sl::kBufferTypeScalingInputColor:
        return fl::FL_SL_TAG_SCALING_INPUT;
    case sl::kBufferTypeScalingOutputColor:
        return fl::FL_SL_TAG_SCALING_OUTPUT;
    case sl::kBufferTypeUIColorAndAlpha:
        return fl::FL_SL_TAG_UI_COLOR_ALPHA;
    case sl::kBufferTypeUIAlpha:
        return fl::FL_SL_TAG_UI_ALPHA;
    case sl::kBufferTypeBackbuffer:
        return fl::FL_SL_TAG_BACKBUFFER;
    default:
        return fl::FL_SL_TAG_OTHER;
    }
}
static_assert(TagTypeBit(sl::kBufferTypeHUDLessColor) == fl::FL_SL_TAG_HUDLESS, "the DLSS-G input maps to its bit");
static_assert(TagTypeBit(static_cast<sl::BufferType>(9999)) == fl::FL_SL_TAG_OTHER, "an unknown type still marks");

struct ScanResult {
    uint16_t renderW = 0;
    uint16_t renderH = 0;
    bool     found = false;

    // Every readable ResourceTag's type, as FlSlTagType bits -- the local route's
    // contribution to FlWriterState::slTagCensus and to the per-present DLSS-G mark.
    uint32_t tagTypes = 0;

    // The vendor's own DLSSMode value, or 0xFF for "a hook ran and could not
    // tell". NEVER 0: fl_shm.h has no in-band "not measured" for
    // upscalerQuality, so 0 would publish DLSSMode::eMaxPerformance -- "DLSS
    // Performance" -- as a measurement.
    uint8_t quality = 0xFFu;
    bool    qualityFound = false;
};

// DLSSMode -> the byte fl_shm.h carries. Never 0, for any input.
//
// eOff IS ZERO in the vendor enum, and that is the trap this exists for: storing
// DLSSMode verbatim would make "the title turned DLSS off" and "nobody looked"
// the same byte. Exactly the collision class as
// D3D12_RAYTRACING_TIER_NOT_SUPPORTED against rtTier's "not queried", and
// resolved the same way -- at the writer, where the two are still
// distinguishable, rather than at the reader where they no longer are.
inline uint8_t QualityFromMode(sl::DLSSMode mode) noexcept {
    const uint32_t v = static_cast<uint32_t>(mode);
    if (v == static_cast<uint32_t>(sl::DLSSMode::eOff) || v >= static_cast<uint32_t>(sl::DLSSMode::eCount)) {
        // eOff is a real statement -- "not upscaling" -- but it is not a
        // QUALITY, and the field it would land in reserves 0 for "nobody
        // looked". A mode at or beyond eCount is a newer SDK than these headers
        // describe. Both are honestly "a hook ran and could not tell".
        return 0xFFu;
    }
    return static_cast<uint8_t>(v);
}

// Is this structure a ResourceTag we can read?
//
// BOTH the GUID and the version, and the version half is not ceremony:
// sl_struct.h gives every SL structure a structType and a structVersion, and the
// vendor's contract is that a version is only ever extended. A struct declaring
// a version BELOW the one our headers describe is one whose fields we cannot
// place, so it is skipped rather than read optimistically.
//
// What this CANNOT check is a structure whose GUID matches and whose allocation
// is shorter than the declaration -- there is no length in the ABI to compare
// against. That is the vendor's contract to keep, and it is stated here so the
// limit is known rather than discovered.
template <typename T>
inline bool IsReadable(const sl::BaseStructure* s) noexcept {
    return s != nullptr && s->structType == T::s_structType && s->structVersion >= sl::kStructVersion1;
}

inline bool IsReadableResourceTag(const sl::BaseStructure* s) noexcept {
    return IsReadable<sl::ResourceTag>(s);
}

// The size a scaling-input tag states, from EITHER of the two places the vendor
// struct can carry it. Shared by the local walk below and by the global-tag detours
// (slSetTag, slSetTagForFrame), so the three routes cannot disagree about what a
// tag says.
//
// 1. `extent` -- "the area of the tagged resource to use". Set by titles that
//    render into a buffer larger than the render resolution (dynamic resolution
//    on an output-sized target). Cyberpunk 2077 sets it: 1485x835.
// 2. `resource->width/height` when the extent is ZERO. sl_core_types.h documents a
//    zero extent as "using the entire resource" and the Resource's own size as
//    "MANDATORY only when using Vulkan" -- so on D3D12 it MAY be filled and may not
//    be, and a filled one is the title's statement of the buffer's size, which for a
//    whole-resource tag IS the render resolution. Both are fields of the argument
//    the hooked API received (CLAUDE.md rule 4), nothing is dereferenced beyond the
//    struct the title passed, and the Resource is GUID-checked the way every other
//    Streamline structure is before a field is read.
//
// A tag with neither is the honest unknown fl_shm.h already defines for renderW/H:
// reported as not-found rather than as a resolution of zero. Measured need: Dying
// Light: The Beast (SL 2.8.0) published DLSS identity on every batch and no extent
// from either global-tag route on four captures; the whole-resource shape is the
// remaining reading the record can distinguish, and this is how it is tested.
inline bool TagSize(const sl::ResourceTag& tag, uint32_t& w, uint32_t& h) noexcept {
    w = tag.extent.width;
    h = tag.extent.height;
    if (w == 0 || h == 0) {
        if (!IsReadable<sl::Resource>(tag.resource)) {
            return false;
        }
        w = tag.resource->width;
        h = tag.resource->height;
    }
    return w != 0 && h != 0 && w <= 0xFFFFu && h <= 0xFFFFu;
}

// Find the extent a title tagged as the upscaler's INPUT colour buffer.
//
// kBufferTypeScalingInputColor is the render target the upscaler reads, so its
// extent is the render resolution -- the thing 03_METRICS needs to state a real
// upscale ratio instead of N/A.
//
// A ZERO EXTENT IS NOT AN ERROR AND NOT A MEASUREMENT. sl_core_types.h documents
// `extent` as "the area of the tagged resource to use (if using the entire
// resource leave as null)", and it is a value member defaulting to all-zero. So
// a title tagging whole resources yields 0 from the extent -- and TagSize then
// reads the Resource's own declared size, which is the in-band unknown fl_shm.h
// already defines for renderW/H when the title left that zero too. Reported as
// not-found rather than as a resolution of zero.
inline ScanResult FindScalingInputExtent(const sl::BaseStructure** inputs, uint32_t numInputs) noexcept {
    ScanResult out;
    if (inputs == nullptr || numInputs == 0) {
        return out;
    }

    const uint32_t n = numInputs < kMaxInputs ? numInputs : kMaxInputs;
    for (uint32_t i = 0; i < n; ++i) {
        // A null element mid-array is skipped rather than ending the walk: the
        // tag we want may be after it. The chain budget is per element, so one
        // long chain cannot exhaust the allowance for the rest.
        //
        // THE DEPTH CAP IS THE CYCLE HANDLING. A self-referential or looping
        // `next` terminates here after kMaxChain hops. No visited-set is needed
        // and none would be affordable: this runs in a hook path, where
        // allocation is forbidden.
        const sl::BaseStructure* s = inputs[i];
        for (uint32_t depth = 0; s != nullptr && depth < kMaxChain; ++depth, s = s->next) {
            // TWO THINGS ARE WANTED, so this no longer returns on the first hit:
            // the extent and the quality arrive in different structures and a
            // title may chain them in either order. Returning early on the
            // extent would have made quality depend on chain order, which is a
            // property of the title rather than of what it is doing.
            if (IsReadableResourceTag(s)) {
                const auto* tag = static_cast<const sl::ResourceTag*>(s);
                out.tagTypes |= TagTypeBit(tag->type);
                if (tag->type == sl::kBufferTypeScalingInputColor && !out.found) {
                    uint32_t w = 0;
                    uint32_t h = 0;
                    if (TagSize(*tag, w, h)) {
                        out.renderW = static_cast<uint16_t>(w);
                        out.renderH = static_cast<uint16_t>(h);
                        out.found = true;
                    }
                    // Otherwise a whole-resource tag whose Resource states no size,
                    // or a size that cannot be a render target. Keep looking: a title
                    // may chain several tags and only one of them carries a size.
                }
            } else if (IsReadable<sl::DLSSOptions>(s) && !out.qualityFound) {
                // `mode` is the FIRST member of DLSSOptions and has been since
                // kStructVersion1, so it is readable at any version at or above
                // the one these headers describe -- which is what makes matching
                // on `>=` correct here rather than optimistic. The struct is
                // kStructVersion3 today and grew by appending.
                out.quality = QualityFromMode(static_cast<const sl::DLSSOptions*>(s)->mode);
                out.qualityFound = true;
            }
        }
    }
    return out;
}

}    // namespace fl::slinputs

#endif    // FRAMELEDGER_FL_SL_INPUTS_H
