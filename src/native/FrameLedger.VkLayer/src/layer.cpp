// FrameLedger.VkLayer — Vulkan implicit layer.
//
// Vulkan titles use a layer INSTEAD of injection: it is the mechanism Khronos
// supports, and it is how OBS and RTSS do it. There is no LoadLibraryW here and
// no remote thread — the loader maps us itself.
//
// THAT IS ALSO THE PROBLEM. An implicit layer is machine-wide: once registered,
// the loader maps this DLL into EVERY Vulkan process on the system, including
// ones the user never associated with FrameLedger, and it does so before
// anything of ours has run. The injection guard cannot help — it is defined as
// pre-injection, and there is no injection here (docs/19_SAFETY §The anti-cheat
// guard, docs/20_OPEN_QUESTIONS §S2).
//
// So this file is built around one rule:
//
//     DO NOTHING UNTIL CERTAIN WE WERE INVITED, AND LET EVERY UNCERTAINTY
//     RESOLVE TO DOING NOTHING.
//
// That is the opposite direction from the injection guard, where an unknown
// means REFUSE-to-inject. Here the risk being managed is our code running
// somewhere it was not invited, so "do nothing" IS the fail-safe.
//
// Two gates, because §S2 has two halves:
//   1. enable_environment in the manifest — MEASURED to work (spike-notes §2):
//      with FRAMELEDGER_ENABLE_VK_LAYER unset the loader locates our manifest
//      and never maps the DLL. A LOADING gate, not a security gate: anything
//      running as the user can set the variable, and the loader checks that the
//      variable EXISTS rather than comparing its value. It shrinks blast
//      radius; it does not authorise.
//   2. The enable-list (docs/17_HOOK_ENGINE §The enable-list), which decides
//      what we INTERCEPT once mapped — not whether we load. That distinction is
//      not stylistic: declining to load crashes the host, measured below.
//
// §S2's second half is now here: an in-layer blocklist scan of our OWN process
// at init, going fully passthrough on any hit. It uses the SAME matcher and the
// SAME rules file as the injection guard (fl_ac_rules.h) — a layer with its own
// blocklist would be a second matcher that can disagree with the first, which
// is the defect the managed facade was built to avoid.
//
// vkQueuePresentKHR is still deliberately NOT hooked: presentation
// interception is P1. Passthrough remains the correct state, not an unfinished
// one — but the gate that will protect it is in place and tested first.

#include <windows.h>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fl_ac_rules.h>
#include <fl_shm.h>
#include <psapi.h>

namespace fl::vklayer {

unsigned int LayoutVersion() noexcept {
    return FL_SHM_LAYOUT_VERSION;
}

namespace {

constexpr const char* kLayerName = "VK_LAYER_FRAMELEDGER_overlay";

// Filled from the loader's chain info at vkCreateInstance / vkCreateDevice.
// Never called without a null check: if the chain is not what we expect we
// decline rather than guess.
PFN_vkGetInstanceProcAddr g_nextGetInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr   g_nextGetDeviceProcAddr = nullptr;

// ---------------------------------------------------------------------------
// The enable-list (docs/17_HOOK_ENGINE §The enable-list).
//
// %LOCALAPPDATA%\FrameLedger\vklayer\enabled.txt — one lowercased process image
// name per line, '#' comments, <= 64 KiB, <= 1024 entries, exact
// case-insensitive match on the image name only.
//
// EVERY failure path returns false. Missing, unreadable, oversized, malformed,
// or simply not listed — all mean "not invited", all mean do nothing.
// ---------------------------------------------------------------------------
constexpr DWORD    kMaxEnableListBytes = 64u * 1024u;
constexpr unsigned kMaxEnableListEntries = 1024u;

bool CurrentProcessImageName(char* out, DWORD cap) {
    char        full[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, full, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return false;
    }
    const char* leaf = std::strrchr(full, '\\');
    leaf = (leaf != nullptr) ? leaf + 1 : full;
    if (std::strlen(leaf) >= cap) {
        return false;
    }
    strcpy_s(out, cap, leaf);
    return true;
}

bool EnableListPath(char* out, size_t cap) {
    char*  base = nullptr;
    size_t len = 0;
    if (_dupenv_s(&base, &len, "LOCALAPPDATA") != 0 || base == nullptr) {
        return false;
    }
    const int written = _snprintf_s(out, cap, _TRUNCATE, "%s\\FrameLedger\\vklayer\\enabled.txt", base);
    std::free(base);
    return written > 0;
}

bool ThisProcessIsEnabled() {
    char image[MAX_PATH]{};
    if (!CurrentProcessImageName(image, MAX_PATH)) {
        return false;
    }
    char path[MAX_PATH]{};
    if (!EnableListPath(path, MAX_PATH)) {
        return false;
    }

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;    // absent or unreadable — not invited
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > kMaxEnableListBytes) {
        CloseHandle(h);
        return false;    // oversized reads as corrupt, never as permissive
    }

    char* buf = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(size.QuadPart) + 1));
    if (buf == nullptr) {
        CloseHandle(h);
        return false;
    }
    DWORD      read = 0;
    const BOOL ok = ReadFile(h, buf, static_cast<DWORD>(size.QuadPart), &read, nullptr);
    CloseHandle(h);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, buf);
        return false;
    }
    buf[read] = '\0';

    bool     enabled = false;
    unsigned entries = 0;
    char*    ctx = nullptr;
    for (char* line = strtok_s(buf, "\r\n", &ctx); line != nullptr; line = strtok_s(nullptr, "\r\n", &ctx)) {
        while (*line == ' ' || *line == '\t') {
            ++line;
        }
        if (*line == '\0' || *line == '#') {
            continue;
        }
        if (++entries > kMaxEnableListEntries) {
            enabled = false;    // over the bound: treat the whole file as corrupt
            break;
        }
        // Exact, case-insensitive, image name only — never a prefix, never a
        // substring, never a path. Each of those matches more than intended,
        // and here "more than intended" means loading into processes nobody
        // opted in.
        if (_stricmp(line, image) == 0) {
            enabled = true;
        }
    }

    HeapFree(GetProcessHeap(), 0, buf);
    return enabled;
}

// ---------------------------------------------------------------------------
// §S2 second half — the in-layer blocklist scan.
//
// An implicit layer is mapped into a process before anything of ours runs, so
// the injection guard structurally cannot cover it. This is the layer's own
// version of pre-injection check 1, run against ITSELF.
//
// EVERY uncertainty resolves to "blocked", i.e. to passthrough. That is the
// opposite polarity from the injection guard, where an unknown means refuse to
// inject — but it is the same principle: do the thing that leaves the host
// alone. Rules unreadable, malformed, incomplete, module enumeration failed,
// or an actual hit — all of them mean we observe nothing.
//
// Reuses fl::guard::ParseRules and fl::guard::MatchName. Not a copy: a layer
// with its own matcher would be a second blocklist that can disagree with the
// guard's, and the day they diverge one is wrong with nothing to say which.
// ---------------------------------------------------------------------------
bool RunSelfScan() noexcept {
    using namespace fl::guard;

    // Heap, not static. This runs once at init inside a process we do not own,
    // and 17_HOOK_ENGINE budgets 8 MB resident for everything of ours in a
    // game — so the ~1 MiB of rules text and ~155 KB of parsed rules are
    // released the moment the verdict is known. Init is not a hook path, so
    // allocating here breaks no rule; keeping it resident would waste budget
    // on data we never look at again.
    auto* text = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, kMaxRulesBytes));
    if (text == nullptr) {
        return true;
    }
    auto* rules = static_cast<Rules*>(HeapAlloc(GetProcessHeap(), 0, sizeof(Rules)));
    if (rules == nullptr) {
        HeapFree(GetProcessHeap(), 0, text);
        return true;
    }

    bool              inert = true;
    const std::size_t n = ReadRulesFile(text, kMaxRulesBytes);
    if (n != static_cast<std::size_t>(-1) && n != 0 && ParseRules(text, n, *rules) == ParseResult::kOk) {
        HMODULE mods[1024]{};
        DWORD   needed = 0;
        // Our own process, so no OpenProcess and no rights question — but
        // LIST_MODULES_ALL all the same, for the same reason the guard uses it.
        if (EnumProcessModulesEx(GetCurrentProcess(), mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
            const bool   truncated = needed > sizeof(mods);
            const size_t count = (truncated ? sizeof(mods) : needed) / sizeof(HMODULE);
            bool         hit = truncated;    // a partial list is not a clean one
            for (size_t i = 0; i < count && !hit; ++i) {
                char name[MAX_PATH]{};
                if (GetModuleBaseNameA(GetCurrentProcess(), mods[i], name, MAX_PATH) == 0) {
                    hit = true;    // a module we could not name is one we could not check
                    break;
                }
                if (MatchName(*rules, Group::kModules, name) != nullptr) {
                    hit = true;
                }
            }
            inert = hit;
        }
    }

    HeapFree(GetProcessHeap(), 0, rules);
    HeapFree(GetProcessHeap(), 0, text);
    return inert;
}

// Evaluated once. The answer cannot change usefully within a process lifetime:
// anti-cheat that loads AFTER us is the mid-session case, which needs the
// runtime unhook §S2 still lists as open, not a re-read here.
bool MustStayInert() noexcept {
    static const bool inert = RunSelfScan();
    return inert;
}

// ---------------------------------------------------------------------------
// Loader interface. Everything forwards; nothing is observed.
// ---------------------------------------------------------------------------
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* pName);

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo*  pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    auto* chain = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));
    while (chain != nullptr &&
           !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO)) {
        chain = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain->pNext));
    }
    if (chain == nullptr || chain->u.pLayerInfo == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_nextGetInstanceProcAddr = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    if (g_nextGetInstanceProcAddr == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto createInstance =
        reinterpret_cast<PFN_vkCreateInstance>(g_nextGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    if (createInstance == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Advance the chain for the layer below us before calling down.
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    return createInstance(pCreateInfo, pAllocator, pInstance);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    auto* chain = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));
    while (chain != nullptr &&
           !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO)) {
        chain = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain->pNext));
    }
    if (chain == nullptr || chain->u.pLayerInfo == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const PFN_vkGetInstanceProcAddr nextInstanceProc = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    g_nextGetDeviceProcAddr = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    if (nextInstanceProc == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(nextInstanceProc(nullptr, "vkCreateDevice"));
    if (createDevice == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    return createDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

// Evaluated once, on first use. The enable-list decides what we INTERCEPT, not
// whether we load — see vkNegotiateLoaderLayerInterfaceVersion for why the
// latter is not an option this loader supports. Reading the file on every
// vkGetInstanceProcAddr call would also put file I/O on a hot-ish path for no
// benefit; the answer cannot change within a process lifetime.
bool ThisProcessIsEnabledCached() {
    static const bool enabled = ThisProcessIsEnabled();
    return enabled;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (pName == nullptr) {
        return nullptr;
    }

    // THE GATE, in its only safe position. Not invited => hand back whatever
    // the next layer down would have returned, for every single name. We stay
    // in the chain (the loader requires that) and observe nothing.
    //
    // vkCreateInstance and vkCreateDevice are the exception: we must return our
    // own for those regardless, because that is how the chain is walked and
    // g_next* get populated. They forward unconditionally and inspect nothing.
    if ((!ThisProcessIsEnabledCached() || MustStayInert()) && std::strcmp(pName, "vkCreateInstance") != 0 &&
        std::strcmp(pName, "vkCreateDevice") != 0 && std::strcmp(pName, "vkGetInstanceProcAddr") != 0) {
        return (g_nextGetInstanceProcAddr != nullptr) ? g_nextGetInstanceProcAddr(instance, pName) : nullptr;
    }

    // Exactly the entry points needed to stay in the chain and forward.
    // vkQueuePresentKHR is deliberately absent — see the file header.
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr);
    }
    if (std::strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateInstance);
    }
    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
    }
    if (g_nextGetInstanceProcAddr == nullptr) {
        return nullptr;
    }
    return g_nextGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (pName == nullptr) {
        return nullptr;
    }
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    }
    if (g_nextGetDeviceProcAddr == nullptr) {
        return nullptr;
    }
    return g_nextGetDeviceProcAddr(device, pName);
}

}    // namespace
}    // namespace fl::vklayer

// ---------------------------------------------------------------------------
// Exports. Real names, no ordinals, no aliasing — a loader log or an anti-cheat
// vendor must be able to say what this is (docs/19_SAFETY). The export list
// lives in FrameLedger.VkLayer.def: the Vulkan headers already declare
// vkGetInstanceProcAddr without dllexport, so re-declaring it with one is a
// linkage conflict (C2375).
// ---------------------------------------------------------------------------
extern "C" {

// The modern negotiation entry point. Returning anything other than VK_SUCCESS
// makes the loader skip us entirely — a documented, supported way to be absent,
// and what we use when the process was not opted in.
// ALWAYS SUCCEEDS (given a well-formed struct). Do not "decline" here.
//
// MEASURED 2026-08-02 against loader 1.4.357, and it cost an afternoon to find:
// returning VK_ERROR_INITIALIZATION_FAILED from this function does NOT make the
// loader politely skip us — it ACCESS-VIOLATES the host application. Reproduced
// every time, with and without VK_LOADER_DEBUG:
//
//     enable-list absent                 -> 0xC0000005 in vulkaninfo
//     enable-list present, not listed    -> 0xC0000005 in vulkaninfo
//     enable-list present, listed        -> ok
//
// So the obvious-looking gate — "refuse to negotiate unless invited" — would
// have crashed EVERY Vulkan application on the machine that was not in our
// enable-list. That is not a smaller blast radius than loading everywhere; it
// is a much larger one, and it is the exact opposite of what §S2 is for.
//
// The gate therefore lives in what we DO, not in whether we load: accept the
// negotiation, join the chain, and forward everything untouched unless this
// process was invited. Being present and inert is cheap; being absent by
// erroring out is not something this loader supports.
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (pVersionStruct == nullptr || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pVersionStruct->loaderLayerInterfaceVersion > 2) {
        pVersionStruct->loaderLayerInterfaceVersion = 2;
    }
    pVersionStruct->pfnGetInstanceProcAddr = &fl::vklayer::GetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = &fl::vklayer::GetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}

// Legacy entry points for loaders predating negotiation. Same rule: forward,
// never return nullptr as a way of opting out. Handing the loader a null
// vkCreateInstance is the same class of failure as declining negotiation.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return fl::vklayer::GetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    return fl::vklayer::GetDeviceProcAddr(device, pName);
}

// Diagnostics for the blast-radius test: lets it assert what this binary is
// without loading it into a Vulkan process.
const char* FlVkLayerName() {
    return fl::vklayer::kLayerName;
}

int FlVkLayerWouldActivate() {
    return (fl::vklayer::ThisProcessIsEnabledCached() && !fl::vklayer::MustStayInert()) ? 1 : 0;
}

// Diagnostics for the §S2 test: runs the blocklist self-scan and reports
// whether this process would be left alone. Uncached, so a test can plant a
// module and see the answer change.
int FlVkLayerSelfScanSaysPassthrough() {
    return fl::vklayer::RunSelfScan() ? 1 : 0;
}

}    // extern "C"
