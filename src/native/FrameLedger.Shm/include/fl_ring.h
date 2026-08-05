// The SPSC frame ring — writer, and a reader for tests and probes.
//
// NORMATIVE SOURCE: docs/07_IPC.md §Protocol rules. This header implements that
// sequence and nothing else; where the two disagree, the document wins and this
// file is the bug.
//
// Header-only for the same reason fl_shm.h is: the writer runs inside a game
// process and the reader outside it, so there must be exactly one definition and
// no link-time dependency between the two.
//
// THE PRODUCTION READER IS C# (docs/04_CAPTURE.md §Ring draining, P2). The reader
// here exists so the Catch2 suite and the native probes can drive a real writer
// end to end. It is deliberately the same protocol rather than a convenient
// approximation: a test reader that skipped the second `seq` load would pass
// against a writer that never published a torn record, which is precisely the
// case the suite exists to force.
//
// HOT-PATH RULES (docs/17_HOOK_ENGINE.md §Ring writer, NFR-1): Publish() performs
// one QPC-free 60-byte store, two relaxed atomic stores and two fences. No
// syscall, no allocation, no lock, no logging, no throwing STL. -D_HAS_EXCEPTIONS=0
// makes a would-be throw an uncatchable __fastfail, so "no throwing STL in hook
// paths" is load-bearing rather than stylistic.

#ifndef FRAMELEDGER_FL_RING_H
#define FRAMELEDGER_FL_RING_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fl_shm.h"

namespace fl {

// Payload bytes are [0,56) and [60,64). `seq` at 56 is the guard and must never
// be part of the payload write -- "fill 64 bytes" would overwrite the very field
// protecting the write (07_IPC).
inline constexpr std::size_t kSeqOffset = offsetof(FlFrameRecord, seq);
inline constexpr std::size_t kSeqSize = sizeof(std::uint32_t);
inline constexpr std::size_t kTailOffset = kSeqOffset + kSeqSize;
inline constexpr std::size_t kTailSize = sizeof(FlFrameRecord) - kTailOffset;

static_assert(kSeqOffset == 56, "07_IPC pins seq at 56");
static_assert(kTailOffset == 60 && kTailSize == 4, "swapchainId occupies [60,64)");

[[nodiscard]] inline bool IsPowerOfTwo(std::uint32_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

// ---------------------------------------------------------------------------
// Writer — one per mapping, single producer.
// ---------------------------------------------------------------------------
class RingWriter {
public:
    // `base` is the start of the mapping, i.e. the handshake block.
    [[nodiscard]] bool Init(void* base, std::uint32_t capacity) noexcept {
        if (base == nullptr || !IsPowerOfTwo(capacity)) {
            return false;
        }
        auto* bytes = static_cast<unsigned char*>(base);
        state_ = reinterpret_cast<FlWriterState*>(bytes + FL_SHM_WRITER_OFFSET);
        ring_ = reinterpret_cast<FlFrameRecord*>(bytes + FL_SHM_RING_OFFSET);
        mask_ = capacity - 1;
        return true;
    }

    [[nodiscard]] bool Ready() const noexcept { return ring_ != nullptr; }

    // The hot path. Overwrite-oldest: the writer never consults a read index,
    // because it does not have one -- drop accounting belongs to the reader,
    // which is the only side that knows what was consumed (07_IPC).
    void Publish(const FlFrameRecord& rec) noexcept {
        const std::uint64_t idx = next_++;
        FlFrameRecord*      slot = &ring_[idx & mask_];

        std::atomic_ref<std::uint32_t> seq{slot->seq};

        // Sole writer of seq, so a relaxed read of our own last value is exact.
        // MONOTONIC AND NEVER RESET: a reader stalled exactly one full lap must
        // not validate a different frame as unchanged (14_TESTING §Native unit
        // tests, "the one-full-lap case").
        const std::uint32_t s = seq.load(std::memory_order_relaxed);
        seq.store(s + 1, std::memory_order_relaxed);    // odd => writing
        std::atomic_thread_fence(std::memory_order_release);

        // The payload, in two spans that step over seq.
        auto*       dst = reinterpret_cast<unsigned char*>(slot);
        const auto* src = reinterpret_cast<const unsigned char*>(&rec);
        std::memcpy(dst, src, kSeqOffset);
        std::memcpy(dst + kTailOffset, src + kTailOffset, kTailSize);

        std::atomic_thread_fence(std::memory_order_release);
        seq.store(s + 2, std::memory_order_relaxed);    // even => complete

        std::atomic_ref<std::uint64_t> wi{state_->writeIndex};
        wi.store(idx + 1, std::memory_order_release);
    }

    [[nodiscard]] std::uint64_t PublishedCount() const noexcept { return next_; }

private:
    FlWriterState* state_ = nullptr;
    FlFrameRecord* ring_ = nullptr;
    std::uint32_t  mask_ = 0;
    std::uint64_t  next_ = 0;
};

// ---------------------------------------------------------------------------
// Reader — test/probe side. Mirrors what 04_CAPTURE specifies for the Agent.
// ---------------------------------------------------------------------------
struct DrainResult {
    std::uint32_t copied = 0;     // records accepted
    std::uint32_t gaps = 0;       // torn slots: a DATA GAP, never a skipped frame
    std::uint64_t dropped = 0;    // records overwritten before we consumed them
};

class RingReader {
public:
    [[nodiscard]] bool Init(const void* base, std::uint32_t capacity) noexcept {
        if (base == nullptr || !IsPowerOfTwo(capacity)) {
            return false;
        }
        const auto* bytes = static_cast<const unsigned char*>(base);
        state_ = reinterpret_cast<const FlWriterState*>(bytes + FL_SHM_WRITER_OFFSET);
        ring_ = reinterpret_cast<const FlFrameRecord*>(bytes + FL_SHM_RING_OFFSET);
        capacity_ = capacity;
        mask_ = capacity - 1;
        return true;
    }

    // Copies up to `max` records into `out`. `gapAt` receives the ring indices
    // whose record was torn, because 07_IPC and 03_METRICS both require a gap to
    // be RECORDED: dropping it silently merges two frame times into one
    // double-length interval, i.e. fabricates a stutter in the metric the
    // product exists to report honestly.
    DrainResult Drain(FlFrameRecord* out, std::uint32_t max, std::uint64_t* gapAt = nullptr,
                      std::uint32_t maxGaps = 0) noexcept {
        DrainResult r{};
        if (ring_ == nullptr || out == nullptr) {
            return r;
        }

        std::atomic_ref<const std::uint64_t> wi{state_->writeIndex};
        const std::uint64_t                  writeIndex = wi.load(std::memory_order_acquire);

        // Overwrite-oldest accounting, owned here because only the reader knows
        // what it consumed: everything older than writeIndex - capacity is gone.
        if (writeIndex > read_ + capacity_) {
            r.dropped = (writeIndex - read_) - capacity_;
            read_ = writeIndex - capacity_;
        }

        while (read_ < writeIndex && r.copied < max) {
            const FlFrameRecord* slot = &ring_[read_ & mask_];

            std::atomic_ref<const std::uint32_t> seq{slot->seq};
            const std::uint32_t                  before = seq.load(std::memory_order_acquire);
            if ((before & 1u) != 0u) {    // odd => a write is in flight
                if (gapAt != nullptr && r.gaps < maxGaps) {
                    gapAt[r.gaps] = read_;
                }
                ++r.gaps;
                ++read_;
                continue;
            }

            std::memcpy(&out[r.copied], slot, sizeof(FlFrameRecord));
            std::atomic_thread_fence(std::memory_order_acquire);

            // Accept only if the slot did not change under the copy. An unequal
            // value means the writer lapped us or overwrote mid-copy; either way
            // the bytes we hold are not one frame.
            if (seq.load(std::memory_order_relaxed) != before) {
                if (gapAt != nullptr && r.gaps < maxGaps) {
                    gapAt[r.gaps] = read_;
                }
                ++r.gaps;
                ++read_;
                continue;
            }

            ++r.copied;
            ++read_;
        }
        return r;
    }

    [[nodiscard]] std::uint64_t ReadIndex() const noexcept { return read_; }

private:
    const FlWriterState* state_ = nullptr;
    const FlFrameRecord* ring_ = nullptr;
    std::uint32_t        capacity_ = 0;
    std::uint32_t        mask_ = 0;
    std::uint64_t        read_ = 0;
};

}    // namespace fl

#endif    // FRAMELEDGER_FL_RING_H
