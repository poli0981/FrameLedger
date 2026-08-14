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

#include <cstdio>
#include <cstring>
#include <fl_dxgi_vtable.h>
#include <fl_hook_inventory.h>
#include <fl_ring.h>
#include <fl_shm.h>
#include <fl_sl_inputs.h>
#include <MinHook.h>
#include <sddl.h>

// Vendored MIT headers, for TYPES ONLY -- never linked, and no vendor function's
// address is ever taken in evaluated code. See
// third_party/streamline/README.md: linking one would make sl.interposer.dll a
// LOAD-TIME dependency of this DLL, and the Overlay would then fail to load in
// every game that ships no Streamline, inside the loader, before any of our code
// runs. We use exactly one thing from here -- the PFun_slEvaluateFeature
// typedef and the kFeature* ids -- and resolve the symbol at runtime.
#include <sl.h>

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

struct SwapChainSlot {
    void*    ptr = nullptr;
    uint32_t id = 0;
    uint16_t outW = 0;
    uint16_t outH = 0;
    uint8_t  api = FL_API_UNKNOWN;
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
enum FlSlSeen : uint32_t {
    FL_SL_SEEN_DLSS = 1u << 0,
    FL_SL_SEEN_NIS = 1u << 1,
    FL_SL_SEEN_DLSS_RR = 1u << 2,
    FL_SL_SEEN_DLSS_G = 1u << 3,
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
void NoteTags(const sl::ResourceTag* tags, uint32_t numTags) noexcept {
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

    for (uint32_t i = 0; i < n; ++i) {
        const sl::ResourceTag& t = tags[i];
        if (t.type != sl::kBufferTypeScalingInputColor) {
            continue;
        }
        // extent is by VALUE and defaults to all-zero, which the vendor header
        // documents as "using the entire resource". That is the honest unknown
        // fl_shm.h already defines for renderW/H, so it is stored as such rather
        // than guessed at from the resource description -- which for D3D12 is
        // itself unset (sl_core_types.h: mandatory only for Vulkan).
        const uint64_t w = t.extent.width;
        const uint64_t h = t.extent.height;
        if (w == 0 || h == 0 || w > 0xFFFFu || h > 0xFFFFu) {
            g_tagExtent.store(0, std::memory_order_release);
            return;
        }
        g_tagExtent.store(w | (h << 16) | kTagValid, std::memory_order_release);
        return;
    }
}

sl::Result STDMETHODCALLTYPE Hook_SlSetTag(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags,
                                           uint32_t numTags, sl::CommandBuffer* cmdBuffer) {
    FL_HOOK_GUARD({
        if (MayObserve()) {
            NoteTags(tags, numTags);
        }
    })
    // ALWAYS exactly once, on every path including the fault path, with every
    // argument forwarded untouched.
    return g_origSlSetTag(viewport, tags, numTags, cmdBuffer);
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
            uint32_t bit = FL_SL_SEEN_OTHER;
            if (feature == sl::kFeatureDLSS) {
                bit = FL_SL_SEEN_DLSS;
            } else if (feature == sl::kFeatureNIS) {
                bit = FL_SL_SEEN_NIS;
            } else if (feature == sl::kFeatureDLSS_RR) {
                bit = FL_SL_SEEN_DLSS_RR;
            } else if (feature == sl::kFeatureDLSS_G) {
                bit = FL_SL_SEEN_DLSS_G;
            }
            g_slSeen.fetch_or(bit, std::memory_order_relaxed);

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

    if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK) {
        return false;
    }

    // STOPPING IS ONE-WAY, and this closes the window that would have broken it.
    // StopObserving can run between the caller's g_observing check and here, and
    // MH_DisableHook(MH_ALL_HOOKS) would then have run BEFORE this hook existed
    // -- leaving a hook patched in after we promised the Agent there were none.
    // Nothing false would be recorded (the body consults MayObserve), but
    // legal/DISCLAIMER.md §2 promises the part inside the game stops, and a hook
    // installed after the stop is not that.
    //
    // FACTORED HERE ON PURPOSE. It used to be inline in the single installer, so
    // a second lazy installer that forgot it would reopen the window silently.
    if (g_observing.load(std::memory_order_acquire) == 0) {
        MH_DisableHook(target);
        return false;
    }

    if (g_state != nullptr) {
        std::atomic_ref<uint32_t> hooks{g_state->hooksInstalledMask};
        hooks.fetch_or(family, std::memory_order_relaxed);
    }
    live.store(1, std::memory_order_release);
    return true;
}

// Bind a row's family bit to the detour that implements it.
//
// A row whose family has no detour installs NOTHING and publishes NOTHING. That
// is the safe direction: an unbound row is a table entry somebody added without
// writing its hook, and hooking it with a neighbour's body is exactly what this
// function exists to stop.
bool InstallByFamily(const wchar_t* module, const char* symbol, uint32_t family) noexcept {
    if (family == static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_IDENTITY)) {
        return InstallRow(module, symbol, family, reinterpret_cast<void*>(&Hook_SlEvaluateFeature),
                          reinterpret_cast<void**>(&g_origSlEvaluateFeature), g_upscalerIdentityLive);
    }
    if (family == static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS)) {
        return InstallRow(module, symbol, family, reinterpret_cast<void*>(&Hook_SlSetTag),
                          reinterpret_cast<void**>(&g_origSlSetTag), g_upscalerParamsLive);
    }
    return false;
}

bool InstallUpscalerHooks() noexcept {
    bool any = false;
#define FL_INSTALL_ROW(mod, sym, family) any = InstallByFamily(mod, sym, static_cast<uint32_t>(family)) || any;
    FL_HOOK_INVENTORY(FL_INSTALL_ROW)
#undef FL_INSTALL_ROW
    return any;
}

// The watchdog. Runs whether or not the game presents, which is the reason it
// exists; see the block comment above for why a thread is acceptable in the
// Overlay and was not in the Vulkan layer.
DWORD WINAPI WatchdogThread(LPVOID) noexcept {
    for (;;) {
        Sleep(kWatchdogIntervalMs);

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

        // Lazy feature-hook installation. AFTER the two stops, never before: a
        // tick that is going to unhook must not install anything first.
        //
        // THIS IS THE VEHICLE, AND IT IS WHY NO LoadLibrary HOOK IS NEEDED FOR
        // P0. 17_HOOK_ENGINE §DLL entry step 5 installs feature hooks "the first
        // time their module appears", and a game that loads Streamline lazily --
        // most of them, since sl.interposer is pulled in at device creation --
        // would be missed by a one-shot check at init. A LoadLibrary hook would
        // catch it too, but it runs under the loader lock while MinHook suspends
        // every thread to patch (§H2), so it must defer the work to a thread
        // anyway. This thread already exists and already wakes once a second, so
        // §S6 stays genuinely separable rather than becoming a prerequisite.
        //
        // It RETRIES until it succeeds and latches only on SUCCESS. An
        // install-attempted flag would give the module exactly one chance, at a
        // moment chosen by our sleep rather than by the game.
        InstallUpscalerHooks();

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

    // --- Upscaler identity, and the three states it must keep apart ----------
    //
    // The counter is CONSUMED here, once per present, so the record describes
    // THIS frame and the next frame starts from nothing. A read-without-clear
    // would latch: one DLSS evaluation would report DLSS forever, including
    // after the user turned it off mid-session, which is precisely the
    // mid-session settings change 03_METRICS §Upscaling segments on.
    const uint32_t seen = g_slSeen.exchange(0, std::memory_order_acq_rel);

    if (g_upscalerIdentityLive.load(std::memory_order_relaxed) != 0) {
        // A hook CAPABLE of answering was live for this frame, so the field may
        // be read. What it says is a separate question, below.
        rec.measuredMask = static_cast<uint16_t>(rec.measuredMask | FL_MEASURED_UPSCALER);

        if ((seen & FL_SL_SEEN_DLSS) != 0u) {
            rec.upscaler = FL_UPSCALER_DLSS;
        } else if ((seen & FL_SL_SEEN_NIS) != 0u) {
            rec.upscaler = FL_UPSCALER_NIS;
        } else {
            // UNKNOWN, AND NEVER `NONE`. FL_UPSCALER_NONE means "a hook ran and
            // there was genuinely no upscaler" -- the one state fl_shm.h allows
            // to be aggregated as a negative. This writer hooks STREAMLINE and
            // nothing else, so an FSR, XeSS or NGX-direct title evaluates its
            // upscaler somewhere we are not looking. Reporting NONE would turn
            // "we do not cover that vendor" into a measured fact about the
            // title. UNKNOWN says the true thing: our coverage is short.
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
        rec.featureFlags = feat;
    }

    // NOT SET, deliberately, and each absence is a producer that does not exist
    // yet rather than an oversight:
    //
    //   FL_MEASURED_UPSCALER_PARAMS -- upscalerQuality, upscalerSharpness and
    //     renderW/H have no source in this writer. 17_HOOK_ENGINE recommends
    //     hooking NVSDK_NGX_Parameter_SetUI for them, and that route is CLOSED:
    //     the NGX SDK is the proprietary RTX SDKs Licence, which
    //     18_GPU_VENDOR_APIS §Checklist step 3 forbids vendoring AND forbids
    //     working around by re-declaring. The in-policy route is Streamline's own
    //     slSetTag extents, and it lands with the PR that consumes sl_dlss.h.
    //     upscalerQuality has no in-band "not measured" sentinel -- 0 is NGX
    //     MaxPerf, a real preset -- so this bit is the ONLY thing standing
    //     between an unhooked writer and "DLSS Performance" as a measurement.
    //
    //   FL_MEASURED_FG / FL_MEASURED_FG_COUNTS -- kFeatureDLSS_G is decoded above
    //     into FL_SL_SEEN_DLSS_G and deliberately goes no further. Frame
    //     generation is item 3, it needs the swapchain question answered
    //     (§H5 case 3), and fgMode/fgEvaluations published from a half-built
    //     counter is how fg_factor becomes 1.0.

    // rtFlags = 0 is now the honest value, not a claim. Layout v3 flipped the
    // polarity: every bit means "we OBSERVED this", so zero says "no RT evidence
    // seen" and FL_MEASURED_RT -- which this writer does not set -- is what says
    // whether anyone looked. v2 needed an explicit FL_RT_NOT_MEASURED here
    // because zero meant a measured absence; that bit is retired.
    rec.rtFlags = 0u;

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
