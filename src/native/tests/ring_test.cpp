// The SPSC frame ring (docs/14_TESTING.md §Native unit tests, the suite
// src/native/tests/CMakeLists.txt has carried as TODO(P1) since Catch2 landed).
//
// Every case here forces a property docs/07_IPC.md §Protocol rules states.
//
// WHAT IS PROVEN RED, AND WHAT IS NOT. Four canaries were run against the writer
// and reader (2026-08-05); the suite must be read as covering only what actually
// went red, because a property whose red path has never been exercised is
// unverified however many assertions surround it.
//
//   PROVEN RED
//     - `seq` reset on every write  ->  the one-full-lap case fails.
//     - reader ignores the in-flight (odd) bit  ->  the torn-record case fails.
//
//   NOT PROVEN RED — these two properties are currently UNVERIFIED
//     - writer memcpy's all 64 bytes instead of stepping over `seq`.
//     - reader skips the second `seq` load and never revalidates.
//
// Both survive because the damage is only observable inside the write window,
// which is a 64-byte memcpy — nanoseconds — and neither the 1024-slot nor the
// 8-slot concurrency case lands in it reliably even across 200,000 records. The
// single-threaded cases structurally cannot see it: the trailing
// seq.store(s + 2) tidies the clobbered value away, so the final state of a
// correct writer and a 64-byte-copying one is identical.
//
// Recorded rather than papered over. Closing it needs either a seam that widens
// the window under test, or a dedicated sampling thread that only reads `seq` —
// and until one exists, "the payload write steps over seq" rests on review, not
// on this suite. The poisoned kPoisonSeq below and the tiny-ring case were both
// added trying to close it and did not; they are kept because they are the right
// shape and would catch a wider window.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <fl_ring.h>
#include <thread>
#include <vector>

using namespace fl;

namespace {

constexpr std::uint32_t kCap = 16;    // small on purpose: wrap is the interesting case

// A mapping, minus the mapping. The protocol does not care that this is heap
// rather than a section object, and using heap keeps the suite CI-runnable.
struct Mapping {
    std::vector<unsigned char> bytes;

    explicit Mapping(std::uint32_t capacity) : bytes(FlShmSizeForCapacity(capacity), 0) {}
    void*          base() { return bytes.data(); }
    const void*    base() const { return bytes.data(); }
    FlFrameRecord* slot(std::uint32_t i) {
        return reinterpret_cast<FlFrameRecord*>(bytes.data() + FL_SHM_RING_OFFSET) + i;
    }
    FlWriterState* writer() { return reinterpret_cast<FlWriterState*>(bytes.data() + FL_SHM_WRITER_OFFSET); }
};

// A poisoned, EVEN seq in every source record.
//
// This is what makes "the payload write steps over seq" detectable at all. A
// single-threaded check cannot see the difference: a writer that memcpy'd all 64
// bytes would drop this value into the slot mid-window and then the trailing
// seq.store(s + 2) would tidy it away, so the final state is identical either
// way. The observable damage is to a CONCURRENT reader — during the write window
// the slot's seq reads as this EVEN value instead of an odd one, so the reader
// believes no write is in flight, copies a half-written record, re-reads the same
// even value and accepts it.
//
// Even on purpose: an odd poison would be caught by the in-flight test and prove
// nothing.
constexpr std::uint32_t kPoisonSeq = 0xEEEE0000u;

FlFrameRecord MakeRecord(std::uint32_t index) {
    FlFrameRecord r{};
    r.frameIndex = index;
    r.qpc = 1000ULL + index;
    r.swapchainId = 0xABCD0000u | (index & 0xFFFFu);
    r.seq = kPoisonSeq;
    // What a present-only writer is entitled to claim, and nothing more.
    // rtFlags = 0 is the v3 honest value: the bits are *_OBSERVED, so zero says
    // "no RT evidence seen" and FL_MEASURED_RT (unset) says nobody looked.
    r.measuredMask = static_cast<uint16_t>(FL_MEASURED_OUTPUT_RES | FL_MEASURED_PRESENT_ARGS);
    r.rtFlags = 0u;
    return r;
}

}    // namespace

TEST_CASE("a published record round-trips through the reader", "[ring]") {
    Mapping    m{kCap};
    RingWriter w;
    RingReader rd;
    REQUIRE(w.Init(m.base(), kCap));
    REQUIRE(rd.Init(m.base(), kCap));

    w.Publish(MakeRecord(7));

    FlFrameRecord     out[kCap]{};
    const DrainResult r = rd.Drain(out, kCap);
    REQUIRE(r.copied == 1);
    CHECK(r.gaps == 0);
    CHECK(r.dropped == 0);
    CHECK(out[0].frameIndex == 7);
    CHECK(out[0].qpc == 1007);
    CHECK(out[0].swapchainId == (0xABCD0000u | 7u));
}

TEST_CASE("the tail span [60,64) survives the payload write", "[ring]") {
    // swapchainId lives after `seq`. A writer that memcpy'd [0,56) and stopped
    // would leave it zero, and the Agent must read 0 as "unidentified" -- so this
    // case is what stops a silent loss of the discriminator #36 added.
    Mapping    m{kCap};
    RingWriter w;
    RingReader rd;
    REQUIRE(w.Init(m.base(), kCap));
    REQUIRE(rd.Init(m.base(), kCap));

    FlFrameRecord rec = MakeRecord(1);
    rec.swapchainId = 0x5A5A5A5Au;
    w.Publish(rec);

    FlFrameRecord out[kCap]{};
    REQUIRE(rd.Drain(out, kCap).copied == 1);
    CHECK(out[0].swapchainId == 0x5A5A5A5Au);
}

TEST_CASE("the payload write never touches seq", "[ring]") {
    // The record handed to Publish carries a poisoned seq. If the writer filled
    // 64 bytes it would land in the slot and the sequence would go wrong -- which
    // is the whole reason 07_IPC splits the write into two spans.
    Mapping    m{kCap};
    RingWriter w;
    REQUIRE(w.Init(m.base(), kCap));

    FlFrameRecord rec = MakeRecord(1);
    rec.seq = 0xDEADBEEFu;
    w.Publish(rec);

    // Slot 0, not slot 1: the slot is chosen by the PUBLISH COUNTER, which starts
    // at zero, and has nothing to do with the record's frameIndex. Written out
    // because the first draft of this case asserted on slot 1 and failed against
    // a correct writer -- the test was wrong, and a reader of it should not have
    // to rediscover why.
    //
    // First write of a zeroed slot: 0 -> 1 (writing) -> 2 (complete). Had the
    // writer filled all 64 bytes, 0xDEADBEEF would be here instead.
    CHECK(m.slot(0)->seq == 2u);
}

TEST_CASE("seq is monotonic per slot and never reset across laps", "[ring]") {
    Mapping    m{kCap};
    RingWriter w;
    REQUIRE(w.Init(m.base(), kCap));

    for (std::uint32_t i = 0; i < kCap * 3; ++i) {
        w.Publish(MakeRecord(i));
    }
    // Slot 0 was written three times: 2, 4, 6.
    CHECK(m.slot(0)->seq == 6u);
    CHECK(m.writer()->writeIndex == kCap * 3);
}

TEST_CASE("a torn record is reported as a GAP, not silently skipped", "[ring]") {
    // 07_IPC: dropping a torn record merges two frame times into one
    // double-length interval, i.e. manufactures a stutter. Forced by leaving a
    // slot's seq ODD, which is exactly the state a half-finished write leaves.
    Mapping    m{kCap};
    RingWriter w;
    RingReader rd;
    REQUIRE(w.Init(m.base(), kCap));
    REQUIRE(rd.Init(m.base(), kCap));

    w.Publish(MakeRecord(0));
    w.Publish(MakeRecord(1));
    w.Publish(MakeRecord(2));
    m.slot(1)->seq |= 1u;    // slot 1 now reads as write-in-flight

    FlFrameRecord     out[kCap]{};
    std::uint64_t     gaps[kCap]{};
    const DrainResult r = rd.Drain(out, kCap, gaps, kCap);

    CHECK(r.copied == 2);
    REQUIRE(r.gaps == 1);
    CHECK(gaps[0] == 1);    // the ring index, so the Agent can place the gap
    CHECK(out[0].frameIndex == 0);
    CHECK(out[1].frameIndex == 2);
}

TEST_CASE("overwrite-oldest: drops are counted by the READER and it resumes correctly", "[ring]") {
    // The writer has no read index and cannot know what was consumed, so it has
    // no drop counter by design (fl_shm.h FlWriterState). This is the reader-side
    // computation 14_TESTING names.
    Mapping    m{kCap};
    RingWriter w;
    RingReader rd;
    REQUIRE(w.Init(m.base(), kCap));
    REQUIRE(rd.Init(m.base(), kCap));

    const std::uint32_t published = kCap + 5;
    for (std::uint32_t i = 0; i < published; ++i) {
        w.Publish(MakeRecord(i));
    }

    FlFrameRecord     out[kCap]{};
    const DrainResult r = rd.Drain(out, kCap);

    CHECK(r.dropped == 5);
    REQUIRE(r.copied == kCap);
    // Resumes at writeIndex - capacity, so the OLDEST surviving record is first.
    CHECK(out[0].frameIndex == 5);
    CHECK(out[kCap - 1].frameIndex == published - 1);
}

TEST_CASE("a reader stalled exactly ONE FULL LAP does not accept a stale frame", "[ring]") {
    // The case the never-reset seq defends (14_TESTING). If seq were reset per
    // lap, or were a per-slot boolean, the second occupant of slot 0 would be
    // indistinguishable from the first and the reader would accept a frame it
    // had already consumed as new -- silently, with a plausible frameIndex.
    Mapping    m{kCap};
    RingWriter w;
    RingReader rd;
    REQUIRE(w.Init(m.base(), kCap));
    REQUIRE(rd.Init(m.base(), kCap));

    for (std::uint32_t i = 0; i < kCap; ++i) {
        w.Publish(MakeRecord(i));
    }
    FlFrameRecord out[kCap]{};
    REQUIRE(rd.Drain(out, kCap).copied == kCap);
    const std::uint32_t seqAfterFirstLap = m.slot(0)->seq;

    for (std::uint32_t i = kCap; i < kCap * 2; ++i) {
        w.Publish(MakeRecord(i));
    }
    // Same slot, DIFFERENT seq — which is what makes the second lap legible.
    CHECK(m.slot(0)->seq != seqAfterFirstLap);
    CHECK(m.slot(0)->seq == seqAfterFirstLap + 2);

    const DrainResult second = rd.Drain(out, kCap);
    REQUIRE(second.copied == kCap);
    CHECK(out[0].frameIndex == kCap);    // the SECOND occupant, not the first
}

TEST_CASE("SPSC under a hammering writer: every accepted record is internally consistent", "[ring]") {
    // The real concurrency case. The reader may legitimately miss records
    // (overwrite-oldest) and may see torn ones; what it must NEVER do is accept a
    // record assembled from two different frames. qpc == 1000 + frameIndex by
    // construction, so a mixed record is detectable without locking anything.
    constexpr std::uint32_t kBigCap = 1024;
    constexpr std::uint32_t kTotal = 200000;

    Mapping    m{kBigCap};
    RingWriter w;
    RingReader rd;
    REQUIRE(w.Init(m.base(), kBigCap));
    REQUIRE(rd.Init(m.base(), kBigCap));

    std::atomic<bool> done{false};
    std::thread       producer([&] {
        for (std::uint32_t i = 0; i < kTotal; ++i) {
            w.Publish(MakeRecord(i));
        }
        done.store(true, std::memory_order_release);
    });

    std::vector<FlFrameRecord> buf(kBigCap);
    std::uint64_t              accepted = 0;
    std::uint64_t              torn = 0;
    bool                       mixed = false;

    while (!done.load(std::memory_order_acquire) || rd.ReadIndex() < m.writer()->writeIndex) {
        const DrainResult r = rd.Drain(buf.data(), kBigCap);
        for (std::uint32_t i = 0; i < r.copied; ++i) {
            if (buf[i].qpc != 1000ULL + buf[i].frameIndex) {
                mixed = true;
            }
            if (buf[i].swapchainId != (0xABCD0000u | (buf[i].frameIndex & 0xFFFFu))) {
                mixed = true;    // the tail span belongs to the same frame as the head
            }
        }
        accepted += r.copied;
        torn += r.gaps;
    }
    producer.join();

    INFO("accepted " << accepted << ", torn " << torn << " of " << kTotal);
    CHECK_FALSE(mixed);
    // Not a vacuous run: the reader must actually have seen most of the stream.
    CHECK(accepted > kTotal / 2);
}

TEST_CASE("a tiny ring, so the writer laps the reader constantly", "[ring]") {
    // The case that exercises the SECOND seq read. With 1024 slots the reader
    // keeps up and the re-read never fires — measured: disarming it left the
    // suite green. Eight slots against an unthrottled writer guarantees mid-copy
    // overwrites, which is the only thing that can distinguish a reader that
    // validates after copying from one that does not.
    //
    // Also the case that proves the poisoned seq matters: a writer filling all 64
    // bytes leaves kPoisonSeq — an EVEN value — visible during the write window,
    // and a reader has no way to tell that from a completed record.
    constexpr std::uint32_t kTiny = 8;
    constexpr std::uint32_t kTotal = 200000;

    Mapping    m{kTiny};
    RingWriter w;
    RingReader rd;
    REQUIRE(w.Init(m.base(), kTiny));
    REQUIRE(rd.Init(m.base(), kTiny));

    std::atomic<bool> done{false};
    std::thread       producer([&] {
        for (std::uint32_t i = 0; i < kTotal; ++i) {
            w.Publish(MakeRecord(i));
        }
        done.store(true, std::memory_order_release);
    });

    FlFrameRecord buf[kTiny]{};
    std::uint64_t accepted = 0;
    std::uint64_t dropped = 0;
    bool          mixed = false;
    bool          poisonAccepted = false;

    while (!done.load(std::memory_order_acquire) || rd.ReadIndex() < m.writer()->writeIndex) {
        const DrainResult r = rd.Drain(buf, kTiny);
        for (std::uint32_t i = 0; i < r.copied; ++i) {
            if (buf[i].qpc != 1000ULL + buf[i].frameIndex ||
                buf[i].swapchainId != (0xABCD0000u | (buf[i].frameIndex & 0xFFFFu))) {
                mixed = true;
            }
            // An accepted record's seq is the seqlock counter and must be even.
            // kPoisonSeq here means the payload write reached the guard field.
            if (buf[i].seq == kPoisonSeq || (buf[i].seq & 1u) != 0u) {
                poisonAccepted = true;
            }
        }
        accepted += r.copied;
        dropped += r.dropped;
    }
    producer.join();

    INFO("accepted " << accepted << ", dropped " << dropped << " of " << kTotal);
    CHECK_FALSE(mixed);
    CHECK_FALSE(poisonAccepted);
    // Not vacuous in the other direction either: with 8 slots the writer MUST
    // have lapped us, so a run reporting no drops means the threads never
    // actually overlapped and the case proved nothing.
    CHECK(dropped > 0);
}

TEST_CASE("Init refuses a capacity that is not a power of two", "[ring]") {
    Mapping    m{kCap};
    RingWriter w;
    CHECK_FALSE(w.Init(m.base(), 15));
    CHECK_FALSE(w.Init(nullptr, kCap));
    CHECK(w.Init(m.base(), kCap));
}
