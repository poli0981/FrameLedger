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

    // Check 4's "cannot determine": the game directory was absent, unlistable,
    // truncated by a bound, or crossed a reparse point we will not follow.
    //
    // APPENDED, not slotted in beside kAntiCheatFile where it belongs
    // logically. These values cross a C ABI into the managed mirror, so
    // inserting one renumbers every reason after it — the whole tail would
    // shift by one and every stored or logged value would mean something else.
    // Grouping is worth less than a number that never moves.
    kPreScanFailed,

    // The guard PASSED and the injection still did not happen.
    //
    // These exist because the previous code returned kAllow with the truth in a
    // free-text signal, above a comment claiming "the caller distinguishes them
    // by reason" — and there was no reason to distinguish by. A caller reading
    // Allowed() got `true` for an injection that never occurred. Measured
    // 2026-08-03 against a real 32-bit title.
    //
    // Allowed() is now false for both: it means "the DLL is loaded in the
    // target", which is the only reading a caller can act on safely. The reason
    // says whose fault it was, because the responses differ — a refusal is
    // permanent, a failed injection may be worth retrying, and WOW64 is
    // permanent but for an entirely different reason.
    kInjectionFailed,

    // The target is a 32-bit process. Permanent and expected, not an error:
    // the Overlay is x64-only, so an x64 DLL cannot load there
    // (20_OPEN_QUESTIONS §Scope decisions). The UI should say so and offer
    // Tier 2 rather than reporting a failure the user could act on.
    kTargetIsWow64,

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

// As NameSink, plus whether the entry is a directory. Check 4 matches
// directories and files against different blocklist groups, and guessing from
// the name would be a second, weaker classifier.
using DirEntrySink = bool (*)(void* ctx, const char* name, bool isDirectory);

struct Sources {
    // Loaded modules of one process, by base name.
    Collected (*EnumerateModules)(std::uint32_t pid, NameSink sink, void* ctx) = nullptr;

    // Machine-wide loaded kernel drivers, by full native path.
    Collected (*EnumerateDrivers)(NameSink sink, void* ctx) = nullptr;

    // A service by name. kOk + present=false means it is not RUNNING — either
    // absent (1060) or installed and stopped. kFailed means DENIED or anything
    // else, which is "cannot determine" and refuses. That distinction is the
    // whole reason this is not a bool (docs/19_SAFETY §Pre-injection checks
    // item 2).
    //
    // `present` means RUNNING, not installed. Measured: EasyAntiCheat_EOS is
    // installed machine-wide by any EOS title and sits Stopped/Manual, so the
    // installed-means-present reading refused every process on the machine.
    Collected (*QueryService)(const char* name, bool* present) = nullptr;

    // The scan set for §S16: the injection target, its descendants, and its
    // ancestors up to but excluding the first known platform launcher.
    Collected (*EnumerateScanSet)(std::uint32_t targetPid, bool (*sink)(void*, std::uint32_t), void* ctx) = nullptr;

    // Whole rules file into a caller-owned buffer. Returns bytes written, or
    // SIZE_MAX on any failure — unreadable, absent, or larger than the cap.
    std::size_t (*ReadRulesFile)(char* buffer, std::size_t cap) = nullptr;

    // Check 4 — the static pre-scan.
    //
    // The directory the target's image lives in. kFailed means we could not
    // name it, which refuses: check 4 cannot run against a directory we cannot
    // find, and "we did not look" has never been a pass in this file.
    Collected (*ImageDirectory)(std::uint32_t pid, wchar_t* out, std::size_t cap) = nullptr;

    // Entries at and below `dir`, flattened, bounded by the caps in
    // fl_prescan.h. `isDirectory` is what decides which blocklist group the
    // name is matched against, so it is part of the evidence rather than
    // something the caller infers from the string.
    Collected (*EnumerateDirEntries)(const wchar_t* dir, DirEntrySink sink, void* ctx) = nullptr;
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
