// fl-probe-guard — measures the Windows APIs the anti-cheat guard is built on.
//
// This is NOT the guard. It installs nothing, injects nothing, and opens no
// process with CREATE_THREAD | VM_OPERATION | VM_WRITE. It answers the
// questions docs/spike-notes.md §1 leaves blank, so that the guard is written
// against measured behaviour rather than against documentation.
//
// Why a probe at all: the most serious defect found in this project so far was
// EnumDeviceDrivers reporting ok=True, 258 drivers and ZERO usable base
// addresses when unelevated — a call that succeeds while telling you nothing,
// which a guard would have read as "no anti-cheat present" on a machine running
// Vanguard (docs/19_SAFETY_AND_ANTICHEAT.md §Pre-injection checks item 2).
// Every check here therefore runs in the configuration users actually have —
// UNELEVATED, the default Agent under ADR-9 — and ships a canary that must
// FAIL, so a green result proves the check discriminates rather than merely
// proving it ran.
//
// A1  EnumProcessModulesEx against a CREATE_SUSPENDED process        (§S1)
// A2  32-bit target from x64, with and without LIST_MODULES_ALL      (§S7)
// A3  handle rights, and a target we are not allowed to open         (§S7)
// A4  NtQuerySystemInformation(SystemModuleInformation)              (driver scan)
// A5  OpenServiceW/QueryServiceStatusEx: absent vs present           (19_SAFETY)
//
// A1 launches a stock system binary suspended and resumes it. That is the
// launch-mode primitive MINUS LoadLibraryW, and it is deliberately where this
// probe stops: measuring what a suspended process shows needs no injection.

#include <windows.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <psapi.h>
#include <string>
#include <vector>
#include <winsvc.h>

#pragma comment(lib, "advapi32.lib")

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

void Note(const char* fmt, ...) {
    std::printf("       ");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
}

std::wstring SystemPath(const wchar_t* leaf) {
    wchar_t    buf[MAX_PATH]{};
    const UINT n = GetSystemWindowsDirectoryW(buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return L"";
    }
    return std::wstring(buf) + leaf;
}

// ---------------------------------------------------------------------------
// Module enumeration, with the handle rights 19_SAFETY specifies.
// ---------------------------------------------------------------------------
struct ModuleScan {
    bool                     opened = false;
    bool                     enumerated = false;
    DWORD                    openError = 0;
    DWORD                    enumError = 0;
    std::vector<std::string> names;
};

ModuleScan ScanModules(DWORD pid, DWORD filterFlag) {
    ModuleScan r;

    // Exactly the rights docs/19_SAFETY_AND_ANTICHEAT.md asks for — no more.
    // A probe that quietly opened with PROCESS_ALL_ACCESS would answer a
    // question the guard never gets to ask.
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h == nullptr) {
        r.openError = GetLastError();
        return r;
    }
    r.opened = true;

    HMODULE mods[1024]{};
    DWORD   needed = 0;
    if (!EnumProcessModulesEx(h, mods, sizeof(mods), &needed, filterFlag)) {
        r.enumError = GetLastError();
        CloseHandle(h);
        return r;
    }
    r.enumerated = true;

    const size_t count = needed / sizeof(HMODULE);
    for (size_t i = 0; i < count && i < 1024; ++i) {
        char name[MAX_PATH]{};
        if (GetModuleBaseNameA(h, mods[i], name, MAX_PATH) != 0) {
            r.names.emplace_back(name);
        }
    }
    CloseHandle(h);
    return r;
}

bool HasModule(const ModuleScan& s, const char* needle) {
    for (const auto& n : s.names) {
        if (_stricmp(n.c_str(), needle) == 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// A1 — what does a CREATE_SUSPENDED process actually show?
//
// docs/20_OPEN_QUESTIONS.md §S1: launch mode is the PREFERRED path and it does
// guard -> inject -> ResumeThread, but a suspended process has run no loader,
// so the module scan (pre-injection check 1) has almost nothing to look at.
// ---------------------------------------------------------------------------
bool ProbeA1_SuspendedModules() {
    std::printf("\nA1 - EnumProcessModulesEx against a CREATE_SUSPENDED process (S1)\n");

    const std::wstring exe = SystemPath(L"\\System32\\cmd.exe");
    if (exe.empty()) {
        Check(false, "locate a stock 64-bit target");
        return false;
    }

    // `/c pause` keeps the target alive after ResumeThread. With `/c exit` it
    // was gone before the second scan ran, and the "after resume" measurement
    // silently read a dead process as an empty module list — the exact shape
    // this probe exists to refuse to accept.
    std::wstring cmdline = L"\"" + exe + L"\" /c pause";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        Note("CreateProcessW(CREATE_SUSPENDED) failed: %lu", GetLastError());
        Check(false, "launch a target suspended");
        return false;
    }

    const ModuleScan suspended = ScanModules(pi.dwProcessId, LIST_MODULES_ALL);
    Note("suspended: opened=%d enumerated=%d modules=%zu (openErr=%lu enumErr=%lu)", suspended.opened ? 1 : 0,
         suspended.enumerated ? 1 : 0, suspended.names.size(), suspended.openError, suspended.enumError);
    for (const auto& n : suspended.names) {
        Note("  - %s", n.c_str());
    }

    ResumeThread(pi.hThread);
    WaitForInputIdle(pi.hProcess, 1000);
    Sleep(200);
    const ModuleScan resumed = ScanModules(pi.dwProcessId, LIST_MODULES_ALL);
    Note("after ResumeThread: modules=%zu", resumed.names.size());

    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 2000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // S1 predicted EnumProcessModulesEx "returns essentially nothing" against a
    // suspended target. MEASURED, it is sharper than that and the difference
    // matters: the call FAILS, with ERROR_PARTIAL_COPY (299). It does not hand
    // back a plausible empty success.
    //
    // That is the good version of this news. An empty success is the dangerous
    // shape - it is what EnumDeviceDrivers does, and it is what a guard reads
    // as "clean". An outright error cannot be mistaken for a clean scan by any
    // caller that checks the return value. The guard's obligation is therefore
    // narrow and explicit: ERROR_PARTIAL_COPY means CANNOT DETERMINE, which is
    // REFUSE - never "no modules found".
    Check(suspended.opened, "the guard's own handle rights suffice to open a suspended target");
    Check(!suspended.enumerated && suspended.enumError == ERROR_PARTIAL_COPY,
          "EnumProcessModulesEx FAILS with ERROR_PARTIAL_COPY on a suspended target - it does not return an empty "
          "success (S1 measured, and sharper than documented)");
    Check(!HasModule(suspended, "kernel32.dll"),
          "a suspended target has not loaded kernel32 - the module scan is blind in launch mode (S1 confirmed)");
    Check(resumed.enumerated && HasModule(resumed, "kernel32.dll"),
          "the same target enumerates normally once resumed - so the blindness is the suspension, not our rights");
    Check(resumed.names.size() > suspended.names.size(),
          "resumed shows more modules than suspended - that difference is what launch mode cannot see");
    return true;
}

// ---------------------------------------------------------------------------
// A2 — WOW64. docs/20_OPEN_QUESTIONS.md §S7: enumerating a 32-bit process from
// a 64-bit one needs LIST_MODULES_ALL or the list comes back empty, and an
// empty module list must never read as "clean".
// ---------------------------------------------------------------------------
bool ProbeA2_Wow64() {
    std::printf("\nA2 - enumerating a 32-bit process from x64 (S7)\n");

    // SysWOW64\\cmd.exe is a deterministic 32-bit target present on every x64
    // Windows. Depending on some third-party 32-bit app happening to run would
    // make this probe machine-state-dependent.
    const std::wstring exe = SystemPath(L"\\SysWOW64\\cmd.exe");
    if (exe.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Note("no SysWOW64\\cmd.exe on this machine - 32-bit subsystem absent");
        Check(false, "locate a stock 32-bit target");
        return false;
    }

    std::wstring cmdline = L"\"" + exe + L"\" /c pause";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &pi)) {
        Note("CreateProcessW failed: %lu", GetLastError());
        Check(false, "launch a 32-bit target");
        return false;
    }
    Sleep(400);    // let its loader finish

    const ModuleScan all = ScanModules(pi.dwProcessId, LIST_MODULES_ALL);
    const ModuleScan def = ScanModules(pi.dwProcessId, LIST_MODULES_DEFAULT);
    const ModuleScan m32 = ScanModules(pi.dwProcessId, LIST_MODULES_32BIT);
    const ModuleScan m64 = ScanModules(pi.dwProcessId, LIST_MODULES_64BIT);

    Note("LIST_MODULES_ALL     modules=%zu (enumerated=%d err=%lu)", all.names.size(), all.enumerated ? 1 : 0,
         all.enumError);
    Note("LIST_MODULES_DEFAULT modules=%zu (enumerated=%d err=%lu)", def.names.size(), def.enumerated ? 1 : 0,
         def.enumError);
    Note("LIST_MODULES_32BIT   modules=%zu (enumerated=%d err=%lu)", m32.names.size(), m32.enumerated ? 1 : 0,
         m32.enumError);
    Note("LIST_MODULES_64BIT   modules=%zu (enumerated=%d err=%lu)", m64.names.size(), m64.enumerated ? 1 : 0,
         m64.enumError);

    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 2000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    Check(all.enumerated && !all.names.empty(), "LIST_MODULES_ALL returns a non-empty list for a 32-bit target");
    Check(all.names.size() >= m32.names.size() && all.names.size() >= m64.names.size(),
          "LIST_MODULES_ALL is a superset of the 32- and 64-bit views");

    // The point of S7, stated as an assertion the guard must honour: the flag
    // changes the answer, and the WRONG flag produces a SHORTER list. A guard
    // that treated a short list as "clean" would be least accurate exactly
    // where it is being asked about a 32-bit title.
    if (def.names.size() < all.names.size()) {
        Note("CONFIRMED: the default filter under-reports a 32-bit target (%zu vs %zu)", def.names.size(),
             all.names.size());
    } else {
        Note("NOTE: default and ALL agreed on this OS build - the rule still stands, it is not guaranteed");
    }
    return true;
}

// ---------------------------------------------------------------------------
// A3 — a target the guard is NOT allowed to read. 19_SAFETY §Elevated /
// protected targets: report "cannot attach", never escalate creatively. What
// matters for the gate is that this is distinguishable from "clean".
// ---------------------------------------------------------------------------
bool ProbeA3_Denied() {
    std::printf("\nA3 - a protected target we cannot open (S7, 19_SAFETY)\n");

    // csrss.exe runs as a protected process; an unelevated caller cannot open
    // it for VM_READ. Found by name so the probe does not hardcode a pid.
    DWORD pids[2048]{};
    DWORD needed = 0;
    if (!EnumProcesses(pids, sizeof(pids), &needed)) {
        Check(false, "EnumProcesses");
        return false;
    }

    bool foundDenied = false;
    for (DWORD i = 0; i < needed / sizeof(DWORD); ++i) {
        if (pids[i] == 0 || pids[i] == 4) {
            continue;
        }
        const ModuleScan s = ScanModules(pids[i], LIST_MODULES_ALL);
        if (!s.opened && s.openError == ERROR_ACCESS_DENIED) {
            Note("pid %lu -> OpenProcess ERROR_ACCESS_DENIED (5)", pids[i]);
            foundDenied = true;
            break;
        }
    }

    // This is the distinction the guard depends on. "I could not look" and "I
    // looked and it was clean" must never collapse into the same value - that
    // collapse is precisely the EnumDeviceDrivers defect.
    Check(foundDenied,
          "at least one process is unopenable unelevated - so 'cannot inspect' is a real, distinguishable state");
    return true;
}

// ---------------------------------------------------------------------------
// A4 — the driver scan. 19_SAFETY now specifies the Nt* route because
// EnumDeviceDrivers fails OPEN unelevated. Both are run here, side by side,
// so the difference is measured rather than quoted.
// ---------------------------------------------------------------------------
constexpr ULONG kSystemModuleInformation = 11;

struct RTL_PROCESS_MODULE_INFORMATION {
    HANDLE Section;
    PVOID  MappedBase;
    PVOID  ImageBase;
    ULONG  ImageSize;
    ULONG  Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR  FullPathName[256];
};

struct RTL_PROCESS_MODULES {
    ULONG                          NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
};

// Asserted so an accidental edit is caught at compile time. Note this proves
// only internal consistency - it compares our constants against themselves and
// there is no ground truth to check against, which is exactly why the CONTENT
// validation below is the check that actually matters.
static_assert(sizeof(void*) == 8, "x64 only");
static_assert(offsetof(RTL_PROCESS_MODULE_INFORMATION, ImageSize) == 24, "ImageSize offset");
static_assert(offsetof(RTL_PROCESS_MODULE_INFORMATION, OffsetToFileName) == 38, "OffsetToFileName offset");
static_assert(offsetof(RTL_PROCESS_MODULE_INFORMATION, FullPathName) == 40, "FullPathName offset");
static_assert(sizeof(RTL_PROCESS_MODULE_INFORMATION) == 296, "record size");

using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

// Does a parsed driver list look like real driver paths, or like a plausible
// pile of bytes?
//
// This function exists because "258 distinct non-empty strings" is NOT a
// discriminating assertion: docs/spike-notes.md records a probe whose path
// offset was two bytes off and which produced exactly that - 258 distinct
// strings, every one of them garbage ("INDOWS\\system32\\..."). A count check
// passes on the very defect it was written to catch.
struct PathVerdict {
    size_t total = 0;
    size_t wellFormed = 0;
    size_t resolvable = 0;
    bool   sawKernel = false;
};

PathVerdict ValidateDriverPaths(const std::vector<std::string>& paths) {
    PathVerdict v;
    v.total = paths.size();
    for (const auto& p : paths) {
        // The kernel reports native paths. Anything else means we parsed the
        // wrong bytes.
        const bool wellFormed = p.rfind("\\SystemRoot\\", 0) == 0 || p.rfind("\\??\\", 0) == 0;
        if (!wellFormed) {
            continue;
        }
        ++v.wellFormed;

        if (p.find("ntoskrnl.exe") != std::string::npos) {
            v.sawKernel = true;
        }

        // Resolve \SystemRoot\ and confirm the file is really there. A
        // mis-parsed path can still start with the right prefix by luck; it
        // cannot also name a file that exists.
        if (p.rfind("\\SystemRoot\\", 0) == 0) {
            const std::wstring win = SystemPath(L"");
            std::wstring       rest;
            for (size_t i = std::strlen("\\SystemRoot"); i < p.size(); ++i) {
                rest.push_back(static_cast<wchar_t>(static_cast<unsigned char>(p[i])));
            }
            if (GetFileAttributesW((win + rest).c_str()) != INVALID_FILE_ATTRIBUTES) {
                ++v.resolvable;
            }
        }
    }
    return v;
}

std::vector<std::string> ParseDriverPaths(const std::vector<unsigned char>& buf, size_t byteSkew) {
    std::vector<std::string> out;
    if (buf.size() < sizeof(ULONG)) {
        return out;
    }
    const auto* mods = reinterpret_cast<const RTL_PROCESS_MODULES*>(buf.data());
    const ULONG n = mods->NumberOfModules;

    const size_t base = offsetof(RTL_PROCESS_MODULES, Modules);
    for (ULONG i = 0; i < n; ++i) {
        const size_t rec = base + static_cast<size_t>(i) * sizeof(RTL_PROCESS_MODULE_INFORMATION);
        const size_t at = rec + offsetof(RTL_PROCESS_MODULE_INFORMATION, FullPathName) + byteSkew;
        if (at + 256 > buf.size()) {
            break;
        }
        const char* s = reinterpret_cast<const char*>(buf.data() + at);
        out.emplace_back(s, ::strnlen(s, 255));
    }
    return out;
}

bool ProbeA4_DriverScan() {
    std::printf("\nA4 - driver enumeration, unelevated (19_SAFETY check 2)\n");

    // The API 19_SAFETY used to specify, kept as the contrast case.
    LPVOID       drivers[2048]{};
    DWORD        cbNeeded = 0;
    const BOOL   edd = EnumDeviceDrivers(drivers, sizeof(drivers), &cbNeeded);
    const size_t eddCount = cbNeeded / sizeof(LPVOID);
    size_t       eddNamed = 0;
    size_t       eddNonNull = 0;
    for (size_t i = 0; i < eddCount && i < 2048; ++i) {
        if (drivers[i] != nullptr) {
            ++eddNonNull;
            char nm[MAX_PATH]{};
            if (GetDeviceDriverBaseNameA(drivers[i], nm, MAX_PATH) != 0) {
                ++eddNamed;
            }
        }
    }
    Note("EnumDeviceDrivers: ok=%d count=%zu non-null bases=%zu recoverable names=%zu", edd ? 1 : 0, eddCount,
         eddNonNull, eddNamed);

    // The route 19_SAFETY specifies now.
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto          nq = reinterpret_cast<NtQuerySystemInformationFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtQuerySystemInformation")));
    if (nq == nullptr) {
        Check(false, "resolve NtQuerySystemInformation");
        return false;
    }

    ULONG len = 0;
    nq(kSystemModuleInformation, nullptr, 0, &len);
    if (len == 0) {
        Check(false, "NtQuerySystemInformation reported a size");
        return false;
    }
    std::vector<unsigned char> buf(len + 4096);
    ULONG                      got = 0;
    const LONG                 status = nq(kSystemModuleInformation, buf.data(), static_cast<ULONG>(buf.size()), &got);
    Note("NtQuerySystemInformation(SystemModuleInformation): status=0x%08lX bytes=%lu",
         static_cast<unsigned long>(status), got);
    if (status < 0) {
        Check(false, "NtQuerySystemInformation(SystemModuleInformation) succeeded");
        return false;
    }

    const std::vector<std::string> paths = ParseDriverPaths(buf, 0);
    const PathVerdict              v = ValidateDriverPaths(paths);
    Note("parsed=%zu well-formed=%zu resolvable-on-disk=%zu ntoskrnl=%s", v.total, v.wellFormed, v.resolvable,
         v.sawKernel ? "yes" : "NO");
    for (size_t i = 0; i < paths.size() && i < 3; ++i) {
        Note("  e.g. %s", paths[i].c_str());
    }

    // Content, not count. Every one of these fails on the two-byte-skew bug.
    Check(v.total > 0, "the driver list is non-empty");
    Check(v.sawKernel, "ntoskrnl.exe is present - we are reading real driver paths");
    Check(v.wellFormed == v.total, "EVERY path is a native path (\\SystemRoot\\ or \\??\\)");
    Check(v.resolvable > 8, "many parsed paths name files that actually exist on disk");

    // 19_SAFETY records EnumDeviceDrivers as failing open unelevated. Assert
    // that the replacement is strictly better rather than trusting the note.
    Check(v.total > eddNamed,
          "the Nt* route recovers more driver identities than EnumDeviceDrivers does (the documented fail-open)");

    // --- CANARY: the content validator must REJECT the known-bad parse ------
    // Reproduce the exact historical defect - FullPathName read two bytes late
    // - and require the validator to catch it. Without this, "all paths
    // well-formed" is an assertion nobody has ever seen fail.
    const std::vector<std::string> skewed = ParseDriverPaths(buf, 2);
    const PathVerdict              sv = ValidateDriverPaths(skewed);
    Note("CANARY 2-byte skew: parsed=%zu well-formed=%zu resolvable=%zu ntoskrnl=%s", sv.total, sv.wellFormed,
         sv.resolvable, sv.sawKernel ? "yes" : "NO");
    if (!skewed.empty()) {
        Note("  e.g. %s", skewed[0].c_str());
    }
    Check(sv.total > 0 && sv.wellFormed < sv.total,
          "CANARY: a 2-byte offset error is REJECTED by content validation (a count check would have passed it)");
    return true;
}

// ---------------------------------------------------------------------------
// A5 — services. 19_SAFETY: OpenServiceW/QueryServiceStatusEx must distinguish
// ABSENT (1060) from DENIED, because denied has to fail closed while absent
// must not.
// ---------------------------------------------------------------------------
bool ProbeA5_Services() {
    std::printf("\nA5 - service query: absent vs present, unelevated (19_SAFETY)\n");

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        Note("OpenSCManager failed: %lu", GetLastError());
        Check(false, "connect to the service control manager unelevated");
        return false;
    }

    struct Case {
        const wchar_t* name;
        bool           expectPresent;
    };
    const Case cases[] = {
        {L"Schedule", true},                             // Task Scheduler: always present
        {L"FrameLedgerDefinitelyNotAService", false},    // must report ABSENT, specifically
    };

    bool absentDiscriminated = false;
    bool presentFound = false;
    for (const auto& c : cases) {
        SC_HANDLE svc = OpenServiceW(scm, c.name, SERVICE_QUERY_STATUS);
        if (svc != nullptr) {
            SERVICE_STATUS_PROCESS st{};
            DWORD                  need = 0;
            const BOOL             q =
                QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&st), sizeof(st), &need);
            Note("%-36ls -> OPEN ok, QueryServiceStatusEx=%d state=%lu", c.name, q ? 1 : 0, st.dwCurrentState);
            if (c.expectPresent) {
                presentFound = true;
            }
            CloseServiceHandle(svc);
        } else {
            const DWORD e = GetLastError();
            Note("%-36ls -> OPEN failed, GetLastError=%lu%s", c.name, e,
                 e == ERROR_SERVICE_DOES_NOT_EXIST ? " (ERROR_SERVICE_DOES_NOT_EXIST)" : "");
            if (!c.expectPresent && e == ERROR_SERVICE_DOES_NOT_EXIST) {
                absentDiscriminated = true;
            }
        }
    }
    CloseServiceHandle(scm);

    Check(presentFound, "a known service is queryable unelevated");
    Check(absentDiscriminated,
          "an absent service reports ERROR_SERVICE_DOES_NOT_EXIST (1060) specifically, not a generic failure");

    // Honesty about what this probe CANNOT establish here: a standard user
    // holds SERVICE_QUERY_STATUS on the stock service set, so ACCESS_DENIED is
    // not producible against real services on this machine. The guard's
    // denied-means-refuse path therefore has to be driven by a fake in unit
    // tests, and is not measured here. Saying so beats implying coverage.
    Note("NOT MEASURED: the DENIED branch - stock services are queryable by standard users.");
    Note("              Drive it from a unit-test fake; do not read A5 as covering it.");
    return true;
}

}    // namespace

int main() {
    std::printf("FrameLedger guard probe - measures the APIs docs/19_SAFETY specifies.\n");
    std::printf("Run UNELEVATED first: that is the default Agent under ADR-9.\n");

    BOOL   elevated = FALSE;
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION te{};
        DWORD           cb = 0;
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &cb)) {
            elevated = te.TokenIsElevated;
        }
        CloseHandle(tok);
    }
    std::printf("This run is %s.\n", elevated ? "ELEVATED" : "unelevated");

    bool ok = true;
    ok = ProbeA1_SuspendedModules() && ok;
    ok = ProbeA2_Wow64() && ok;
    ok = ProbeA3_Denied() && ok;
    ok = ProbeA4_DriverScan() && ok;
    ok = ProbeA5_Services() && ok;

    const bool passed = ok && g_failures == 0;
    std::printf("\n%s (%d failure(s))\n", passed ? "GUARD PROBE OK" : "GUARD PROBE FAILURES", g_failures);
    return passed ? 0 : 1;
}
