#include <cstring>
#include <fl_ac_rules.h>
#include <fl_guard.h>

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
bool InjectViaLoadLibrary(std::uint32_t pid, const wchar_t* dllPath) noexcept {
    // NOT IMPLEMENTED YET, AND THAT IS THE CORRECT STATE.
    //
    // CLAUDE.md rule 2 and 15_ROADMAP both say the guard ships before the first
    // real injection, not after — and the guard's Catch2 fail-closed matrix is
    // still being written. Opening a process with
    // CREATE_THREAD|VM_OPERATION|VM_WRITE before those tests pass would be the
    // exact ordering the project has refused from the start.
    //
    // When it lands it is VirtualAllocEx + WriteProcessMemory + CreateRemoteThread
    // on LoadLibraryW. Documented API, real DLL name, real exports. No manual
    // mapping, no header erasure, no PEB unlinking (19_SAFETY §What we will
    // never build).
    (void)pid;
    (void)dllPath;
    return false;
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
    case Reason::kSuspiciousUnsigned:
        return "SuspiciousUnsigned";
    case Reason::kRulesUnreadable:
        return "RulesUnreadable";
    case Reason::kRulesMalformed:
        return "RulesMalformed";
    case Reason::kRulesIncomplete:
        return "RulesIncomplete";
    }
    return "Unknown";
}

Verdict Evaluate(std::uint32_t targetPid, const Sources& sources) noexcept {
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
    return Allow();
}

Verdict GuardedInject(std::uint32_t targetPid, const wchar_t* dllPath, const Sources& sources) noexcept {
    const Verdict v = Evaluate(targetPid, sources);
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

}    // namespace fl::guard
