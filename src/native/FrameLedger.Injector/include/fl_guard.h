// The anti-cheat guard — the hard gate (docs/19_SAFETY_AND_ANTICHEAT.md).
//
// This is the one component where a bug can cost somebody their account
// (docs/14_TESTING.md §Safety-guard tests), and the whole file is shaped by
// three rules that are not negotiable:
//
//   1. EVERY uncertainty is a REFUSAL. "I could not look" and "I looked and it
//      was clean" must never produce the same value. The project has already
//      shipped one defect of exactly that shape — EnumDeviceDrivers returning
//      258 drivers and zero usable names to a standard user, which a guard
//      would have read as "no anti-cheat present" on a machine running
//      Vanguard (docs/spike-notes.md §1).
//   2. NO CLEARANCE ESCAPES. The guard owns the chokepoint: it collects
//      evidence, matches, and calls the injection primitive itself. A token
//      handed to a caller can be ignored — the caller can simply not ask for
//      one. A symbol that does not exist cannot be called (§S8, §S13(b)).
//   3. EVERY INPUT IS A SEAM. Module enumeration, driver enumeration, service
//      queries, the process tree and the rules file all arrive through
//      function pointers, so the Catch2 suite can force each failure the
//      matrix in 14_TESTING requires. An input that cannot be made to fail in
//      a test is an input whose failure path is unverified.
//
// There is deliberately no "check and tell me the answer" public entry point.
// See FlGuardedInject at the bottom.

#ifndef FL_GUARD_H
#define FL_GUARD_H

#include <cstddef>
#include <cstdint>

namespace fl::guard {

// ---------------------------------------------------------------------------
// Verdict
// ---------------------------------------------------------------------------

// Why the guard refused. One value per pre-injection check and per failure
// class, because "refused" alone is not something the UI can explain and
// 19_SAFETY requires the user be told which check fired.
//
// Ordering note: kAllow is 0 ONLY so that a default-constructed Verdict is
// obviously wrong to a reader — it is never produced by the guard except on the
// one path that has actually passed every check. If that ever feels risky,
// change it; nothing depends on the numeric values.
enum class Reason : std::uint8_t {
    kAllow = 0,

    // Check 1 — target module scan.
    kBlockedModule,
    kModuleScanFailed,          // enumeration failed: cannot determine => refuse
    kProcessUnreadable,         // OpenProcess denied: cannot determine => refuse
    kProcessTreeUnavailable,    // could not establish the scan set (§S16)

    // Check 2 — machine-wide driver scan.
    kBlockedDriver,
    kDriverScanFailed,

    // Check 2b — services.
    kBlockedService,
    kServiceQueryFailed,    // DENIED, not ABSENT: cannot determine => refuse

    // Check 3 — per-title rules.
    kBlockedExecutable,
    kBlockedStoreId,

    // Check 4 — static, pre-launch.
    kAntiCheatDirectory,
    kAntiCheatFile,

    // The unknown-but-suspicious heuristic.
    kSuspiciousUnsigned,

    // The rules data itself.
    kRulesUnreadable,
    kRulesMalformed,
    kRulesIncomplete,    // parsed, but a required family is missing

    // NOT A REASON. The count, so appending above it updates the exported
    // FlGuardReasonCount by construction.
    //
    // This replaces a static_assert that could not fire on the change it existed
    // to catch: it pinned kRulesIncomplete == 16, and kRulesIncomplete was the
    // LAST enumerator, so appending a reason left it at 16, the assert passed,
    // the exported count stayed at 17, and the managed mirror test iterated 0-16
    // and never compared the new value at all. Deriving the count removes the
    // need for anyone to remember.
    kCount,
};

// A refusal carries the signal that produced it, so the UI can say
// "EasyAntiCheat.dll was loaded in the target" rather than "blocked".
// Fixed-size: the guard allocates nothing.
struct Verdict {
    Reason reason = Reason::kRulesUnreadable;    // fail-closed default
    char   family[64] = {};                      // e.g. "Easy Anti-Cheat"
    char   signal[260] = {};                     // e.g. "EasyAntiCheat_EOS.dll"

    [[nodiscard]] bool Allowed() const noexcept { return reason == Reason::kAllow; }
};

// The literal default must be a refusal, not an allow. Asserted rather than
// trusted, because a future edit to the enum could silently invert it.
static_assert(static_cast<int>(Reason::kAllow) == 0, "kAllow must stay 0 for the Allowed() check");

// ---------------------------------------------------------------------------
// Evidence sources — the seams (rule 3 above)
// ---------------------------------------------------------------------------

// Every collector returns a tri-state, never a bare list. A bare empty list is
// the exact ambiguity that produced this project's worst defect: it reads as
// "nothing found" when it may mean "could not look".
enum class Collected : std::uint8_t {
    kOk = 0,
    kFailed,        // the call failed outright
    kIncomplete,    // partial result: e.g. WOW64 without LIST_MODULES_ALL
};

// Callbacks receive (context, name) for each item found. Returning false stops
// enumeration early — used when a match has already been found.
using NameSink = bool (*)(void* ctx, const char* name);

struct Sources {
    // Loaded modules of one process, by base name.
    Collected (*EnumerateModules)(std::uint32_t pid, NameSink sink, void* ctx) = nullptr;

    // Machine-wide loaded kernel drivers, by full native path.
    Collected (*EnumerateDrivers)(NameSink sink, void* ctx) = nullptr;

    // A service by name. kOk + present=false means ABSENT (1060), which is a
    // genuine "not there". kFailed means DENIED or anything else, which is
    // "cannot determine" and refuses. That distinction is the whole reason this
    // is not a bool (docs/19_SAFETY §Pre-injection checks item 2).
    Collected (*QueryService)(const char* name, bool* present) = nullptr;

    // The scan set for §S16: the injection target, its descendants, and its
    // ancestors up to but excluding the first known platform launcher.
    Collected (*EnumerateScanSet)(std::uint32_t targetPid, bool (*sink)(void*, std::uint32_t), void* ctx) = nullptr;

    // Whole rules file into a caller-owned buffer. Returns bytes written, or
    // SIZE_MAX on any failure — unreadable, absent, or larger than the cap.
    std::size_t (*ReadRulesFile)(char* buffer, std::size_t cap) = nullptr;
};

// The real Windows implementations. Behaviour measured in spike-notes.md §1;
// notably EnumerateModules reports kFailed on ERROR_PARTIAL_COPY (a suspended
// target) and uses LIST_MODULES_ALL, without which a 32-bit target under-reports
// by more than half AS A SUCCESS.
[[nodiscard]] Sources SystemSources() noexcept;

// ---------------------------------------------------------------------------
// The chokepoint
// ---------------------------------------------------------------------------

// Collect evidence, match, and — only if every check passes — inject.
//
// THERE IS NO Check() THAT RETURNS A VERDICT FOR SOMEBODY ELSE TO ACT ON. That
// shape was considered and rejected (§S13(b)): a clearance that escapes can be
// ignored by a caller who never asks for one, whereas an injection primitive
// with no external symbol cannot be reached at all. It lives in an anonymous
// namespace inside fl_guard.cpp, and tools/chokepoint-check.ps1 fails the build
// if any other translation unit names it.
//
// THE EVIDENCE IS NOT A PARAMETER EITHER. These take no Sources: they always
// use SystemSources(). The seam that the fail-closed matrix needs is real, but
// it is compiled out of every shipping target — see FL_GUARD_TESTABLE below.
// While the injection primitive was a stub, a caller passing all-clean fakes
// was a theoretical hole; the moment injection became real it would have been
// a way into a game process that never consulted a single genuine signal.
[[nodiscard]] Verdict GuardedInject(std::uint32_t targetPid, const wchar_t* dllPath) noexcept;

// Evaluate the guard WITHOUT injecting. Exists for the 30 s in-session re-scan
// (19_SAFETY §During a session), which must reach a verdict on a process it is
// already inside and has nothing to inject. Deliberately cannot be used to
// pre-authorise an injection: it takes no dll path and returns no token, so the
// only way to act on a pass is to call GuardedInject, which re-collects.
[[nodiscard]] Verdict Evaluate(std::uint32_t targetPid) noexcept;

#ifdef FL_GUARD_TESTABLE
// ---------------------------------------------------------------------------
// TEST-ONLY. Defined by exactly one target — src/native/tests — and by nothing
// that ships. tools/chokepoint-check.ps1 fails the build if any other
// CMakeLists defines it.
//
// 14_TESTING's matrix requires forcing EnumProcessModulesEx failures, partial
// module lists, unreadable processes and a denied service query. None of that
// is reachable without injectable evidence, and an input whose failure path
// cannot be exercised is an input whose failure path is unverified. So the seam
// exists — and is unavailable to anything a user runs.
// ---------------------------------------------------------------------------
[[nodiscard]] Verdict EvaluateWithSources(std::uint32_t targetPid, const Sources& sources) noexcept;
[[nodiscard]] Verdict GuardedInjectWithSources(std::uint32_t targetPid, const wchar_t* dllPath,
                                               const Sources& sources) noexcept;
#endif

// Human-readable reason, for logs and for mapping to resx keys.
[[nodiscard]] const char* ReasonName(Reason r) noexcept;

}    // namespace fl::guard

#endif    // FL_GUARD_H
