// FrameLedger.Overlay — the injected DLL.
//
// WHAT IS HERE: DllMain, the init thread, the shared mapping and handshake, the
// DXGI present hooks, the ring writer, the fault policy, and the two runtime
// stops -- the Agent's safety unhook and supervision loss.
//
// `status` is INIT until hooks are actually installed and only then READY,
// because READY claims a capture side exists; if MinHook fails it stays INIT and
// the Agent's degradation path is what should run.
//
// NOT HERE: D3D12 and OpenGL device-type refinement (`api` reports D3D11, which
// is what the dummy device resolves), and the upscaler / FG / RT feature hooks.
// The record's measuredMask says so on every frame rather than letting the
// zero-defaults assert a measurement nobody made.
//
// docs/17_HOOK_ENGINE.md is the specification. The constraints that shape every
// line:
//
//   - DllMain(DLL_PROCESS_ATTACH) does ONLY DisableThreadLibraryCalls and
//     CreateThread -> InitThread. Loader-lock rules: anything else here runs
//     while the loader lock is held, and MinHook suspends every other thread to
//     patch (20_OPEN_QUESTIONS §H2).
//   - Every hook body will be SEH-guarded, allocation-free, lock-free and log
//     nothing. Three faults => self-disable and go dormant.
//   - Read nothing but the arguments of APIs we hooked (CLAUDE.md rule 4).
//   - -D_HAS_EXCEPTIONS=0 turns a would-be STL throw into __fastfail, which SEH
//     cannot intercept, so "no throwing STL" is load-bearing rather than
//     stylistic (spike-notes.md §H3).

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstdio>
#include <cstring>
#include <fl_ring.h>
#include <fl_shm.h>
#include <MinHook.h>
#include <sddl.h>

using namespace fl;

namespace {

HANDLE         g_mapping = nullptr;
void*          g_base = nullptr;
fl::RingWriter g_writer;
HANDLE         g_initThread = nullptr;

// The mapping is created with a DACL granting ONLY the current user's SID, and
// lives in the session-scoped Local\ namespace (docs/07_IPC.md §Security). No
// Global\ object: it would need admin and would be visible across sessions for
// no benefit.
//
// Returns false rather than falling back to a default DACL. A mapping the whole
// machine can write is not a degraded version of this one -- the Agent's control
// block is in it, and unhookRequested is the safety stop.
bool BuildUserOnlySecurity(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& sd) noexcept {
    sd = nullptr;

    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    unsigned char buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
    DWORD         got = 0;
    const BOOL    ok = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &got);
    CloseHandle(token);
    if (!ok) {
        return false;
    }

    LPWSTR      sidText = nullptr;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer);
    if (!ConvertSidToStringSidW(user->User.Sid, &sidText)) {
        return false;
    }

    // D:P = a protected DACL, so nothing is inherited in. One ACE, generic all,
    // for us alone.
    wchar_t   sddl[256]{};
    const int n = _snwprintf_s(sddl, _TRUNCATE, L"D:P(A;;GA;;;%ls)", sidText);
    LocalFree(sidText);
    if (n < 0) {
        return false;
    }

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    return true;
}

// Local\FrameLedger.Ring.<pid>. Deliberately plain and identifiable: 19_SAFETY
// forbids obfuscated object names, because being visible to anti-cheat is the
// whole design posture.
bool MakeRingName(wchar_t* out, size_t cap, DWORD pid) noexcept {
    return _snwprintf_s(out, cap, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", pid) >= 0;
}

bool CreateRing() noexcept {
    SECURITY_ATTRIBUTES  sa{};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!BuildUserOnlySecurity(sa, sd)) {
        return false;
    }

    wchar_t name[128]{};
    if (!MakeRingName(name, 128, GetCurrentProcessId())) {
        LocalFree(sd);
        return false;
    }

    const size_t bytes = fl::FlShmSizeForCapacity(FL_SHM_DEFAULT_CAPACITY);
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, static_cast<DWORD>(bytes >> 32),
                                   static_cast<DWORD>(bytes & 0xFFFFFFFFu), name);
    const DWORD err = GetLastError();
    LocalFree(sd);
    if (g_mapping == nullptr) {
        return false;
    }
    // A pre-existing mapping under our own pid is not a mapping we understand:
    // either a stale object or another writer. Refuse rather than share a ring
    // with an unknown producer.
    if (err == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }

    g_base = MapViewOfFile(g_mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bytes);
    if (g_base == nullptr) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }
    return true;
}

// Write-once at init, per docs/07_IPC.md §A + B. adapterLuid is deliberately
// left 0 = "not yet known": this runs two steps before any graphics module is
// resolved, and our own dummy device's adapter is not the game's (#36). It is
// published at first present, with the hook.
void PublishHandshake() noexcept {
    auto* h = reinterpret_cast<FlShmHandshake*>(static_cast<unsigned char*>(g_base) + FL_SHM_HANDSHAKE_OFFSET);
    h->recordSize = sizeof(FlFrameRecord);
    h->capacity = FL_SHM_DEFAULT_CAPACITY;
    h->pid = GetCurrentProcessId();

    // FL_BUILD_ID comes from git describe at configure time (FrameLedger.Shm's
    // CMakeLists). It had no producer at all until #36; without one the Agent's
    // refuse-to-attach-on-mismatch compares "" with "" forever.
    const char* build = FL_BUILD_ID;
    size_t      i = 0;
    for (; i + 1 < sizeof(h->buildId) && build[i] != '\0'; ++i) {
        h->buildId[i] = build[i];
    }
    h->buildId[i] = '\0';

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    h->qpcEpoch = static_cast<uint64_t>(qpc.QuadPart);

    h->adapterLuid = 0;

    // layoutVersion LAST, with a release fence in front of it. It is the field a
    // reader validates first, so it must not become visible before the fields it
    // vouches for. A reader that saw the version while capacity was still zero
    // would compute a ring of no slots and read garbage.
    std::atomic_thread_fence(std::memory_order_release);
    std::atomic_ref<uint32_t> version{h->layoutVersion};
    version.store(FL_SHM_LAYOUT_VERSION, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Fault policy (docs/17_HOOK_ENGINE.md §Fault policy, NFR-3).
// ---------------------------------------------------------------------------
FlWriterState* g_state = nullptr;

// Only OUR code is guarded, never the call to the original -- otherwise a game's
// own fault inside the trampoline would be counted as ours, and ours as the
// game's. Both directions of that confusion are bad.
LONG FlFilter(DWORD code) noexcept {
    // EXCEPTION_BREAKPOINT belongs to a debugger, not to us.
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
    const uint32_t            n = faults.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (n >= 3) {
        // Three faults total => stop writing and go dormant. MH_DisableHook is
        // safe here because we are outside our own guarded body by now.
        MH_DisableHook(MH_ALL_HOOKS);
        std::atomic_ref<uint32_t> status{g_state->status};
        status.store(FL_STATUS_SELF_DISABLED, std::memory_order_release);
    }
}

#define FL_HOOK_GUARD(body)                                                                                            \
    __try {                                                                                                            \
        body                                                                                                           \
    } __except (FlFilter(GetExceptionCode())) {                                                                        \
        NoteFault();                                                                                                   \
    }

// ---------------------------------------------------------------------------
// Per-swapchain identity and cached output size.
//
// Patching a vtable slot patches the SHARED dxgi.dll class vtable, so ONE hook
// sees EVERY swapchain in the process -- measured across five configurations
// (#36). A title with a separate UI or video swapchain therefore inflates F_disp
// unless the records say which stream they came from.
//
// Fixed capacity, linear scan, no allocation: this runs on the present path.
// Overflow yields id 0, which fl_shm.h defines as "unidentified" and the Agent
// must treat as one undifferentiated stream -- never as a valid id.
// ---------------------------------------------------------------------------
constexpr size_t kMaxSwapChains = 16;

struct SwapChainSlot {
    void*    ptr = nullptr;
    uint32_t id = 0;
    uint16_t outW = 0;
    uint16_t outH = 0;
};

SwapChainSlot g_chains[kMaxSwapChains]{};
uint32_t      g_nextChainId = 1;

SwapChainSlot* FindOrAdd(IDXGISwapChain* sc) noexcept {
    for (auto& s : g_chains) {
        if (s.ptr == sc) {
            return &s;
        }
    }
    for (auto& s : g_chains) {
        if (s.ptr == nullptr) {
            s.ptr = sc;
            s.id = g_nextChainId++;
            DXGI_SWAP_CHAIN_DESC desc{};
            if (SUCCEEDED(sc->GetDesc(&desc))) {
                s.outW = static_cast<uint16_t>(desc.BufferDesc.Width);
                s.outH = static_cast<uint16_t>(desc.BufferDesc.Height);
            }
            return &s;
        }
    }
    return nullptr;
}

void ForgetChainSize(IDXGISwapChain* sc) noexcept {
    for (auto& s : g_chains) {
        if (s.ptr == sc) {
            s.outW = 0;
            s.outH = 0;
            DXGI_SWAP_CHAIN_DESC desc{};
            if (SUCCEEDED(sc->GetDesc(&desc))) {
                s.outW = static_cast<uint16_t>(desc.BufferDesc.Width);
                s.outH = static_cast<uint16_t>(desc.BufferDesc.Height);
            }
            return;
        }
    }
}

// adapterLuid is published at FIRST PRESENT, not at init: at init we are two
// steps before any graphics module is resolved and our dummy device's adapter is
// not the game's. 0 means "not yet known" (#36).
void PublishAdapterOnce(IDXGISwapChain* sc) noexcept {
    auto* h = reinterpret_cast<FlShmHandshake*>(static_cast<unsigned char*>(g_base) + FL_SHM_HANDSHAKE_OFFSET);
    std::atomic_ref<uint64_t> luid{h->adapterLuid};
    if (luid.load(std::memory_order_relaxed) != 0) {
        return;
    }
    IDXGIDevice* dev = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dev))) || dev == nullptr) {
        return;
    }
    IDXGIAdapter* ad = nullptr;
    if (SUCCEEDED(dev->GetAdapter(&ad)) && ad != nullptr) {
        DXGI_ADAPTER_DESC d{};
        if (SUCCEEDED(ad->GetDesc(&d))) {
            uint64_t v = 0;
            std::memcpy(&v, &d.AdapterLuid, sizeof(v));
            luid.store(v, std::memory_order_release);
        }
        ad->Release();
    }
    dev->Release();
}

uint32_t g_frameIndex = 0;

// ---------------------------------------------------------------------------
// The safety stop, and supervision loss (07_IPC §Protocol rules, 19_SAFETY
// §During a session).
//
// Both are checked ON THE PRESENT PATH, which is the only place that runs often
// enough to react "within one frame" as 07_IPC requires. Neither costs a
// syscall: the control block is mapped memory, and GetTickCount64 reads
// KUSER_SHARED_DATA.
//
// STOPPING IS ONE-WAY. Once we stop observing we do not resume, even if ticks
// start again -- a capture side that can un-stop itself is a capture side whose
// stop is advisory, and this is the behaviour 19_SAFETY calls the single most
// important runtime behavior in the whole capture layer.
// ---------------------------------------------------------------------------
FlControlBlock* g_control = nullptr;
bool            g_observing = true;
uint32_t        g_lastTicks = 0;
ULONGLONG       g_lastTickAt = 0;

// CANARY RESULT, recorded because it is not what I expected. Removing
// `g_observing = false` and leaving only MH_DisableHook keeps the suite GREEN:
// unhooking alone stops the writes, so the flag is not what the test is proving.
// It is kept deliberately and is not redundant -- it closes the window between a
// thread already inside our hook body and the patch being removed, and it is the
// only thing that holds if MH_DisableHook ever fails -- but "the flag is
// necessary" is NOT a property this suite verifies, and saying so is cheaper than
// letting a reader assume it does.
void StopObserving(uint32_t reason) noexcept {
    if (!g_observing) {
        return;
    }
    g_observing = false;
    MH_DisableHook(MH_ALL_HOOKS);
    if (g_state != nullptr) {
        std::atomic_ref<uint32_t> status{g_state->status};
        status.store(reason, std::memory_order_release);
    }
}

// Returns false when we must not record this present.
bool MayObserve() noexcept {
    if (!g_observing || g_control == nullptr) {
        return g_observing;
    }

    // The safety stop first: the Agent's guard fired mid-session and wants the
    // hooks gone. 07_IPC calls this the fastest, most-tested path in the DLL.
    std::atomic_ref<uint32_t> unhook{g_control->unhookRequested};
    if (unhook.load(std::memory_order_acquire) != 0) {
        StopObserving(FL_STATUS_UNHOOKED);
        return false;
    }

    // Supervision loss. guardTicks counts COMPLETED guard evaluations, not
    // seconds, so a stalled guard loop stops it advancing even while the Agent
    // process is alive -- which is exactly the case a timer-driven heartbeat
    // would have missed.
    //
    // "Never advanced" and "stopped advancing" are the same state: the clock
    // starts when the mapping is published, so a capture side no Agent ever
    // adopted is inert from the beginning rather than enjoying a grace window.
    std::atomic_ref<uint32_t> ticks{g_control->guardTicks};
    const uint32_t            now = ticks.load(std::memory_order_acquire);
    const ULONGLONG           t = GetTickCount64();
    if (now != g_lastTicks) {
        g_lastTicks = now;
        g_lastTickAt = t;
        return true;
    }
    if (t - g_lastTickAt >= FL_GUARD_TICK_DEADLINE_MS) {
        StopObserving(FL_STATUS_UNHOOKED);
        return false;
    }

    // Pausing is not stopping: the Agent asked us to hold, and the supervision
    // clock keeps running, so a paused session still stops if the guard dies.
    std::atomic_ref<uint32_t> paused{g_control->pauseRequested};
    return paused.load(std::memory_order_relaxed) == 0;
}

// The hot path. One QPC read, a few cached-state reads, one 60-byte store in two
// spans, two relaxed atomic stores, two fences. No syscall, no allocation, no
// lock, no logging (NFR-1, target <= 1 us; a bare vtable detour measured 8.4 ns).
void RecordPresent(IDXGISwapChain* sc, UINT syncInterval, UINT flags) noexcept {
    if (!MayObserve()) {
        return;
    }
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);

    SwapChainSlot* slot = FindOrAdd(sc);
    PublishAdapterOnce(sc);

    FlFrameRecord rec{};
    rec.qpc = static_cast<uint64_t>(qpc.QuadPart);
    rec.frameIndex = g_frameIndex++;
    rec.presentFlags = flags;
    rec.syncInterval = static_cast<uint16_t>(syncInterval);
    rec.api = FL_API_D3D11;    // refined when the device type is resolved
    rec.swapchainId = slot != nullptr ? slot->id : 0u;
    if (slot != nullptr) {
        rec.outputW = slot->outW;
        rec.outputH = slot->outH;
    }

    // HONESTY, and it is the whole reason #36 spent two bytes. A present-only
    // writer has installed no upscaler, FG, RT, PSO, VRAM or latency hook, so it
    // may claim exactly one thing: the output size it read off the swapchain.
    // Leaving measuredMask at 0 with the zero-defaults would assert "no
    // upscaler, no frame generation, no ray tracing" as MEASURED FACT ~118 times
    // a second -- producing fg_factor 1.0 (CLAUDE.md rule 6) and a definite RT
    // No (rule 7) about a title nobody looked at.
    rec.measuredMask = FL_MEASURED_OUTPUT_RES;
    rec.rtFlags = FL_RT_NOT_MEASURED;

    g_writer.Publish(rec);
}

// ---------------------------------------------------------------------------
// The hooks. Indices proved BY BEHAVIOUR, not asserted: slot 8 Present, 13
// ResizeBuffers, 22 Present1 (spike-notes.md §H4, ctest fl_vtable_indices).
// ---------------------------------------------------------------------------
using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using PFN_Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PFN_Present       g_origPresent = nullptr;
PFN_Present1      g_origPresent1 = nullptr;
PFN_ResizeBuffers g_origResizeBuffers = nullptr;

HRESULT STDMETHODCALLTYPE Hook_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    FL_HOOK_GUARD({ RecordPresent(sc, sync, flags); })
    // ALWAYS exactly once, on every path including the fault path. Never inside
    // the __try: a fault in the game's own present must not be attributed to us.
    return g_origPresent(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE Hook_Present1(IDXGISwapChain1* sc, UINT sync, UINT flags,
                                        const DXGI_PRESENT_PARAMETERS* params) {
    FL_HOOK_GUARD({ RecordPresent(reinterpret_cast<IDXGISwapChain*>(sc), sync, flags); })
    return g_origPresent1(sc, sync, flags, params);
}

HRESULT STDMETHODCALLTYPE Hook_ResizeBuffers(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt,
                                             UINT flags) {
    // The size is re-read AFTER the original runs, or we would cache the size
    // that is being replaced. 17_HOOK_ENGINE hooks this precisely because output
    // resolution changes mid-session.
    const HRESULT hr = g_origResizeBuffers(sc, count, w, h, fmt, flags);
    FL_HOOK_GUARD({ ForgetChainSize(sc); })
    return hr;
}

// A throwaway WARP swapchain, purely to read the shared class vtable. Released
// immediately: 17_HOOK_ENGINE §Getting vtable addresses. Never hardcode the
// pointer, never keep the object.
bool InstallPresentHooks() noexcept {
    ID3D11Device*        dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL    got{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, &got, &ctx))) {
        return false;
    }

    IDXGIDevice*     dxgiDev = nullptr;
    IDXGIAdapter*    adapter = nullptr;
    IDXGIFactory2*   factory = nullptr;
    IDXGISwapChain1* dummy = nullptr;
    bool             ok = false;

    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDev))) &&
        SUCCEEDED(dxgiDev->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 8;
        desc.Height = 8;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        // Composition, so no HWND and no interactive window station is needed --
        // the same choice that lets hook-harness run headless on CI.
        if (SUCCEEDED(factory->CreateSwapChainForComposition(dev, &desc, nullptr, &dummy)) && dummy != nullptr) {
            void** vtbl = *reinterpret_cast<void***>(dummy);
            ok = MH_CreateHook(vtbl[8], reinterpret_cast<void*>(&Hook_Present),
                               reinterpret_cast<void**>(&g_origPresent)) == MH_OK &&
                 MH_CreateHook(vtbl[13], reinterpret_cast<void*>(&Hook_ResizeBuffers),
                               reinterpret_cast<void**>(&g_origResizeBuffers)) == MH_OK &&
                 MH_CreateHook(vtbl[22], reinterpret_cast<void*>(&Hook_Present1),
                               reinterpret_cast<void**>(&g_origPresent1)) == MH_OK &&
                 MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
        }
    }

    if (dummy != nullptr) {
        dummy->Release();
    }
    if (factory != nullptr) {
        factory->Release();
    }
    if (adapter != nullptr) {
        adapter->Release();
    }
    if (dxgiDev != nullptr) {
        dxgiDev->Release();
    }
    if (ctx != nullptr) {
        ctx->Release();
    }
    if (dev != nullptr) {
        dev->Release();
    }
    return ok;
}

DWORD WINAPI InitThread(LPVOID) noexcept {
    if (!CreateRing()) {
        return 1;
    }
    PublishHandshake();

    g_state = reinterpret_cast<FlWriterState*>(static_cast<unsigned char*>(g_base) + FL_SHM_WRITER_OFFSET);
    g_control = reinterpret_cast<FlControlBlock*>(static_cast<unsigned char*>(g_base) + FL_SHM_CONTROL_OFFSET);
    // The supervision clock starts HERE, when the mapping is published -- not at
    // first present. 07_IPC is explicit that a capture side no Agent ever adopts
    // is inert from the beginning rather than enjoying a grace window.
    g_lastTicks = 0;
    g_lastTickAt = GetTickCount64();
    if (!g_writer.Init(g_base, FL_SHM_DEFAULT_CAPACITY)) {
        return 1;
    }

    std::atomic_ref<uint32_t> status{g_state->status};

    if (MH_Initialize() != MH_OK || !InstallPresentHooks()) {
        // Hooking failed, so nothing will ever be recorded. Staying at INIT says
        // exactly that; READY would be a claim about a capture side that does not
        // exist, and the Agent's degradation path is what should run instead.
        status.store(FL_STATUS_INIT, std::memory_order_release);
        return 1;
    }

    std::atomic_ref<uint32_t> apiMask{g_state->apiMask};
    apiMask.store(1u << FL_API_D3D11, std::memory_order_relaxed);
    status.store(FL_STATUS_READY, std::memory_order_release);
    return 0;
}

}    // namespace

// Exports keep their real names. Being identifiable to anti-cheat is a
// requirement, not an accident (docs/19_SAFETY_AND_ANTICHEAT.md).
extern "C" {

__declspec(dllexport) unsigned int FlGetLayoutVersion() {
    return FL_SHM_LAYOUT_VERSION;
}

// 17_HOOK_ENGINE §Build profile requires this export, and 07_IPC makes a build-id
// mismatch a hard refuse-to-attach.
__declspec(dllexport) const char* FlGetBuildId() {
    return FL_BUILD_ID;
}

__declspec(dllexport) unsigned int FlGetStatus() {
    if (g_base == nullptr) {
        return FL_STATUS_INIT;
    }
    const auto* state =
        reinterpret_cast<const FlWriterState*>(static_cast<const unsigned char*>(g_base) + FL_SHM_WRITER_OFFSET);
    std::atomic_ref<const uint32_t> status{state->status};
    return status.load(std::memory_order_acquire);
}

// The safety stop's local entry point. It does nothing yet because nothing is
// hooked; the body lands with the hooks, where 07_IPC requires it to be the
// fastest, most-tested path in the DLL. Declared now because the export list is
// part of what an anti-cheat vendor inspects, and a DLL whose exports change
// shape between builds is harder to identify, not easier.
__declspec(dllexport) void FlRequestUnhook() {
    StopObserving(FL_STATUS_UNHOOKED);
}

}    // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // ONLY these two calls. Everything else -- mapping creation, MinHook,
        // dummy-device creation, hook installation -- happens on the init thread,
        // outside the loader lock.
        DisableThreadLibraryCalls(module);
        g_initThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    // No DLL_PROCESS_DETACH teardown: 17_HOOK_ENGINE §Unhooking is explicit that
    // the DLL is never FreeLibrary'd from a live process, because a thread may
    // still be inside a trampoline. It goes dormant and unloads with the process,
    // which also unmaps the view.
    return TRUE;
}
