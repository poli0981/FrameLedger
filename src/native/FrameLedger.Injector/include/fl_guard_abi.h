// The C ABI the managed Agent reaches the guard through.
//
// 20_OPEN_QUESTIONS §S15 item 1: `04_CAPTURE` writes `AntiCheatGuard.Check(pid)`
// and `01_ARCHITECTURE` draws the guard inside the Agent box, while §S13(a) put
// the authoritative guard in C++. Those descriptions are reconciled by making
// the managed side a FACADE — one implementation, reached through this ABI —
// and never a second matcher. Two blocklist matchers that can disagree is a
// fail-open by construction: the day they diverge, one of them is wrong and
// nothing tells you which.
//
// WHY THIS IS A DLL EXPORT AND §S9's INJECTOR EXE STILL IS NOT.
//
// §S9 refused to ship a user-runnable injector because it was a path into a
// game process THAT THE GUARD DID NOT STAND IN FRONT OF. This is the opposite:
// there is no entry point here that skips a check. `FlGuardedInject` runs the
// full guard — module scan across the §S16 scan set, driver scan, service
// scan, rules completeness — and returns a refusal if any of it fails. A caller
// can ask; only the guard answers.
//
// What this ABI deliberately does NOT carry: per-game consent. That lives in
// the Agent's database (`games.hook_consent_at`), because it is a record of
// something a human did, and CLAUDE.md rule 1 makes it the Agent's
// responsibility to check before asking. This ABI enforces the ANTI-CHEAT gate
// — the part that protects accounts — not the opt-in.

#ifndef FL_GUARD_ABI_H
#define FL_GUARD_ABI_H

#include <cstdint>

#if defined(FL_GUARD_ABI_BUILD)
#define FL_GUARD_ABI __declspec(dllexport)
#else
#define FL_GUARD_ABI __declspec(dllimport)
#endif

extern "C" {

// Mirrored by FrameLedger.Domain's AntiCheatRefusalReason. The mirror is
// asserted by a test that reads the names back through FlGuardReasonName, so a
// value added on one side and forgotten on the other fails the build rather
// than silently mapping to the wrong refusal in the UI.
struct FlGuardResult {
    std::int32_t reason;         // 0 == allowed
    char         family[64];     // e.g. "Easy Anti-Cheat"; empty when allowed
    char         signal[260];    // e.g. "EasyAntiCheat_EOS.dll"
};

// Run every pre-injection check against `targetPid`. No injection.
// Used for the 30 s in-session re-scan (19_SAFETY §During a session).
FL_GUARD_ABI void FlGuardEvaluate(std::uint32_t targetPid, FlGuardResult* out);

// Run every check and, only on a pass, inject `dllPath` via documented
// LoadLibraryW. There is no variant that skips the checks, and no way to hand
// in evidence — the guard collects its own.
FL_GUARD_ABI void FlGuardedInject(std::uint32_t targetPid, const wchar_t* dllPath, FlGuardResult* out);

// Check 4 against a directory, before anything is launched (FR-2.2). ADVISORY:
// it answers "may this game's hooking toggle be offered at all", and gates
// nothing. The same scan runs inside FlGuardEvaluate and FlGuardedInject
// against a directory derived from the target's own pid, so a caller who never
// asks this — or ignores the answer — changes nothing about what is allowed.
//
// It reports through FlGuardResult rather than an outcome enum of its own, so
// there is ONE reason table and ONE mirror surface: kAllow means clean,
// AntiCheatDirectory/AntiCheatFile name what was found, and PreScanFailed or a
// Rules* reason means the scan could not reach an answer.
FL_GUARD_ABI void FlStaticPreScan(const wchar_t* gameDirectory, FlGuardResult* out);

// Stable name for a reason code. The managed mirror test compares these against
// its own enum, so this is a contract and not a debugging aid.
FL_GUARD_ABI const char* FlGuardReasonName(std::int32_t reason);

// Number of reason codes. Lets the mirror test assert that neither side has
// gained a value the other does not know about.
FL_GUARD_ABI std::int32_t FlGuardReasonCount(void);

}    // extern "C"

#endif    // FL_GUARD_ABI_H
