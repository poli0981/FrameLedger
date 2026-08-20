// The arithmetic behind dispatchRaysVolume, in a header so it can be checked
// without a hook.
//
// WHY IT IS NOT IN dllmain.cpp. Every branch here is reached by multiplying three
// numbers a CALLER filled in, and the failure mode is silent: a wrapped product is
// a small number that reads like a measurement. fl_sl_seen.h is the precedent --
// the word RecordPresent drains has its field algebra as static_asserts in a
// header, checked in every translation unit that includes it, and its BEHAVIOUR in
// `ctest fl_sl_seen`. Same split here: the identities below cannot be forgotten by
// a test, and hook-harness --probe-dxr-inputs drives the shapes a game could send.
//
// WHY SATURATION GOES HIGH. dispatchRaysVolume is the NUMERATOR of 03_METRICS'
// rays_per_pixel, so a wrapped value under-reports the one input the path-tracing
// heuristic actually reads -- and it does so most on exactly the titles the
// heuristic exists to recognise, since those dispatch the most rays. Saturating at
// UINT32_MAX makes the failure visible and one-directional; a consumer seeing the
// ceiling must refuse to publish rays_per_pixel rather than divide by a floor.
// This is the mirror of fgEvaluations' 255, which saturates for the opposite
// reason: that one is a DENOMINATOR, and a wrap there inflates without bound.

#ifndef FRAMELEDGER_FL_RT_ACCUM_H
#define FRAMELEDGER_FL_RT_ACCUM_H

#include <cstdint>

namespace fl::rtaccum {

// The value fl_shm.h's uint32 dispatchRaysVolume saturates at.
inline constexpr uint32_t kVolumeMax = 0xFFFFFFFFu;

// a*b without wrapping. UINT64_MAX is the ceiling, not a valid product.
constexpr uint64_t MulSaturating(uint64_t a, uint64_t b) noexcept {
    if (a == 0u || b == 0u) {
        return 0u;
    }
    return a > (UINT64_MAX / b) ? UINT64_MAX : a * b;
}

// W*H*D, in 64 bits, saturating.
//
// THE THIRD MULTIPLY IS THE ONE THAT CAN OVERFLOW, and the first draft of this
// comment said the first one could -- corrected by the static_assert below, which
// is the entire reason it is written as an assertion rather than as prose. Two
// UINT32s multiply to at most (2^32-1)^2 = 2^64 - 2^33 + 1, which FITS in a
// uint64 with room to spare. Bring in a third and it does not. D3D12 will never
// legally present such a descriptor; a corrupted or hostile one is not bound by
// that, and this runs inside somebody's game.
constexpr uint64_t VolumeOf(uint32_t w, uint32_t h, uint32_t d) noexcept {
    return MulSaturating(MulSaturating(w, h), d);
}

// `cur + add`, clamped into the record's 32-bit field. Pure, so the atomic
// compare-exchange loop that applies it holds no arithmetic of its own.
//
// THE FIRST BRANCH IS NOT DEFENSIVE, IT IS A BUG FIX THE ASSERTIONS BELOW FOUND.
// Without it, `cur + add` with `add` near UINT64_MAX -- which VolumeOf returns for
// a saturated product -- WRAPS a uint64 to a small number, the comparison passes,
// and the accumulator is assigned a plausible low value. That is the exact failure
// this whole header exists to prevent, reached inside the function meant to
// prevent it. Caught by `AddedTo(1, UINT64_MAX)` at compile time, before it ever
// ran anywhere.
constexpr uint32_t AddedTo(uint32_t cur, uint64_t add) noexcept {
    if (add >= kVolumeMax) {
        return kVolumeMax;
    }
    const uint64_t sum = static_cast<uint64_t>(cur) + add;    // both operands <= kVolumeMax: cannot wrap
    return sum > kVolumeMax ? kVolumeMax : static_cast<uint32_t>(sum);
}

// The identities, checked in every TU that includes this header.
static_assert(VolumeOf(0u, 0u, 0u) == 0u, "no rays is zero, not a saturated value");
static_assert(VolumeOf(1u, 1u, 1u) == 1u, "the trivial dispatch counts as one ray");
static_assert(VolumeOf(3840u, 2160u, 1u) == 8294400u, "one 4K primary-ray dispatch, the fl_shm.h worked example");
static_assert(VolumeOf(0xFFFFFFFFu, 0xFFFFFFFFu, 1u) == 0xFFFFFFFE00000001ull,
              "two UINT32s FIT in a uint64 -- this asserted saturation until the compiler said otherwise");
static_assert(VolumeOf(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu) == UINT64_MAX, "the THIRD dimension is what overflows");
static_assert(VolumeOf(0xFFFFFFFFu, 0u, 0xFFFFFFFFu) == 0u, "a zero dimension is zero however large the others are");

static_assert(AddedTo(0u, 0u) == 0u, "nothing added to nothing");
static_assert(AddedTo(0u, 2048u) == 2048u, "the fixture's own dispatch, once");
static_assert(AddedTo(kVolumeMax, 1u) == kVolumeMax, "SATURATES: one more ray must not wrap to zero");
static_assert(AddedTo(kVolumeMax - 1u, 5u) == kVolumeMax, "and an overshoot lands on the ceiling, not past it");
static_assert(AddedTo(1u, UINT64_MAX) == kVolumeMax, "a saturated 64-bit volume clamps rather than truncating");
static_assert(AddedTo(kVolumeMax, 0u) == kVolumeMax, "already saturated and nothing added stays put");

}    // namespace fl::rtaccum

#endif    // FRAMELEDGER_FL_RT_ACCUM_H
