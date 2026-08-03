#include <cstring>
#include <fl_guard.h>
#include <fl_guard_abi.h>

namespace {

void Fill(FlGuardResult* out, const fl::guard::Verdict& v) noexcept {
    if (out == nullptr) {
        return;
    }
    out->reason = static_cast<std::int32_t>(v.reason);
    strncpy_s(out->family, sizeof(out->family), v.family, _TRUNCATE);
    strncpy_s(out->signal, sizeof(out->signal), v.signal, _TRUNCATE);
}

// A null out-pointer is a caller bug, not a reason to proceed. We cannot report
// a verdict to somebody who gave us nowhere to put it, so nothing runs.
bool Usable(FlGuardResult* out) noexcept {
    return out != nullptr;
}

}    // namespace

extern "C" {

void FlGuardEvaluate(std::uint32_t targetPid, FlGuardResult* out) {
    if (!Usable(out)) {
        return;
    }
    Fill(out, fl::guard::Evaluate(targetPid));
}

void FlGuardedInject(std::uint32_t targetPid, const wchar_t* dllPath, FlGuardResult* out) {
    if (!Usable(out)) {
        return;
    }
    Fill(out, fl::guard::GuardedInject(targetPid, dllPath));
}

const char* FlGuardReasonName(std::int32_t reason) {
    if (reason < 0 || reason >= FlGuardReasonCount()) {
        // Not "Unknown" — an out-of-range code means the two sides have drifted,
        // and the mirror test exists to catch that. Returning a plausible string
        // would hide it.
        return "";
    }
    return fl::guard::ReasonName(static_cast<fl::guard::Reason>(reason));
}

std::int32_t FlGuardReasonCount(void) {
    // DERIVED, never restated. This used to return a literal 17 guarded by a
    // static_assert on kRulesIncomplete == 16 — but kRulesIncomplete was the
    // last enumerator, so the one change the assert existed to catch (appending
    // a Reason) left it at 16 and the assert passed while the count went stale.
    // The managed mirror test iterates this value, so a stale count means the
    // new reason is never compared against the managed enum either.
    return static_cast<std::int32_t>(fl::guard::Reason::kCount);
}

}    // extern "C"

// kAllow must stay 0: a default-constructed managed AntiCheatVerdict zeroes
// every field, and the mirror only holds if 0 means the same thing on both
// sides. fl_guard.h asserts this too; repeated here because this file is what
// the managed side actually talks to.
static_assert(static_cast<int>(fl::guard::Reason::kAllow) == 0,
              "kAllow must stay 0 — the managed mirror depends on it");
static_assert(static_cast<int>(fl::guard::Reason::kCount) > 0, "Reason::kCount must be the last enumerator");
