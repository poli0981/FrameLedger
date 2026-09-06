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
//      running as the user can set the variable. The loader compares the
//      variable's VALUE, not merely its existence (spike-notes §2), so a stray
//      `=0` does not enable us. It shrinks blast radius; it does not authorise.
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

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fl_ac_rules.h>
#include <fl_ring.h>
#include <fl_shm.h>
#include <fl_shm_host.h>
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

// §S21. Shell-resolved and wide, for the same two reasons the rules path is:
// the environment variable was inherited from whoever launched the game, and the
// ANSI form silently failed on any profile path the active code page cannot
// spell. The second one mattered more here than it looks — a `ja` or `vi` user
// would have had a layer that loaded into every Vulkan process and then never
// found its own enable-list, i.e. Vulkan Tier 1 quietly never working, with no
// error anywhere. Shares LocalAppDataDir with the guard rather than carrying a
// second copy of the resolution.
//
// Residual, stated rather than discovered later: the enable-list's CONTENTS are
// still compared as ANSI image names (CurrentProcessImageName above). That is a
// file-format question, not a path question, and it is left alone here.
bool EnableListPath(wchar_t* out, size_t cap) {
    wchar_t base[fl::guard::kMaxRulesPathLen]{};
    if (!fl::guard::LocalAppDataDir(base, fl::guard::kMaxRulesPathLen)) {
        return false;
    }
    const int written = _snwprintf_s(out, cap, _TRUNCATE, L"%s\\FrameLedger\\vklayer\\enabled.txt", base);
    return written > 0;
}

bool ThisProcessIsEnabled() {
    char image[MAX_PATH]{};
    if (!CurrentProcessImageName(image, MAX_PATH)) {
        return false;
    }
    wchar_t path[fl::guard::kMaxRulesPathLen]{};
    if (!EnableListPath(path, fl::guard::kMaxRulesPathLen)) {
        return false;
    }

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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
// The capture side (P1 item 3): the ring, the present record, the stop.
//
// Created at the first vkCreateDevice of an ADMITTED process -- not at load, not
// at vkCreateInstance -- so a process the gates refused never has a mapping
// with its pid on it. Init is not a hook path; the present hook is, and it
// follows the Overlay's rules: SEH-guarded, allocation-free, lock-free, no
// logging, one record, forward.
// ---------------------------------------------------------------------------
bool ThisProcessIsEnabledCached();

// The supervision deadline. FL_VKLAYER_TICK_DEADLINE_MS exists so a TEST-ONLY
// flavour of this DLL (fl_vklayer_shortdeadline, never shipped) can prove the
// stop within seconds; the shipped layer uses the one number 07_IPC pins, the
// same the Overlay uses.
#ifndef FL_VKLAYER_TICK_DEADLINE_MS
#define FL_VKLAYER_TICK_DEADLINE_MS FL_GUARD_TICK_DEADLINE_MS
#endif
constexpr ULONGLONG kTickDeadlineMs = FL_VKLAYER_TICK_DEADLINE_MS;

HANDLE                 g_mapping = nullptr;
void*                  g_base = nullptr;
FlWriterState*         g_state = nullptr;
FlControlBlock*        g_control = nullptr;
fl::RingWriter         g_writer;
std::atomic<uint32_t>  g_observing{0};
std::atomic<uint32_t>  g_captureState{0};    // 0 = not tried, 1 = live, 2 = failed (inert for good)
std::atomic<uint32_t>  g_frameIndex{0};
std::atomic<uint32_t>  g_lastTicks{0};
std::atomic<ULONGLONG> g_lastTickAt{0};

// Which device a queue belongs to is answered by the dispatch key: the loader
// gives a VkDevice and every VkQueue created from it the SAME first-pointer
// (the dispatch table), which is how every layer keys its per-device state.
inline void* DispatchKey(const void* dispatchable) noexcept {
    return *reinterpret_cast<void* const*>(dispatchable);
}

struct DeviceSlot {
    void*                     key = nullptr;
    PFN_vkQueuePresentKHR     present = nullptr;
    PFN_vkCreateSwapchainKHR  createSwapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchain = nullptr;
};
constexpr size_t      kMaxDevices = 8;
DeviceSlot            g_devices[kMaxDevices];
std::atomic<uint32_t> g_deviceCount{0};

bool HasDevice(void* key) noexcept {
    const uint32_t n = g_deviceCount.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n && i < kMaxDevices; ++i) {
        if (g_devices[i].key == key) {
            return true;
        }
    }
    return false;
}

const DeviceSlot* FindDevice(void* key) noexcept {
    const uint32_t n = g_deviceCount.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n && i < kMaxDevices; ++i) {
        if (g_devices[i].key == key) {
            return &g_devices[i];
        }
    }
    // A queue whose device we never registered (more than kMaxDevices, or a
    // device created before the gates admitted us): forward through the first
    // device's next pointer rather than through nothing.
    return n != 0 ? &g_devices[0] : nullptr;
}

// Per-swapchain output size, keyed by the non-dispatchable handle; id 0 stays
// "unidentified" (fl_shm.h), exactly as the Overlay's table does.
struct SwapchainSlot {
    std::atomic<uint64_t> handle{0};
    uint32_t              id = 0;
    uint16_t              w = 0;
    uint16_t              h = 0;
};
constexpr size_t      kMaxSwapchains = 16;
SwapchainSlot         g_swapchains[kMaxSwapchains];
std::atomic<uint32_t> g_nextSwapchainId{1};

const SwapchainSlot* FindSwapchain(VkSwapchainKHR sc) noexcept {
    const uint64_t key = reinterpret_cast<uint64_t>(sc);
    for (size_t i = 0; i < kMaxSwapchains; ++i) {
        if (g_swapchains[i].handle.load(std::memory_order_acquire) == key) {
            return &g_swapchains[i];
        }
    }
    return nullptr;
}

void RememberSwapchain(VkSwapchainKHR sc, VkExtent2D extent) noexcept {
    const uint64_t key = reinterpret_cast<uint64_t>(sc);
    for (size_t i = 0; i < kMaxSwapchains; ++i) {
        uint64_t none = 0;
        if (g_swapchains[i].handle.compare_exchange_strong(none, ~0ull, std::memory_order_acq_rel)) {
            g_swapchains[i].id = g_nextSwapchainId.fetch_add(1u, std::memory_order_relaxed);
            g_swapchains[i].w = static_cast<uint16_t>(extent.width > 0xFFFFu ? 0u : extent.width);
            g_swapchains[i].h = static_cast<uint16_t>(extent.height > 0xFFFFu ? 0u : extent.height);
            g_swapchains[i].handle.store(key, std::memory_order_release);    // visible LAST
            return;
        }
    }
    // Full: the present will carry id 0 = unidentified, never a wrong id.
}

void ForgetSwapchain(VkSwapchainKHR sc) noexcept {
    const uint64_t key = reinterpret_cast<uint64_t>(sc);
    for (size_t i = 0; i < kMaxSwapchains; ++i) {
        uint64_t have = key;
        if (g_swapchains[i].handle.compare_exchange_strong(have, 0ull, std::memory_order_acq_rel)) {
            return;
        }
    }
}

// "Stops" = passthrough forever, with the reason on the mapping. There is no
// unhook: the loader owns the chain, and the Overlay's MH_DisableHook has no
// equivalent here. First reason wins, exactly as in the Overlay.
void StopObserving(uint32_t reason) noexcept {
    uint32_t live = 1;
    if (!g_observing.compare_exchange_strong(live, 0u, std::memory_order_acq_rel)) {
        return;
    }
    if (g_state != nullptr) {
        std::atomic_ref<uint32_t> status{g_state->status};
        status.store(reason, std::memory_order_release);
    }
}

LONG FlFilter(DWORD code) noexcept {
    if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void NoteFault() noexcept {
    if (g_state == nullptr) {
        return;
    }
    std::atomic_ref<uint32_t> faults{g_state->faultCount};
    if (faults.fetch_add(1, std::memory_order_acq_rel) + 1 >= 3) {
        StopObserving(fl::FL_STATUS_SELF_DISABLED);    // 17_HOOK_ENGINE §Fault policy: three faults, dormant
    }
}

// The present-path decision. Same order and same polarity as the Overlay's
// MayObserve, minus the LoadLibrary detour it has no equivalent of -- plus the
// supervision check that the Overlay runs on its watchdog and this layer cannot
// (§S2 part three). guardTicks counts COMPLETED guard evaluations, not seconds;
// "never advanced" and "stopped advancing" are the same state, and the clock
// starts when the mapping is published.
bool MayObserve() noexcept {
    if (g_observing.load(std::memory_order_acquire) == 0u) {
        return false;
    }
    if (g_control == nullptr) {
        return true;
    }
    std::atomic_ref<uint32_t> unhook{g_control->unhookRequested};
    if (unhook.load(std::memory_order_acquire) != 0u) {
        StopObserving(fl::FL_STATUS_UNHOOKED);
        return false;
    }

    std::atomic_ref<uint32_t> ticks{g_control->guardTicks};
    const uint32_t            now = ticks.load(std::memory_order_acquire);
    const ULONGLONG           t = GetTickCount64();
    if (now != g_lastTicks.load(std::memory_order_relaxed)) {
        g_lastTicks.store(now, std::memory_order_relaxed);
        g_lastTickAt.store(t, std::memory_order_relaxed);
    } else if (t - g_lastTickAt.load(std::memory_order_relaxed) >= kTickDeadlineMs) {
        StopObserving(fl::FL_STATUS_UNHOOKED);
        return false;
    }

    std::atomic_ref<uint32_t> paused{g_control->pauseRequested};
    return paused.load(std::memory_order_relaxed) == 0u;
}

// Once, on the first admitted vkCreateDevice. Failure -- most likely the ring
// already existing because an Overlay got there first -- is final: the layer
// forwards and observes nothing, which is what an un-owned ring requires.
bool EnsureCapture() noexcept {
    uint32_t expected = 0;
    if (!g_captureState.compare_exchange_strong(expected, 3u, std::memory_order_acq_rel)) {
        // 3 = in progress on another thread; treat as not yet live.
        return g_captureState.load(std::memory_order_acquire) == 1u;
    }
    if (!fl::shmhost::CreateRingMapping(g_mapping, g_base)) {
        g_captureState.store(2u, std::memory_order_release);
        return false;
    }
    fl::shmhost::PublishHandshake(g_base);
    g_state = reinterpret_cast<FlWriterState*>(static_cast<unsigned char*>(g_base) + FL_SHM_WRITER_OFFSET);
    g_control = reinterpret_cast<FlControlBlock*>(static_cast<unsigned char*>(g_base) + FL_SHM_CONTROL_OFFSET);
    if (!g_writer.Init(g_base, FL_SHM_DEFAULT_CAPACITY)) {
        g_captureState.store(2u, std::memory_order_release);
        return false;
    }
    g_lastTicks.store(0, std::memory_order_relaxed);
    g_lastTickAt.store(GetTickCount64(), std::memory_order_relaxed);
    {
        std::atomic_ref<uint32_t> api{g_state->apiMask};
        api.fetch_or(1u << fl::FL_API_VULKAN, std::memory_order_relaxed);
        std::atomic_ref<uint32_t> hooks{g_state->hooksInstalledMask};
        hooks.fetch_or(static_cast<uint32_t>(fl::FL_HOOK_PRESENT), std::memory_order_relaxed);
        // No DXGI counter exists on this path; the word stays "not read".
        std::atomic_ref<uint32_t> before{g_state->dxgiPresentsBeforeHook};
        before.store(0xFFFFFFFFu, std::memory_order_relaxed);
        std::atomic_ref<uint32_t> status{g_state->status};
        status.store(fl::FL_STATUS_READY, std::memory_order_release);
    }
    g_observing.store(1u, std::memory_order_release);
    g_captureState.store(1u, std::memory_order_release);
    return true;
}

// RULE 4: `pPresentInfo` is the argument of the API we intercept, and the only
// thing read from it is which swapchain(s) it names -- to look up an output size
// WE recorded at creation. Nothing of the game's is dereferenced beyond that.
void RecordPresent(const VkPresentInfoKHR* info) noexcept {
    if (!MayObserve()) {
        return;
    }
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);

    const SwapchainSlot* slot = nullptr;
    if (info != nullptr && info->swapchainCount > 0u && info->pSwapchains != nullptr) {
        slot = FindSwapchain(info->pSwapchains[0]);
    }

    FlFrameRecord rec{};
    rec.qpc = static_cast<uint64_t>(qpc.QuadPart);
    rec.frameIndex = g_frameIndex.fetch_add(1u, std::memory_order_relaxed);
    rec.api = static_cast<uint8_t>(fl::FL_API_VULKAN);
    if (slot != nullptr) {
        rec.swapchainId = slot->id;
        rec.outputW = slot->w;
        rec.outputH = slot->h;
    }
    // vkQueuePresentKHR carries no sync interval and no present flags, which is
    // exactly why FL_MEASURED_PRESENT_ARGS exists rather than being assumed
    // (fl_shm.h); only the output size may be claimed, and only when there is one.
    rec.measuredMask = (rec.outputW != 0u && rec.outputH != 0u) ? fl::FL_MEASURED_OUTPUT_RES : 0u;
    g_writer.Publish(rec);
}

// SEH around OUR code only, never around the forward (the Overlay's rule: a
// game's fault inside the next layer must not be counted as ours). A separate
// function because __try cannot share a frame with objects that need unwinding.
void RecordPresentGuarded(const VkPresentInfoKHR* info) noexcept {
    __try {
        RecordPresent(info);
    } __except (FlFilter(GetExceptionCode())) {
        NoteFault();
    }
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    const DeviceSlot* d = (queue != nullptr) ? FindDevice(DispatchKey(queue)) : nullptr;
    RecordPresentGuarded(pPresentInfo);
    if (d == nullptr || d->present == nullptr) {
        return VK_ERROR_DEVICE_LOST;    // unreachable by construction: we only hand out this hook for registered
                                        // devices
    }
    return d->present(queue, pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    const DeviceSlot* d = (device != nullptr) ? FindDevice(DispatchKey(device)) : nullptr;
    if (d == nullptr || d->createSwapchain == nullptr) {
        return VK_ERROR_DEVICE_LOST;
    }
    const VkResult r = d->createSwapchain(device, pCreateInfo, pAllocator, pSwapchain);
    if (r == VK_SUCCESS && pCreateInfo != nullptr && pSwapchain != nullptr && *pSwapchain != VK_NULL_HANDLE) {
        RememberSwapchain(*pSwapchain, pCreateInfo->imageExtent);    // rule 4: the API's own argument
    }
    return r;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                               const VkAllocationCallbacks* pAllocator) {
    if (swapchain != VK_NULL_HANDLE) {
        ForgetSwapchain(swapchain);
    }
    const DeviceSlot* d = (device != nullptr) ? FindDevice(DispatchKey(device)) : nullptr;
    if (d != nullptr && d->destroySwapchain != nullptr) {
        d->destroySwapchain(device, swapchain, pAllocator);
    }
}

// After the next layer's vkCreateDevice succeeded in an admitted process: the
// ring (once) and this device's next-pointers, resolved through the chain's own
// GetDeviceProcAddr so we call whatever sits below us and never the loader's
// trampoline for ourselves.
void RegisterDevice(VkDevice device) noexcept {
    if (device == nullptr || g_nextGetDeviceProcAddr == nullptr || HasDevice(DispatchKey(device))) {
        return;
    }
    // THE NEXT POINTERS FIRST, UNCONDITIONALLY. Once GetDeviceProcAddr has handed
    // out our hooks, every present on this device passes through them, and a hook
    // with nothing to forward to would fail the application's present -- measured
    // 2026-09-06 as VK_ERROR_DEVICE_LOST on the harness when an injected Overlay
    // owned the ring and the layer had registered nothing. The ring failing to
    // come up must only ever mean "observe nothing", never "forward nothing".
    const uint32_t i = g_deviceCount.load(std::memory_order_acquire);
    if (i >= kMaxDevices) {
        return;
    }
    DeviceSlot& d = g_devices[i];
    d.key = DispatchKey(device);
    d.present = reinterpret_cast<PFN_vkQueuePresentKHR>(g_nextGetDeviceProcAddr(device, "vkQueuePresentKHR"));
    d.createSwapchain =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(g_nextGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
    d.destroySwapchain =
        reinterpret_cast<PFN_vkDestroySwapchainKHR>(g_nextGetDeviceProcAddr(device, "vkDestroySwapchainKHR"));
    g_deviceCount.store(i + 1u, std::memory_order_release);    // visible LAST

    // Then the capture side, whose failure (an Overlay already owning the ring,
    // most likely) leaves the hooks in place as pure forwarders.
    EnsureCapture();
}

bool Active() noexcept {
    return ThisProcessIsEnabledCached() && !MustStayInert();
}

// The three names we answer for, in an admitted process, from either proc-addr
// entry: applications and layers above us may fetch device functions through
// vkGetInstanceProcAddr as well.
PFN_vkVoidFunction OurDeviceFunction(const char* pName) noexcept {
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&QueuePresentKHR);
    }
    if (std::strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateSwapchainKHR);
    }
    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&DestroySwapchainKHR);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Loader interface. Everything forwards; an admitted process is also observed.
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
    const VkResult r = createDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    // The capture side comes up HERE and only here: after the next layer built the
    // device, and only in a process both gates admitted (P1 item 3).
    if (r == VK_SUCCESS && pDevice != nullptr && Active()) {
        RegisterDevice(*pDevice);
    }
    return r;
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

    // The entry points needed to stay in the chain and forward, then ours.
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr);
    }
    if (std::strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateInstance);
    }
    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
    }
    // Past the gate: an admitted process gets our present / swapchain entries.
    if (PFN_vkVoidFunction ours = OurDeviceFunction(pName); ours != nullptr) {
        return ours;
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
    if (Active()) {
        if (PFN_vkVoidFunction ours = OurDeviceFunction(pName); ours != nullptr) {
            // A device that reached us here without passing through CreateDevice
            // (created before the gates were consulted, or beyond the table) is
            // registered now, so the hook we hand out always has a next pointer.
            if (device != nullptr) {
                RegisterDevice(device);
            }
            return ours;
        }
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

// The modern negotiation entry point.
//
// ALWAYS SUCCEEDS (given a well-formed struct). Do not "decline" here.
//
// An earlier version of this comment said the opposite — that returning
// anything other than VK_SUCCESS "makes the loader skip us entirely, a
// documented, supported way to be absent". That is false, it is the design the
// measurement below killed, and it sat directly above the text correcting it.
// Left in place it was an instruction to re-introduce a crash in every Vulkan
// application on the machine, in the one function where that is the cost.
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
