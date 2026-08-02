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
    // Must equal the number of enumerators in fl::guard::Reason. Asserted below
    // against the first name that would be missing if one were added.
    return 17;
}

}    // extern "C"

// If a Reason is added without bumping the count, this fires at compile time
// rather than at the managed mirror test — the earlier the better, since the
// count is what the mirror test iterates.
static_assert(static_cast<int>(fl::guard::Reason::kRulesIncomplete) == 16,
              "fl::guard::Reason gained or lost a value — update FlGuardReasonCount and the managed mirror");
