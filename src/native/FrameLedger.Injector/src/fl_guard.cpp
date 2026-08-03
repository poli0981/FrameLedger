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
bool InjectViaLoadLibrary(std::uint32_t pid, const wchar_t* dllPath) noexcept {
    // The payload must exist and be a file before we ask another process to
    // load it. A missing DLL turns into a remote LoadLibraryW that fails inside
    // the game rather than an error we can report here.
    const DWORD attrs = GetFileAttributesW(dllPath);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }

    // Only exactly the rights needed. PROCESS_ALL_ACCESS would work and would
    // be worse: a handle that can do more than the operation requires is a
    // larger blast radius for any bug in the code below.
    const DWORD rights = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                         PROCESS_QUERY_LIMITED_INFORMATION;
    HANDLE      proc = OpenProcess(rights, FALSE, pid);
    if (proc == nullptr) {
        return false;
    }

    // An x64 DLL cannot load into a 32-bit process, and kernel32 sits at a
    // different address there — so the LoadLibraryW address computed below
    // would be meaningless. Refuse rather than write a wrong pointer into
    // somebody else's address space. (This is also why D3D9 is not Tier 1:
    // 20_OPEN_QUESTIONS §Scope.)
    BOOL targetIsWow64 = FALSE;
    if (!IsWow64Process(proc, &targetIsWow64) || targetIsWow64) {
        CloseHandle(proc);
        return false;
    }

    // kernel32 is mapped at the same base in every 64-bit process for the life
    // of a boot, so our own LoadLibraryW address is valid in the target. This
    // is the documented consequence of ASLR being per-boot for system images,
    // not a trick.
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        CloseHandle(proc);
        return false;
    }
    auto loadLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<void*>(GetProcAddress(kernel32, "LoadLibraryW")));
    if (loadLibrary == nullptr) {
        CloseHandle(proc);
        return false;
    }

    const std::size_t bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    // PAGE_READWRITE, never PAGE_EXECUTE_*. We are writing a STRING — a path
    // for the loader to read. Nothing we place in the target is ever executed;
    // the only code that runs is kernel32's own LoadLibraryW.
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) {
        CloseHandle(proc);
        return false;
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
    return ok;
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
};

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
    if (st->group == Group::kModules && !st->sawSuspicious && HasSuspiciousFragment(*st->rules, name)) {
        st->sawSuspicious = true;
        strncpy_s(st->suspicious, name, _TRUNCATE);
    }
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
        st.group = Group::kModules;

        const Collected c = s.EnumerateModules(set.pids[i], &NameSinkFn, &st);
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
    case Reason::kSuspiciousUnsigned:
        return "SuspiciousUnsigned";
    case Reason::kRulesUnreadable:
        return "RulesUnreadable";
    case Reason::kRulesMalformed:
        return "RulesMalformed";
    case Reason::kRulesIncomplete:
        return "RulesIncomplete";
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
        return Refuse(Reason::kRulesUnreadable, nullptr, "no payload path");
    }
    if (!InjectViaLoadLibrary(targetPid, dllPath)) {
        // Injection failing is not a guard refusal — the gate passed. The
        // caller distinguishes them by reason; this one is reported as an
        // allow whose injection did not take, and CaptureError carries it.
        Verdict failed = Allow();
        strncpy_s(failed.signal, "injection failed after a passing guard", _TRUNCATE);
        return failed;
    }
    return v;
}

}    // namespace

Verdict Evaluate(std::uint32_t targetPid) noexcept {
    return EvaluateImpl(targetPid, SystemSources());
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
#endif

}    // namespace fl::guard
