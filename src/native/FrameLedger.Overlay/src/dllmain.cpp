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
// D3D11 AND D3D12 BOTH: one hook on the shared dxgi.dll class vtable catches
// both, and `api` is resolved per swapchain by asking the swapchain which device
// created it -- FL_API_UNKNOWN when it will not say, never a guess.
//
// NOT HERE: OpenGL (wglSwapBuffers is a flat export in opengl32.dll and needs no
// vtable, but hook-harness has no OpenGL mode, and shipping an untested hook into
// a game process is not something this project does), Vulkan (the layer, P1), and
// the upscaler / FG / RT feature hooks. The record's measuredMask says so on
// every frame rather than letting the zero-defaults assert a measurement nobody
// made.
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
#include <d3d12.h>
#include <dxgi1_2.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fl_ac_rules.h>
#include <fl_d3d12_vtable.h>
#include <fl_dxgi_vtable.h>
#include <fl_hook_inventory.h>
#include <fl_ring.h>
#include <fl_rt_accum.h>
#include <fl_shm.h>
#include <fl_sl_inputs.h>
#include <fl_sl_seen.h>
#include <MinHook.h>
#include <sddl.h>

// Its own block, below the sorted one, because it needs Family/Group/MatchKind
// from fl_ac_rules.h and clang-format sorts each block independently. Types plus
// the generated floor table only -- for the LoadLibrary detour's early stop; nothing
// of the rules parser is compiled into the Overlay.
#include <fl_ac_floor.generated.h>

// Vendored MIT headers, for TYPES ONLY -- never linked, and no vendor function's
// address is ever taken in evaluated code. See
// third_party/streamline/README.md: linking one would make sl.interposer.dll a
// LOAD-TIME dependency of this DLL, and the Overlay would then fail to load in
// every game that ships no Streamline, inside the loader, before any of our code
// runs. We use exactly one thing from here -- the PFun_slEvaluateFeature
// typedef and the kFeature* ids -- and resolve the symbol at runtime.
#include <sl.h>
// AMD FidelityFX (ffx-api), the same way and under the same rule. Used for exactly
// four things: ffxApiHeader::type, the FFX_API_DISPATCH_DESC_TYPE_* constants, the
// renderSize / frameID fields of the descriptors those constants name, and the
// PfnFfxDispatch typedef the trampolines are typed by. ffx_api.h declares the entry
// points __declspec(dllexport) with no import switch; nothing here defines or takes
// the address of one, so nothing is exported -- hookinventory-check Pass C reads the
// built DLL's export table to keep it that way.
#include <ffx_api.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>
// The FSR 3.0 HOST API (tag fsr3-v3.0.4), the same rule again, for exactly two things:
// FfxFsr3DispatchUpscaleDescription::renderSize and the type of the one export hooked.
// FFX_API is __declspec(dllexport) with no switch at all at that tag; nothing here
// defines or takes the address of a name, so nothing is exported, and Pass C's
// forbidden-export pattern already covers ffxFsr3*. A separate tree from ffx-api's
// (third_party/fidelityfx-fsr3/README.md), INCLUDED INSIDE A NAMESPACE: the two trees
// each define a struct named FfxFrameGenerationConfig, and wrapping the include is the
// one way to keep both headers verbatim in one TU. Legal C++: the entry points keep
// their C linkage, so fsr3host::ffxFsr3ContextDispatchUpscale names the same function
// the module exports, and every struct read here is still the vendor's declaration.
namespace fsr3host {
#include <FidelityFX/host/ffx_fsr3.h>
}    // namespace fsr3host

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

// Defined below with the safety stop. NoteFault needs it, and the fault policy
// is declared first because FL_HOOK_GUARD wraps every hook body.
void StopObserving(uint32_t reason) noexcept;

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
        //
        // ROUTED THROUGH StopObserving, and that is a fix rather than a tidy-up.
        // This used to call MH_DisableHook directly, DISCARD ITS RETURN VALUE and
        // then store the status unconditionally -- so on a MinHook failure the
        // Overlay reported SELF_DISABLED while its hooks were still patched in and
        // still writing. The status field is not consulted anywhere on the write
        // path, so nothing else would have stopped it either.
        //
        // StopObserving clears g_observing, which IS consulted on the write path,
        // and whose comment (below) already called it "the only thing that holds
        // if MH_DisableHook ever fails". That claim was true of the safety stop
        // and false of the fault policy, which is exactly the kind of asymmetry
        // between two paths doing the same job that nobody re-reads for.
        StopObserving(FL_STATUS_SELF_DISABLED);
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

// The last GetLastPresentCount value read at a hooked present on this chain, and whether
// one has been read at all (fl_shm.h §dxgiPresentsUnseen). Per chain, because the
// counter is per chain.
struct SwapChainSlot {
    void*    ptr = nullptr;
    uint32_t id = 0;
    uint16_t outW = 0;
    uint16_t outH = 0;
    uint8_t  api = FL_API_UNKNOWN;
    bool     haveDxgiCount = false;
    uint32_t lastDxgiCount = 0;
    uint32_t dxgiEpoch = 0;    // g_dxgiEpoch when lastDxgiCount was read; a mismatch means presents went by unrecorded
};

// Ask the device whether it can ray-trace, once, and publish it as FlRtTier.
//
// NOT A HOOK. This is a capability QUERY on a device DXGI just handed us for a
// swapchain we were called on, so it reads nothing but an object we legitimately
// own (CLAUDE.md rule 4) and installs nothing. It is here rather than in a
// feature PR because 03_METRICS' RT `No` needs all three of rtTier, the AS-build
// hook and a silent session -- and this is the only conjunct that needs no hook
// at all, so it can be true before any RT hook exists.
//
// NOT write-once, and the difference is worth a line because the field's comment
// says "queried once". FindOrAdd calls ResolveApi once per NEWLY SEEN swapchain,
// so a title with several D3D12 swapchains queries several times. That is fine
// and is left unguarded on purpose: raytracing support is a property of the
// adapter, not of the swapchain, so every query in one process answers the same
// thing, and a compare-exchange here would buy nothing while hiding the day that
// stops being true.
//
// ONLY THE ID3D12Device BRANCH CALLS THIS. ResolveApi keeps a second, currently
// unreachable branch for a DXGI that hands back the command queue instead; that
// branch resolves api = D3D12 and leaves rtTier NOT_QUERIED. Honest rather than
// wrong -- nothing asked the device -- and deliberately not fixed here, because
// the fix would be untested code on a path no fixture reaches.
//
// Runs inside FL_HOOK_GUARD: the call chain is Hook_Present -> RecordPresent ->
// FindOrAdd -> ResolveApi, and dllmain.cpp:944 wraps that whole body.
void PublishRtTier(ID3D12Device* device) noexcept {
    if (device == nullptr || g_state == nullptr) {
        return;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    // A FAILED query stays FL_RT_TIER_NOT_QUERIED. It is not "no ray tracing":
    // this call can fail on a device created at a feature level that predates the
    // capability, and reporting that as UNSUPPORTED would be the same
    // could-not-look/looked-and-found-nothing collision FlRtTier exists to close,
    // one layer up.
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)))) {
        return;
    }
    // The enum's own value, with ONE substitution -- see FlRtTier. Nothing here
    // names TIER_1_0/1_1/1_2, so a tier newer than this SDK still arrives intact
    // instead of being clamped to whatever this build happened to know about.
    const uint32_t tier = (options.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
                              ? static_cast<uint32_t>(fl::FL_RT_TIER_UNSUPPORTED)
                              : static_cast<uint32_t>(options.RaytracingTier);

    std::atomic_ref<uint32_t> slot{g_state->rtTier};
    slot.store(tier, std::memory_order_relaxed);
}

// The game's D3D12 device, published once, so the WATCHDOG can reach it.
//
// It lives here beside PublishRtTier because this is the only place in the Overlay
// where the game's device is reachable at all: DXGI hands it to ResolveApi for a
// swapchain we were called on. The ray-tracing installer needs a live device to
// create a throwaway command list and read its vtable, and it runs on the watchdog
// thread, which has no swapchain of its own.
//
// AddRef'd and NEVER RELEASED, for the reason fl_hook_inventory.h's ResolveScoped
// gives about the module reference it takes: MinHook writes its patch into
// D3D12Core's code and keeps the hook in its own table, and MH_DisableHook(ALL) --
// which is the SAFETY STOP -- must not run against a torn-down implementation.
// One refcount on an object the game owns and keeps alive anyway, visible to
// anyone inspecting our references, hiding nothing.
std::atomic<void*> g_d3d12Device{nullptr};

void PublishD3D12Device(ID3D12Device* device) noexcept {
    if (device == nullptr) {
        return;
    }
    // AddRef BEFORE the exchange. Publishing first would expose a pointer the
    // watchdog could load in the window before we owned a reference to it.
    device->AddRef();
    void* expected = nullptr;
    if (!g_d3d12Device.compare_exchange_strong(expected, device, std::memory_order_acq_rel,
                                               std::memory_order_relaxed)) {
        device->Release();    // an earlier swapchain got here first; one device is all we need
    }
}

// Which API this swapchain belongs to, asked of the swapchain itself.
//
// One hook sees every swapchain in the process, and a D3D11 title and a D3D12
// title are not distinguishable from the present call -- so the api byte was
// hardcoded to D3D11 until now, which was a guess written into a field
// 03_METRICS consumes and 06_DATA_MODEL persists.
//
// GetDevice returns what the swapchain was CREATED WITH, and for D3D12 that is
// the COMMAND QUEUE, not the device (CreateSwapChainForComposition takes the
// queue). Querying for ID3D12Device on it fails; ID3D12CommandQueue is the
// interface that answers.
//
// FL_API_UNKNOWN when neither answers. 0 is the honest value for "we could not
// tell", and it is what the enum reserves it for -- guessing D3D11 is how the
// field became wrong in the first place.
uint8_t ResolveApi(IDXGISwapChain* sc) noexcept {
    IUnknown* dev = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(IUnknown), reinterpret_cast<void**>(&dev))) || dev == nullptr) {
        return FL_API_UNKNOWN;
    }
    // BOTH D3D12 shapes are tried, because MEASURED: GetDevice does not return
    // the command queue that was passed to CreateSwapChainForComposition. The
    // first version of this asked only for ID3D12CommandQueue on the grounds that
    // the queue is what creates a D3D12 swapchain, and every record from a real
    // D3D12 target came back FL_API_UNKNOWN. DXGI resolves the queue to its
    // owning device before storing it, so ID3D12Device is what answers.
    //
    // The queue query is kept rather than deleted: it costs one failed QI on a
    // path that runs once per swapchain, and a DXGI version that does hand back
    // the queue would otherwise regress to UNKNOWN silently.
    uint8_t             api = FL_API_UNKNOWN;
    ID3D12Device*       d12 = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D11Device*       d11 = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device), reinterpret_cast<void**>(&d12))) && d12 != nullptr) {
        api = FL_API_D3D12;
        PublishRtTier(d12);
        // The ONE place the game's D3D12 device is reachable from. The ray-tracing
        // installer needs a live device to read a command list's vtable off, and it
        // runs on the watchdog thread, which has no swapchain of its own.
        PublishD3D12Device(d12);
        d12->Release();
    } else if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue))) &&
               queue != nullptr) {
        api = FL_API_D3D12;
        queue->Release();
    } else if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device), reinterpret_cast<void**>(&d11))) &&
               d11 != nullptr) {
        api = FL_API_D3D11;
        d11->Release();
    }
    dev->Release();
    return api;
}

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
            s.api = ResolveApi(sc);
            // apiMask records what we have ACTUALLY seen present, not what the
            // process happens to have loaded: a game can link d3d12.dll and
            // present through D3D11.
            if (s.api != FL_API_UNKNOWN && g_state != nullptr) {
                std::atomic_ref<uint32_t> mask{g_state->apiMask};
                mask.fetch_or(1u << s.api, std::memory_order_relaxed);
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
// TWO PLACES EVALUATE THESE, AND THE SECOND ONE IS THE POINT.
//
// The present path reacts "within one frame", which is what 07_IPC requires of
// the safety stop, and costs no syscall: the control block is mapped memory.
//
// But the present path is reachable ONLY WHILE THE GAME IS PRESENTING, and until
// 2026-08-05 it was the only place either check ran. `MayObserve` has exactly one
// caller (`RecordPresent`), which has two (the Present and Present1 hooks) -- so
// in a process that had stopped presenting, because it hung or was alt-tabbed or
// sat in a menu, unhookRequested was never read and the deadline was never
// evaluated. The hooks stayed patched in indefinitely.
//
// That is the case the mechanism exists for. fl_shm.h says so in capitals over
// FL_GUARD_TICK_DEADLINE_MS -- "NOT DRIVEN BY THE PRESENT HOOK ... the clock
// would stop when presents stop, which is the exact scenario this exists for, a
// game that has hung, or been alt-tabbed, while anti-cheat loads behind it" --
// and the code did the thing its own normative comment forbade. A process that
// has stopped presenting is also not RECORDING, so nothing false was written;
// what failed is the promise in legal/DISCLAIMER.md §2 that the part inside the
// game stops when contact is lost, and 19_SAFETY's clean unhook on detection.
//
// So the watchdog thread below supplements the present path rather than
// replacing it: within-one-frame while presenting, within-one-second otherwise.
// Same shape as §S6's LoadLibrary hook, which supplements the 30 s poll because
// the poll also catches things the hook cannot.
//
// WHY A THREAD IS ACCEPTABLE HERE AND WAS REJECTED FOR THE VULKAN LAYER.
// 20_OPEN_QUESTIONS §S2 rejected a layer-owned worker thread for three reasons,
// all of which are properties of the LAYER: the Vulkan loader owns the layer's
// mapping, so a thread outliving it access-violates a host we do not own; the
// re-scan it would have run allocates ~1.15 MB transiently; and it would have
// called NtQuerySystemInformation and probed the SCM from inside a game, which
// is the behavioural signature of anti-analysis code (CLAUDE.md rule 3). None
// applies here. The Overlay is loaded by documented LoadLibraryW and is never
// FreeLibrary'd from a live process (17_HOOK_ENGINE §Unhooking), so we own the
// lifetime; and this thread enumerates nothing, probes nothing and allocates
// nothing -- it reads two uint32s out of our own mapping and sleeps.
//
// It also EXITS once stopped. There is nothing left for it to decide, and a
// sleeping thread that can never do anything again is footprint for nothing.
//
// STOPPING IS ONE-WAY. Once we stop observing we do not resume, even if ticks
// start again -- a capture side that can un-stop itself is a capture side whose
// stop is advisory, and this is the behaviour 19_SAFETY calls the single most
// important runtime behavior in the whole capture layer.
// ---------------------------------------------------------------------------
FlControlBlock* g_control = nullptr;

// ATOMIC because two threads now write it: a hook body reacting to
// unhookRequested, and the watchdog. It was a plain bool while the present path
// was the only writer.
std::atomic<uint32_t> g_observing{1};

// Owned by the watchdog thread ALONE. They used to live on the present path,
// where they were read and written by whichever game thread happened to present
// -- and where the freshness check returned early, which is what made
// pauseRequested unreachable (see MayObserve).
uint32_t  g_lastTicks = 0;
ULONGLONG g_lastTickAt = 0;

// One second. The safety stop's real deadline is the guard's, and this only
// bounds how late we notice; 07_IPC's "within one frame" is still met by the
// present path whenever the game is presenting at all.
constexpr DWORD kWatchdogIntervalMs = 1000;

// CANARY RESULT, recorded because it is not what I expected. Removing
// `g_observing = false` and leaving only MH_DisableHook keeps the suite GREEN:
// unhooking alone stops the writes, so the flag is not what the test is proving.
// It is kept deliberately and is not redundant -- it closes the window between a
// thread already inside our hook body and the patch being removed, and it is the
// only thing that holds if MH_DisableHook ever fails -- but "the flag is
// necessary" is NOT a property this suite verifies, and saying so is cheaper than
// letting a reader assume it does.
void StopObserving(uint32_t reason) noexcept {
    // Compare-exchange, not a plain check-then-set: the watchdog and a game
    // thread inside a hook body can both arrive here, and MH_DisableHook must run
    // exactly once. The loser returns without touching status, so the FIRST
    // reason wins -- a self-disable already in flight is not overwritten by a
    // safety stop that arrives a millisecond later, or the record of WHY we
    // stopped would depend on thread scheduling.
    uint32_t expected = 1;
    if (!g_observing.compare_exchange_strong(expected, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
    }
    MH_DisableHook(MH_ALL_HOOKS);
    if (g_state != nullptr) {
        std::atomic_ref<uint32_t> status{g_state->status};
        status.store(reason, std::memory_order_release);
    }
}

// Returns false when we must not record this present.
//
// The supervision deadline is NOT here any more -- it is the watchdog's, which
// is the whole point of having one. What stays is the safety stop, because
// 07_IPC requires it within one frame and a one-second watchdog cannot promise
// that, and the pause check, which is inherently per-frame.
// The LoadLibrary detour's early stop (defined with the detour below; the present
// path reads it first): 0 = none; else 1-based index among the floor's MODULE
// families in rules order.
std::atomic<uint32_t> g_earlyStopFamily{0};
void                  PublishLoaderWords() noexcept;

bool MayObserve() noexcept {
    if (g_observing.load(std::memory_order_acquire) == 0) {
        return false;
    }
    if (g_control == nullptr) {
        return true;
    }

    // The safety stop first: the Agent's guard fired mid-session and wants the
    // hooks gone. 07_IPC calls this the fastest, most-tested path in the DLL.
    std::atomic_ref<uint32_t> unhook{g_control->unhookRequested};
    if (unhook.load(std::memory_order_acquire) != 0) {
        StopObserving(FL_STATUS_UNHOOKED);
        return false;
    }

    // The IN-PROCESS stop, same path and same polarity: the LoadLibrary detour saw
    // a module matching the compiled anti-cheat floor load. Published before the
    // unhook so the reason is on the mapping whichever thread gets here first.
    if (g_earlyStopFamily.load(std::memory_order_acquire) != 0u) {
        PublishLoaderWords();
        StopObserving(FL_STATUS_STOPPED_BLOCKLISTED);
        return false;
    }

    // Pausing is not stopping: the Agent asked us to hold, and the supervision
    // clock keeps running (in the watchdog), so a paused session still stops if
    // the guard dies.
    //
    // THIS LINE WAS UNREACHABLE ON ANY FRAME WHERE guardTicks HAD CHANGED. The
    // freshness check used to sit between the safety stop and here and `return
    // true` as soon as the tick differed from the cached value -- so the first
    // present after every guard evaluation was recorded regardless of pause. One
    // leaked record per evaluation does not sound like much; its qpc is ~30 s
    // after its predecessor, which is a FABRICATED 30-SECOND FRAME INTERVAL in
    // the series 03_METRICS computes 1% and 0.1% lows from. 07_IPC forbids
    // exactly that artefact for torn records and the same reasoning applies here.
    // Latent until now only because nothing writes pauseRequested yet.
    std::atomic_ref<uint32_t> paused{g_control->pauseRequested};
    return paused.load(std::memory_order_relaxed) == 0;
}

// ---------------------------------------------------------------------------
// Upscaler identity, via Streamline (17_HOOK_ENGINE §Upscaling / frame
// generation; docs/HANDOFF.md queue item 2).
//
// ONE HOOK, ONE MODULE. sl.interposer.dll!slEvaluateFeature is the single point
// a Streamline-shimmed title routes every feature evaluation through, and it is
// exported by exactly ONE measured module -- unlike the NGX names, which seven
// modules export (fl_hook_inventory.h has the numbers). Hooking one tier is also
// a correctness requirement and not only a simplification: a Streamline title
// runs slEvaluateFeature -> sl.common's NGX shim -> an nvngx_* snippet, so
// hooking two tiers counts one logical evaluation twice.
//
// RULE 4. We read ONE argument of an API we hooked -- `feature`, a uint32 the
// caller passed us -- and forward all five unchanged. No game memory is read, no
// structure is dereferenced, nothing is written. `inputs`, `frame` and
// `cmdBuffer` are passed straight through and never touched.
//
// THE SIGNATURE IS THE VENDOR'S, NOT A GUESS, and that is what the vendored MIT
// header bought. sl_core_api.h publishes PFun_slEvaluateFeature; every parameter
// is integer-class (Feature is uint32_t, CommandBuffer is void, a reference is a
// pointer) and the return is an enum, so nothing travels in XMM. A hand-written
// declaration that was wrong by one argument would corrupt the stack INSIDE THE
// ORIGINAL FUNCTION, where FL_HOOK_GUARD's __try cannot reach, in somebody's
// game.
// ---------------------------------------------------------------------------

// Which Streamline features were evaluated since the last present. Bits, not a
// last-writer-wins value: DLSS super-resolution and Ray Reconstruction run in
// the SAME frame (fl_shm.h retired FL_UPSCALER_RETIRED_RAY_RECONSTRUCTION for
// exactly that reason), so a single slot would drop one of them.
// FRAME GENERATION NEEDS A COUNT, NOT A BIT, so the word is now split: features in
// the low byte, a saturating count of kFeatureDLSS_G evaluations above them
// (fl_sl_seen.h owns the encoding and is unit-tested without a hook). A bit
// collapses two evaluations between two presents into one, and under multi-frame
// generation that is the common case -- 10,169 presents carried 2,461 batches on the
// one real title measured.
//
// THE FIVE EXISTING CONSUMERS ARE UNCHANGED, which is the reason this split was
// chosen over a second atomic. `seen != 0` still means "an evaluation happened this
// present" -- any evaluation either sets a feature bit or increments the count, so
// the word is non-zero either way -- and the three bit tests read bits 0-2, which
// the count cannot reach.
enum FlSlSeen : uint32_t {
    FL_SL_SEEN_DLSS = 1u << 0,
    FL_SL_SEEN_NIS = 1u << 1,
    FL_SL_SEEN_DLSS_RR = 1u << 2,

    // RETIRED, and the bit is RESERVED rather than reused -- the same treatment
    // fl_shm.h gave FL_UPSCALER_RETIRED_RAY_RECONSTRUCTION, for the same reason.
    // DLSS-G is carried as fl::slseen's count now. Leaving the enumerator here
    // stops the next reader reaching for `fetch_or(FL_SL_SEEN_DLSS_G)`, which would
    // compile, set a bit nothing reads, and leave fgEvaluations at zero -- i.e.
    // fg_factor 1.0, from a writer that looks like it is counting.
    FL_SL_SEEN_RETIRED_DLSS_G = 1u << 3,

    FL_SL_SEEN_OTHER = 1u << 4,    // an id we do not decode -- coverage is short, and we say so
};

std::atomic<uint32_t> g_slSeen{0};

// Set once the identity hook is live. Read on the present path to decide whether
// this writer may claim FL_MEASURED_UPSCALER at all.
std::atomic<uint32_t> g_upscalerIdentityLive{0};

using PFN_SlEvaluateFeature = ::PFun_slEvaluateFeature*;
PFN_SlEvaluateFeature g_origSlEvaluateFeature = nullptr;

// The params half, behind its own family bit and its own latch: an NGX-direct
// title yields identity and nothing else, and the two must be separable.
std::atomic<uint32_t> g_upscalerParamsLive{0};

// NO #pragma warning FOR C4996, and that is measured rather than assumed.
// sl_core_api.h marks slSetTag [[deprecated]] behind `#if __cplusplus >= 201402L`,
// and src/native/CMakeLists.txt sets /W4 /WX WITHOUT /Zc:__cplusplus -- so MSVC
// reports 199711L here and the attribute never applies. Compiled and run under
// this target's exact flags on 2026-08-14: 199711L; adding /Zc:__cplusplus gives
// 202002L. A pragma would therefore suppress a warning that is not emitted, and
// its justification comment would assert a compile behaviour that does not occur.
//
// IF ANYONE ADDS /Zc:__cplusplus, this line is where the build will break, and
// the fix is a scoped pragma -- not deleting the hook. slSetTagForFrame, the
// replacement the attribute names, is exported by ZERO measured modules
// (docs/vendor-exports.json), so the deprecated entry point is the one that
// exists at runtime.
using PFN_SlSetTag = ::PFun_slSetTag*;
PFN_SlSetTag g_origSlSetTag = nullptr;

// The frame-based twin (Streamline 2.8: slSetTag is deprecated in favour of it).
// Its own trampoline; and the two rows carry PATCHED flags of their own while the
// family keeps ONE published latch (g_upscalerParamsLive), because the family is
// published when whole -- PublishParamsFamilyIfWhole -- and not by whichever row
// installed first. InstallRow's latch would have done both jobs wrongly: shared, the
// first row would have claimed it and the second would never patch; separate, the
// first row would have published a family the second was not yet producing.
using PFN_SlSetTagForFrame = ::PFun_slSetTagForFrame*;
PFN_SlSetTagForFrame  g_origSlSetTagForFrame = nullptr;
std::atomic<uint32_t> g_slSetTagPatched{0};
std::atomic<uint32_t> g_slSetTagForFramePatched{0};

// The application-frame count (HANDOFF item 3, decided 2026-09-03). Streamline
// hands a title one FrameToken per frame through slGetNewFrameToken, and a title
// has to ask for it -- so the number of DISTINCT tokens handed out between two
// presents is the number of application frames those presents carried. The
// detour counts pointer changes: SL returns the same object for the same frame
// when a title re-requests it with an explicit index, and a different one when
// the frame advances. Nothing is dereferenced -- FrameToken is an abstract vendor
// object and reading its index would be a virtual call into vendor code from a
// hook path.
//
// DRAINED WITH exchange(0) IN RecordPresent, beside g_slSeen's, and for the same
// reason: a count that is read without being cleared latches, and this one is
// the DENOMINATOR of fg_factor.
using PFN_SlGetNewFrameToken = ::PFun_slGetNewFrameToken*;
PFN_SlGetNewFrameToken g_origSlGetNewFrameToken = nullptr;
std::atomic<uint32_t>  g_frameTokensLive{0};
std::atomic<uint32_t>  g_frameTokens{0};
// The HIGHEST frame index seen so far, tagged in bit 32 so that index 0 is
// distinct from "no frame yet". Measured 2026-09-03 on Cyberpunk 2077
// (20_OPEN_QUESTIONS §S31, row P4): the title asks for a token 3 to 4.6 times per
// application frame and the interposer hands back a DIFFERENT object each time,
// so a pointer-change count read the request rate. The index is the vendor's own
// identity for a frame -- and the count advances only when an index is NEW, i.e.
// above every index seen before, not merely different from the last one: a title
// with two or three frames in flight asks for N and N+1 from different threads
// in whatever order they run, and "differs from the last" would count every
// switch. A monotone maximum counts each frame exactly once whatever the
// interleaving. A large jump backwards (a level reload restarting the counter)
// re-bases rather than freezing the count at zero forever.
std::atomic<uint64_t> g_maxFrameKey{0};
constexpr uint32_t    kFrameIndexRebaseGap = 1024u;

// The render-resolution sample, packed into one word.
//
//   bits  0-15  renderW      bits 16-31  renderH      bit 32  a real sample
//
// ONE WORD BECAUSE TWO CANNOT BE READ TOGETHER. RecordPresent runs on the
// present thread while slSetTag runs on whatever thread the title tags from --
// the vendor documents slSetTag as thread-safe and slEvaluateFeature as NOT --
// so two separate atomics could be read half-updated and publish a width from
// one resolution beside a height from another. A number nobody rendered at.
std::atomic<uint64_t> g_tagExtent{0};

// The DLSS quality preset, as fl_shm.h's byte. 0xFF until a title chains
// sl::DLSSOptions -- "a hook ran and could not tell", which is the honest state
// for the many titles that set options out of band through slDLSSSetOptions and
// never pass them here.
std::atomic<uint8_t> g_dlssQuality{0xFFu};

constexpr uint64_t kTagValid = 1ull << 32;

// --- AMD FidelityFX through the ffx-api leaves (HANDOFF item 7c, 2026-09-04) -----
//
// ONE OBSERVER, FOUR TRAMPOLINES. The four modules in fl_hook_inventory.h -- three
// leaves and the SDK 2.x loader -- each export their own ffxDispatch, so each needs
// its own MinHook target, its own trampoline and its own liveness latch -- and one
// observer, because what the detour does with a descriptor does not depend on which
// module it arrived at.
//
// WHAT IS READ, AND FROM WHERE (CLAUDE.md rule 4). Only the descriptor the title
// passed to the API we hooked: its head type, and -- once the type has matched a
// vendored constant -- renderSize (UPSCALE, PREPARE) and frameID (PREPARE). Never the
// context handle, never pNext, never a command list or a resource, never
// numGeneratedFrames (there is no field to publish it into, and the factor is
// MEASURED from counts rather than read off the configuration). Nothing is written.
//
// THE WORD RecordPresent DRAINS, in fl_sl_seen.h's encoding because that encoding is
// vendor-neutral -- bits below kCountShift are facts, the field above them a COUNT:
//
//   bit 0  FL_FFX_SEEN_FG_DISPATCH  a FRAMEGENERATION dispatch -- a generated batch,
//                                   sent back through the export from the title's own
//                                   frameGenerationCallback -- since the last present
//   bit 1  FL_FFX_SEEN_OTHER        a dispatch type this build does not decode
//   count  UPSCALE dispatches since the last present. A COUNT and not a bit, on
//          purpose: it is the second application-frame count beside PREPARE's, and a
//          loader+leaf double hook reads 2.00 on their ratio where a bit would read 1.
enum FlFfxSeen : uint32_t {
    FL_FFX_SEEN_FG_DISPATCH = 1u << 0,
    FL_FFX_SEEN_OTHER = 1u << 1,
};
static_assert(((FL_FFX_SEEN_FG_DISPATCH | FL_FFX_SEEN_OTHER) & ~fl::slseen::kFeatureMask) == 0u,
              "the FFX fact bits must stay below the count field");
std::atomic<uint32_t> g_ffxSeen{0};

// PREPARE dispatches whose frameID was NEW -- the application-frame count on this
// vendor, this vendor's slGetNewFrameToken. Keyed on a monotone maximum of frameID
// for the reason Hook_SlGetNewFrameToken gives: the vendor's "must increment by
// exactly one for each frame" is a contract about the INDEX, not about the number of
// calls, and the SDK's own sample re-issues the prepare when its configuration
// changes. Drained with exchange(0) in RecordPresent beside g_frameTokens.
std::atomic<uint32_t> g_ffxPrepares{0};
// frameID + 1, so that 0 means "no frame yet". 64 bits wide, so no tag bit is needed.
std::atomic<uint64_t> g_ffxMaxFrameKey{0};

// The render extent, packed exactly as g_tagExtent is and persisting for the same
// reason: it is the size the title is upscaling FROM, restated on every UPSCALE (and
// every PREPARE) dispatch, and RecordPresent publishes it only on a present that
// drained a dispatch -- so it cannot outlive the frame it describes.
std::atomic<uint64_t> g_ffxExtent{0};

// Which leaf the last UPSCALE dispatch arrived at, plus one (0 = none yet). The
// identity byte depends on it: the SDK 1.1.x monolith hosts FSR 3.1 and nothing
// else, while the SDK 2.x upscaler DLL hosts FSR 3.1 AND FSR 4 and the dispatch does
// not say which -- so the same UPSCALE type decodes to FL_UPSCALER_FSR3 from one leaf
// and to FL_UPSCALER_FSR_UNVERSIONED from the other (fl_shm.h says why).
std::atomic<uint32_t> g_ffxUpscaleLeaf{0};

using PFN_FfxDispatch = ::PfnFfxDispatch;
PFN_FfxDispatch g_origFfxDispatch[fl::inventory::kFfxLeafCount] = {};
// Per leaf: PATCHED, not published. The family is published once, below, when every
// leaf the process has loaded is patched -- PublishHookFamily's own rule ("a hook that
// spans several targets must not claim its family until every one of them is in"),
// and it was measured mattering before it was applied: with the family published on
// the FIRST leaf, an injected fixture caught the window between the upscaler leaf's
// patch and the frame-generation leaf's, during which UPSCALE dispatches were counted
// and the PREPARE and FRAMEGENERATION dispatches on the other leaf were not -- two
// application frames identified as FSR with no FSR_FG mode, in a window the consumer
// had every right to read as complete.
std::atomic<uint32_t> g_ffxPatched[fl::inventory::kFfxLeafCount] = {};
std::atomic<uint32_t> g_ffxLive{0};

// --- The FSR 3.0 HOST API: ffx_fsr3_x64.dll!ffxFsr3ContextDispatchUpscale ----------
//
// The fifth AMD target, and NOT a leaf: a different export with a different signature,
// hooked at its own address with its own trampoline and latch, feeding the SAME drain
// word as the ffx-api UPSCALE arm -- so the count, the extent and the identity byte all
// flow through the paths above unchanged, and PublishFfxFamilyIfWhole is its publish
// point. Which source the last UPSCALE came from is recorded in g_ffxUpscaleLeaf as
// leaf + 1; the host takes the value past every leaf.
//
// WHAT IS READ (CLAUDE.md rule 4): renderSize of the descriptor the title passed, and
// nothing else -- never the context, never a resource, never the command list.
using PFN_FfxFsr3ContextDispatchUpscale = decltype(&fsr3host::ffxFsr3ContextDispatchUpscale);
PFN_FfxFsr3ContextDispatchUpscale g_origFfxFsr3DispatchUpscale = nullptr;
std::atomic<uint32_t>             g_fsr3HostPatched{0};
constexpr uint32_t                kFfxUpscaleSourceFsr3Host = fl::inventory::kFfxLeafCount + 1u;
static_assert(kFfxUpscaleSourceFsr3Host > fl::inventory::kFfxLeafCount,
              "the host's source value must sit past every leaf's (leaf + 1) so the identity arm can tell them apart");

// THE ONE OFFSET THE HOST DETOUR DEPENDS ON, PINNED TO A LITERAL. commandList (8) +
// seven FfxResource (176 each: a pointer, the 32-byte description, the 4-byte state,
// 128 bytes of name, padded to 8) + jitterOffset (8) + motionVectorScale (8). The
// prefix through renderSize is identical at fsr3-v3.0.3, fsr3-v3.0.4 and v1.1.4 (1.1.4
// appends upscaleSize, flags and frameID AFTER it), and the literal is the point: a
// re-vendoring that moved the field fails here rather than reading the wrong bytes off
// a descriptor Cyberpunk's 3.0 module actually passed.
static_assert(sizeof(fsr3host::FfxDimensions2D) == 8u && offsetof(fsr3host::FfxDimensions2D, height) == 4u,
              "FfxDimensions2D is two uint32_t");
static_assert(offsetof(fsr3host::FfxFsr3DispatchUpscaleDescription, renderSize) == 1256u,
              "FfxFsr3DispatchUpscaleDescription::renderSize moved -- the vendored tag no longer matches the prefix "
              "3.0.3 / 3.0.4 / 1.1.4 share, and the detour would read the wrong bytes");
static_assert(offsetof(fsr3host::FfxFsr3DispatchUpscaleDescription, renderSize) + sizeof(fsr3host::FfxDimensions2D) <=
                  sizeof(fsr3host::FfxFsr3DispatchUpscaleDescription),
              "the field read must lie inside the descriptor");

// WHY THIS PERSISTS RATHER THAN BEING exchange(0)'d LIKE g_slSeen.
//
// g_slSeen answers "did an upscaler run THIS FRAME", so a sample that outlived
// its frame would be a lie. A global tag answers "how big is the input buffer",
// which is viewport state the title sets once and leaves alone -- Streamline's
// own lifecycle for it is eValidUntilPresent, and re-tagging is how a settings
// change is expressed. Draining it per present would report the resolution on
// one frame in N and nothing on the rest.
//
// The staleness that DOES matter -- a title that stops upscaling while the tag
// stands -- is handled at the consumer end in RecordPresent: renderW/H are only
// published on a frame where an evaluation was actually seen. Tag alone is never
// enough.
// WHICH TYPES the title tagged, on which route -- the identity half of frame
// generation (fl_shm.h §slTagCensus). Two words: the per-present one, drained with
// exchange(0) in RecordPresent like g_slSeen so a HUD-less tag cannot outlive the
// frame it was set for (Streamline's lifecycle for it is eValidUntilPresent), and the
// session one, OR-only, published by the watchdog like the runtime census.
std::atomic<uint32_t> g_slTagTypes{0};
std::atomic<uint32_t> g_slTagCensus{0};

// DXGI's own present counter against ours (fl_shm.h §dxgiPresentsUnseen): hook-local,
// watchdog-published, monotonic.
std::atomic<uint32_t> g_dxgiUnseen{0};
std::atomic<uint32_t> g_dxgiSamples{0};
// Whether FlWriterState::dxgiPresentsBeforeHook has been written: once, at the first
// hooked present, whichever chain it lands on.
std::atomic<bool> g_dxgiBeforeHookPublished{false};

// A PRESENT THIS HOOK SAW AND DECLINED TO RECORD IS NOT ONE IT NEVER SAW. While a
// session is paused (MayObserve false) the title keeps presenting and DXGI keeps
// counting, so the first record after the resume would otherwise claim the whole
// pause as unseen presents -- measured 0x55 on the paused-session integration case.
// Every declined present bumps this epoch; a slot whose last read is from an older
// epoch differences nothing and starts again. DXGI_PRESENT_TEST presents do not bump
// it: they do not move the counter (#35), so there is nothing to invalidate.
std::atomic<uint32_t> g_dxgiEpoch{0};

// ---------------------------------------------------------------------------
// THE LoadLibrary DETOUR (17_HOOK_ENGINE §DLL entry step 3; §H2; §S6). P1 item 1,
// 2026-09-06. Two jobs, one rule.
//
// The jobs: (a) a module the hook inventory or the runtime census names loaded
// AFTER init -- the watchdog is woken now instead of on its next 1 Hz tick, so
// the window in which a late vendor module's calls go unobserved shrinks from up
// to a second to milliseconds; (b) a module whose base name matches the
// anti-cheat floor compiled into this binary loaded mid-session -- the Overlay
// stops ITSELF on the next present or watchdog wake, the in-process half of
// 19_SAFETY §During a session, where the host's 30 s scan was the only half.
//
// The rule: the detour INSTALLS NOTHING AND UNHOOKS NOTHING INLINE. MinHook
// suspends every other thread to patch, and a LoadLibrary caller may hold the
// loader lock (a DllMain calling LoadLibrary is legal); suspending a thread that
// holds it, from a thread that then waits on it, is the deadlock §H2 names. So
// the detour reads a base name off the module the ORIGINAL returned, compares it
// against two fixed tables without allocating, stores two atomics, and signals an
// event. Everything that patches runs on the watchdog thread, outside the lock.
//
// kernelbase!LoadLibraryExW is the target because it is the funnel: LoadLibraryA,
// LoadLibraryW and LoadLibraryExA all reach it inside KernelBase, so one detour
// covers the four documented entry points. What it does NOT cover, stated:
// LdrLoadDll called directly (no measured title does), and delay-load thunks
// resolved before we attached (they went through it before we existed). The 1 Hz
// watchdog and the host's 30 s scan remain the backstops for both jobs.
//
// NOT an FL_HOOK_INVENTORY row: that table is (vendor module, exported symbol)
// and Pass A checks each row against measured vendor exports. This is a system
// module, and 17_HOOK_ENGINE §Hook inventory records the asymmetry beside the
// ray-tracing vtable slots, which are outside the table for the same reason.
// ---------------------------------------------------------------------------
using PFN_LoadLibraryExW = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
PFN_LoadLibraryExW g_origLoadLibraryExW = nullptr;

// Bit 15 = installed; bits 0..14 = wake-ups for inventoried modules (saturating).
constexpr uint32_t    kLoaderInstalledBit = 0x8000u;
constexpr uint32_t    kLoaderCountMask = 0x7FFFu;
std::atomic<uint32_t> g_loaderSignals{0};
// Auto-reset; the watchdog waits on it with its 1 s timeout, so a signal ends the
// wait early and a missing event (creation failed) degrades to the plain sleep.
HANDLE g_watchdogWake = nullptr;

const wchar_t* BaseNameOf(const wchar_t* path) noexcept {
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            base = p + 1;
        }
    }
    return base;
}

wchar_t AsciiLower(wchar_t c) noexcept {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
}

// Case-insensitive ASCII compare of a wide base name against a narrow rule value:
// exact, or the value as a prefix. The rules are ASCII by schema, so a wide
// character outside ASCII simply never matches.
bool NameMatches(const wchar_t* base, const char* value, bool prefix) noexcept {
    size_t i = 0;
    for (; value[i] != '\0'; ++i) {
        const wchar_t b = base[i];
        if (b == L'\0' || AsciiLower(b) != AsciiLower(static_cast<wchar_t>(static_cast<unsigned char>(value[i])))) {
            return false;
        }
    }
    return prefix || base[i] == L'\0';
}

// The compiled floor's MODULE families, in order: the 1-based index of the first
// whose value matches, else 0. The same prefix/exact semantics the guard's
// MatchName applies to a base name, on the same generated table.
uint32_t MatchesFloorModule(const wchar_t* base) noexcept {
    uint32_t index = 0;
    for (const fl::guard::Family& f : fl::guard::generated::kFloorFamilies) {
        if (f.group != fl::guard::Group::kModules) {
            continue;
        }
        ++index;
        for (std::size_t v = 0; v < f.valueCount; ++v) {
            if (NameMatches(base, f.values[v], f.match == fl::guard::MatchKind::kPrefix)) {
                return index;
            }
        }
    }
    return 0;
}

// Does the inventory or the census name this module? A load of one of these is
// what the watchdog installs or counts on its next tick, so it is what wakes it.
bool ModuleIsInventoried(const wchar_t* base) noexcept {
#define FL_LOADER_NAME(mod, ...)                                                                                       \
    if (_wcsicmp(base, mod) == 0) {                                                                                    \
        return true;                                                                                                   \
    }
    FL_HOOK_INVENTORY(FL_LOADER_NAME)
    FL_RUNTIME_CENSUS(FL_LOADER_NAME)
#undef FL_LOADER_NAME
    return false;
}

void WakeWatchdog() noexcept {
    if (g_watchdogWake != nullptr) {
        SetEvent(g_watchdogWake);
    }
}

HMODULE WINAPI Hook_LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags) noexcept {
    // The original FIRST, always, and its result is returned untouched: this
    // detour must be invisible to the loader's caller whatever happens below.
    const HMODULE h = g_origLoadLibraryExW(name, file, flags);
    FL_HOOK_GUARD({
        // A data-file or resource mapping is not a module: no code runs from it
        // and the loader does not list it, so neither job applies.
        constexpr DWORD kNotAModule =
            LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE;
        if (h != nullptr && (flags & kNotAModule) == 0u && g_observing.load(std::memory_order_acquire) != 0) {
            // The module's real file name, not the caller's string: a relative or
            // API-set name resolves to whatever the loader mapped, and that is the
            // name the guard would see.
            wchar_t path[MAX_PATH]{};
            if (GetModuleFileNameW(h, path, MAX_PATH) != 0) {
                const wchar_t* base = BaseNameOf(path);
                const uint32_t family = MatchesFloorModule(base);
                if (family != 0u) {
                    uint32_t none = 0;
                    g_earlyStopFamily.compare_exchange_strong(none, family, std::memory_order_acq_rel);
                    WakeWatchdog();
                } else if (ModuleIsInventoried(base)) {
                    uint32_t cur = g_loaderSignals.load(std::memory_order_relaxed);
                    while ((cur & kLoaderCountMask) < kLoaderCountMask &&
                           !g_loaderSignals.compare_exchange_weak(cur, cur + 1u, std::memory_order_relaxed)) {
                    }
                    WakeWatchdog();
                }
            }
        }
    })
    return h;
}

// Installed AFTER the present hooks, from InitThread, outside any loader lock.
// Failure is not fatal and is visible: bit 15 stays clear, so the report can say
// the detour is absent and the two backstops are all there is.
bool InstallLoaderHook() noexcept {
    const HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    if (kb == nullptr) {
        return false;
    }
    void* target = reinterpret_cast<void*>(GetProcAddress(kb, "LoadLibraryExW"));
    if (target == nullptr) {
        return false;
    }
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Hook_LoadLibraryExW),
                      reinterpret_cast<void**>(&g_origLoadLibraryExW)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        return false;
    }
    g_loaderSignals.fetch_or(kLoaderInstalledBit, std::memory_order_release);
    return true;
}

void PublishLoaderWords() noexcept {
    if (g_state == nullptr) {
        return;
    }
    std::atomic_ref<uint16_t> signals{g_state->loaderSignals};
    std::atomic_ref<uint16_t> family{g_state->earlyStopFamily};
    signals.store(static_cast<uint16_t>(g_loaderSignals.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    family.store(static_cast<uint16_t>(g_earlyStopFamily.load(std::memory_order_relaxed)), std::memory_order_relaxed);
}

void NoteTagTypes(uint32_t typeBits, uint32_t routeShift) noexcept {
    if (typeBits == 0u) {
        return;
    }
    g_slTagTypes.fetch_or(typeBits, std::memory_order_relaxed);
    g_slTagCensus.fetch_or((typeBits & FL_SL_TAG_TYPE_MASK) << routeShift, std::memory_order_relaxed);
}

void NoteTags(const sl::ResourceTag* tags, uint32_t numTags, uint32_t routeShift) noexcept {
    // A null list REMOVES tags (sl_core_api.h: "set to null to remove the
    // specified tag"). Forgetting this is how renderW/H would latch a stale
    // resolution across a settings change.
    if (tags == nullptr || numTags == 0) {
        g_tagExtent.store(0, std::memory_order_release);
        return;
    }

    // BOUNDED. numTags is the caller's number and we are inside their process:
    // a wrong one walks off the end of an array we do not own. 64 is far above
    // any plausible tag count and the cap costs nothing.
    constexpr uint32_t kMaxTags = 64;
    const uint32_t     n = numTags < kMaxTags ? numTags : kMaxTags;

    // EVERY tag's TYPE is recorded, and only the scaling input's SIZE is read. The
    // first version of this walked to the scaling input and returned, dropping every
    // other tag on the floor -- and the HUD-less and UI tags it dropped are the ones
    // that say a title is feeding DLSS Frame Generation (fl_shm.h §slTagCensus).
    uint32_t types = 0;
    bool     extentDone = false;
    for (uint32_t i = 0; i < n; ++i) {
        const sl::ResourceTag& t = tags[i];
        types |= fl::slinputs::TagTypeBit(t.type);
        if (t.type != sl::kBufferTypeScalingInputColor || extentDone) {
            continue;
        }
        extentDone = true;
        // The extent, or -- when the title tagged the whole resource -- the size the
        // Resource itself declares; fl_sl_inputs.h's TagSize is the one reading of a
        // tag all three routes share. Neither present is the honest unknown fl_shm.h
        // already defines for renderW/H, stored as such rather than guessed.
        uint32_t w = 0;
        uint32_t h = 0;
        if (!fl::slinputs::TagSize(t, w, h)) {
            g_tagExtent.store(0, std::memory_order_release);
            continue;
        }
        g_tagExtent.store(static_cast<uint64_t>(w) | (static_cast<uint64_t>(h) << 16) | kTagValid,
                          std::memory_order_release);
    }
    NoteTagTypes(types, routeShift);
}

sl::Result STDMETHODCALLTYPE Hook_SlSetTag(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags,
                                           uint32_t numTags, sl::CommandBuffer* cmdBuffer) {
    FL_HOOK_GUARD({
        if (MayObserve()) {
            NoteTags(tags, numTags, FL_SL_TAG_ROUTE_GLOBAL);
        }
    })
    // ALWAYS exactly once, on every path including the fault path, with every
    // argument forwarded untouched.
    return g_origSlSetTag(viewport, tags, numTags, cmdBuffer);
}

// slSetTagForFrame: the FrameToken first, then the same list slSetTag takes. The
// token is not read -- the frame count comes from slGetNewFrameToken -- and the tag
// list is handed to the same NoteTags, because a per-frame tag and a viewport tag
// state the same thing about the input buffer. Forwarded exactly once on every path.
sl::Result STDMETHODCALLTYPE Hook_SlSetTagForFrame(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
                                                   const sl::ResourceTag* tags, uint32_t numTags,
                                                   sl::CommandBuffer* cmdBuffer) {
    FL_HOOK_GUARD({
        if (MayObserve()) {
            NoteTags(tags, numTags, FL_SL_TAG_ROUTE_FRAME);
        }
    })
    return g_origSlSetTagForFrame(frame, viewport, tags, numTags, cmdBuffer);
}

sl::Result STDMETHODCALLTYPE Hook_SlEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame,
                                                    const sl::BaseStructure** inputs, uint32_t numInputs,
                                                    sl::CommandBuffer* cmdBuffer) {
    FL_HOOK_GUARD({
        if (MayObserve()) {
            // kFeatureDLSS IS ZERO, which is worth the line it costs. Every enum
            // in the record uses 0 for "nobody said"; Streamline uses it for a
            // real feature. Mapping to our own bit here keeps that collision out
            // of the record entirely -- a `feature` of 0 must never reach a field
            // whose 0 means NOT_REPORTED.
            // THE CHAIN IS UNCHANGED, DELIBERATELY. Which id maps to which answer is
            // §S30's open question -- a real title decoded every params-carrying record
            // as UNKNOWN while running DLSS -- and docs/HANDOFF.md forbids by name
            // "fixing" the decode before the ids that actually arrive have been
            // printed. That would turn a wrong answer into a confident wrong answer.
            // This PR builds the instrument; the decode changes when the measurement
            // says what to change it to.
            uint32_t bit = FL_SL_SEEN_OTHER;
            if (feature == sl::kFeatureDLSS) {
                bit = FL_SL_SEEN_DLSS;
            } else if (feature == sl::kFeatureNIS) {
                bit = FL_SL_SEEN_NIS;
            } else if (feature == sl::kFeatureDLSS_RR) {
                bit = FL_SL_SEEN_DLSS_RR;
            } else if (feature == sl::kFeatureDLSS_G) {
                bit = 0u;    // carried as a COUNT below, not as a bit
            }

            // STILL EXACTLY ONE read-modify-write, on either arm. Frame generation
            // adds fl::slseen::kCountOne to the high field instead of OR-ing a bit
            // into the low one; everything else is the fetch_or it always was. A
            // compare-exchange loop would have been needed only to set a bit AND
            // bump a counter atomically, and making the count itself the identity
            // removes that requirement rather than paying for it on a hook path.
            if (feature == sl::kFeatureDLSS_G) {
                g_slSeen.fetch_add(fl::slseen::kCountOne, std::memory_order_relaxed);
            } else {
                g_slSeen.fetch_or(bit, std::memory_order_relaxed);
            }

            // AND THE WALK BELOW RUNS FOR EVERY FEATURE ID, INCLUDING DLSS-G.
            // Returning early from the arm above would have been the obvious tidy-up
            // and would have silently removed FindScalingInputExtent's ONLY
            // production call site from the frame-generation path -- so a title that
            // tags LOCALLY would stop publishing renderW/H and upscalerQuality, and
            // nothing in the tree goes red on that: every test calls the header
            // directly and every fixture passes inputs = nullptr.

            // LOCAL TAGS, which slSetTag never sees.
            //
            // sl_core_api.h:258 is explicit that buffer tags passed here are
            // "local" and "do NOT interact with same tags sent in the global
            // scope using slSetTag API". A title that tags locally therefore
            // yields nothing from the slSetTag hook, and vice versa -- the two
            // are alternative integration styles, not layers, so both are read
            // and neither is sufficient alone.
            //
            // LOCAL WINS WHEN PRESENT, because it is scoped to this evaluation
            // rather than to the viewport, so it cannot be older than the frame
            // being measured.
            const auto scan = fl::slinputs::FindScalingInputExtent(inputs, numInputs);
            // Every tag TYPE the walk saw, on the local route (fl_shm.h §slTagCensus).
            NoteTagTypes(scan.tagTypes, FL_SL_TAG_ROUTE_LOCAL);
            if (scan.found) {
                g_tagExtent.store(static_cast<uint64_t>(scan.renderW) | (static_cast<uint64_t>(scan.renderH) << 16) |
                                      kTagValid,
                                  std::memory_order_release);
            }
            // Quality travels separately from the extent, because a title can
            // chain sl::DLSSOptions without a local ResourceTag and vice versa.
            // A separate word, not packed with the extent: they have different
            // producers and pretending otherwise would tie one's absence to the
            // other's.
            if (scan.qualityFound) {
                g_dlssQuality.store(scan.quality, std::memory_order_release);
            }
        }
    })
    // ALWAYS exactly once, on every path including the fault path, with every
    // argument forwarded untouched.
    return g_origSlEvaluateFeature(feature, frame, inputs, numInputs, cmdBuffer);
}

// ORIGINAL FIRST, then observe, and the order is forced by the signature: the
// token is an OUT parameter, so there is nothing to count until the vendor has
// returned it. Every argument is forwarded untouched and the result is returned
// unchanged on every path, including the fault path.
sl::Result STDMETHODCALLTYPE Hook_SlGetNewFrameToken(sl::FrameToken*& token, const uint32_t* frameIndex) {
    const sl::Result result = g_origSlGetNewFrameToken(token, frameIndex);
    FL_HOOK_GUARD({
        if (MayObserve() && token != nullptr) {
            // THE INDEX, NOT THE POINTER. When the title supplies the index it is read
            // from the argument the title passed to the API we hooked (rule 4, the
            // same class of read as slEvaluateFeature's `inputs`). When it does not,
            // the token's own accessor is the vendor's documented way to read the
            // index SL assigned -- one virtual call on the object SL just returned,
            // inside the guard.
            const uint32_t index = frameIndex != nullptr ? *frameIndex : static_cast<uint32_t>(*token);
            const uint64_t key = (1ull << 32) | index;
            uint64_t       seen = g_maxFrameKey.load(std::memory_order_relaxed);
            for (;;) {
                const bool     none = seen == 0u;
                const uint32_t max = static_cast<uint32_t>(seen);
                const bool     rebase = !none && index + kFrameIndexRebaseGap < max;
                if (!none && !rebase && index <= max) {
                    break;    // a frame already counted, asked for again from another thread
                }
                if (g_maxFrameKey.compare_exchange_weak(seen, key, std::memory_order_relaxed)) {
                    g_frameTokens.fetch_add(1u, std::memory_order_relaxed);
                    break;
                }
            }
        }
    })
    return result;
}

// ---------------------------------------------------------------------------
// AMD FidelityFX: the one observer behind the three ffxDispatch trampolines.
//
// HEAD TYPE FIRST, AND NOTHING PAST THE HEAD UNTIL IT MATCHED. ffxApiHeader is the
// first sixteen bytes of every descriptor -- a uint64 type and a pNext -- and that is
// all that is read on an unmatched type. A body is reinterpreted only after its type
// equalled a vendored constant whose layout is identical in every SDK tag consulted
// (1.1.4 and 2.3.0), so the SDK 1.1.x monolith and the 2.x effect DLLs decode with
// one switch. pNext is never followed: nothing this writer publishes lives in a
// chained extension.
//
// WHAT EACH TYPE MEANS FOR THE RECORD:
//   UPSCALE                the upscaler ran this application frame. Identity (which
//                          leaf it arrived at), renderSize (PARAMS), and one more COUNT
//                          in the drain word -- the second application-frame count,
//                          beside PREPARE's, so the consumer can print their ratio the
//                          way it prints tokens/batch for Streamline.
//   PREPARE / PREPARE_V2   the frame-generation prepare pass the title issues once per
//                          application frame, carrying frameID: this vendor's
//                          slGetNewFrameToken. Counted on a NEW frameID only.
//   FRAMEGENERATION        a generated batch, dispatched back through the export from
//                          the title's own frameGenerationCallback -- the fact that
//                          names FL_FG_FSR_FG on this present.
//   anything else          FL_FFX_SEEN_OTHER: a dispatch this build does not decode.
//
// ONE READ-MODIFY-WRITE PER ARM on the drain word, exactly as Hook_SlEvaluateFeature
// keeps; the PREPARE arm's CAS loop is on its own word and is the token detour's
// shape. Nothing here allocates, locks, logs or calls out.
// ---------------------------------------------------------------------------

// PREPARE (what an SDK 1.1.x title sends) and PREPARE_V2 (SDK 2.x) share their layout
// through renderSize byte for byte, and the Overlay reads a PREPARE through the V2
// struct. The older struct is [[deprecated]] in the vendored header, so under /W4 /WX
// naming it is a build error -- the equality is asserted here, inside the smallest
// possible suppression, rather than silencing the attribute for the file. Both offsets
// sit inside the OLDER struct's size, so the read never leaves the smaller descriptor
// a 1.1.x title actually passed.
#pragma warning(push)
#pragma warning(disable : 4996)
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, frameID) ==
                  offsetof(ffxDispatchDescFrameGenerationPrepareV2, frameID),
              "PREPARE and PREPARE_V2 must agree on where frameID is, or a 1.1.x title's frame index is read from "
              "the wrong bytes");
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, renderSize) ==
                  offsetof(ffxDispatchDescFrameGenerationPrepareV2, renderSize),
              "PREPARE and PREPARE_V2 must agree on where renderSize is");
static_assert(offsetof(ffxDispatchDescFrameGenerationPrepare, renderSize) + sizeof(FfxApiDimensions2D) <=
                  sizeof(ffxDispatchDescFrameGenerationPrepare),
              "the fields read must lie inside the smaller descriptor");
#pragma warning(pop)

// The render extent, from an UPSCALE or PREPARE descriptor. Same packing and the same
// in-band unknown as NoteTags: a zero or absurd size clears the sample rather than
// publishing a resolution nobody rendered at.
void NoteFfxExtent(uint64_t w, uint64_t h) noexcept {
    if (w == 0 || h == 0 || w > 0xFFFFu || h > 0xFFFFu) {
        g_ffxExtent.store(0, std::memory_order_release);
        return;
    }
    g_ffxExtent.store(w | (h << 16) | kTagValid, std::memory_order_release);
}

void NoteFfxExtent(const FfxApiDimensions2D& size) noexcept {
    NoteFfxExtent(size.width, size.height);
}

void ObserveFfxDispatch(uint32_t leaf, const ffxDispatchDescHeader* desc) noexcept {
    if (desc == nullptr) {
        return;
    }
    switch (desc->type) {
    case FFX_API_DISPATCH_DESC_TYPE_UPSCALE: {
        const auto* up = reinterpret_cast<const ffxDispatchDescUpscale*>(desc);
        NoteFfxExtent(up->renderSize);
        g_ffxUpscaleLeaf.store(leaf + 1u, std::memory_order_relaxed);
        g_ffxSeen.fetch_add(fl::slseen::kCountOne, std::memory_order_relaxed);
        break;
    }
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE:
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2: {
        const auto* prep = reinterpret_cast<const ffxDispatchDescFrameGenerationPrepareV2*>(desc);
        NoteFfxExtent(prep->renderSize);
        // frameID + 1, so 0 stays "no frame yet"; a NEW maximum counts, a re-issue of a
        // frame already counted does not, and a large jump backwards (a level reload
        // restarting the counter) re-bases rather than freezing the count at zero.
        const uint64_t id = prep->frameID;
        const uint64_t key = id + 1u;
        uint64_t       seen = g_ffxMaxFrameKey.load(std::memory_order_relaxed);
        for (;;) {
            const bool     none = seen == 0u;
            const uint64_t max = seen - 1u;
            const bool     rebase = !none && max > id && max - id > kFrameIndexRebaseGap;
            if (!none && !rebase && id <= max) {
                break;
            }
            if (g_ffxMaxFrameKey.compare_exchange_weak(seen, key, std::memory_order_relaxed)) {
                g_ffxPrepares.fetch_add(1u, std::memory_order_relaxed);
                break;
            }
        }
        break;
    }
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION:
        g_ffxSeen.fetch_or(FL_FFX_SEEN_FG_DISPATCH, std::memory_order_relaxed);
        break;
    default:
        g_ffxSeen.fetch_or(FL_FFX_SEEN_OTHER, std::memory_order_relaxed);
        break;
    }
}

// Four trampolines from one template, one per module slot: MinHook needs a distinct
// detour address per patched target, and the slot index is what lets the trampoline
// find its own original. Every argument forwarded untouched, the original called
// exactly once on every path including the fault path.
template <uint32_t Leaf>
ffxReturnCode_t Hook_FfxDispatchT(ffxContext* context, const ffxDispatchDescHeader* desc) {
    static_assert(Leaf < fl::inventory::kFfxLeafCount, "a trampoline per leaf, and no more");
    FL_HOOK_GUARD({
        if (MayObserve()) {
            ObserveFfxDispatch(Leaf, desc);
        }
    })
    return g_origFfxDispatch[Leaf](context, desc);
}

// The FSR 3.0 host detour: the UPSCALE arm of ObserveFfxDispatch, verbatim, on a
// descriptor of the host API's shape. Original called exactly once on every path.
fsr3host::FfxErrorCode Hook_FfxFsr3ContextDispatchUpscale(fsr3host::FfxFsr3Context*                          context,
                                                          const fsr3host::FfxFsr3DispatchUpscaleDescription* desc) {
    FL_HOOK_GUARD({
        if (MayObserve() && desc != nullptr) {
            NoteFfxExtent(desc->renderSize.width, desc->renderSize.height);
            g_ffxUpscaleLeaf.store(kFfxUpscaleSourceFsr3Host, std::memory_order_relaxed);
            g_ffxSeen.fetch_add(fl::slseen::kCountOne, std::memory_order_relaxed);
        }
    })
    return g_origFfxFsr3DispatchUpscale(context, desc);
}

void* FfxDispatchDetour(uint32_t leaf) noexcept {
    switch (leaf) {
    case fl::inventory::kFfxLeafMonolith:
        return reinterpret_cast<void*>(&Hook_FfxDispatchT<fl::inventory::kFfxLeafMonolith>);
    case fl::inventory::kFfxLeafUpscaler:
        return reinterpret_cast<void*>(&Hook_FfxDispatchT<fl::inventory::kFfxLeafUpscaler>);
    case fl::inventory::kFfxLeafFrameGeneration:
        return reinterpret_cast<void*>(&Hook_FfxDispatchT<fl::inventory::kFfxLeafFrameGeneration>);
    case fl::inventory::kFfxLeafLoader:
        return reinterpret_cast<void*>(&Hook_FfxDispatchT<fl::inventory::kFfxLeafLoader>);
    default:
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Ray tracing (docs/HANDOFF.md item 4, 03_METRICS §RT/PT/RR).
//
// TWO DETOURS, BOTH OF THEM, and that is 03_METRICS:226 rather than thoroughness.
// A writer with only DispatchRays sees NOTHING on an inline-RayQuery title, and
// its silence is indistinguishable from a real negative -- which is how a
// confident `Ray Tracing: No` gets published about a title that ray-traces every
// frame. The AS-build hook is what makes RayQuery visible at all.
//
// RECORDED, NOT EXECUTED (§H6). Both methods record into a command list, possibly
// on many threads, possibly re-executed, possibly never executed. The unit these
// counters carry is therefore "recorded between the previous present and this
// one", and 03_METRICS §Accuracy budget says so where the numbers are defined.
// The concurrency model is the one g_slSeen already proves: relaxed atomics
// written by any recording thread, drained once per present with exchange(0).
// ---------------------------------------------------------------------------

// Evidence since the last present. Bits, drained with the volume below.
std::atomic<uint32_t> g_rtFlags{0};

// Sum of W*H*D over the DispatchRays calls recorded since the last present.
//
// SATURATES AT UINT32_MAX RATHER THAN WRAPPING, and this is the same rule
// fgEvaluations' 255 carries for the opposite reason. A wrapped volume reads LOW
// and is the NUMERATOR of rays_per_pixel, so a wrap under-reports the one input
// the path-tracing heuristic actually reads. 03_METRICS' consumer must refuse to
// publish rays_per_pixel on a saturated record rather than divide a floor.
std::atomic<uint32_t> g_rtDispatchVolume{0};

// One latch per family, because 03_METRICS' `No` branch reads RtAsBuild
// SPECIFICALLY -- not "some RT hook ran". A single latch would let a
// DispatchRays-only writer satisfy the conjunct the whole branch rests on.
std::atomic<uint32_t> g_rtDispatchLive{0};
std::atomic<uint32_t> g_rtAsBuildLive{0};

using PFN_DispatchRays = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*, const D3D12_DISPATCH_RAYS_DESC*);
using PFN_BuildRtAs = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*,
                                               const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC*, UINT,
                                               const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC*);

PFN_DispatchRays g_origDispatchRays = nullptr;
PFN_BuildRtAs    g_origBuildRtAs = nullptr;

// The compare-exchange loop, and NO ARITHMETIC OF ITS OWN.
//
// fl_rtaccum::AddedTo is a pure function in a header, with its identities as
// static_asserts and its shapes driven by hook-harness --probe-dxr-inputs. The
// same split fl_sl_seen.h uses, for the same reason: the maths here is where the
// silent failure lives, and a loop is where a test cannot reach.
void AddSaturating(std::atomic<uint32_t>& slot, uint64_t add) noexcept {
    uint32_t cur = slot.load(std::memory_order_relaxed);
    for (;;) {
        const uint32_t next = fl::rtaccum::AddedTo(cur, add);
        if (cur == next || slot.compare_exchange_weak(cur, next, std::memory_order_relaxed)) {
            return;
        }
    }
}

// THE FLAG IS SET BEFORE THE DESC IS READ, deliberately. A null or malformed desc
// still means the title recorded a ray dispatch; only the VOLUME is unknown.
// Returning early without the bit would turn "we could not size it" into "no ray
// tracing happened", which is the affirmative negative layout v3 exists to stop.
void NoteDispatchRays(const D3D12_DISPATCH_RAYS_DESC* desc) noexcept {
    g_rtFlags.fetch_or(static_cast<uint32_t>(FL_RT_DISPATCH_OBSERVED), std::memory_order_relaxed);
    if (desc == nullptr) {
        return;
    }
    AddSaturating(g_rtDispatchVolume, fl::rtaccum::VolumeOf(desc->Width, desc->Height, desc->Depth));
}

// NO DEREFERENCE AT ALL, and that is the point of this one. What 03_METRICS needs
// from an AS build is that it HAPPENED -- that is what makes inline RayQuery
// visible, since those shaders never call DispatchRays. Reading the desc would
// buy nothing and would put a caller-filled pointer walk on a second hot path.
void NoteBuildRtAs() noexcept {
    g_rtFlags.fetch_or(static_cast<uint32_t>(FL_RT_AS_BUILD_OBSERVED), std::memory_order_relaxed);
}

void STDMETHODCALLTYPE Hook_DispatchRays(ID3D12GraphicsCommandList4* list, const D3D12_DISPATCH_RAYS_DESC* desc) {
    FL_HOOK_GUARD({
        if (MayObserve()) {
            NoteDispatchRays(desc);
        }
    })
    // ALWAYS exactly once, on every path including the fault path, and never
    // inside the __try: a fault in the game's own DispatchRays must not be
    // attributed to us.
    g_origDispatchRays(list, desc);
}

void STDMETHODCALLTYPE Hook_BuildRtAs(ID3D12GraphicsCommandList4*                               list,
                                      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* desc, UINT numPostbuild,
                                      const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* postbuild) {
    FL_HOOK_GUARD({
        if (MayObserve()) {
            NoteBuildRtAs();
        }
    })
    g_origBuildRtAs(list, desc, numPostbuild, postbuild);
}

// Installed from the watchdog the first time the module appears, so a title that
// loads Streamline lazily is still caught without a LoadLibrary hook (§S6 stays
// separable, exactly as docs/HANDOFF.md item 2 requires).
//
// Returns true once the hook is live, so the caller can stop trying.
// Install ONE inventory row: its own target, its own detour, its own family bit,
// its own latch.
//
// THE SHAPE THIS REPLACES WOULD HAVE MIS-BOUND THE SECOND ROW. The previous
// expansion walked FL_HOOK_INVENTORY, stopped at the FIRST row that resolved,
// hooked it with Hook_SlEvaluateFeature and OR'd a hardcoded
// FL_HOOK_UPSCALER_IDENTITY -- ignoring the `family` column entirely. Correct
// only while the table had one row, and silently wrong the moment it did not:
// slSetTag would have been detoured by the slEvaluateFeature body, which reads
// argument 1 as a feature id. Same class of defect as the SL1 ABI break (#71),
// reached from the other direction.
// Patch ONE address, and close the install-after-stop window.
//
// STOPPING IS ONE-WAY, and the check below is what keeps it so. StopObserving can
// run between a caller's g_observing check and here, and MH_DisableHook(MH_ALL_HOOKS)
// would then have run BEFORE this hook existed -- leaving a hook patched in after
// we promised the Agent there were none. Nothing false would be recorded (every
// body consults MayObserve), but legal/DISCLAIMER.md §2 promises the part inside
// the game stops, and a hook installed after the stop is not that.
//
// FACTORED OUT OF InstallRow ON PURPOSE, and the reason has now happened twice.
// It was first inline in the single installer, so a second lazy installer that
// forgot it would reopen the window silently; the ray-tracing installer IS that
// second one, and it cannot reuse InstallRow because its targets come from a
// VTABLE rather than from a name. Sharing this is what keeps the window closed in
// one place rather than in two that can drift.
bool PatchTarget(void* target, void* detour, void** original) noexcept {
    if (target == nullptr) {
        return false;
    }
    if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK) {
        return false;
    }
    if (g_observing.load(std::memory_order_acquire) == 0) {
        MH_DisableHook(target);
        return false;
    }
    return true;
}

// Publish the family bit and latch. Separate from PatchTarget because a hook that
// spans several targets must not claim its family until every one of them is in.
void PublishHookFamily(uint32_t family, std::atomic<uint32_t>& live) noexcept {
    if (g_state != nullptr) {
        std::atomic_ref<uint32_t> hooks{g_state->hooksInstalledMask};
        hooks.fetch_or(family, std::memory_order_relaxed);
    }
    live.store(1, std::memory_order_release);
}

// The runtime census (fl_shm.h §FlRuntimeCensus). Watchdog only; OR-only; never
// on the present path. It is the answer to "was any known frame-generation or
// upscaler runtime in this process" -- which is not the same question as "did the
// title generate frames", and fl_shm.h spends a section on why it must not be
// read as one.
void PublishRuntimeCensus() noexcept {
    if (g_state == nullptr) {
        return;
    }
    const uint32_t            seen = fl::inventory::ObserveRuntimeModules();
    std::atomic_ref<uint32_t> census{g_state->runtimeCensus};
    census.fetch_or(seen, std::memory_order_relaxed);
}

// The tag census, on the same tick and by the same rule: OR-only, from the
// process-local word the three tag routes accumulate into, so region 2 takes one
// write a second rather than one per tag list (fl_shm.h §slTagCensus).
// DXGI's counter against ours, published on the same tick. Monotonic, so a plain store
// of the hook-local value is the whole publish.
void PublishDxgiPresentCounters() noexcept {
    if (g_state == nullptr) {
        return;
    }
    std::atomic_ref<uint32_t> unseen{g_state->dxgiPresentsUnseen};
    std::atomic_ref<uint32_t> samples{g_state->dxgiPresentSamples};
    unseen.store(g_dxgiUnseen.load(std::memory_order_relaxed), std::memory_order_relaxed);
    samples.store(g_dxgiSamples.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void PublishSlTagCensus() noexcept {
    if (g_state == nullptr) {
        return;
    }
    const uint32_t seen = g_slTagCensus.load(std::memory_order_relaxed);
    if (seen == 0u) {
        return;
    }
    std::atomic_ref<uint32_t> census{g_state->slTagCensus};
    census.fetch_or(seen, std::memory_order_relaxed);
}

bool InstallRow(const wchar_t* module, const char* symbol, uint32_t family, void* detour, void** original,
                std::atomic<uint32_t>& live) noexcept {
    if (live.load(std::memory_order_acquire) != 0) {
        return true;
    }

    void* target = fl::inventory::ResolveScoped(module, symbol);
    if (target == nullptr) {
        // The game has not loaded Streamline, or loaded a generation whose ABI we
        // do not speak (#71). An answer, not a failure.
        return false;
    }

    if (!PatchTarget(target, detour, original)) {
        return false;
    }

    PublishHookFamily(family, live);
    return true;
}

// Bind a row's family bit to the detour that implements it.
//
// A row whose family has no detour installs NOTHING and publishes NOTHING. That
// is the safe direction: an unbound row is a table entry somebody added without
// writing its hook, and hooking it with a neighbour's body is exactly what this
// function exists to stop.
bool InstallByFamily(const wchar_t* module, const char* symbol, uint32_t family) noexcept {
    // EQUALITY AGAINST THE COMPOUND CONSTANT, not `& FL_HOOK_UPSCALER_IDENTITY`. A
    // bit test would bind any future row that happens to include identity to THIS
    // detour -- which is the neighbour's-body defect this function exists to stop,
    // and it would arrive silently because the row would still install.
    if (family == fl::inventory::kFamilyEvaluateFeature) {
        return InstallRow(module, symbol, family, reinterpret_cast<void*>(&Hook_SlEvaluateFeature),
                          reinterpret_cast<void**>(&g_origSlEvaluateFeature), g_upscalerIdentityLive);
    }
    if (family == static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS)) {
        // TWO ROWS IN THIS FAMILY WITH DIFFERENT SIGNATURES, so the family alone would
        // bind the wrong body to one of them. The symbol decides, through the constant
        // the inventory header binds to its own row -- no literal here, which is what
        // keeps hookinventory-check Pass B able to see every resolver in the Overlay.
        //
        // PATCHED WITHOUT PUBLISHING, as the ffx leaves are. The family is published by
        // PublishParamsFamilyIfWhole once every row this interposer exports is in;
        // published on the first row, it entitled records the second was not yet
        // producing -- 38 of 41 records claiming params on the frame-based fixture.
        const bool             frameRow = fl::inventory::SameA(symbol, fl::inventory::kSymbolSlSetTagForFrame);
        std::atomic<uint32_t>& patched = frameRow ? g_slSetTagForFramePatched : g_slSetTagPatched;
        if (patched.load(std::memory_order_acquire) != 0) {
            return true;
        }
        void* target = fl::inventory::ResolveScoped(module, symbol);
        if (target == nullptr) {
            return false;    // not loaded, a generation without this export, or an ABI we do not speak
        }
        void* detour =
            frameRow ? reinterpret_cast<void*>(&Hook_SlSetTagForFrame) : reinterpret_cast<void*>(&Hook_SlSetTag);
        void** original =
            frameRow ? reinterpret_cast<void**>(&g_origSlSetTagForFrame) : reinterpret_cast<void**>(&g_origSlSetTag);
        if (!PatchTarget(target, detour, original)) {
            return false;
        }
        patched.store(1, std::memory_order_release);
        return true;
    }
    if (family == static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS)) {
        return InstallRow(module, symbol, family, reinterpret_cast<void*>(&Hook_SlGetNewFrameToken),
                          reinterpret_cast<void**>(&g_origSlGetNewFrameToken), g_frameTokensLive);
    }
    if (family == fl::inventory::kFamilyFfxDispatch) {
        // FOUR ROWS, ONE ARM: the row's MODULE picks the trampoline and the latch slot,
        // because the four modules export the same name and each must be patched at its
        // own address. Per-leaf latches rather than one, so that on a UE5 title -- two
        // leaves, loaded in whatever order the engine's plugin loads them -- the watchdog
        // keeps trying the leaf that is not in yet after the other one is.
        //
        // PATCHED HERE, PUBLISHED IN PublishFfxFamilyIfWhole. InstallRow publishes the
        // family the moment one target is in, which is right for a one-target row and
        // wrong for this one: the family's three claims come from two different leaves
        // on a 2.x title, so a family published on the first leaf entitles records the
        // second leaf is not yet producing.
        //
        // A module the leaf table does not know is either the FSR 3.0 HOST -- its own
        // export, its own body, its own latch, the same family and publish point -- or
        // nothing: fl_hook_inventory.h's static_asserts make the latter unreachable for
        // the rows as written, and installing nothing is the safe direction for a row
        // somebody adds later.
        const int leaf = fl::inventory::FfxLeafOf(module);
        if (leaf < 0) {
            if (_wcsicmp(module, fl::inventory::kModuleFfxFsr3Host) != 0) {
                return false;
            }
            if (g_fsr3HostPatched.load(std::memory_order_acquire) != 0) {
                return true;
            }
            void* target = fl::inventory::ResolveScoped(module, symbol);
            if (target == nullptr) {
                return false;    // not loaded, or a module of the name that does not speak the 3.0 host ABI
            }
            if (!PatchTarget(target, reinterpret_cast<void*>(&Hook_FfxFsr3ContextDispatchUpscale),
                             reinterpret_cast<void**>(&g_origFfxFsr3DispatchUpscale))) {
                return false;
            }
            g_fsr3HostPatched.store(1, std::memory_order_release);
            return true;
        }
        const auto slot = static_cast<uint32_t>(leaf);
        if (g_ffxPatched[slot].load(std::memory_order_acquire) != 0) {
            return true;
        }
        void* target = fl::inventory::ResolveScoped(module, symbol);
        if (target == nullptr) {
            return false;    // not loaded, or a module of the name that does not speak ffx-api
        }
        if (!PatchTarget(target, FfxDispatchDetour(slot), reinterpret_cast<void**>(&g_origFfxDispatch[slot]))) {
            return false;
        }
        g_ffxPatched[slot].store(1, std::memory_order_release);
        return true;
    }
    return false;
}

// Publish kFamilyFfxDispatch once EVERY LEAF THE PROCESS HAS LOADED is patched, and
// at least one is. Runs every watchdog tick after the rows, so a leaf that loads later
// (a title enabling frame generation from its menu after launch) is patched on the
// next tick; by then the family is already published, and the short window in which
// that leaf's dispatches go unobserved is what the consumer's frames/upscale-drained
// ratio exists to show. A leaf of the right NAME that ResolveScoped refuses keeps the
// family unpublished for as long as it stays loaded -- the writer cannot claim what
// it cannot read, and a partial claim is the defect this function exists to prevent.
void PublishFfxFamilyIfWhole() noexcept {
    if (g_ffxLive.load(std::memory_order_acquire) != 0) {
        return;
    }
    bool anyPatched = false;
    for (uint32_t i = 0; i < fl::inventory::kFfxLeafCount; ++i) {
        if (g_ffxPatched[i].load(std::memory_order_acquire) != 0) {
            anyPatched = true;
            continue;
        }
        if (fl::inventory::IsModuleLoaded(fl::inventory::kFfxLeafModules[i])) {
            return;    // loaded and not yet patched: the family is not whole
        }
    }
    // The FSR 3.0 host is the fifth target under the same rule. On Cyberpunk it lives
    // beside the 1.1.x monolith, and the family's FsrFg claim comes from the monolith
    // while the identity comes from the host -- published on whichever patched first,
    // the other's records would be entitled before they were produced (#110's window).
    if (g_fsr3HostPatched.load(std::memory_order_acquire) != 0) {
        anyPatched = true;
    } else if (fl::inventory::IsModuleLoaded(fl::inventory::kModuleFfxFsr3Host)) {
        return;
    }
    if (anyPatched) {
        PublishHookFamily(fl::inventory::kFamilyFfxDispatch, g_ffxLive);
    }
}

// Publish FL_HOOK_UPSCALER_PARAMS once EVERY TAG EXPORT THE LOADED INTERPOSER HAS is
// patched, and at least one is. "Has" is the export table, not the module list: a
// 2.7 interposer exports slSetTag alone and is whole with that one row patched; a
// 2.8 interposer exports both, and is whole only with both. Runs after the rows on
// every watchdog tick, so a row whose patch failed once holds the family back until
// it is in rather than letting the other row publish over it.
void PublishParamsFamilyIfWhole() noexcept {
    if (g_upscalerParamsLive.load(std::memory_order_acquire) != 0) {
        return;
    }
    struct Row {
        const char*            symbol;
        std::atomic<uint32_t>* patched;
    };
    const Row rows[] = {{fl::inventory::kSymbolSlSetTag, &g_slSetTagPatched},
                        {fl::inventory::kSymbolSlSetTagForFrame, &g_slSetTagForFramePatched}};
    bool      anyPatched = false;
    for (const Row& r : rows) {
        if (r.patched->load(std::memory_order_acquire) != 0) {
            anyPatched = true;
            continue;
        }
        if (fl::inventory::ResolveScoped(fl::inventory::kModuleSlInterposer, r.symbol) != nullptr) {
            return;    // exported and not yet patched: the family is not whole
        }
    }
    if (anyPatched) {
        PublishHookFamily(static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS), g_upscalerParamsLive);
    }
}

bool InstallUpscalerHooks() noexcept {
    bool any = false;
#define FL_INSTALL_ROW(mod, sym, family) any = InstallByFamily(mod, sym, static_cast<uint32_t>(family)) || any;
    FL_HOOK_INVENTORY(FL_INSTALL_ROW)
#undef FL_INSTALL_ROW
    PublishParamsFamilyIfWhole();
    PublishFfxFamilyIfWhole();
    return any;
}

// ---------------------------------------------------------------------------
// Installing the ray-tracing hooks.
//
// WHY THIS IS NOT AN FL_HOOK_INVENTORY ROW. That table is (module, exported
// symbol) and tools/hookinventory-check.ps1 checks each row against measured
// export data. These are COM vtable slots: no name is resolved, so Pass A has no
// row to check and Pass B's stray-literal sweep is silent. The corresponding gate
// is `ctest fl_d3d12_vtable_indices`, which proves both slots BY BEHAVIOUR on both
// list types. 17_HOOK_ENGINE §Ray tracing records the asymmetry so nobody reads
// §Hook inventory as "every hook we install is in that table".
//
// THE VTABLE COMES OFF A LIST CREATED ON THE GAME'S OWN DEVICE. ResolveApi
// already receives that device from IDXGISwapChain::GetDevice on a swapchain we
// were called on -- an object we legitimately own (CLAUDE.md rule 4) -- and
// PublishRtTier already argues that on the record. A throwaway WARP device would
// also have worked (measured 2026-08-20: a WARP list and a hardware list share a
// vtable), and is still not used: this machine lost WARP's D3D12 path to a
// Windows Insider build for a fortnight, and a design that needs no WARP cannot
// be taken down by one.
//
// ON THE WATCHDOG, NOT THE PRESENT PATH. D3D12 device methods are free-threaded,
// so creating a throwaway allocator and list off it is legal from this thread; and
// keeping installation on the thread that already installs the Streamline rows
// means ONE install-after-stop window rather than two.
// ---------------------------------------------------------------------------

// A throwaway command list of `type`, upgraded to ID3D12GraphicsCommandList4.
//
// The QI is the part that legitimately fails: ID3D12GraphicsCommandList4 arrived
// in Windows 10 1809, and on an older runtime there is no ray-tracing surface to
// hook. That is an answer -- FL_MEASURED_RT stays clear and the session says N/A.
ID3D12GraphicsCommandList4* MakeThrowawayList(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type,
                                              ID3D12CommandAllocator**    allocOut,
                                              ID3D12GraphicsCommandList** listOut) noexcept {
    if (FAILED(device->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator),
                                              reinterpret_cast<void**>(allocOut)))) {
        return nullptr;
    }
    if (FAILED(device->CreateCommandList(0, type, *allocOut, nullptr, __uuidof(ID3D12GraphicsCommandList),
                                         reinterpret_cast<void**>(listOut)))) {
        return nullptr;
    }
    ID3D12GraphicsCommandList4* list4 = nullptr;
    if (FAILED((*listOut)->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), reinterpret_cast<void**>(&list4)))) {
        return nullptr;
    }

    // THE RESET IS LOAD-BEARING, AND IT WAS MEASURED THE HARD WAY.
    //
    // A freshly created command list carries D3D12Core.dll's own class vtable. The
    // FIRST Reset replaces it with a PER-OBJECT vtable in which the vendor driver
    // has taken methods over -- measured 2026-08-20 on an RTX 5080: DispatchRays
    // moves from D3D12Core.dll to nvwgf2umx.dll, while
    // BuildRaytracingAccelerationStructure stays in D3D12Core.
    //
    // Every game resets its command lists every frame, so the addresses in an
    // UNRESET list's vtable are ones no title ever calls for the moved methods. A
    // hook installed from those is live, publishes its family bit, and NEVER FIRES
    // -- which the injected fixture caught as `withDispatch = 0` sitting beside
    // `hooks = RT_DISPATCH | RT_AS_BUILD`, a mask bit with nothing behind it.
    //
    // So the throwaway has to go through the same lifecycle the game's lists do, or
    // it is not a sample of them. §H5's lesson at a different layer: the object you
    // read is not necessarily the object the title uses.
    if (FAILED(list4->Close()) || FAILED((*allocOut)->Reset()) || FAILED(list4->Reset(*allocOut, nullptr))) {
        list4->Release();
        return nullptr;
    }
    return list4;
}

void ReleaseIf(IUnknown* p) noexcept {
    if (p != nullptr) {
        p->Release();
    }
}

// Both list types, both slots, and a REFUSAL where the measurement stops holding.
//
// MEASURED 2026-08-20 (ctest fl_dxr_probe): a DIRECT and a COMPUTE command list
// have DIFFERENT vtables holding the SAME function pointers. So one MinHook patch
// per method covers both list types -- and a hook that patched only the DIRECT
// vtable would have missed every AS build recorded on a compute list, which async
// BLAS building makes ordinary. The mask bit would still be set and the evidence
// absent, so 03_METRICS' `No` branch would publish a confident negative about a
// title that ray-traces every frame.
//
// IF A FUTURE RUNTIME SPLITS THEM, WE REFUSE. One g_orig* cannot serve two
// implementations: forwarding one's detour through the other's trampoline calls
// the wrong function inside the game. Two detours and two trampolines would be the
// fix, and building that for a case no measurement has produced is how untested
// code reaches a hot path. Degrading to N/A is the safe direction; guessing is not.
bool InstallRtHooks() noexcept {
    if (g_rtDispatchLive.load(std::memory_order_acquire) != 0 && g_rtAsBuildLive.load(std::memory_order_acquire) != 0) {
        return true;
    }
    auto* device = static_cast<ID3D12Device*>(g_d3d12Device.load(std::memory_order_acquire));
    if (device == nullptr) {
        return false;    // no D3D12 swapchain has presented yet. An answer, and it retries.
    }

    ID3D12CommandAllocator*     directAlloc = nullptr;
    ID3D12GraphicsCommandList*  directList = nullptr;
    ID3D12CommandAllocator*     computeAlloc = nullptr;
    ID3D12GraphicsCommandList*  computeList = nullptr;
    ID3D12GraphicsCommandList4* direct4 =
        MakeThrowawayList(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &directAlloc, &directList);
    ID3D12GraphicsCommandList4* compute4 =
        MakeThrowawayList(device, D3D12_COMMAND_LIST_TYPE_COMPUTE, &computeAlloc, &computeList);

    bool installed = false;
    if (direct4 != nullptr && compute4 != nullptr) {
        // BOTH ARE REQUIRED, not just whichever succeeded. With one list we could
        // not compare the two vtables, and hooking on the strength of a measurement
        // taken on another machine is the assumption this whole path replaces. The
        // watchdog retries every second, so a transient failure costs a tick.
        void* const* directVtbl = *reinterpret_cast<void* const* const*>(direct4);
        void* const* computeVtbl = *reinterpret_cast<void* const* const*>(compute4);

        const bool dispatchAgrees =
            directVtbl[fl::d3d12::kDispatchRaysIndex] == computeVtbl[fl::d3d12::kDispatchRaysIndex];
        const bool buildAgrees = directVtbl[fl::d3d12::kBuildRaytracingAccelerationStructureIndex] ==
                                 computeVtbl[fl::d3d12::kBuildRaytracingAccelerationStructureIndex];

        if (dispatchAgrees && g_rtDispatchLive.load(std::memory_order_acquire) == 0 &&
            PatchTarget(directVtbl[fl::d3d12::kDispatchRaysIndex], reinterpret_cast<void*>(&Hook_DispatchRays),
                        reinterpret_cast<void**>(&g_origDispatchRays))) {
            PublishHookFamily(static_cast<uint32_t>(fl::FL_HOOK_RT_DISPATCH), g_rtDispatchLive);
            installed = true;
        }
        if (buildAgrees && g_rtAsBuildLive.load(std::memory_order_acquire) == 0 &&
            PatchTarget(directVtbl[fl::d3d12::kBuildRaytracingAccelerationStructureIndex],
                        reinterpret_cast<void*>(&Hook_BuildRtAs), reinterpret_cast<void**>(&g_origBuildRtAs))) {
            PublishHookFamily(static_cast<uint32_t>(fl::FL_HOOK_RT_AS_BUILD), g_rtAsBuildLive);
            installed = true;
        }
    }

    // Closed before release: a command list left recording holds its allocator.
    if (directList != nullptr) {
        directList->Close();
    }
    if (computeList != nullptr) {
        computeList->Close();
    }
    ReleaseIf(direct4);
    ReleaseIf(compute4);
    ReleaseIf(directList);
    ReleaseIf(computeList);
    ReleaseIf(directAlloc);
    ReleaseIf(computeAlloc);
    return installed;
}

// The watchdog. Runs whether or not the game presents, which is the reason it
// exists; see the block comment above for why a thread is acceptable in the
// Overlay and was not in the Vulkan layer.
DWORD WINAPI WatchdogThread(LPVOID) noexcept {
    for (;;) {
        // The 1 s tick, OR the LoadLibrary detour's wake -- whichever comes first.
        // An event that failed to create degrades to the plain sleep.
        if (g_watchdogWake != nullptr) {
            WaitForSingleObject(g_watchdogWake, kWatchdogIntervalMs);
        } else {
            Sleep(kWatchdogIntervalMs);
        }

        if (g_observing.load(std::memory_order_acquire) == 0) {
            return 0;    // stopped by us or by a hook body; nothing left to decide
        }
        if (g_control == nullptr) {
            continue;
        }

        std::atomic_ref<uint32_t> unhook{g_control->unhookRequested};
        if (unhook.load(std::memory_order_acquire) != 0) {
            StopObserving(FL_STATUS_UNHOOKED);
            return 0;
        }

        // The in-process stop, for a title that is NOT presenting (menu, hung,
        // alt-tabbed) -- the case the present path cannot reach. Same order as the
        // Agent's stop: before any install.
        if (g_earlyStopFamily.load(std::memory_order_acquire) != 0u) {
            PublishLoaderWords();
            StopObserving(FL_STATUS_STOPPED_BLOCKLISTED);
            return 0;
        }

        // The detour's words BEFORE the installers: a reader that sees the hook
        // family appear must already be able to see the wake that caused it.
        PublishLoaderWords();

        // Lazy feature-hook installation. AFTER the two stops, never before: a
        // tick that is going to unhook must not install anything first.
        //
        // THIS IS THE VEHICLE. 17_HOOK_ENGINE §DLL entry step 5 installs feature
        // hooks "the first time their module appears", and a game that loads
        // Streamline lazily -- most of them, since sl.interposer is pulled in at
        // device creation -- would be missed by a one-shot check at init. The
        // LoadLibrary detour (step 3, P1 item 1) does NOT install anything itself:
        // it runs under the loader lock while MinHook suspends every thread to
        // patch (§H2), so it only signals g_watchdogWake and the install still
        // happens HERE, on this thread -- within milliseconds of the module rather
        // than on the next tick. Through P0 the tick alone was the vehicle, which
        // is why §S6 stayed separable rather than becoming a prerequisite.
        //
        // It RETRIES until it succeeds and latches only on SUCCESS. An
        // install-attempted flag would give the module exactly one chance, at a
        // moment chosen by our sleep rather than by the game.
        InstallUpscalerHooks();

        // The ray-tracing hooks, on the same vehicle and for a related reason: the
        // device only becomes available when a D3D12 swapchain first presents, which
        // is a moment chosen by the game rather than by our init. It RETRIES and
        // latches only on success, exactly as the upscaler installer does.
        InstallRtHooks();

        // The census, on the same tick. After the installers so that a module which
        // appeared this second is both hooked and counted in the same pass.
        PublishRuntimeCensus();
        PublishSlTagCensus();
        PublishDxgiPresentCounters();
        PublishLoaderWords();

        // Supervision loss. guardTicks counts COMPLETED guard evaluations, not
        // seconds, so a stalled guard loop stops it advancing even while the
        // Agent process is alive -- which is exactly the case a timer-driven
        // heartbeat would have missed.
        //
        // "Never advanced" and "stopped advancing" are the same state: the clock
        // starts when the mapping is published, so a capture side no Agent ever
        // adopted is inert from the beginning rather than enjoying a grace
        // window.
        std::atomic_ref<uint32_t> ticks{g_control->guardTicks};
        const uint32_t            now = ticks.load(std::memory_order_acquire);
        const ULONGLONG           t = GetTickCount64();
        if (now != g_lastTicks) {
            g_lastTicks = now;
            g_lastTickAt = t;
            continue;
        }
        if (t - g_lastTickAt >= FL_GUARD_TICK_DEADLINE_MS) {
            StopObserving(FL_STATUS_UNHOOKED);
            return 0;
        }
    }
}

// The hot path. One QPC read, a few cached-state reads, one 60-byte store in two
// spans, two relaxed atomic stores, two fences. No syscall, no allocation, no
// lock, no logging (NFR-1, target <= 1 us; a bare vtable detour measured 8.4 ns).
void RecordPresent(IDXGISwapChain* sc, UINT syncInterval, UINT flags) noexcept {
    if (!MayObserve()) {
        if ((flags & DXGI_PRESENT_TEST) == 0u) {
            g_dxgiEpoch.fetch_add(1u, std::memory_order_relaxed);    // see the declaration
        }
        return;
    }

    // DXGI_PRESENT_TEST IS NOT A FRAME, AND THE RING ONLY CARRIES FRAMES.
    //
    // It is the occlusion probe: DXGI runs the presentation test and submits
    // nothing. Measured on this harness (#35): 500 of them leave
    // GetLastPresentCount at 0 while 37 real presents move it by 37. Applications
    // issue them while minimised or fully occluded, so a backgrounded game can
    // emit a steady stream of non-frames.
    //
    // Recording them would corrupt exactly the metric this product exists to
    // report. frameIndex would advance without a frame; 03_METRICS derives
    // Displayed FPS from count(F_disp)/D and frame times from consecutive qpc, so
    // a minimised game would report a frame rate it is not rendering, and the
    // interval either side of a probe burst is not a frame time at all.
    //
    // WHO FILTERS was undecided until 2026-08-05 -- 07_IPC assigned it to nobody
    // and 03_METRICS was silent, which is how a criterion counting "N presents ->
    // N records" once became satisfiable only by a writer that counts non-frames.
    // Decided: the WRITER drops them, so the ring means one thing and no
    // downstream consumer has to remember to filter. Cost is one AND and one
    // branch on the hot path, after the safety checks so a probe-only process
    // still evaluates the stop.
    if ((flags & DXGI_PRESENT_TEST) != 0u) {
        return;
    }

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);

    SwapChainSlot* slot = FindOrAdd(sc);
    PublishAdapterOnce(sc);

    // DXGI'S OWN COUNT OF THIS CHAIN'S PRESENTS, read here on the object the title passed
    // (the same class of read as GetDesc above), BEFORE this present is forwarded: the
    // value is therefore the number of presents completed before this one, and the
    // delta from the previous hooked present on the same chain is 1 + whatever DXGI
    // counted that this hook never saw -- a pacer presenting through a body the inline
    // patches do not cover. A delta of exactly 1 every time says DXGI saw nothing more
    // than we did. DXGI_PRESENT_TEST presents are already filtered above and do not
    // move this counter (measured, #35), so they cannot read as unseen presents.
    bool    dxgiDelta = false;
    uint8_t dxgiUnseenHere = 0;
    if (slot != nullptr) {
        UINT           dxgiCount = 0;
        const uint32_t epoch = g_dxgiEpoch.load(std::memory_order_relaxed);
        if (SUCCEEDED(sc->GetLastPresentCount(&dxgiCount))) {
            if (slot->haveDxgiCount && slot->dxgiEpoch == epoch) {
                const uint32_t delta = dxgiCount - slot->lastDxgiCount;
                dxgiDelta = true;
                if (delta > 1u) {
                    g_dxgiUnseen.fetch_add(delta - 1u, std::memory_order_relaxed);
                    dxgiUnseenHere = fl::slseen::SaturateToByte(delta - 1u);
                }
            } else if (!slot->haveDxgiCount && g_state != nullptr &&
                       !g_dxgiBeforeHookPublished.exchange(true, std::memory_order_relaxed)) {
                // The FIRST hooked present: DXGI's count is the presents that ran before
                // this hook was in (fl_shm.h §dxgiPresentsBeforeHook). One store, once.
                std::atomic_ref<uint32_t> before{g_state->dxgiPresentsBeforeHook};
                before.store(dxgiCount, std::memory_order_relaxed);
            }
            slot->lastDxgiCount = dxgiCount;
            slot->dxgiEpoch = epoch;
            slot->haveDxgiCount = true;
            g_dxgiSamples.fetch_add(1u, std::memory_order_relaxed);
        }
    }

    FlFrameRecord rec{};
    rec.qpc = static_cast<uint64_t>(qpc.QuadPart);
    rec.frameIndex = g_frameIndex++;
    rec.presentFlags = flags;
    rec.syncInterval = static_cast<uint16_t>(syncInterval);
    rec.api = slot != nullptr ? slot->api : static_cast<uint8_t>(FL_API_UNKNOWN);
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
    //
    // ...AND IT CLAIMED THE ONE THING UNCONDITIONALLY, WHICH IS THE SAME DEFECT.
    // The bit was set even when there was no size to claim, and two paths reach
    // that (20_OPEN_QUESTIONS §S29(g)):
    //
    //   - FindOrAdd returns nullptr once kMaxSwapChains slots are taken, so
    //     outputW/H are never assigned and stay 0.
    //   - GetDesc failing in FindOrAdd or ForgetChainSize leaves them 0.
    //
    // A record saying "output resolution MEASURED: 0 x 0" is worse than one
    // saying nothing: 03_METRICS computes the upscale ratio as
    // sqrt((outW*outH)/(renW*renH)) from exactly these fields. The bit is now
    // conditional on there being a value behind it, which is what every other
    // bit in this mask already means.
    //
    // FL_MEASURED_PRESENT_ARGS is claimed here and not further up because it is
    // the one thing a DXGI present hook always has: syncInterval and presentFlags
    // are the call's own arguments. wglSwapBuffers and vkQueuePresentKHR have
    // neither, which is why the bit exists at all rather than being assumed.
    const uint16_t haveOutputRes = (rec.outputW != 0 && rec.outputH != 0) ? FL_MEASURED_OUTPUT_RES : 0u;
    rec.measuredMask = static_cast<uint16_t>(haveOutputRes | FL_MEASURED_PRESENT_ARGS);

    // The per-present form of the DXGI counter read above (fl_shm.h §dxgiUnseen). The
    // bit says a delta EXISTED -- a zero under it is DXGI agreeing with this hook, not
    // silence -- which is why the first present of a chain claims nothing.
    if (dxgiDelta) {
        rec.dxgiUnseen = dxgiUnseenHere;
        rec.measuredMask = static_cast<uint16_t>(rec.measuredMask | FL_MEASURED_DXGI_PRESENTS);
    }

    // --- Upscaler identity, and the three states it must keep apart ----------
    //
    // The counter is CONSUMED here, once per present, so the record describes
    // THIS frame and the next frame starts from nothing. A read-without-clear
    // would latch: one DLSS evaluation would report DLSS forever, including
    // after the user turned it off mid-session, which is precisely the
    // mid-session settings change 03_METRICS §Upscaling segments on.
    const uint32_t seen = g_slSeen.exchange(0, std::memory_order_acq_rel);
    // The tag TYPES set since the last present, on any route. ALWAYS drained, like the
    // AMD word below: a HUD-less tag is valid until the frame it was set for is
    // presented, and a word that outlived that frame would mark the wrong present.
    const uint32_t tagged = g_slTagTypes.exchange(0, std::memory_order_acq_rel);

    // The AMD word and count, drained beside the Streamline word and for the same
    // reason. ALWAYS drained, whether or not anything below reads them: a count that
    // is consumed only once its hook is live would latch through the install window
    // and land whole on the first present after it.
    const uint32_t ffx = g_ffxSeen.exchange(0, std::memory_order_acq_rel);
    const uint32_t ffxPrepares = g_ffxPrepares.exchange(0u, std::memory_order_relaxed);
    const uint32_t ffxUpscales = fl::slseen::FgEvals(ffx);
    const bool     ffxLive = g_ffxLive.load(std::memory_order_relaxed) != 0;
    const bool     ffxSaw = ffxUpscales != 0u || ffxPrepares != 0u;

    if (g_upscalerIdentityLive.load(std::memory_order_relaxed) != 0 || ffxLive) {
        // A hook CAPABLE of answering was live for this frame, so the field may
        // be read. What it says is a separate question, below.
        rec.measuredMask = static_cast<uint16_t>(rec.measuredMask | FL_MEASURED_UPSCALER);

        if ((seen & FL_SL_SEEN_DLSS) != 0u) {
            rec.upscaler = FL_UPSCALER_DLSS;
        } else if ((seen & FL_SL_SEEN_NIS) != 0u) {
            rec.upscaler = FL_UPSCALER_NIS;
        } else if ((seen & FL_SL_SEEN_DLSS_RR) != 0u) {
            // RAY RECONSTRUCTION IS DOING THE UPSCALING, and this arm is a MEASUREMENT
            // rather than the preference §S30 forbids.
            //
            // Cyberpunk 2077, 2026-08-15, two 40 s captures: with DLSS_D = True the title
            // evaluates kFeatureDLSS_RR on every application frame and kFeatureDLSS NOT
            // ONCE -- 2544 of 2544 batches, zero DLSS, zero NIS, zero undecoded ids. RR
            // replaces the separate super-resolution pass rather than running beside it.
            //
            // WHAT MAKES THIS EVIDENCE AND NOT AN INFERENCE: renderW/H are published only
            // on a frame where an evaluation was seen, and they came back 1485x835 against
            // the title's own `DLSS = Balanced` at 2560x1440 -- 0.58 exactly. So the
            // scaling-input tag ARRIVES ON THE RR EVALUATION. The evaluation that upscales
            // is the one we are looking at, and reporting UNKNOWN for it was our decode
            // dropping an answer it had been handed.
            //
            // FL_UPSCALER_DLSS AND NOT A NEW VALUE. Layout v3 retired
            // FL_UPSCALER_RETIRED_RAY_RECONSTRUCTION and reserved the slot precisely
            // because RR is not mutually exclusive with DLSS -- it is an independent
            // tri-state axis, which FL_FEAT_RAY_RECONSTRUCTION already carries from the
            // same word. So the technology is DLSS and the RR axis stays where it is;
            // giving RR its own upscaler value would resurrect the conflation v3 removed.
            rec.upscaler = FL_UPSCALER_DLSS;
        } else if (ffxUpscales != 0u) {
            // AN UPSCALE DISPATCH REACHED AN ffx-api MODULE THIS PRESENT, and which one
            // decides the byte. The SDK 1.1.x monolith (amd_fidelityfx_dx12.dll) hosts
            // FSR 3.1 and nothing else, so it is FSR3 as a fact. The SDK 2.x upscaler
            // DLL hosts FSR 3.1 AND FSR 4 behind the same UPSCALE type, the provider is
            // chosen at context creation by an opaque version id, and the dispatch does
            // not carry it -- so from that leaf, and from the SDK 2.x loader that fronts
            // it, the honest byte is "FSR, version not named" (fl_shm.h
            // §FL_UPSCALER_FSR_UNVERSIONED): FSR3 would be right on every non-RDNA4
            // machine and a fabrication on one, and UNKNOWN would print the very N/A
            // this arm exists to remove. The FSR 3.0 HOST (ffx_fsr3_x64.dll, Cyberpunk's
            // copy) hosts FSR 3.0 and nothing else, so it is FSR3 as a fact too.
            const uint32_t src = g_ffxUpscaleLeaf.load(std::memory_order_relaxed);
            rec.upscaler = src == fl::inventory::kFfxLeafMonolith + 1u || src == kFfxUpscaleSourceFsr3Host
                               ? static_cast<uint8_t>(FL_UPSCALER_FSR3)
                               : static_cast<uint8_t>(FL_UPSCALER_FSR_UNVERSIONED);
        } else {
            // UNKNOWN, AND NEVER `NONE`. FL_UPSCALER_NONE means "a hook ran and
            // there was genuinely no upscaler" -- the one state fl_shm.h allows
            // to be aggregated as a negative. This writer hooks Streamline, the
            // ffx-api leaves and the FSR 3.0 host facade and nothing else, so an XeSS
            // or NGX-direct title, or an FSR 3.0 title calling ffx_fsr3upscaler_x64.dll
            // directly, evaluates its upscaler somewhere we are not looking -- and so
            // does an FSR title on a present that drained no dispatch, which under
            // frame generation is every generated present.
            // Reporting NONE would turn "we do not cover that vendor" into a measured
            // fact about the title. UNKNOWN says the true thing: our coverage is short.
            //
            // It is also what an unrecognised feature id lands on
            // (FL_SL_SEEN_OTHER) -- a NEW Streamline feature we do not decode is
            // the same admission, from the other direction.
            rec.upscaler = FL_UPSCALER_UNKNOWN;
        }
    }

    // --- Render resolution, from the global resource tags ---------------------
    //
    // TWO CONDITIONS, AND THE SECOND IS THE ONE THAT IS EASY TO DROP. The params
    // hook must be live (we were in a position to read a tag), AND an evaluation
    // must have been seen THIS FRAME (`seen != 0`). A tag is viewport state that
    // outlives any one frame, so publishing on the tag alone would report a
    // render resolution for every frame after a title stopped upscaling --
    // dressing stale state as a measurement, which is the whole failure class
    // layout v3 exists to prevent.
    if (g_upscalerParamsLive.load(std::memory_order_relaxed) != 0 && seen != 0u) {
        const uint64_t tag = g_tagExtent.load(std::memory_order_acquire);
        if ((tag & kTagValid) != 0u) {
            rec.renderW = static_cast<uint16_t>(tag & 0xFFFFu);
            rec.renderH = static_cast<uint16_t>((tag >> 16) & 0xFFFFu);

            // 0xFF, NEVER 0, and for two different reasons that land on the same
            // byte. upscalerQuality has no in-band "not measured" sentinel -- 0 is
            // NGX MaxPerf, a real preset -- so 0 here would publish "DLSS
            // Performance" as a measurement (fl_shm.h §FL_MEASURED_UPSCALER_PARAMS).
            // 0xFF is the defined "a hook ran and could not tell", and it stays
            // the value for the many titles that set the preset out of band
            // through slDLSSSetOptions and never pass sl::DLSSOptions to us.
            //
            // When a title DOES chain it, this carries the vendor's own DLSSMode
            // value -- and QualityFromMode guarantees the byte is never 0, so
            // eOff cannot masquerade as "nobody looked" the way
            // D3D12_RAYTRACING_TIER_NOT_SUPPORTED once could against rtTier.
            rec.upscalerQuality = g_dlssQuality.load(std::memory_order_acquire);

            // PERMANENTLY 0xFF, and this is the true value rather than a
            // placeholder. DLSSOptions::sharpness is
            // [[deprecated("Sharpness is not supported")]] and
            // DLSSOptimalSettings::optimalSharpness is Streamline's RECOMMENDATION,
            // not what the title applied. There is no in-policy route to what was
            // actually used, so "a hook ran and could not tell" is the answer, now
            // and later.
            rec.upscalerSharpness = 0xFFu;

            rec.measuredMask = static_cast<uint16_t>(rec.measuredMask | FL_MEASURED_UPSCALER_PARAMS);
        }
    } else if (ffxLive && ffxSaw) {
        // THE SAME TWO CONDITIONS, ONE VENDOR OVER: an ffx-api leaf is hooked (we were
        // in a position to read a descriptor) AND a dispatch drained THIS PRESENT, so
        // the extent describes this frame and not a resolution the title stopped
        // rendering at. renderSize is read off the UPSCALE or PREPARE descriptor itself.
        const uint64_t ext = g_ffxExtent.load(std::memory_order_acquire);
        if ((ext & kTagValid) != 0u) {
            rec.renderW = static_cast<uint16_t>(ext & 0xFFFFu);
            rec.renderH = static_cast<uint16_t>((ext >> 16) & 0xFFFFu);
            // 0xFF for both, and for this vendor it is the TRUE value rather than a
            // gap: the ffx-api dispatch carries no quality mode at all -- the
            // FfxApiUpscaleQualityMode enum is an input to a QUERY the title makes
            // before creating its context, never to the per-frame dispatch -- and no
            // sharpness travels here either. A preset label derived from the measured
            // ratio is HANDOFF item 7a's owner decision, not this byte's.
            rec.upscalerQuality = 0xFFu;
            rec.upscalerSharpness = 0xFFu;
            rec.measuredMask = static_cast<uint16_t>(rec.measuredMask | FL_MEASURED_UPSCALER_PARAMS);
        }
    }

    // --- Ray Reconstruction, and the fabricated `No` this avoids -------------
    //
    // FL_FEAT_RAY_RECONSTRUCTION_OBSERVED is gated on HAVING SEEN AT LEAST ONE
    // Streamline evaluation this frame, NOT merely on the hook being installed,
    // and the difference is a wrong answer shipped to a user.
    //
    // The consumer (MeasuredFacts.RayReconstructionOf) requires OBSERVED on
    // EVERY record and then returns `No` when no record carries the fact bit. So
    // a writer that sets OBSERVED whenever the hook is live would publish
    // "Ray Reconstruction: No" for an NGX-DIRECT title running DLSS-RR every
    // frame -- our hook sees nothing there, because that title never calls
    // slEvaluateFeature at all. That is a fabricated negative under CLAUDE.md
    // rule 7, produced by the honest-looking choice.
    //
    // Seeing any SL evaluation proves the title routes through Streamline, which
    // is the conjunct that makes our silence about RR meaningful: if RR had run,
    // we would have seen it on the same path.
    if (seen != 0u) {
        uint8_t feat = FL_FEAT_RAY_RECONSTRUCTION_OBSERVED;
        if ((seen & FL_SL_SEEN_DLSS_RR) != 0u) {
            feat = static_cast<uint8_t>(feat | FL_FEAT_RAY_RECONSTRUCTION);
        }

        // THE UNDECODED-ID FACT, and it exists to answer §S30 rather than to
        // decorate the byte. Once frame generation is carried as a count, a present
        // holding kFeatureDLSS_G together with any id that fell to FL_SL_SEEN_OTHER
        // is byte-identical to one that held DLSS-G alone -- so a consumer counting
        // "presents that carried an id we cannot decode" could read ZERO on a title
        // that evaluates one. Its OBSERVED companion rides the same condition as Ray
        // Reconstruction's: seeing ANY Streamline evaluation is what makes our silence
        // about the rest meaningful.
        //
        // THIS COMMENT PREDICTED REFLEX WOULD LIGHT IT, AND FIVE REAL CAPTURES SAY
        // OTHERWISE. It read "Cyberpunk runs Reflex, which is exactly such an id" --
        // and Cyberpunk does run Reflex (`ReflexMode = Enabled` in its own settings),
        // yet UNDECODED is 0 across ~14,000 batches at four different frame-generation
        // settings. Two readings survive and this data cannot separate them: Reflex is
        // not routed through slEvaluateFeature at all (it has its own SL entry points,
        // and NVAPI Reflex is a separate surface entirely -- 17_HOOK_ENGINE §Memory /
        // latency lists it as its own hook class), or the OTHER -> UNDECODED path does
        // not reach the record in the shipped build. The consumer half is unit-tested;
        // the writer half has never been driven with an undecoded id through an
        // injected target, so the bucket is UNPROVEN IN THE POSITIVE DIRECTION.
        //
        // That matters because the zero is load-bearing: it is what excludes a vendored
        // sl::kFeatureDLSS_G constant not matching this title's runtime id, which is the
        // most likely silent failure behind "frame generation is never evaluated". A
        // discrimination that has only ever been observed reading zero is the shape this
        // repo keeps catching -- `a != b` passes when one side is absent. The fixture is
        // cheap: --hold-presenting-fg already drives a chosen sl::Feature, so a mode
        // passing an id outside the four decoded constants would settle it.
        feat = static_cast<uint8_t>(feat | FL_FEAT_SL_UNDECODED_OBSERVED);
        if ((seen & FL_SL_SEEN_OTHER) != 0u) {
            feat = static_cast<uint8_t>(feat | FL_FEAT_SL_UNDECODED);
        }

        // THE RAW SUPER-RESOLUTION FACT, kept apart from the decoded `upscaler` byte.
        //
        // The decode maps kFeatureDLSS_RR to FL_UPSCALER_DLSS, because Ray Reconstruction
        // performs the upscale and carries the scaling-input tag -- measured. That is the
        // right answer for the FIELD, and it makes the field useless as evidence about
        // WHICH ID ARRIVED: a census reading `upscaler == DLSS` reported thousands of
        // arrivals of kFeatureDLSS on a title that evaluates it zero times. This bit does
        // not move when the decode moves.
        if ((seen & (FL_SL_SEEN_DLSS | FL_SL_SEEN_NIS)) != 0u) {
            feat = static_cast<uint8_t>(feat | FL_FEAT_SL_SUPER_RESOLUTION);
        }

        rec.featureFlags = feat;
    }

    // --- Frame generation, COUNTED rather than inferred -----------------------
    //
    // fgEvaluations counts EVALUATIONS, not generated frames, and that is the
    // 2026-08-14 owner ruling rather than a shortcut. slEvaluateFeature(kFeatureDLSS_G)
    // fires once per APPLICATION frame and produces N-1 generated ones, and N lives
    // in sl::DLSSGOptions -- set out of band through slDLSSGSetOptions, which is the
    // route HANDOFF §2b refused on five separate grounds. Counting evaluations gives
    // F_app = Σ evaluations, F_disp = presents and fg_factor = presents / Σ with no
    // multiplier and no vendor header at all. 03_METRICS §Frame Generation is
    // corrected to match in this same commit.
    //
    // GATED ON THE DETOUR, NOT ON HAVING COUNTED SOMETHING, and the difference is a
    // number. A present that drained no DLSS-G evaluation still gets the bits and a
    // zero byte, because a consumer that filtered zero-count presents out would be
    // left with presents == Σ, i.e. fg_factor 1.0 -- CLAUDE.md rule 6's forbidden
    // number, reached by discarding exactly the records that prove otherwise.
    //
    // fgMode is UNKNOWN and never NONE when nothing was counted: this writer hooks
    // Streamline only, so an XeFG or FSR3-FG title generates frames somewhere we are
    // not looking, and NONE is the one FG state that may be aggregated as a negative.
    //
    // IDENTITY AND COUNT ARE TWO DETOURS NOW (2026-09-03). The evaluate detour still
    // names DLSS-G when a kFeatureDLSS_G evaluation drained -- zero on every title
    // measured, kept because a title that does evaluate it would be named correctly.
    // The COUNT comes from slGetNewFrameToken: distinct frame tokens handed to the
    // title since the last present, i.e. application frames, on every Streamline 2
    // title including the ones that evaluate nothing through the other export.
    if (g_upscalerIdentityLive.load(std::memory_order_relaxed) != 0 || ffxLive) {
        const uint32_t evals = fl::slseen::FgEvals(seen);
        // FSR_FG when a generated batch came back through an ffx-api leaf's export this
        // present; DLSS_G when a kFeatureDLSS_G evaluation drained; UNKNOWN otherwise,
        // and never NONE -- `none` is the CONSUMER's verdict from the count
        // (FgWindow.NoneCeiling), because from here "nothing was generated" and
        // "generated somewhere we do not hook" are the same absence.
        uint8_t mode = FL_FG_UNKNOWN;
        if ((ffx & FL_FFX_SEEN_FG_DISPATCH) != 0u) {
            mode = FL_FG_FSR_FG;
        } else if (evals != 0u) {
            mode = FL_FG_DLSS_G;
        } else if ((tagged & FL_SL_TAG_DLSSG_INPUTS) != 0u) {
            // THE TAGS SAY IT, since 2026-09-05. A HUD-less or UI buffer tagged through
            // Streamline since the last present is an input to DLSS Frame Generation and
            // to nothing else the title evaluates through Streamline that generates
            // frames (fl_shm.h §slTagCensus; the DLSS-G programming guide §5.0 requires
            // them). Read off the argument of an API already hooked, on whichever of the
            // three tag routes the title uses -- which is what kFeatureDLSS_G evaluations
            // were supposed to give and gave on no measured title. IDENTITY ONLY: whether
            // frames were generated is still the consumer's verdict from the COUNT, and a
            // count of 1.0 beside this mark prints `none` (inputs fed, nothing generated).
            mode = FL_FG_DLSS_G;
        }
        rec.fgMode = mode;
        rec.measuredMask = static_cast<uint16_t>(rec.measuredMask | FL_MEASURED_FG);
    }
    if (g_frameTokensLive.load(std::memory_order_relaxed) != 0 || ffxLive) {
        const uint32_t tokens = g_frameTokens.exchange(0u, std::memory_order_relaxed);

        // WHICH COUNT, WHEN TWO VENDORS ARE IN THE PROCESS -- and on UE5 they routinely
        // are (Hell Is Us: sl.interposer.dll AND both AMD leaves loaded whatever the menu
        // says). Two latches, each one-way for the life of the process:
        //
        //   Streamline keeps precedence once it has EVER handed out a token
        //   (g_maxFrameKey != 0). Every Streamline title validated on §S31's table is
        //   then byte-identical to before the AMD rows existed, which is what a
        //   validated producer is owed. A process where Streamline is loaded and idle --
        //   the plugin present, the token never requested -- falls through to AMD.
        //
        //   On the AMD side, PREPARE's frameID once a PREPARE has EVER arrived
        //   (g_ffxMaxFrameKey != 0), else the UPSCALE count. Choosing PER PRESENT would
        //   double-count an application frame whose UPSCALE and PREPARE straddle a
        //   present; the latch cannot. A title generating frames from the first frame
        //   counts prepares throughout; a title with frame generation off never issues
        //   one and counts upscales; a title that switches frame generation OFF
        //   mid-session and stops preparing counts nothing from then on, which the
        //   consumer refuses as a data gap rather than reading as 1.0 -- and which
        //   03_METRICS says to segment on in any case.
        const bool     slEver = g_frameTokensLive.load(std::memory_order_relaxed) != 0 &&
                                g_maxFrameKey.load(std::memory_order_relaxed) != 0u;
        const bool     ffxPreparesEver = g_ffxMaxFrameKey.load(std::memory_order_relaxed) != 0u;
        const uint32_t ffxApp = ffxPreparesEver ? ffxPrepares : ffxUpscales;
        rec.fgEvaluations = fl::slseen::SaturateToByte(slEver ? tokens : ffxApp);
        rec.measuredMask = static_cast<uint16_t>(rec.measuredMask | FL_MEASURED_FG_COUNTS);
    }

    // NOT SET, deliberately, and each absence is a producer that does not exist
    // yet rather than an oversight:
    //
    //   FL_MEASURED_PSO      -- psoCreatedThisFrame needs the pipeline-creation
    //                           hooks (17_HOOK_ENGINE §Pipeline). Without them
    //                           03_METRICS' `PSO stutter %` has no input, and
    //                           FlWriterState.rasterPsoCreated -- kept as the
    //                           cheapest pt_confidence proxy -- stays 0.
    //   FL_MEASURED_VRAM     -- IDXGIAdapter3::QueryVideoMemoryInfo, and §H10 has
    //                           not settled whether it is sampled from a thread of
    //                           ours or every N presents.
    //   FL_MEASURED_LATENCY  -- Reflex.
    //   FL_MEASURED_HDR      -- SetColorSpace1, which is unhooked.
    //
    // THIS LIST HAD GONE STALE THREE ENTRIES DEEP, which is worth a line because
    // the whole file's register is comments that outlive their subject.
    // FL_MEASURED_UPSCALER_PARAMS was described here as having "no source in this
    // writer" after Hook_SlSetTag and the inputs walk gave it one; FL_MEASURED_FG
    // and FL_MEASURED_FG_COUNTS were already noted as corrected in place; and
    // FL_MEASURED_RT joins them below in this same commit. A comment naming an
    // absence is a claim, and claims here go stale by not being touched.
    //
    // §H5 case 3 -- whether DLSS-G's GENERATED presents reach the vtable we patch
    // -- is STILL OPEN, and this writer does not assume either answer: it counts
    // what it sees and the ratio is what a real-title run has to move.

    // --- Ray tracing, RECORDED between the previous present and this one -------
    //
    // GATED ON THE DETOURS BEING LIVE, not on having observed anything -- the same
    // rule the frame-generation block above follows, and for the same reason. A
    // frame that recorded no ray-tracing work still gets the bit and a zero
    // rtFlags, because that IS a measurement of that frame; dropping it would leave
    // only the RT-active frames and turn rt_frame_pct into 100% on every title.
    //
    // EITHER family entitles the mask bit, and neither is enough for a `No`.
    // 03_METRICS' negative branch reads RtAsBuild SPECIFICALLY, because a writer
    // with only DispatchRays sees nothing on an inline-RayQuery title -- so the two
    // latches stay separate and the consumer decides, rather than this writer
    // pre-collapsing them.
    //
    // exchange(0) BESIDE g_slSeen's, and for the identical reason: a read without
    // a clear latches, so one dispatch would report ray tracing forever, including
    // after the user turned it off mid-session.
    //
    // maxTraceRecursionDepth, rtStateObjectsCreated, rasterPsoCreated and
    // FL_HOOK_RT_PSO are STILL UNPRODUCED and left at their honest zeros: they need
    // ID3D12Device5::CreateStateObject, which is a separate PR. 03_METRICS'
    // path-tracing heuristic reads two of them, which is why PathTracing stays N/A
    // -- as CLAUDE.md rule 7 requires of it in any case.
    if (g_rtDispatchLive.load(std::memory_order_relaxed) != 0 || g_rtAsBuildLive.load(std::memory_order_relaxed) != 0) {
        rec.rtFlags = static_cast<uint8_t>(g_rtFlags.exchange(0, std::memory_order_acq_rel));
        rec.dispatchRaysVolume = g_rtDispatchVolume.exchange(0, std::memory_order_acq_rel);
        rec.measuredMask = static_cast<uint16_t>(rec.measuredMask | FL_MEASURED_RT);
    }

    // With no RT hook live, rtFlags stays 0 and that is the honest value rather
    // than a claim. Layout v3 flipped the polarity: every bit means "we OBSERVED
    // this", so zero says "no RT evidence seen" and FL_MEASURED_RT is what says
    // whether anyone looked. v2 needed an explicit FL_RT_NOT_MEASURED here because
    // zero meant a measured absence; that bit is retired.

    // Likewise upscaler/fgMode/colorSpace: FL_*_NOT_REPORTED is 0 in v3, so the
    // value-initialisation above already says "nobody looked" instead of
    // "we looked and there was none". Nothing to assign, which is the point --
    // a writer that FORGETS is now honest by construction.

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
            // The slot numbers come from fl_dxgi_vtable.h, which hook-harness
            // also reads. They were inline literals here and duplicated there,
            // so ctest fl_vtable_indices proved a fact about dxgi.dll rather
            // than about this DLL: changing an 8 here left it green
            // (20_OPEN_QUESTIONS §S29(b)).
            ok = MH_CreateHook(vtbl[fl::dxgi::kPresentIndex], reinterpret_cast<void*>(&Hook_Present),
                               reinterpret_cast<void**>(&g_origPresent)) == MH_OK &&
                 MH_CreateHook(vtbl[fl::dxgi::kResizeBuffersIndex], reinterpret_cast<void*>(&Hook_ResizeBuffers),
                               reinterpret_cast<void**>(&g_origResizeBuffers)) == MH_OK &&
                 MH_CreateHook(vtbl[fl::dxgi::kPresent1Index], reinterpret_cast<void*>(&Hook_Present1),
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

    // "Not read" until the first hooked present, so a title that never presents is
    // distinguishable from one whose first present the hook was in for. BEFORE the
    // present hooks go in: a title presenting every 8 ms had its first hooked present
    // between the install and a store placed after it, and the sentinel overwrote the
    // measurement (caught by ctest fl_guard [launch] on the first run).
    {
        std::atomic_ref<uint32_t> before{g_state->dxgiPresentsBeforeHook};
        before.store(0xFFFFFFFFu, std::memory_order_relaxed);
    }

    if (MH_Initialize() != MH_OK || !InstallPresentHooks()) {
        // Hooking failed, so nothing will ever be recorded. Staying at INIT says
        // exactly that; READY would be a claim about a capture side that does not
        // exist, and the Agent's degradation path is what should run instead.
        status.store(FL_STATUS_INIT, std::memory_order_release);
        return 1;
    }

    // hooksInstalledMask GETS ITS FIRST PRODUCER HERE, and it had none anywhere
    // in the tree -- not even this bit, which the present hook has always been
    // entitled to. MeasuredFacts.RayTracingOf already says so in its own comment
    // and reaches N/A on every session because of it.
    //
    // MONOTONIC, via fetch_or and never a store (fl_shm.h §FlHookFamily): hooks
    // install lazily as vendor modules appear, so a bit that could clear would
    // make a session-level check race the frame that read it.
    std::atomic_ref<uint32_t> hooks{g_state->hooksInstalledMask};
    hooks.fetch_or(static_cast<uint32_t>(FL_HOOK_PRESENT), std::memory_order_relaxed);

    // The watchdog's wake, created BEFORE the loader hook that signals it and before
    // the thread that waits on it. Then the LoadLibrary detour -- non-fatal: without
    // it the 1 Hz tick and the host's scan are the backstops, and bit 15 says so.
    g_watchdogWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    InstallLoaderHook();
    PublishLoaderWords();

    status.store(FL_STATUS_READY, std::memory_order_release);

    // AFTER the hooks, deliberately. The watchdog's only job is to take them out
    // again, so starting it earlier would create a window in which it could
    // "stop" a capture side that had not started -- setting g_observing to 0
    // before InstallPresentHooks ran, and leaving a permanently inert Overlay
    // reporting UNHOOKED with nothing ever hooked. Failure to start it is NOT
    // fatal: the present-path checks still work, and an Overlay that reacts only
    // while the game presents is strictly better than none. It is recorded in the
    // fault counter so the Agent can see it rather than inferring it.
    HANDLE watchdog = CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr);
    if (watchdog == nullptr) {
        std::atomic_ref<uint32_t> faults{g_state->faultCount};
        faults.fetch_add(1, std::memory_order_acq_rel);
    } else {
        CloseHandle(watchdog);    // fire and forget; it exits on its own once stopped
    }
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

// The safety stop's local entry point. This comment said "it does nothing yet
// because nothing is hooked; the body lands with the hooks" until 2026-08-05 --
// the body landed in #43 and the comment did not move. It removes the hooks and
// records UNHOOKED, exactly like the control-block path.
//
// It is NOT how the Agent stops a session: that is FlControlBlock::unhookRequested,
// read on the present path and by the watchdog. This export exists because the
// export list is part of what an anti-cheat vendor inspects, and a DLL whose
// exports change shape between builds is harder to identify, not easier.
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
