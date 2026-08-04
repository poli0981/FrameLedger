// The measurement baseline P0 item 4 compares hooks against (15_ROADMAP item 3).
//
// WHY THIS EXISTS. §M9 established that the "old detection" this rewrite claims
// to improve on does not exist in this repository, so item 4's comparison had no
// left-hand side and ADR-7's founding claim was unfalsifiable. 15_ROADMAP asks
// for "passive file/module scanning", and the MODULE half is the part that
// matters: "nvngx_dlssg.dll is loaded in this process" is a claim of the same
// kind as what a hook reports. "A file of that name sits on disk" is not — per
// 05_DETECTION a static hint may never set a runtime fact.
//
// WHAT IT IS NOT. It is not the detector, and it is not a guard. It opens
// processes read-only, reaches no injection primitive, and writes nothing. It
// answers two questions per capability: is it ON DISK beside the game, and is it
// LOADED right now.
//
// WHY IT READS THE RULES ITSELF. The capability list lives in
// rules/detection-rules.json, and jsmn is already vendored — but the guard's
// parser deliberately reads ONLY the anticheat subtree, and teaching it a group
// the hard gate does not need would spend the gate's parse budget on inference
// data. So the tokenising happens here, in a translation unit nothing safety-
// critical links.

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fl_guard.h>
#include <jsmn.h>
#include <psapi.h>

namespace {

constexpr int kMaxCaps = 64;
constexpr int kMaxSignalsPerCap = 32;

struct Capability {
    char id[64] = {};
    char name[64] = {};
    char signals[kMaxSignalsPerCap][96] = {};
    int  signalCount = 0;
    bool onDisk = false;
    bool loaded = false;
};

Capability g_caps[kMaxCaps];
int        g_capCount = 0;

bool TokenIs(const char* json, const jsmntok_t& t, const char* lit) {
    const size_t n = static_cast<size_t>(t.end - t.start);
    return std::strlen(lit) == n && std::strncmp(json + t.start, lit, n) == 0;
}

bool CopyTok(const char* json, const jsmntok_t& t, char* out, size_t cap) {
    const size_t n = static_cast<size_t>(t.end - t.start);
    if (t.start < 0 || n >= cap) {
        return false;
    }
    std::memcpy(out, json + t.start, n);
    out[n] = '\0';
    return true;
}

// Case-insensitive glob with '*' only — the capability signals are shapes like
// `ffx_fsr2_*.dll`. Deliberately not a regex: the seed contains no other
// metacharacter and a regex engine here would be a bigger surface than the job.
bool GlobMatch(const char* pat, const char* text) {
    const char* star = nullptr;
    const char* back = nullptr;
    while (*text != '\0') {
        const char pc = static_cast<char>(tolower(static_cast<unsigned char>(*pat)));
        const char tc = static_cast<char>(tolower(static_cast<unsigned char>(*text)));
        if (*pat == '*') {
            star = pat++;
            back = text;
        } else if (*pat != '\0' && pc == tc) {
            ++pat;
            ++text;
        } else if (star != nullptr) {
            pat = star + 1;
            text = ++back;
        } else {
            return false;
        }
    }
    while (*pat == '*') {
        ++pat;
    }
    return *pat == '\0';
}

// Parse only `capabilities`. Everything else in the file is ignored on purpose.
bool LoadCapabilities(const char* path) {
    std::FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || f == nullptr) {
        std::printf("  could not open %s\n", path);
        return false;
    }
    static char  text[1 << 20];
    const size_t n = std::fread(text, 1, sizeof(text) - 1, f);
    std::fclose(f);
    if (n == 0) {
        return false;
    }
    text[n] = '\0';

    static jsmntok_t toks[16384];
    jsmn_parser      p;
    jsmn_init(&p);
    const int count = jsmn_parse(&p, text, static_cast<unsigned>(n), toks, static_cast<unsigned>(16384));
    if (count < 1) {
        std::printf("  rules file did not parse (jsmn %d)\n", count);
        return false;
    }

    int capsArr = -1;
    for (int i = 1; i < count; ++i) {
        if (toks[i].parent == 0 && toks[i].type == JSMN_STRING && TokenIs(text, toks[i], "capabilities")) {
            capsArr = i + 1;
            break;
        }
    }
    if (capsArr < 0 || toks[capsArr].type != JSMN_ARRAY) {
        std::printf("  no `capabilities` array\n");
        return false;
    }

    for (int i = capsArr + 1; i < count && g_capCount < kMaxCaps; ++i) {
        if (toks[i].parent != capsArr || toks[i].type != JSMN_OBJECT) {
            continue;
        }
        Capability& c = g_caps[g_capCount];
        for (int k = i + 1; k < count; ++k) {
            if (toks[k].parent != i || toks[k].type != JSMN_STRING) {
                continue;
            }
            if (TokenIs(text, toks[k], "id")) {
                CopyTok(text, toks[k + 1], c.id, sizeof(c.id));
            } else if (TokenIs(text, toks[k], "name")) {
                CopyTok(text, toks[k + 1], c.name, sizeof(c.name));
            } else if (TokenIs(text, toks[k], "signals") && toks[k + 1].type == JSMN_ARRAY) {
                const int arr = k + 1;
                for (int s = arr + 1; s < count && c.signalCount < kMaxSignalsPerCap; ++s) {
                    if (toks[s].parent != arr) {
                        continue;
                    }
                    if (CopyTok(text, toks[s], c.signals[c.signalCount], sizeof(c.signals[0]))) {
                        ++c.signalCount;
                    }
                }
            }
        }
        if (c.id[0] != '\0' && c.signalCount > 0) {
            ++g_capCount;
        }
    }
    return g_capCount > 0;
}

void MarkOnDisk(const wchar_t* dir) {
    wchar_t pattern[MAX_PATH]{};
    if (_snwprintf_s(pattern, MAX_PATH, _TRUNCATE, L"%s\\*", dir) < 0) {
        return;
    }
    WIN32_FIND_DATAW fd{};
    HANDLE           h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        char name[MAX_PATH]{};
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), nullptr, nullptr) <= 0) {
            continue;
        }
        for (int i = 0; i < g_capCount; ++i) {
            for (int s = 0; s < g_caps[i].signalCount; ++s) {
                if (GlobMatch(g_caps[i].signals[s], name)) {
                    g_caps[i].onDisk = true;
                }
            }
        }
    } while (FindNextFileW(h, &fd) != 0);
    FindClose(h);
}

// The load path is unused here: the baseline answers "is this capability's DLL
// present", which the base name settles. It is in the signature because the
// guard's enumerator carries it for §S22(b)'s ownership question.
bool ModuleSink(void*, const char* name, const wchar_t* /*path*/) {
    for (int i = 0; i < g_capCount; ++i) {
        for (int s = 0; s < g_caps[i].signalCount; ++s) {
            if (GlobMatch(g_caps[i].signals[s], name)) {
                g_caps[i].loaded = true;
            }
        }
    }
    return true;    // never stop early: we want every capability, not the first
}

// REUSES the guard's enumerator rather than writing a second one. It is the
// measured, fail-closed one (LIST_MODULES_ALL, spike-notes §1), and a probe with
// its own module walk would drift from the thing the product actually uses.
fl::guard::Collected ScanModules(unsigned pid) {
    const fl::guard::Sources s = fl::guard::SystemSources();
    if (s.EnumerateModules == nullptr) {
        return fl::guard::Collected::kFailed;
    }
    return s.EnumerateModules(pid, &ModuleSink, nullptr);
}

const char* CollectedName(fl::guard::Collected c) {
    switch (c) {
    case fl::guard::Collected::kOk:
        return "ok";
    case fl::guard::Collected::kFailed:
        return "FAILED";
    case fl::guard::Collected::kIncomplete:
        return "INCOMPLETE";
    }
    return "?";
}

void ResetFindings() {
    for (int i = 0; i < g_capCount; ++i) {
        g_caps[i].onDisk = false;
        g_caps[i].loaded = false;
    }
}

int LoadedCount() {
    int n = 0;
    for (int i = 0; i < g_capCount; ++i) {
        n += g_caps[i].loaded ? 1 : 0;
    }
    return n;
}

int Fail(const char* what) {
    std::printf("[FAIL] %s\n", what);
    return 1;
}

// The both-directions self-test. A probe that never reports "loaded" would pass
// any one-sided check, so this plants a module and asserts the answer CHANGES.
//
// The planted module is OUR OWN DLL copied under a capability name — 14_TESTING
// §Integration tests: a renamed harmless DLL, never a vendor binary. Nothing
// belonging to NVIDIA, AMD or Intel is shipped, downloaded or executed.
int SelfTest() {
    const unsigned self = GetCurrentProcessId();

    ResetFindings();
    const fl::guard::Collected before = ScanModules(self);
    if (before != fl::guard::Collected::kOk) {
        return Fail("the baseline scan of our own process did not complete");
    }
    if (LoadedCount() != 0) {
        return Fail("a capability was already loaded, so the planted case below would prove nothing");
    }
    std::printf("[PASS] a clean process reports no capability loaded\n");

    // Copy ourselves under the first signal of the first capability.
    wchar_t src[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, src, MAX_PATH) == 0) {
        return Fail("could not locate our own image");
    }
    wchar_t dir[MAX_PATH]{};
    wcscpy_s(dir, src);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash == nullptr) {
        return Fail("our own path has no separator");
    }
    *slash = L'\0';

    // A literal signal, not a glob, so the planted file name is exact.
    const char* signal = nullptr;
    int         capIndex = -1;
    for (int i = 0; i < g_capCount && signal == nullptr; ++i) {
        for (int s = 0; s < g_caps[i].signalCount; ++s) {
            if (std::strchr(g_caps[i].signals[s], '*') == nullptr) {
                signal = g_caps[i].signals[s];
                capIndex = i;
                break;
            }
        }
    }
    if (signal == nullptr) {
        return Fail("no literal capability signal to plant");
    }

    wchar_t wideSignal[128]{};
    MultiByteToWideChar(CP_UTF8, 0, signal, -1, wideSignal, 128);
    wchar_t planted[MAX_PATH]{};
    if (_snwprintf_s(planted, MAX_PATH, _TRUNCATE, L"%s\\%s", dir, wideSignal) < 0) {
        return Fail("planted path too long");
    }

    wchar_t payload[MAX_PATH]{};
    wcscpy_s(payload, FL_PLANT_SOURCE);
    if (!CopyFileW(payload, planted, FALSE)) {
        std::printf("  CopyFileW(%ls -> %ls) failed: %lu\n", payload, planted, GetLastError());
        return Fail("could not plant the renamed module");
    }

    HMODULE h = LoadLibraryW(planted);
    if (h == nullptr) {
        DeleteFileW(planted);
        return Fail("could not load the planted module");
    }

    ResetFindings();
    const fl::guard::Collected after = ScanModules(self);
    const bool                 detected = (after == fl::guard::Collected::kOk) && g_caps[capIndex].loaded;

    FreeLibrary(h);
    DeleteFileW(planted);

    if (!detected) {
        return Fail("the planted capability module was NOT detected as loaded");
    }
    std::printf("[PASS] the planted module '%s' is reported LOADED for capability '%s'\n", signal, g_caps[capIndex].id);

    // And it flips back, so the answer tracks reality rather than latching.
    for (int i = 0; i < 40; ++i) {
        ResetFindings();
        if (ScanModules(self) == fl::guard::Collected::kOk && LoadedCount() == 0) {
            std::printf("[PASS] after unload the scan reports no capability loaded again\n");
            return 0;
        }
        Sleep(25);
    }
    return Fail("the scan latched: it still reports the capability after unload");
}

void Report(unsigned pid, fl::guard::Collected c) {
    std::printf("\n  pid %u — module scan %s\n", pid, CollectedName(c));
    std::printf("  %-12s %-22s %-8s %s\n", "id", "name", "on disk", "loaded");
    for (int i = 0; i < g_capCount; ++i) {
        std::printf("  %-12s %-22s %-8s %s\n", g_caps[i].id, g_caps[i].name, g_caps[i].onDisk ? "yes" : "no",
                    g_caps[i].loaded ? "yes" : "no");
    }
    if (c != fl::guard::Collected::kOk) {
        std::printf("\n  NOT A CLEAN RESULT. An incomplete or failed module list must never be\n"
                    "  read as 'no capability loaded' — that is the empty-list defect.\n");
    }
}

}    // namespace

int wmain(int argc, wchar_t** argv) {
    std::printf("fl-baseline-probe — the module/file baseline for 15_ROADMAP P0 item 3\n");

    const char* rules = FL_SEED_RULES;
    if (!LoadCapabilities(rules)) {
        std::printf("[FAIL] could not read capabilities from %s\n", rules);
        return 1;
    }
    std::printf("  %d capabilities from %s\n", g_capCount, rules);

    bool     selfTest = false;
    unsigned pid = GetCurrentProcessId();
    wchar_t  dir[MAX_PATH]{};
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--self-test") == 0) {
            selfTest = true;
        } else if (wcscmp(argv[i], L"--pid") == 0 && i + 1 < argc) {
            pid = static_cast<unsigned>(wcstoul(argv[++i], nullptr, 10));
        } else if (wcscmp(argv[i], L"--dir") == 0 && i + 1 < argc) {
            wcscpy_s(dir, argv[++i]);
        }
    }

    if (selfTest) {
        return SelfTest();
    }

    if (dir[0] != L'\0') {
        MarkOnDisk(dir);
    }
    const fl::guard::Collected c = ScanModules(pid);
    Report(pid, c);
    return 0;
}
