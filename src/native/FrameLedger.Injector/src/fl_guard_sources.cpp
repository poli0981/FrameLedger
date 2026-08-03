// The real Windows evidence collectors behind fl::guard::Sources.
//
// Every behaviour here was MEASURED by src/native/tools/fl-probe-guard and is
// recorded in docs/spike-notes.md §1, unelevated — the default Agent
// configuration under ADR-9. The measurements are not decoration; two of them
// changed what this file does:
//
//   - EnumProcessModulesEx against a suspended target FAILS with
//     ERROR_PARTIAL_COPY rather than returning an empty list.
//   - LIST_MODULES_ALL is mandatory: the default filter returned 7 of 15
//     modules for a live 32-bit target AS A SUCCESS.
//
// EnumDeviceDrivers is deliberately absent. It reports 266 drivers and zero
// usable base addresses to a standard user, which is a fail-open in the hard
// gate in the default configuration.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fl_ac_rules.h>
#include <fl_guard.h>
#include <fl_prescan.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winsvc.h>

#pragma comment(lib, "advapi32.lib")

namespace fl::guard {
namespace {

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

static_assert(sizeof(void*) == 8, "x64 only");
static_assert(offsetof(RTL_PROCESS_MODULE_INFORMATION, FullPathName) == 40, "FullPathName offset");
static_assert(sizeof(RTL_PROCESS_MODULE_INFORMATION) == 296, "record size");

using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

// Platform launchers. §S16 stops the ancestor walk BELOW these: the ancestor of
// every Steam title is steam.exe, which loads VAC modules, so an unbounded walk
// would refuse every Steam game — not "some false refusals" but the product not
// working, which is how a user ends up hunting for an override.
bool IsPlatformLauncher(const wchar_t* imageName) noexcept {
    static const wchar_t* kLaunchers[] = {L"steam.exe",        L"steamwebhelper.exe", L"EpicGamesLauncher.exe",
                                          L"GalaxyClient.exe", L"itch.exe",           L"explorer.exe",
                                          L"services.exe",     L"svchost.exe"};
    for (const wchar_t* l : kLaunchers) {
        if (_wcsicmp(imageName, l) == 0) {
            return true;
        }
    }
    return false;
}

Collected EnumerateModulesImpl(std::uint32_t pid, NameSink sink, void* ctx) noexcept {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h == nullptr) {
        return Collected::kFailed;    // ACCESS_DENIED on a protected target: cannot determine
    }

    HMODULE mods[1024]{};
    DWORD   needed = 0;
    // LIST_MODULES_ALL, always. Measured: the default filter under-reports a
    // 32-bit target by more than half and returns that as a success.
    if (!EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        CloseHandle(h);
        return Collected::kFailed;    // includes ERROR_PARTIAL_COPY on a suspended target
    }

    const bool   truncated = needed > sizeof(mods);
    const size_t count = (truncated ? sizeof(mods) : needed) / sizeof(HMODULE);
    for (size_t i = 0; i < count; ++i) {
        char name[MAX_PATH]{};
        if (GetModuleBaseNameA(h, mods[i], name, MAX_PATH) == 0) {
            CloseHandle(h);
            return Collected::kIncomplete;    // a module we could not name is one we could not check
        }
        if (!sink(ctx, name)) {
            break;
        }
    }
    CloseHandle(h);
    // More modules than our buffer holds is a PARTIAL answer, not a complete
    // one. Reporting kOk here would be the empty-list defect with extra steps.
    return truncated ? Collected::kIncomplete : Collected::kOk;
}

Collected EnumerateDriversImpl(NameSink sink, void* ctx) noexcept {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return Collected::kFailed;
    }
    auto nq = reinterpret_cast<NtQuerySystemInformationFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtQuerySystemInformation")));
    if (nq == nullptr) {
        return Collected::kFailed;
    }

    ULONG len = 0;
    nq(kSystemModuleInformation, nullptr, 0, &len);
    if (len == 0) {
        return Collected::kFailed;
    }
    const ULONG cap = len + 8192;
    auto*       buf = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), 0, cap));
    if (buf == nullptr) {
        return Collected::kFailed;
    }
    ULONG      got = 0;
    const LONG status = nq(kSystemModuleInformation, buf, cap, &got);
    if (status < 0) {
        HeapFree(GetProcessHeap(), 0, buf);
        return Collected::kFailed;
    }

    const auto*  mods = reinterpret_cast<const RTL_PROCESS_MODULES*>(buf);
    const ULONG  n = mods->NumberOfModules;
    const size_t base = offsetof(RTL_PROCESS_MODULES, Modules);

    Collected result = Collected::kOk;
    for (ULONG i = 0; i < n; ++i) {
        const size_t rec = base + static_cast<size_t>(i) * sizeof(RTL_PROCESS_MODULE_INFORMATION);
        if (rec + sizeof(RTL_PROCESS_MODULE_INFORMATION) > cap) {
            result = Collected::kIncomplete;
            break;
        }
        const auto* m = reinterpret_cast<const RTL_PROCESS_MODULE_INFORMATION*>(buf + rec);
        const char* path = reinterpret_cast<const char*>(m->FullPathName);

        // CONTENT VALIDATION, not a count. The struct layout is
        // version-sensitive and documented-as-unsupported; an earlier probe of
        // this exact API was two bytes off and produced 258 distinct non-empty
        // strings, every one of them garbage ("INDOWS\system32\..."). A parse
        // that yields non-native paths means we are reading the wrong bytes,
        // and 19_SAFETY says treat any parse failure as REFUSE.
        const bool nativePath = std::strncmp(path, "\\SystemRoot\\", 12) == 0 || std::strncmp(path, "\\??\\", 4) == 0;
        if (!nativePath) {
            HeapFree(GetProcessHeap(), 0, buf);
            return Collected::kFailed;
        }
        if (!sink(ctx, path)) {
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return result;
}

Collected QueryServiceImpl(const char* name, bool* present) noexcept {
    if (name == nullptr || present == nullptr) {
        return Collected::kFailed;
    }
    *present = false;

    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return Collected::kFailed;
    }
    SC_HANDLE svc = OpenServiceA(scm, name, SERVICE_QUERY_STATUS);
    if (svc == nullptr) {
        const DWORD e = GetLastError();
        CloseServiceHandle(scm);
        // ABSENT is a real answer. Anything else — notably ACCESS_DENIED — is
        // "cannot determine", and the caller turns that into a refusal. This is
        // the whole reason the return type is not a bool.
        return (e == ERROR_SERVICE_DOES_NOT_EXIST) ? Collected::kOk : Collected::kFailed;
    }

    SERVICE_STATUS_PROCESS st{};
    DWORD                  need = 0;
    const BOOL ok = QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&st), sizeof(st), &need);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (!ok) {
        return Collected::kFailed;
    }

    // PRESENT MEANS RUNNING, NOT INSTALLED.
    //
    // This used to report present for a service that merely existed, on the
    // reasoning that the family being on the machine was enough. MEASURED
    // 2026-08-03, that reasoning breaks the product: `EasyAntiCheat_EOS` is
    // installed machine-wide by any EOS title, sits Stopped/Manual until its own
    // game runs, and one such title anywhere made the guard refuse EVERY process
    // on the machine — explorer.exe and steam.exe included. 19_SAFETY's own
    // words for this shape: "a gate that refuses everything is not a strict gate
    // but a broken one, and it is how a user ends up looking for the override
    // CLAUDE.md rule 2 says does not exist."
    //
    // The machine-wide guarantee does not rest on this check. A loaded
    // anti-cheat DRIVER is check 2 and still refuses for all titles; modules
    // inside the target are check 1. A stopped, manual-start service has no code
    // in any process — when its game actually runs, both of those fire.
    //
    // STOPPED is the only state that counts as absent. Start-pending, paused and
    // stop-pending all mean code is or was live, and the 30 s in-session re-scan
    // closes the window between this call and a later start.
    *present = st.dwCurrentState != SERVICE_STOPPED;
    return Collected::kOk;
}

// §S16: the injection target, its descendants, and its ancestors up to but
// excluding the first known platform launcher.
Collected EnumerateScanSetImpl(std::uint32_t targetPid, bool (*sink)(void*, std::uint32_t), void* ctx) noexcept {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return Collected::kFailed;
    }

    struct Entry {
        DWORD   pid;
        DWORD   ppid;
        wchar_t name[MAX_PATH];
    };
    static Entry entries[4096];
    size_t       count = 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap, &pe)) {
        CloseHandle(snap);
        return Collected::kFailed;
    }
    do {
        if (count >= 4096) {
            CloseHandle(snap);
            return Collected::kFailed;    // a machine we cannot fully enumerate
        }
        entries[count].pid = pe.th32ProcessID;
        entries[count].ppid = pe.th32ParentProcessID;
        wcscpy_s(entries[count].name, pe.szExeFile);
        ++count;
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);

    bool targetFound = false;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].pid == targetPid) {
            targetFound = true;
        }
    }
    if (!targetFound) {
        return Collected::kFailed;    // we were asked about a process that is not there
    }

    if (!sink(ctx, targetPid)) {
        return Collected::kOk;
    }

    // Ancestors, stopping below the first platform launcher.
    DWORD cur = targetPid;
    for (int depth = 0; depth < 16; ++depth) {
        DWORD parent = 0;
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].pid == cur) {
                parent = entries[i].ppid;
            }
        }
        if (parent == 0) {
            break;
        }
        bool found = false;
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].pid != parent) {
                continue;
            }
            found = true;
            if (IsPlatformLauncher(entries[i].name)) {
                parent = 0;    // boundary: the game's tree ends here
            }
            break;
        }
        if (!found || parent == 0) {
            break;
        }
        if (!sink(ctx, parent)) {
            return Collected::kOk;
        }
        cur = parent;
    }

    // Descendants of the target, breadth-first over the snapshot.
    static DWORD frontier[512];
    size_t       head = 0;
    size_t       tail = 0;
    frontier[tail++] = targetPid;
    while (head < tail) {
        const DWORD p = frontier[head++];
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].ppid != p || entries[i].pid == targetPid) {
                continue;
            }
            if (tail >= 512) {
                return Collected::kFailed;
            }
            frontier[tail++] = entries[i].pid;
            if (!sink(ctx, entries[i].pid)) {
                return Collected::kOk;
            }
        }
    }
    return Collected::kOk;
}

std::size_t ReadRulesFileImpl(char* buffer, std::size_t cap) noexcept {
    // Delegates to fl_ac_rules.cpp so the guard and the Vulkan layer read the
    // SAME file. This used to be a second copy of the path logic; two readers
    // pointing at different files would be a second blocklist by accident.
    return ReadRulesFile(buffer, cap);
}

// Check 4 — where the game lives.
//
// Read-only rights, the same PROCESS_QUERY_LIMITED_INFORMATION the module scan
// uses. QueryFullProcessImageNameW rather than GetModuleFileNameEx: it needs no
// VM_READ and works against a target we may only query.
Collected ImageDirectoryImpl(std::uint32_t pid, wchar_t* out, std::size_t cap) noexcept {
    if (out == nullptr || cap == 0) {
        return Collected::kFailed;
    }
    out[0] = L'\0';

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) {
        return Collected::kFailed;    // ACCESS_DENIED on a protected target: cannot determine
    }

    wchar_t    path[kMaxPreScanPathLen] = {};
    DWORD      len = static_cast<DWORD>(kMaxPreScanPathLen);
    const BOOL ok = QueryFullProcessImageNameW(h, 0, path, &len);
    CloseHandle(h);
    if (!ok || len == 0) {
        return Collected::kFailed;
    }

    // Strip the file name. A path with no separator is not a path we understand.
    wchar_t* lastSep = nullptr;
    for (wchar_t* p = path; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            lastSep = p;
        }
    }
    if (lastSep == nullptr || lastSep == path) {
        return Collected::kFailed;
    }
    *lastSep = L'\0';

    // The INSTALL ROOT, not the executable's directory. Unreal puts the exe at
    // <root>\<Project>\Binaries\Win64\, and EasyAntiCheat/ sits at the root —
    // measured on Lies of P, where the pre-scan saw seven files none of which
    // could have been an anti-cheat SDK.
    if (!ResolveInstallRoot(path, out, cap)) {
        return Collected::kFailed;    // truncating a path yields a DIFFERENT directory
    }
    return Collected::kOk;
}

// One level of the walk. Returns false if the caller asked us to stop.
bool WalkDir(const wchar_t* dir, std::size_t depth, DirEntrySink sink, void* ctx, std::size_t& budget, bool& truncated,
             bool& stopped) noexcept {
    wchar_t pattern[kMaxPreScanPathLen] = {};
    if (_snwprintf_s(pattern, kMaxPreScanPathLen, _TRUNCATE, L"%s\\*", dir) < 0) {
        truncated = true;
        return true;
    }

    WIN32_FIND_DATAW fd{};
    HANDLE           h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        // An unreadable subdirectory is a part of the tree we did not see.
        truncated = true;
        return true;
    }

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }
        if (budget == 0) {
            truncated = true;
            break;
        }
        --budget;

        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isReparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        // Names are matched as ASCII (acToken is a strict ASCII allowlist), but
        // a name we cannot convert is a name we could not inspect — record it as
        // an incomplete listing rather than skipping it silently.
        char      name[260] = {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), nullptr, nullptr);
        if (n <= 0) {
            truncated = true;
            continue;
        }

        if (!sink(ctx, name, isDir)) {
            stopped = true;
            break;
        }

        if (isDir) {
            if (isReparse) {
                // NEVER followed. A junction can point anywhere, including back
                // into this tree, and a symlink walk that loops is the classic
                // bug here. Its presence means we did not see everything under
                // it, so the scan cannot come back clean.
                truncated = true;
                continue;
            }
            if (depth + 1 < kMaxPreScanDepth) {
                wchar_t child[kMaxPreScanPathLen] = {};
                if (_snwprintf_s(child, kMaxPreScanPathLen, _TRUNCATE, L"%s\\%s", dir, fd.cFileName) < 0) {
                    truncated = true;
                    continue;
                }
                WalkDir(child, depth + 1, sink, ctx, budget, truncated, stopped);
                if (stopped) {
                    break;
                }
            }
        }
    } while (FindNextFileW(h, &fd) != 0);

    const DWORD err = GetLastError();
    FindClose(h);
    if (!stopped && err != ERROR_NO_MORE_FILES && err != ERROR_SUCCESS) {
        truncated = true;
    }
    return true;
}

Collected EnumerateDirEntriesImpl(const wchar_t* dir, DirEntrySink sink, void* ctx) noexcept {
    if (dir == nullptr || sink == nullptr) {
        return Collected::kFailed;
    }
    const DWORD attrs = GetFileAttributesW(dir);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return Collected::kFailed;    // gone, or not a directory: cannot determine
    }

    std::size_t budget = kMaxPreScanEntries;
    bool        truncated = false;
    bool        stopped = false;
    WalkDir(dir, 0, sink, ctx, budget, truncated, stopped);

    // A sink that stopped us found what it was looking for; the listing being
    // short after that is not a gap.
    if (stopped) {
        return Collected::kOk;
    }
    return truncated ? Collected::kIncomplete : Collected::kOk;
}

}    // namespace

Sources SystemSources() noexcept {
    Sources s;
    s.EnumerateModules = &EnumerateModulesImpl;
    s.EnumerateDrivers = &EnumerateDriversImpl;
    s.QueryService = &QueryServiceImpl;
    s.EnumerateScanSet = &EnumerateScanSetImpl;
    s.ReadRulesFile = &ReadRulesFileImpl;
    s.ImageDirectory = &ImageDirectoryImpl;
    s.EnumerateDirEntries = &EnumerateDirEntriesImpl;
    return s;
}

}    // namespace fl::guard
