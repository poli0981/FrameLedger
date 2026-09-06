#include <windows.h>

#include <cstring>
#include <fl_ac_rules.h>
#include <fl_guard.h>
#include <fl_prescan.h>
#include <psapi.h>

namespace fl::guard {
namespace {

// ---------------------------------------------------------------------------
// THE INJECTION PRIMITIVE.
//
// Internal linkage, in the guard's own translation unit, deliberately. This is
// the mechanism 20_OPEN_QUESTIONS §S8/§S13(b) settled on after the documented
// one was disproved: a "clearance token only the guard can produce" can be
// IGNORED — a caller simply never asks for one — whereas a function with no
// external symbol cannot be called at all.
//
// It is not declared in any header. tools/chokepoint-check.ps1 fails the build
// if the name appears in any source file other than this one.
//
// Injection uses documented LoadLibraryW. No manual mapping, no PE header
// erasure, no PEB unlinking, no thread hiding — CLAUDE.md rule 3, and
// 19_SAFETY §What we will never build.
// ---------------------------------------------------------------------------
// VirtualAllocEx + WriteProcessMemory + CreateRemoteThread on LoadLibraryW.
//
// The most ordinary injection technique there is, and that is the point.
// 19_SAFETY §What we will never build rules out manual mapping, PE header
// erasure, PEB unlinking, thread hiding and every other way of being harder to
// see. A performance tool should be EASY for anti-cheat to identify: the DLL
// keeps its real name, its real exports and a populated VERSIONINFO block, and
// it arrives through the documented loader like RTSS, OBS and ReShade do.
//
// Reached only from GuardedInject, below, after every check has passed.
//
// Returns kAllow on success, or WHY it did not happen. It used to return bool,
// and the caller turned false into an Allow verdict carrying a free-text signal
// — so a failed injection was indistinguishable from a successful one at the
// type level.
Reason InjectViaLoadLibrary(std::uint32_t pid, const wchar_t* dllPath) noexcept {
    // The payload must exist and be a file before we ask another process to
    // load it. A missing DLL turns into a remote LoadLibraryW that fails inside
    // the game rather than an error we can report here.
    //
    // THIS IS NOT THE PAYLOAD CHECK. It answers "is there a file there", which
    // for a long time was the ONLY thing ever asked of `dllPath` — and that is
    // how the shipped, exported guard came to load any DLL on the machine into
    // any process without anti-cheat (§S22). Identity is established by
    // Sources::PayloadIsOurOwn in GuardedInjectImpl, which also runs this same
    // test first so that "absent" and "not ours" get different reasons.
    //
    // Kept here anyway, and deliberately redundant: this function is the
    // primitive, and it must not depend on its one caller having asked the right
    // questions. If it ever acquires a second caller, the check travels with it.
    const DWORD attrs = GetFileAttributesW(dllPath);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return Reason::kInjectionFailed;
    }

    // Only exactly the rights needed. PROCESS_ALL_ACCESS would work and would
    // be worse: a handle that can do more than the operation requires is a
    // larger blast radius for any bug in the code below.
    const DWORD rights = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                         PROCESS_QUERY_LIMITED_INFORMATION;
    HANDLE      proc = OpenProcess(rights, FALSE, pid);
    if (proc == nullptr) {
        return Reason::kInjectionFailed;
    }

    // An x64 DLL cannot load into a 32-bit process, and kernel32 sits at a
    // different address there — so the LoadLibraryW address computed below
    // would be meaningless. Refuse rather than write a wrong pointer into
    // somebody else's address space. (This is also why D3D9 is not Tier 1:
    // 20_OPEN_QUESTIONS §Scope.)
    BOOL targetIsWow64 = FALSE;
    if (!IsWow64Process(proc, &targetIsWow64)) {
        CloseHandle(proc);
        return Reason::kInjectionFailed;
    }
    if (targetIsWow64) {
        CloseHandle(proc);
        // Its own reason. This is permanent and expected for a whole class of
        // titles, and the UI's answer is "Tier 2", not "something went wrong".
        return Reason::kTargetIsWow64;
    }

    // kernel32 is mapped at the same base in every 64-bit process for the life
    // of a boot, so our own LoadLibraryW address is valid in the target. This
    // is the documented consequence of ASLR being per-boot for system images,
    // not a trick.
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        CloseHandle(proc);
        return Reason::kInjectionFailed;
    }
    auto loadLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<void*>(GetProcAddress(kernel32, "LoadLibraryW")));
    if (loadLibrary == nullptr) {
        CloseHandle(proc);
        return Reason::kInjectionFailed;
    }

    const std::size_t bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    // PAGE_READWRITE, never PAGE_EXECUTE_*. We are writing a STRING — a path
    // for the loader to read. Nothing we place in the target is ever executed;
    // the only code that runs is kernel32's own LoadLibraryW.
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) {
        CloseHandle(proc);
        return Reason::kInjectionFailed;
    }

    bool   ok = false;
    SIZE_T written = 0;
    if (WriteProcessMemory(proc, remote, dllPath, bytes, &written) && written == bytes) {
        HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (thread != nullptr) {
            // Bounded wait. A hung loader in someone else's game is not
            // something we can fix, but it is something we must not wait on
            // forever — the Agent has a session to fail cleanly.
            const DWORD waited = WaitForSingleObject(thread, 10000);
            ok = (waited == WAIT_OBJECT_0);
            CloseHandle(thread);
        }
    }

    // Release the path buffer either way. Leaving a page behind in a process we
    // do not own is litter at best and a lifetime bug at worst.
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);

    // VERIFY BY OBSERVATION, not by return value.
    //
    // GetExitCodeThread would give us LoadLibraryW's HMODULE truncated to 32
    // bits, so a module whose handle happens to have a zero low word reads as
    // failure — and, worse, a nonzero value reads as success without anything
    // having checked that our DLL is actually there. Re-enumerating the target
    // and looking for the module by name is the observation that answers the
    // question we actually have.
    if (ok) {
        // BOTH separators. Win32 accepts forward slashes throughout, and the
        // path handed to us may well contain them — CMake generates them, and
        // so does anything that has been through a POSIX-flavoured toolchain.
        // Looking only for a backslash left `leaf` holding the entire path, so
        // the name comparison below never matched and a SUCCESSFUL injection
        // reported failure.
        wchar_t        leaf[MAX_PATH]{};
        const wchar_t* back = wcsrchr(dllPath, L'\\');
        const wchar_t* fwd = wcsrchr(dllPath, L'/');
        const wchar_t* slash = (back > fwd) ? back : fwd;
        wcscpy_s(leaf, (slash != nullptr) ? slash + 1 : dllPath);

        ok = false;
        HMODULE mods[1024]{};
        DWORD   needed = 0;
        if (EnumProcessModulesEx(proc, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
            const size_t count = (needed > sizeof(mods) ? sizeof(mods) : needed) / sizeof(HMODULE);
            for (size_t i = 0; i < count && !ok; ++i) {
                wchar_t name[MAX_PATH]{};
                if (GetModuleBaseNameW(proc, mods[i], name, MAX_PATH) != 0 && _wcsicmp(name, leaf) == 0) {
                    ok = true;
                }
            }
        }
    }

    CloseHandle(proc);
    return ok ? Reason::kAllow : Reason::kInjectionFailed;
}

Verdict Refuse(Reason reason, const char* family, const char* signal) noexcept {
    Verdict v;
    v.reason = reason;
    if (family != nullptr) {
        strncpy_s(v.family, family, _TRUNCATE);
    }
    if (signal != nullptr) {
        strncpy_s(v.signal, signal, _TRUNCATE);
    }
    return v;
}

Verdict Allow() noexcept {
    Verdict v;
    v.reason = Reason::kAllow;
    return v;
}

// Scratch shared by the sinks. The guard allocates nothing, so a match is
// reported by writing here and stopping enumeration.
struct MatchState {
    const Rules*  rules = nullptr;
    Group         group = Group::kModules;
    const Family* hit = nullptr;
    char          signal[260] = {};

    // Heuristic accumulation (19_SAFETY: fragment AND untrusted signer).
    bool sawSuspicious = false;
    char suspicious[260] = {};

    // §S22(b) — what the module sink needs to answer "is this module ours?".
    // Null sources, or the target's own process, mean nothing is ever exempt.
    const Sources* sources = nullptr;
    bool           isTarget = false;
};

// Drivers and any other flat name list. The module scan does NOT come through
// here — it needs the load path, which a base name cannot carry (§S22(b)).
bool NameSinkFn(void* ctx, const char* name) noexcept {
    auto* st = static_cast<MatchState*>(ctx);
    if (st == nullptr || st->rules == nullptr || name == nullptr) {
        return false;
    }
    if (const Family* f = MatchName(*st->rules, st->group, name)) {
        st->hit = f;
        strncpy_s(st->signal, name, _TRUNCATE);
        return false;    // stop: one hit is enough to refuse
    }
    return true;
}

// §S18/§S22(b) — may the fuzzy tier be suppressed for THIS MODULE?
//
// The ONLY exception in this gate, and every clause is load-bearing:
//
//   - not the injection target, however it is spelled. A game copied into our
//     install directory must still be judged.
//   - a null seam does not suppress. Sources members default to nullptr, so
//     forgetting to wire this must fail towards refusing.
//   - a module whose path we could not obtain does not suppress. We cannot
//     exempt what we cannot locate.
//   - anything but kOk does not suppress. "Could not determine whose file this
//     is" is not "it is ours".
//
// Reached only when a fragment already matched, which on a measured machine is
// approximately never — so the file handles it costs are not on the ordinary
// path. Hoisting it above the fragment test would add a new machine-wide
// refusal source, the shape of the EasyAntiCheat_EOS defect that refused every
// process on the machine.
bool ModuleIsExempt(const MatchState& st, const wchar_t* modulePath) noexcept {
    if (st.isTarget || st.sources == nullptr || st.sources->ModuleIsOurOwn == nullptr || modulePath == nullptr) {
        return false;
    }
    bool ours = false;
    return st.sources->ModuleIsOurOwn(modulePath, &ours) == Collected::kOk && ours;
}

// §S19(b) — is this fragment-matching module signed by a TRUSTED organisation?
//
// The other half of 19_SAFETY's heuristic ("name fragment AND not signed by a
// known vendor"), wired 2026-09-06 on row G1. Every clause fails towards refusing:
//
//   - a null seam does not suppress (Sources members default to nullptr);
//   - a module whose path we could not obtain cannot be verified;
//   - anything but kOk from the seam is "could not tell", which is untrusted;
//   - the organisation must be on BOTH the rules list and the compiled-in bound
//     (IsTrustedSigner) — full equality, never a substring.
//
// Unlike ModuleIsExempt this applies to the TARGET too: a game that loads a
// Microsoft-signed key-protection provider is exactly the false refusal the
// signer half exists to remove. Reached only after the fragment matched, so the
// ~2-4 ms verification is not on the ordinary path.
bool ModuleIsTrustedSigned(const MatchState& st, const wchar_t* modulePath) noexcept {
    if (st.sources == nullptr || st.sources->ModuleSignerOrganisation == nullptr || modulePath == nullptr ||
        st.rules == nullptr) {
        return false;
    }
    char org[kMaxValueLen] = {};
    if (st.sources->ModuleSignerOrganisation(modulePath, org, sizeof(org)) != Collected::kOk) {
        return false;
    }
    return IsTrustedSigner(*st.rules, org);
}

// Check 1's sink. The ordering here is the whole of §S22(b).
bool ModuleSinkFn(void* ctx, const char* name, const wchar_t* modulePath) noexcept {
    auto* st = static_cast<MatchState*>(ctx);
    if (st == nullptr || st->rules == nullptr || name == nullptr) {
        return false;
    }
    if (const Family* f = MatchName(*st->rules, Group::kModules, name)) {
        st->hit = f;
        strncpy_s(st->signal, name, _TRUNCATE);
        return false;    // stop: one hit is enough to refuse
    }
    if (st->sawSuspicious || !HasSuspiciousFragment(*st->rules, name)) {
        return true;
    }
    // OURS: not evidence, and — critically — KEEP LOOKING.
    //
    // The old code latched the FIRST fragment-matching module and skipped the
    // fragment test for every module after it. That was harmless while any hit
    // refused, because the latched name only had to be *a* reason. The moment
    // suppression became per-module it would have been a FAIL-OPEN REACHABLE BY
    // LOAD ORDER: our own guard DLL matches first, gets exempted, and a genuinely
    // suspicious module loaded afterwards is never recorded — the process returns
    // Allow. §S19(b) predicted exactly this and said the detection half has to be
    // restructured rather than extended. This return is that restructure.
    if (ModuleIsExempt(*st, modulePath)) {
        return true;
    }
    // TRUSTED SIGNER: the same KEEP LOOKING, for the same reason. A trusted module
    // must never STOP the scan, or a genuinely suspicious module loaded after it
    // is never recorded — the load-order fail-open the paragraph above describes,
    // reachable through this clause exactly as through the exemption.
    if (ModuleIsTrustedSigned(*st, modulePath)) {
        return true;
    }
    st->sawSuspicious = true;
    strncpy_s(st->suspicious, name, _TRUNCATE);
    // Still not a stop: an EXACT blocklist hit later in the same process outranks
    // the fuzzy tier, and CheckModules checks st.hit first precisely so it can.
    return true;
}

// Collect the scan set (§S16) into a fixed array. More processes than this in
// one game's tree means something we do not understand, which refuses.
inline constexpr std::size_t kMaxScanSet = 64;

struct ScanSet {
    std::uint32_t pids[kMaxScanSet] = {};
    std::size_t   count = 0;
    bool          overflowed = false;
};

bool ScanSetSink(void* ctx, std::uint32_t pid) noexcept {
    auto* set = static_cast<ScanSet*>(ctx);
    if (set == nullptr) {
        return false;
    }
    if (set->count >= kMaxScanSet) {
        set->overflowed = true;
        return false;
    }
    set->pids[set->count++] = pid;
    return true;
}

// Load and validate the rules. Every failure is a distinct refusal reason,
// because "the blocklist is unreadable" and "the blocklist is missing BattlEye"
// are different problems for whoever has to fix them.
Verdict LoadRules(const Sources& s, Rules& rules) noexcept {
    if (s.ReadRulesFile == nullptr) {
        return Refuse(Reason::kRulesUnreadable, nullptr, "no rules source");
    }
    // 1 MiB on the stack is too much; static is fine because the guard is not
    // re-entrant and is called from one thread at a time.
    static char       buffer[kMaxRulesBytes];
    const std::size_t n = s.ReadRulesFile(buffer, sizeof(buffer));
    if (n == static_cast<std::size_t>(-1) || n == 0) {
        return Refuse(Reason::kRulesUnreadable, nullptr, "rules file could not be read");
    }

    switch (ParseRules(buffer, n, rules)) {
    case ParseResult::kOk:
        return Allow();
    case ParseResult::kIncomplete:
        return Refuse(Reason::kRulesIncomplete, nullptr, "a required anti-cheat family is missing");
    case ParseResult::kTooLarge:
        return Refuse(Reason::kRulesMalformed, nullptr, "rules file exceeds the parser's bounds");
    case ParseResult::kMalformed:
    default:
        return Refuse(Reason::kRulesMalformed, nullptr, "rules file is not the shape the guard requires");
    }
}

// Check 1 — modules, across the §S16 scan set.
Verdict CheckModules(const Sources& s, const Rules& rules, std::uint32_t targetPid) noexcept {
    if (s.EnumerateScanSet == nullptr || s.EnumerateModules == nullptr) {
        return Refuse(Reason::kProcessTreeUnavailable, nullptr, "no process source");
    }

    ScanSet set;
    if (s.EnumerateScanSet(targetPid, &ScanSetSink, &set) != Collected::kOk || set.overflowed || set.count == 0) {
        // An empty scan set is not "nothing to scan" — it is "we do not know
        // what to scan", and it must never read as clean.
        return Refuse(Reason::kProcessTreeUnavailable, nullptr, "could not establish the scan set");
    }

    for (std::size_t i = 0; i < set.count; ++i) {
        MatchState st;
        st.rules = &rules;
        // No st.group: ModuleSinkFn names Group::kModules itself, because there
        // is exactly one group it could mean. A field set here and read nowhere
        // would read as configuration.
        st.sources = &s;
        st.isTarget = (set.pids[i] == targetPid);

        const Collected c = s.EnumerateModules(set.pids[i], &ModuleSinkFn, &st);
        if (st.hit != nullptr) {
            return Refuse(Reason::kBlockedModule, st.hit->name, st.signal);
        }
        // kFailed covers ERROR_ACCESS_DENIED on a protected target and
        // ERROR_PARTIAL_COPY on a suspended one; kIncomplete covers a WOW64
        // under-report. Both mean we did not see the whole picture, and the
        // whole point of this guard is that a partial look is not a clean one.
        if (c == Collected::kFailed) {
            return Refuse(Reason::kProcessUnreadable, nullptr, "a process in the scan set could not be read");
        }
        if (c == Collected::kIncomplete) {
            return Refuse(Reason::kModuleScanFailed, nullptr, "a module list came back incomplete");
        }
        if (st.sawSuspicious) {
            // 19_SAFETY: fragment AND not signed by a known vendor. The signer
            // lookup is not wired yet, and an unchecked signature is UNTRUSTED
            // by definition — so this refuses today. That is the correct
            // direction, and it is why the fragment list must stay narrow.
            //
            // `sawSuspicious` is already the post-exemption answer: ModuleSinkFn
            // never latches a module that is ours. Asking a second question here
            // is what the process-form did, and it is what made the exemption
            // depend on where the HOST lived (§S22(b)).
            return Refuse(Reason::kSuspiciousUnsigned, "unknown", st.suspicious);
        }
    }
    return Allow();
}

// Check 2 — machine-wide drivers. Independent of process identity, which is
// why it catches the case a module scan structurally cannot: a driver that
// gates the whole machine, loaded by a game we are not even looking at.
Verdict CheckDrivers(const Sources& s, const Rules& rules) noexcept {
    if (s.EnumerateDrivers == nullptr) {
        return Refuse(Reason::kDriverScanFailed, nullptr, "no driver source");
    }
    MatchState st;
    st.rules = &rules;
    st.group = Group::kDrivers;

    if (s.EnumerateDrivers(&NameSinkFn, &st) != Collected::kOk) {
        return Refuse(Reason::kDriverScanFailed, nullptr, "driver enumeration failed");
    }
    if (st.hit != nullptr) {
        return Refuse(Reason::kBlockedDriver, st.hit->name, st.signal);
    }
    return Allow();
}

// Check 2b — services. Sibling anti-cheat services are not in any process
// tree, so §S16's scan set structurally cannot see them; they are covered by
// name instead.
Verdict CheckServices(const Sources& s, const Rules& rules) noexcept {
    if (s.QueryService == nullptr) {
        return Refuse(Reason::kServiceQueryFailed, nullptr, "no service source");
    }
    for (std::size_t i = 0; i < rules.familyCount; ++i) {
        const Family& f = rules.families[i];
        if (f.group != Group::kServices) {
            continue;
        }
        for (std::size_t v = 0; v < f.valueCount; ++v) {
            bool            present = false;
            const Collected c = s.QueryService(f.values[v], &present);
            if (c != Collected::kOk) {
                // ABSENT is kOk+present=false. Anything else — notably
                // ACCESS_DENIED — is "cannot determine", and refuses.
                return Refuse(Reason::kServiceQueryFailed, f.name, f.values[v]);
            }
            if (present) {
                return Refuse(Reason::kBlockedService, f.name, f.values[v]);
            }
        }
    }
    return Allow();
}

}    // namespace

const char* ReasonName(Reason r) noexcept {
    switch (r) {
    case Reason::kAllow:
        return "Allow";
    case Reason::kBlockedModule:
        return "BlockedModule";
    case Reason::kModuleScanFailed:
        return "ModuleScanFailed";
    case Reason::kProcessUnreadable:
        return "ProcessUnreadable";
    case Reason::kProcessTreeUnavailable:
        return "ProcessTreeUnavailable";
    case Reason::kBlockedDriver:
        return "BlockedDriver";
    case Reason::kDriverScanFailed:
        return "DriverScanFailed";
    case Reason::kBlockedService:
        return "BlockedService";
    case Reason::kServiceQueryFailed:
        return "ServiceQueryFailed";
    case Reason::kBlockedExecutable:
        return "BlockedExecutable";
    case Reason::kBlockedStoreId:
        return "BlockedStoreId";
    case Reason::kAntiCheatDirectory:
        return "AntiCheatDirectory";
    case Reason::kAntiCheatFile:
        return "AntiCheatFile";
    case Reason::kPreScanFailed:
        return "PreScanFailed";
    case Reason::kInjectionFailed:
        return "InjectionFailed";
    case Reason::kTargetIsWow64:
        return "TargetIsWow64";
    case Reason::kPayloadNotOurs:
        return "PayloadNotOurs";
    case Reason::kHookNotEnabled:
        return "HookNotEnabled";
    case Reason::kConsentMissing:
        return "ConsentMissing";
    case Reason::kPreviouslyBlocked:
        return "PreviouslyBlocked";
    case Reason::kSuspiciousUnsigned:
        return "SuspiciousUnsigned";
    case Reason::kRulesUnreadable:
        return "RulesUnreadable";
    case Reason::kRulesMalformed:
        return "RulesMalformed";
    case Reason::kRulesIncomplete:
        return "RulesIncomplete";
    case Reason::kLaunchTargetExited:
        return "LaunchTargetExited";
    case Reason::kLaunchNoPresentationRuntime:
        return "LaunchNoPresentationRuntime";
    case Reason::kTargetIsVulkanLayered:
        return "TargetIsVulkanLayered";
    case Reason::kCount:
        break;    // not a reason; falls through to the guard below
    }
    // "Unknown" is the tell, not a fallback. A Reason added without a case here
    // lands on it, and ctest fl_guard's "every Reason has a distinct name" case
    // fails.
    //
    // MEASURED, because the obvious assumption is wrong: omitting `default:`
    // does NOT make the compiler enforce this. C4061/C4062 are off by default
    // even at /W4, so appending an enumerator with no case built clean under
    // /W4 /WX. The comment that used to sit here claimed the opposite — a gate
    // that existed only in prose, which is the exact defect class this file's
    // header warns about.
    return "Unknown";
}

namespace {

// Check 3 — the per-title blocklist, EXECUTABLE HALF.
//
// 19_SAFETY has listed this as check 3 since the beginning and it had NO CALL
// SITE: MatchesBlockedExecutable was implemented, tested, and asked by nobody, so
// populating the data would have changed nothing (§S14). "Check 3 passed" read as
// "this title is not a known online title" while nothing had looked.
//
// UNRESOLVABLE IDENTITY REFUSES. §S14's second decision, and 19_SAFETY says an
// unresolvable identity "must read UNKNOWN, never clean". A pid whose image we
// cannot name is a pid whose identity we do not have, so kFailed and kIncomplete
// both refuse with kProcessUnreadable -- the same reason CheckModules already
// uses for the same underlying inability.
//
// THE STORE-ID HALF IS NOT HERE, and that is a limitation rather than an
// omission. MatchesBlockedStoreId exists and is tested; it cannot be called,
// for three independent reasons (§S14):
//
//   1. NO PRODUCER. Nothing in the tree parses Steam .acf, GOG .info or Epic
//      .item -- the platform metadata extractors were never built, so `store_id`
//      is null for every title.
//   2. NO CHANNEL, BY DESIGN. FlGuardEvaluate takes a pid and nothing else, and
//      fl_guard_abi.h says so deliberately: "no way to hand in evidence -- the
//      guard collects its own". A store id passed in by the caller is a caller
//      asserting a safety fact, which §S3's rule forbids in as many words.
//   3. APPLYING "unknown refuses" TO IT WOULD BE A GATE THAT CANNOT PASS. If the
//      guard can never resolve a store id and an unresolved store id refuses,
//      every title on every machine refuses.
//
// So the exe-name half runs and the store-id half is named as blocked. Do not
// "fix" (2) by widening the ABI.
Verdict CheckBlockedExecutable(const Sources& s, const Rules& rules, std::uint32_t targetPid) noexcept {
    if (s.ImageFileName == nullptr) {
        return Refuse(Reason::kProcessUnreadable, nullptr, "no image-name source");
    }

    char            name[kMaxValueLen] = {};
    const Collected got = s.ImageFileName(targetPid, name, sizeof(name));
    if (got != Collected::kOk || name[0] == '\0') {
        return Refuse(Reason::kProcessUnreadable, nullptr, "could not name the target's executable");
    }

    if (const TitleRule* hit = MatchesBlockedExecutable(rules, name); hit != nullptr) {
        return Refuse(Reason::kBlockedExecutable, hit->family, name);
    }
    return Allow();
}

Verdict EvaluateImpl(std::uint32_t targetPid, const Sources& sources) noexcept {
    // Static so the 1 MiB of rules storage inside LoadRules is not duplicated
    // on the stack. Cleared on every call: a stale blocklist from a previous
    // evaluation would be a gate answering about the wrong data.
    static Rules rules;
    rules = Rules{};

    if (Verdict v = LoadRules(sources, rules); !v.Allowed()) {
        return v;
    }
    // Drivers first. It is machine-wide, it is the cheapest, and it is the one
    // that refuses for ALL titles rather than just this one (19_SAFETY item 2).
    if (Verdict v = CheckDrivers(sources, rules); !v.Allowed()) {
        return v;
    }
    if (Verdict v = CheckServices(sources, rules); !v.Allowed()) {
        return v;
    }
    if (Verdict v = CheckModules(sources, rules, targetPid); !v.Allowed()) {
        return v;
    }
    // Check 3, at last. Cheap -- one OpenProcess and a string compare -- and it
    // runs before check 4 because it needs no filesystem walk.
    if (Verdict v = CheckBlockedExecutable(sources, rules, targetPid); !v.Allowed()) {
        return v;
    }
    // Check 4, INSIDE the chokepoint rather than beside it.
    //
    // 19_SAFETY and 05_DETECTION both describe this as gating injection, and it
    // is placed here so that stays true. Running it as an advisory the UI
    // consults would make it a check that gates nothing — and there is no
    // persistence layer yet, so its verdict would have nowhere to be stored and
    // `hook_blocked_reason` could not carry it either.
    //
    // Last of the four because it is the only one that touches the filesystem;
    // the three cheaper checks have already had their say.
    if (Verdict v = CheckStaticPreScan(sources, rules, targetPid); !v.Allowed()) {
        return v;
    }
    return Allow();
}

Verdict GuardedInjectImpl(std::uint32_t targetPid, const wchar_t* dllPath, const Sources& sources) noexcept {
    const Verdict v = EvaluateImpl(targetPid, sources);
    if (!v.Allowed()) {
        return v;
    }
    if (dllPath == nullptr) {
        // Was kRulesUnreadable, which said the rules file could not be read
        // about a caller that passed no path. A mislabelled reason is a wrong
        // answer for whoever has to fix it.
        return Refuse(Reason::kInjectionFailed, nullptr, "no payload path");
    }

    // ABSENT and FOREIGN are different problems, and collapsing them would be
    // this project's own recurring defect. A damaged install and a misuse of the
    // exported ABI call for opposite responses, which is exactly why
    // kInjectionFailed was split out in the first place.
    //
    // It has to come BEFORE the identity check, because "not ours" would
    // otherwise absorb "not there": the identity seam works by opening the file,
    // so a path naming nothing comes back kFailed and would be reported as a
    // foreign payload. The primitive keeps its own copy of this test as a last
    // line of defence; this one exists to name the right cause.
    const DWORD payloadAttrs = GetFileAttributesW(dllPath);
    if (payloadAttrs == INVALID_FILE_ATTRIBUTES || (payloadAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return Refuse(Reason::kInjectionFailed, nullptr, "the payload path does not name a file");
    }

    // §S22 — the payload, not just the target.
    //
    // Placed AFTER EvaluateImpl on purpose. A caller who hands a foreign payload
    // at a target that is running anti-cheat must see the anti-cheat refusal:
    // that is the fact that matters to the user, and it is the more permanent of
    // the two. This ordering costs a full scan on a call that was going to be
    // refused anyway, which is a price paid on a path nobody legitimate reaches.
    //
    // Every uncertainty refuses, in the ordinary direction of this file: a null
    // seam, a seam that could not answer, and an answer of "not ours" are one
    // outcome. Sources members default to nullptr, so forgetting to wire this
    // fails towards refusing rather than towards loading.
    bool payloadIsOurs = false;
    if (sources.PayloadIsOurOwn == nullptr) {
        return Refuse(Reason::kPayloadNotOurs, nullptr, "no payload identity source");
    }
    if (sources.PayloadIsOurOwn(dllPath, &payloadIsOurs) != Collected::kOk) {
        return Refuse(Reason::kPayloadNotOurs, nullptr, "the payload could not be identified");
    }
    if (!payloadIsOurs) {
        return Refuse(Reason::kPayloadNotOurs, nullptr, "the payload is not one of FrameLedger's own binaries");
    }

    // Injection failing is not a guard refusal — the gate passed — but it is
    // also NOT an allow. Allowed() means "the DLL is loaded in the target",
    // which is the only reading a caller can act on. The reason says whose
    // fault it was, because the responses differ.
    const Reason injected = InjectViaLoadLibrary(targetPid, dllPath);
    if (injected == Reason::kTargetIsWow64) {
        return Refuse(injected, nullptr, "target is a 32-bit process; the Overlay is x64-only");
    }
    if (injected != Reason::kAllow) {
        return Refuse(injected, nullptr, "the guard passed but the injection did not take");
    }
    return v;
}

// ---------------------------------------------------------------------------
// Launch mode (P1 item 2) -- WHEN the guard runs, never WHETHER it passes.
// ---------------------------------------------------------------------------

// The poll's one question: has the target mapped a presentation runtime yet? Read
// through Sources::EnumerateModules -- the module scan's own seam -- so a test can
// drive it and so the answer comes from the loader rather than from a guess about
// the title. The sink looks for five system names and MATCHES NOTHING AGAINST THE
// BLOCKLIST; the verdict is GuardedInjectImpl's, run in full once the answer is
// yes. An enumeration that fails is "not yet" here rather than a refusal, because
// ERROR_PARTIAL_COPY is exactly what a target still inside its loader returns
// (§S1), and the refusal, if one is due, is the full scan's to make.
struct RuntimeProbe {
    bool dxgi = false;
    bool d3d = false;
    bool gl = false;
    bool vk = false;
};

bool RuntimeSinkFn(void* ctx, const char* name, const wchar_t*) noexcept {
    auto* p = static_cast<RuntimeProbe*>(ctx);
    if (p == nullptr || name == nullptr) {
        return false;
    }
    if (_stricmp(name, "dxgi.dll") == 0) {
        p->dxgi = true;
    } else if (_stricmp(name, "d3d11.dll") == 0 || _stricmp(name, "d3d12.dll") == 0) {
        p->d3d = true;
    } else if (_stricmp(name, "opengl32.dll") == 0) {
        p->gl = true;
    } else if (_stricmp(name, "vulkan-1.dll") == 0) {
        p->vk = true;
    }
    return true;
}

// What the target presents through, as far as the loader can say. kVulkan is the
// branch the guard must NOT inject (Reason::kTargetIsVulkanLayered), and IT WINS
// over a D3D runtime mapped beside it. Measured 2026-09-06 on the harness's
// Vulkan mode under an NVIDIA driver: the Vulkan ICD itself maps dxgi.dll and
// d3d12.dll and presents the Vulkan swapchain through a DXGI chain, so "vulkan-1
// beside d3d12" is what EVERY Vulkan title looks like there -- treating it as D3D
// injected the Overlay, which owned the ring, and left the layer forwarding into
// nothing. A process does not map vulkan-1.dll unless it uses Vulkan; a D3D title
// that happens to is the rarer shape and the one this rule gets wrong.
enum class PresentationRuntime : std::uint8_t { kNone, kD3dOrOpenGl, kVulkan };

PresentationRuntime FindPresentationRuntime(const Sources& s, std::uint32_t pid) noexcept {
    if (s.EnumerateModules == nullptr) {
        return PresentationRuntime::kNone;
    }
    RuntimeProbe p;
    if (s.EnumerateModules(pid, &RuntimeSinkFn, &p) == Collected::kFailed) {
        return PresentationRuntime::kNone;    // still in its loader, or unreadable -- the full scan refuses the latter
    }
    if (p.vk) {
        return PresentationRuntime::kVulkan;
    }
    return ((p.dxgi && p.d3d) || p.gl) ? PresentationRuntime::kD3dOrOpenGl : PresentationRuntime::kNone;
}

Verdict GuardedInjectWhenReadyImpl(std::uint32_t targetPid, const wchar_t* dllPath, std::uint32_t timeoutMs,
                                   const Sources& sources) noexcept {
    // SYNCHRONIZE only: this handle exists to notice an exit and for nothing else.
    HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, targetPid);
    if (proc == nullptr) {
        return Refuse(Reason::kProcessUnreadable, nullptr, "could not open the launched target to wait on it");
    }
    constexpr DWORD kPollMs = 50;
    const ULONGLONG start = GetTickCount64();
    Verdict         v;    // fail-closed default, like every Verdict
    for (;;) {
        if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) {
            v = Refuse(Reason::kLaunchTargetExited, nullptr,
                       "the target exited before it mapped a presentation runtime; nothing was injected");
            break;
        }
        const PresentationRuntime runtime = FindPresentationRuntime(sources, targetPid);
        if (runtime == PresentationRuntime::kD3dOrOpenGl) {
            v = GuardedInjectImpl(targetPid, dllPath, sources);
            break;
        }
        if (runtime == PresentationRuntime::kVulkan) {
            // The FULL guard still runs -- a Vulkan title with anti-cheat is refused
            // by name exactly as a D3D one is -- and on a pass nothing is injected.
            v = EvaluateImpl(targetPid, sources);
            if (v.Allowed()) {
                v = Refuse(Reason::kTargetIsVulkanLayered, nullptr,
                           "the guard passed; the target presents through Vulkan, where the implicit layer is the "
                           "capture side, so nothing was injected");
            }
            break;
        }
        if (GetTickCount64() - start >= timeoutMs) {
            v = Refuse(Reason::kLaunchNoPresentationRuntime, nullptr,
                       "no presentation runtime was mapped within the launch budget; nothing was injected");
            break;
        }
        Sleep(kPollMs);
    }
    CloseHandle(proc);
    return v;
}

}    // namespace

Verdict Evaluate(std::uint32_t targetPid) noexcept {
    return EvaluateImpl(targetPid, SystemSources());
}

Verdict GuardedInjectWhenReady(std::uint32_t targetPid, const wchar_t* dllPath, std::uint32_t timeoutMs) noexcept {
    return GuardedInjectWhenReadyImpl(targetPid, dllPath, timeoutMs, SystemSources());
}

Verdict GuardedInject(std::uint32_t targetPid, const wchar_t* dllPath) noexcept {
    return GuardedInjectImpl(targetPid, dllPath, SystemSources());
}

#ifdef FL_GUARD_TESTABLE
Verdict EvaluateWithSources(std::uint32_t targetPid, const Sources& sources) noexcept {
    return EvaluateImpl(targetPid, sources);
}

Verdict GuardedInjectWithSources(std::uint32_t targetPid, const wchar_t* dllPath, const Sources& sources) noexcept {
    return GuardedInjectImpl(targetPid, dllPath, sources);
}

Verdict GuardedInjectWhenReadyWithSources(std::uint32_t targetPid, const wchar_t* dllPath, std::uint32_t timeoutMs,
                                          const Sources& sources) noexcept {
    return GuardedInjectWhenReadyImpl(targetPid, dllPath, timeoutMs, sources);
}
#endif

}    // namespace fl::guard
