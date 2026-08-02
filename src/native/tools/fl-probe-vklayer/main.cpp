// fl-probe-vklayer — proves the Vulkan layer's blocklist self-scan (§S2, second
// half) actually discriminates.
//
// An implicit layer is mapped into a process before anything of ours runs, so
// the injection guard structurally cannot cover it. The layer therefore runs
// its own version of pre-injection check 1 against ITSELF at init, and goes
// fully passthrough on any hit.
//
// This probe loads the layer DLL and calls its self-scan twice: once in a clean
// process, and once after loading a HARMLESS DLL RENAMED to look like an
// anti-cheat module — the fixture 14_TESTING §Integration tests specifies
// ("a renamed harmless DLL, not real anti-cheat software"). Without the second
// half the first proves nothing: a self-scan that always says "stay inert"
// would pass a one-sided test and silently disable the layer everywhere.
//
// Nothing here registers the layer with the Vulkan loader and nothing calls a
// Vulkan entry point. The DLL is loaded as an ordinary library and asked a
// question.
//
// The layer reads rules from the ONE location the product uses — under
// %LOCALAPPDATA% — and there is deliberately no way to point it elsewhere; a
// rules path that could be overridden is exactly the hole §S3 closed. So when
// that file is absent the probe INSTALLS THE REPOSITORY SEED THERE and removes
// it afterwards. It never touches an existing file: on a machine with real
// rules installed, those are what gets used.
//
// Without that, this test skipped on any machine that had not run the product
// — and a ctest that always skips is a gate that cannot fail, which is the
// defect class this project keeps finding.

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

// Set when WE created the rules file, so we remove exactly what we added.
bool    g_installedRules = false;
wchar_t g_rulesPath[MAX_PATH]{};
wchar_t g_rulesDir[MAX_PATH]{};

void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

using SelfScanFn = int (*)();
using LayerNameFn = const char* (*)();

// Build a path beside this executable.
bool BesideMe(const wchar_t* leaf, wchar_t* out, DWORD cap) {
    if (GetModuleFileNameW(nullptr, out, cap) == 0) {
        return false;
    }
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash == nullptr) {
        return false;
    }
    slash[1] = L'\0';
    return wcscat_s(out, cap, leaf) == 0;
}

bool ExpandLocalAppData(const wchar_t* relative, wchar_t* out, DWORD cap) {
    // Concatenated rather than formatted: the pattern contains '%' characters
    // that ExpandEnvironmentStringsW needs and printf would eat.
    wchar_t pattern[MAX_PATH]{};
    if (wcscpy_s(pattern, LR"(%LOCALAPPDATA%\)") != 0 || wcscat_s(pattern, relative) != 0) {
        return false;
    }
    return ExpandEnvironmentStringsW(pattern, out, cap) != 0;
}

// Install the repository's seed rules ONLY if nothing is there. Returns false
// if we could not, which the caller reports rather than papering over.
bool EnsureRulesPresent() {
    if (!ExpandLocalAppData(LR"(FrameLedger\rules\detection-rules.json)", g_rulesPath, MAX_PATH) ||
        !ExpandLocalAppData(LR"(FrameLedger\rules)", g_rulesDir, MAX_PATH)) {
        return false;
    }
    if (GetFileAttributesW(g_rulesPath) != INVALID_FILE_ATTRIBUTES) {
        std::printf("  using the rules already installed on this machine (left untouched)\n");
        return true;
    }

    wchar_t seed[MAX_PATH]{};
    wcscpy_s(seed, FL_REPO_RULES);
    if (GetFileAttributesW(seed) == INVALID_FILE_ATTRIBUTES) {
        std::printf("  repository seed not found at %ls\n", seed);
        return false;
    }

    // CreateDirectory per level; failing because it already exists is fine.
    wchar_t parent[MAX_PATH]{};
    if (ExpandLocalAppData(L"FrameLedger", parent, MAX_PATH)) {
        CreateDirectoryW(parent, nullptr);
    }
    CreateDirectoryW(g_rulesDir, nullptr);

    // TRUE = fail if it exists. We already know it does not, and racing another
    // process into overwriting somebody's real rules is not a risk worth taking
    // for a test.
    if (!CopyFileW(seed, g_rulesPath, TRUE)) {
        std::printf("  could not install the seed rules: %lu\n", GetLastError());
        return false;
    }
    g_installedRules = true;
    std::printf("  installed the repository seed rules for this run (removed afterwards)\n");
    return true;
}

void RemoveInstalledRules() {
    if (g_installedRules) {
        DeleteFileW(g_rulesPath);
        RemoveDirectoryW(g_rulesDir);    // only succeeds if we left it empty
        g_installedRules = false;
    }
}

}    // namespace

int main() {
    std::printf("FrameLedger Vulkan layer self-scan probe (20_OPEN_QUESTIONS S2, second half)\n");

    // The layer's REAL build output, not a copy beside us. It was a copy, via a
    // CMake POST_BUILD command, and that was a genuine bug: POST_BUILD runs only
    // when the PROBE relinks, so editing only the layer left a STALE DLL here.
    // A red-green canary run that way left the broken layer behind and every
    // later run kept failing against it — and the same mechanism could just as
    // easily have kept a WORKING copy and reported a broken layer as passing.
    wchar_t layerPath[MAX_PATH]{};
    wcscpy_s(layerPath, FL_VKLAYER_DLL);

    HMODULE layer = LoadLibraryW(layerPath);
    if (layer == nullptr) {
        std::printf("FAILED: LoadLibrary(%ls) -> %lu\n", layerPath, GetLastError());
        return 2;
    }

    auto selfScan = reinterpret_cast<SelfScanFn>(
        reinterpret_cast<void*>(GetProcAddress(layer, "FlVkLayerSelfScanSaysPassthrough")));
    auto layerName = reinterpret_cast<LayerNameFn>(reinterpret_cast<void*>(GetProcAddress(layer, "FlVkLayerName")));
    if (selfScan == nullptr || layerName == nullptr) {
        std::printf("FAILED: the layer does not export its diagnostics\n");
        return 2;
    }
    std::printf("  layer reports itself as %s\n", layerName());

    if (!EnsureRulesPresent()) {
        std::printf("\nFAILED: no rules available, so the self-scan cannot be exercised at all.\n");
        FreeLibrary(layer);
        return 2;
    }

    // --- 1. A clean process: the layer must NOT be forced inert -------------
    //
    // This is the half that makes the second half mean anything. A self-scan
    // that always answered "stay inert" would pass a one-sided test while
    // silently disabling the layer everywhere.
    const bool inertWhenClean = selfScan() != 0;
    Check(!inertWhenClean, "a clean process is NOT forced inert (so the blocked case below can mean something)");

    // --- 2. Plant a renamed harmless DLL and re-scan ------------------------
    //
    // The payload is our own layer DLL copied under a blocklisted name. It is
    // a real, loadable, signed-by-nobody module of ours — never real
    // anti-cheat software, which we do not ship, download, or execute.
    wchar_t plantedPath[MAX_PATH]{};
    if (!BesideMe(L"EasyAntiCheat_probe.dll", plantedPath, MAX_PATH)) {
        Check(false, "build the planted-module path");
        FreeLibrary(layer);
        RemoveInstalledRules();
        return 1;
    }
    if (!CopyFileW(layerPath, plantedPath, FALSE)) {
        std::printf("  CopyFile -> %lu\n", GetLastError());
        Check(false, "plant a renamed harmless DLL");
        FreeLibrary(layer);
        RemoveInstalledRules();
        return 1;
    }

    HMODULE planted = LoadLibraryW(plantedPath);
    Check(planted != nullptr, "the planted module is loaded into this process");

    const bool inertWhenBlocked = selfScan() != 0;
    Check(inertWhenBlocked, "the self-scan now says STAY INERT — a blocklisted module was found");

    // --- 3. The scan must not LATCH -----------------------------------------
    //
    // A scan that stayed inert after the module was gone would disable the
    // layer for the rest of the process. Not a safety problem — inert is the
    // safe direction — but it would make every later reading of this scan
    // meaningless, including the "clean" case above.
    //
    // This step used to be a bare printf with no assertion, in the file this
    // project cites as its assert-both-directions exemplar. A step that reports
    // and never fails is a gate that cannot go red; it is now checked.
    if (planted != nullptr) {
        FreeLibrary(planted);
    }
    // FreeLibrary only drops a reference. Poll rather than sleeping a fixed
    // interval: if the loader has not unmapped it yet, "still inert" is the
    // correct answer and asserting too early would make this flaky.
    bool unmapped = false;
    for (int i = 0; i < 50 && !unmapped; ++i) {
        Sleep(20);
        unmapped = GetModuleHandleW(plantedPath) == nullptr;
    }
    if (unmapped) {
        Check(selfScan() == 0,
              "with the planted module unloaded, the scan reports observable again (it does not latch)");
    } else {
        // Reported, not skipped silently: the assertion genuinely does not
        // apply while the image is still mapped.
        std::printf("  NOT CHECKED: the loader still has the planted image mapped, so inert remains correct\n");
    }

    FreeLibrary(layer);
    DeleteFileW(plantedPath);
    RemoveInstalledRules();

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "VKLAYER SELF-SCAN OK" : "VKLAYER SELF-SCAN FAILURES",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
