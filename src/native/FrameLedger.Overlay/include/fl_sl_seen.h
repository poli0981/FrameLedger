// The one word `RecordPresent` drains to learn what Streamline did since the last
// present -- WHICH features ran, and HOW MANY times frame generation ran.
//
// WHY A COUNT AND NOT A SECOND BIT. `03_METRICS` needs `fgEvaluations`, and a bit
// collapses two evaluations between two presents into one. Under multi-frame
// generation that is the common case rather than the edge: measured on Cyberpunk
// 2077, 10,169 presents carried only 2,461 Streamline batches, so a bit would have
// reported one evaluation where four presents had gone out.
//
// WHY ONE WORD AND NOT TWO. `RecordPresent` consumes with a single
// `exchange(0)` (dllmain.cpp), so everything it needs must arrive atomically
// together. Two atomics could be read with the features from one frame beside the
// count from another -- and the count is the denominator of `fg_factor`, which is
// the number CLAUDE.md rule 6 exists to protect.
//
// WHY A HEADER AND NOT A STATIC IN dllmain.cpp, which is the same argument
// fl_sl_inputs.h makes beside it: `hook-harness` calls these directly and drives
// saturation and the field boundaries in microseconds (ctest `fl_sl_seen`). A
// helper buried in the injected DLL's anonymous namespace can only be reached by
// injecting it.
//
// NO ATOMIC IN HERE, deliberately. This file owns the ENCODING; dllmain.cpp owns
// the `std::atomic<uint32_t>` and the memory orders. Keeping them apart is what
// lets the encoding be tested without a hook, a process, or a game.

#ifndef FRAMELEDGER_FL_SL_SEEN_H
#define FRAMELEDGER_FL_SL_SEEN_H

#include <cstdint>

namespace fl::slseen {

// The split. Features in the low byte, the frame-generation count above it.
//
// kFeatureMask IS DERIVED FROM kCountShift, and that is not tidiness -- it is the
// one defect this layout can have. Written as a literal (`0x1Fu` for today's five
// FlSlSeen bits) it silently swallows the NEXT enumerator: an XeSS or FSR identity
// bit at 1 << 5 would be set by the hook, masked off by Features(), and both the
// params gate and the Ray-Reconstruction OBSERVED gate in RecordPresent -- which
// test the feature field for zero -- would go false. Render resolution would stop
// being published and RR would start reporting a fabricated answer, on a title
// nobody had changed anything about. Derived, the free bits are genuinely free.
inline constexpr uint32_t kCountShift = 8;
inline constexpr uint32_t kFeatureMask = (1u << kCountShift) - 1u;
inline constexpr uint32_t kCountOne = 1u << kCountShift;

// The largest count the field can hold: 2^24 - 1.
inline constexpr uint32_t kCountMax = (0xFFFFFFFFu >> kCountShift);

// Which Streamline features were evaluated since the last present.
constexpr uint32_t Features(uint32_t word) noexcept {
    return word & kFeatureMask;
}

// How many kFeatureDLSS_G evaluations arrived in the same interval.
constexpr uint32_t FgEvals(uint32_t word) noexcept {
    return word >> kCountShift;
}

// The record's byte, saturated. `fl_shm.h` narrowed fgEvaluations to uint8 because
// DLSS-G is 1 per application frame and multi-frame generation does not multiply
// that -- the count is of EVALUATIONS, not of generated frames (the 2026-08-14
// owner ruling; see 03_METRICS §Frame Generation).
//
// SATURATE, NEVER WRAP, and the direction matters. A wrapped count reads LOW, and
// `fg_factor = presents / Σ evaluations` divides by it -- so a wrap inflates the
// factor without bound, which is the failure mode rule 6 exists to forbid. 255 is
// therefore a sentinel as much as a value: no configuration evaluates frame
// generation 255 times between two presents, so a consumer seeing it must refuse
// to publish a factor rather than divide by a floor.
constexpr uint8_t SaturateToByte(uint32_t evals) noexcept {
    return static_cast<uint8_t>(evals > 255u ? 255u : evals);
}

// The two fields do not overlap, asserted at COMPILE TIME rather than by a test.
//
// A runtime probe for this would have to be a 16-million-iteration loop, and it
// could only ever confirm integer arithmetic: a carry cannot propagate DOWNWARD
// out of bit kCountShift for any shift at all. What can actually be wrong is the
// two constants disagreeing, and that is exactly what a static_assert catches --
// in every translation unit that includes this file, with no test to remember to
// run. ctest `fl_sl_seen` covers the parts that are behaviour rather than algebra.
static_assert(Features(kCountOne) == 0u, "one evaluation must not touch the feature field");
static_assert(FgEvals(kFeatureMask) == 0u, "the feature field must not be read as a count");
static_assert(Features(kFeatureMask) == kFeatureMask, "every feature bit must survive Features()");
static_assert((kFeatureMask & (kCountOne - 1u)) == kFeatureMask, "the fields must be adjacent and disjoint");
static_assert(kCountMax == 0x00FFFFFFu, "the count field is 24 bits");
static_assert(SaturateToByte(kCountMax) == 255u, "a saturating count must not wrap the record byte");

}    // namespace fl::slseen

#endif    // FRAMELEDGER_FL_SL_SEEN_H
