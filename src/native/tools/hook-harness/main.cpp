// hook-harness — a dummy D3D11/D3D12 app for exercising hook paths with no game
// and no anti-cheat surface at all (17_HOOK_ENGINE §Test harness, 14_TESTING).
//
// Two design choices make this runnable on a GPU-less CI runner, which is what
// 20_OPEN_QUESTIONS §H4 flagged as the obstacle to testing vtable indices in CI:
//
//   1. WARP. D3D_DRIVER_TYPE_WARP is Microsoft's software rasteriser, so no
//      graphics adapter is required.
//   2. CreateSwapChainForComposition. It needs no HWND at all, so there is no
//      dependency on a window station or an interactive session — the usual
//      reason graphics tests fail on hosted runners.
//
// Modes:
//   --probe-vtable   H4: prove vtable slot identity by behaviour
//   --probe-proxy    H5: does patching the real vtable catch a present made
//                        through a wrapper object?
//   --probe-unhook   H7: does our unhook clobber an overlay that hooked the
//                        same slot after us?
//   --probe-cost     NFR-1: per-present cost of a vtable detour (measurement,
//                        not a ctest — a timing threshold on a shared runner
//                        fails for reasons unrelated to the code)
//   --probe-frames   what COUNTS as a frame, against GetLastPresentCount
//   --probe-d3d12    the D3D12 acquisition path: device -> command queue ->
//                        swapchain, headless
//   --probe-d3d12-vtable  ID3D12GraphicsCommandList4's ray-tracing slots, proved
//                        by behaviour. Needs D3D12, not DXR
//   --probe-dxr      HANDOFF item 4's four pre-flight questions: is there a DXR
//                        adapter, do two lists share a vtable, do DIRECT and
//                        COMPUTE share one, does WARP share with hardware
//   --present N      present N frames
//   --hold N         present, then stay alive N seconds. Exists so the harness
//                    can be an INJECTION TARGET: a target that has already
//                    exited is not a cross-process test, it is a silent pass
//   --real           make --present/--hold issue REAL presents. Without it they
//                        issue DXGI_PRESENT_TEST, which submits nothing
//   --plus-ui K      also present K frames on a SECOND swapchain in the same
//                        process (fixture for stream separation)
//   --hold-presenting-overflow N
//                    fill the Overlay's fixed 16-slot swapchain table, then
//                    present for N seconds on one that cannot get a slot. The
//                    only path where the writer KNOWS it has no output size
//                    (20_OPEN_QUESTIONS §S29(g))
//   --probe-sl-seen  the g_slSeen word's encoding: free feature bits, and
//                        saturation going HIGH rather than wrapping low
//   --hold-presenting-fg N
//                    present for N seconds, evaluating kFeatureDLSS_G once per
//                    --presents-per-eval presents and passing the scaling-input
//                    extent as a LOCAL tag through slEvaluateFeature's `inputs`.
//                    The only fixture where presents != evaluations, i.e. the only
//                    one that can tell a counter of evaluations from a counter of
//                    presents; and the only one that reaches the inputs walk's
//                    production call site at all
//   --presents-per-eval K
//                    the FG factor --hold-presenting-fg emits. 1 is the control
//   --sl-tag-for-frame
//                    make --hold-presenting-upscaled tag through slSetTagForFrame
//                    (Streamline 2.8's frame-based API) instead of slSetTag
//   --sl-tag-whole-resource
//                    make --hold-presenting-upscaled tag with a ZERO extent and the
//                    size on the Resource instead (the whole-resource shape)
//   --probe-ffx-resolve
//                    AMD ffx-api: the Overlay's resolver picks each LEAF's own
//                    ffxDispatch with the loader decoy present, and a dispatch
//                    pushed through that decoy reaches exactly one leaf, once
//   --hold-presenting-ffx N
//                    present for N seconds dispatching through the ffx-api the way
//                    an FSR title does: one UPSCALE and one PREPARE (issued twice,
//                    same frameID) per application frame, one FRAMEGENERATION per
//                    frame at K > 1, then K presents (--presents-per-eval K)
//   --ffx-topology 1x|2x|ue|fsr3host|fsr3host+mono
//                    which vendor shape --hold-presenting-ffx stands in: 2x (default)
//                    is the SDK 2.x pair of effect DLLs behind the LOADER (the game
//                    calls the loader; the loader forwards NOT through the leaves'
//                    exports -- measured), 1x the SDK 1.1.x monolith sending the
//                    pre-V2 PREPARE, ue the UE5 shape: the two effect DLLs called
//                    directly, no loader; fsr3host the FSR 3.0 HOST DLL alone
//                    (ffx_fsr3_x64.dll!ffxFsr3ContextDispatchUpscale, no PREPARE,
//                    no frame generation), fsr3host+mono Cyberpunk's shape: the host
//                    upscales while the 1.1.x monolith prepares and generates
//   --ffx-no-prepare drop the PREPARE dispatch: the frame-generation-OFF shape, where
//                    the Overlay must count UPSCALE dispatches instead
//
// This list was incomplete for several modes before 2026-08-15 and is not the
// authority: the strcmp chain in main() is. Kept in step because a reader looking
// for a fixture looks here first.

#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "dxr_raygen_dxil.h"
#include "fl_d3d12_vtable.h"
#include "fl_dxgi_vtable.h"
#include "fl_hook_inventory.h"
#include "fl_rt_accum.h"
#include "fl_sl_inputs.h"
#include "fl_sl_seen.h"
#include "proxy_swapchain.h"

// Vendored MIT Streamline headers, for the feature ids and the ABI of the call
// the upscaled-hold mode makes. Types only, never linked -- the same rule the
// Overlay follows (third_party/streamline/README.md).
#include <sl.h>
// AMD FidelityFX, the same way and for the same reason: descriptor TYPES only, from
// the vendored MIT headers, never linked. Used by --probe-ffx-resolve to build the
// descriptors it pushes through the loader decoy.
#include <ffx_api.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>
// And the FSR 3.0 HOST API (tag fsr3-v3.0.4), for the descriptor the fsr3host
// topologies build and the type of the export they call on the host stub. Inside a
// namespace, as the Overlay includes it: both trees define FfxFrameGenerationConfig.
namespace fsr3host {
#include <FidelityFX/host/ffx_fsr3.h>
}    // namespace fsr3host

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

// Vtable slot expectations from 17_HOOK_ENGINE §Getting vtable addresses.
// These are the numbers this harness exists to prove.
//
// THEY COME FROM THE OVERLAY'S OWN HEADER NOW, and that is the whole point.
// They used to be declared here, textually duplicated from the inline literals
// in dllmain.cpp with nothing binding the two. `ctest fl_vtable_indices` was
// therefore proving a fact about dxgi.dll and not about FrameLedger.Overlay:
// change the Overlay's 8 to a 9 and this test still passed
// (20_OPEN_QUESTIONS §S29(b)). One header, two consumers, and the proof lands
// on the shipped value.
using fl::dxgi::kPresent1Index;
using fl::dxgi::kPresentIndex;
using fl::dxgi::kResizeBuffersIndex;

struct Gfx {
    ID3D11Device*        device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain1*     swapChain1 = nullptr;
    IDXGISwapChain*      swapChain = nullptr;

    ~Gfx() {
        if (swapChain) {
            swapChain->Release();
        }
        if (swapChain1) {
            swapChain1->Release();
        }
        if (context) {
            context->Release();
        }
        if (device) {
            device->Release();
        }
    }
};

bool CreateGfx(Gfx& g) {
    // WARP so this runs without a GPU. BGRA support is required by the
    // composition swapchain path.
    const UINT        flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                   &g.device, &got, &g.context);
    if (FAILED(hr)) {
        std::printf("  D3D11CreateDevice(WARP) failed: 0x%08lX\n", static_cast<unsigned long>(hr));
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(g.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))) {
        return false;
    }
    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(hr)) {
        return false;
    }
    IDXGIFactory2* factory = nullptr;
    hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    adapter->Release();
    if (FAILED(hr)) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 64;
    desc.Height = 64;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    // No HWND — this is the headless-capable path.
    hr = factory->CreateSwapChainForComposition(g.device, &desc, nullptr, &g.swapChain1);
    factory->Release();
    if (FAILED(hr)) {
        std::printf("  CreateSwapChainForComposition failed: 0x%08lX\n", static_cast<unsigned long>(hr));
        return false;
    }
    if (FAILED(g.swapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&g.swapChain)))) {
        return false;
    }
    std::printf("  WARP device + composition swapchain created (feature level 0x%04X)\n", static_cast<unsigned>(got));
    return true;
}

// --- vtable entry swap ------------------------------------------------------
// 17_HOOK_ENGINE prefers patching the vtable ENTRY over inline-patching for COM
// methods. The vtable lives in read-only data, so it needs VirtualProtect.
// Note this patches the SHARED vtable of the concrete class: every instance of
// that class in the process is affected, which is exactly why the dummy-object
// technique works at all — and exactly why §H7's "restore only if unchanged"
// concern matters.
void** VtableOf(void* comObject) {
    return *reinterpret_cast<void***>(comObject);
}

bool PatchSlot(void** vtbl, unsigned index, void* replacement, void** outOriginal) {
    DWORD old = 0;
    if (!VirtualProtect(&vtbl[index], sizeof(void*), PAGE_READWRITE, &old)) {
        return false;
    }
    *outOriginal = vtbl[index];
    vtbl[index] = replacement;
    DWORD ignored = 0;
    VirtualProtect(&vtbl[index], sizeof(void*), old, &ignored);
    return true;
}

// Restore a vtable slot ONLY if it still holds what we put there.
//
// §H7: 17_HOOK_ENGINE §Unhooking says we restore the original entry, and calls
// vtable swapping a "cleaner uninstall" than inline patching. In the
// multi-overlay case — which is the common case on a gamer's machine — that is
// backwards. If RTSS, Discord or the Steam overlay hooked the same slot AFTER
// us, writing the original address back silently removes THEIR hook and
// whatever they were doing stops working, with no error anywhere.
//
// Returning false means someone else owns the slot now. The documented
// behaviour is to leave it alone and go dormant; we already stay loaded, so
// there is nothing to unwind.
bool RestoreSlotIfUnchanged(void** vtbl, unsigned index, void* expected, void* original) {
    if (vtbl[index] != expected) {
        return false;    // someone hooked after us — leave their entry alone
    }
    DWORD old = 0;
    if (!VirtualProtect(&vtbl[index], sizeof(void*), PAGE_READWRITE, &old)) {
        return false;
    }
    // Re-check under the write protection: the window above is small but real.
    const bool stillOurs = vtbl[index] == expected;
    if (stillOurs) {
        vtbl[index] = original;
    }
    DWORD ignored = 0;
    VirtualProtect(&vtbl[index], sizeof(void*), old, &ignored);
    return stillOurs;
}

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);

PresentFn        g_origPresent = nullptr;
ResizeBuffersFn  g_origResize = nullptr;
Present1Fn       g_origPresent1 = nullptr;
std::atomic<int> g_presentHits{0};
std::atomic<int> g_resizeHits{0};
std::atomic<int> g_present1Hits{0};

HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags,
                                       const DXGI_PRESENT_PARAMETERS* params) {
    g_present1Hits.fetch_add(1, std::memory_order_relaxed);
    return g_origPresent1(sc, sync, flags, params);
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    g_presentHits.fetch_add(1, std::memory_order_relaxed);
    return g_origPresent(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt,
                                            UINT flags) {
    g_resizeHits.fetch_add(1, std::memory_order_relaxed);
    return g_origResize(sc, count, w, h, fmt, flags);
}

// ---------------------------------------------------------------------------
// H4 — vtable index verification.
//
// A vtable slot carries no identity, so "check that slot 8 is Present" is not
// something you can ask the runtime. What you CAN do is prove it by behaviour:
// patch slot 8, call Present(), and see whether the detour ran. If it did,
// slot 8 is Present on this runtime — which is the only claim we need.
// ---------------------------------------------------------------------------
bool ProbeH4_VtableIndices(Gfx& g) {
    std::printf("\nH4 — vtable slot identity, proved by behaviour\n");

    void** vtbl = VtableOf(g.swapChain);

    // Structural sanity first: the slots should point inside the module that
    // implements the interface. This does NOT identify the method — it only
    // catches a wildly wrong index or a corrupted vtable.
    HMODULE                  dxgi = GetModuleHandleW(L"dxgi.dll");
    MEMORY_BASIC_INFORMATION mbi{};
    const bool inModule = VirtualQuery(vtbl[kPresentIndex], &mbi, sizeof(mbi)) != 0 && (mbi.Type == MEM_IMAGE);
    std::printf("  slot %u -> %p (in a mapped image: %s, dxgi.dll at %p)\n", kPresentIndex, vtbl[kPresentIndex],
                inModule ? "yes" : "no", reinterpret_cast<void*>(dxgi));

    if (!PatchSlot(vtbl, kPresentIndex, reinterpret_cast<void*>(&HookPresent),
                   reinterpret_cast<void**>(&g_origPresent))) {
        Check(false, "VirtualProtect + patch Present slot");
        return false;
    }
    if (!PatchSlot(vtbl, kResizeBuffersIndex, reinterpret_cast<void*>(&HookResizeBuffers),
                   reinterpret_cast<void**>(&g_origResize))) {
        Check(false, "VirtualProtect + patch ResizeBuffers slot");
        return false;
    }

    const int before = g_presentHits.load(std::memory_order_relaxed);
    g.swapChain->Present(0, DXGI_PRESENT_TEST);
    Check(g_presentHits.load(std::memory_order_relaxed) == before + 1,
          "slot 8 IS IDXGISwapChain::Present (detour ran on Present())");

    const int rbefore = g_resizeHits.load(std::memory_order_relaxed);
    g.swapChain->ResizeBuffers(0, 32, 32, DXGI_FORMAT_UNKNOWN, 0);
    Check(g_resizeHits.load(std::memory_order_relaxed) == rbefore + 1, "slot 13 IS IDXGISwapChain::ResizeBuffers");

    // Present1 lives on IDXGISwapChain1, which shares the same concrete object
    // and therefore the same vtable, extended past the base interface.
    void** vtbl1 = VtableOf(g.swapChain1);
    Check(vtbl1 == vtbl, "IDXGISwapChain1 and IDXGISwapChain share one vtable (same concrete object)");

    // Prove slot 22 the same way as the others. Printing its address would
    // prove nothing — an address says where, not which method.
    if (!PatchSlot(vtbl1, kPresent1Index, reinterpret_cast<void*>(&HookPresent1),
                   reinterpret_cast<void**>(&g_origPresent1))) {
        Check(false, "patch Present1 slot");
        return false;
    }
    const int               p1before = g_present1Hits.load(std::memory_order_relaxed);
    DXGI_PRESENT_PARAMETERS params{};
    g.swapChain1->Present1(0, DXGI_PRESENT_TEST, &params);
    Check(g_present1Hits.load(std::memory_order_relaxed) == p1before + 1, "slot 22 IS IDXGISwapChain1::Present1");

    // Restore before leaving so later probes see a clean object, then PROVE the
    // restore. Asserting Check(true) here would report "slots restored" even if
    // VirtualProtect had failed and the object were left detoured — the next
    // probe would then measure our own hook and call it a finding.
    void* dummy = nullptr;
    PatchSlot(vtbl, kPresentIndex, reinterpret_cast<void*>(g_origPresent), &dummy);
    PatchSlot(vtbl, kResizeBuffersIndex, reinterpret_cast<void*>(g_origResize), &dummy);
    PatchSlot(vtbl1, kPresent1Index, reinterpret_cast<void*>(g_origPresent1), &dummy);
    Check(vtbl[kPresentIndex] == reinterpret_cast<void*>(g_origPresent) &&
              vtbl[kResizeBuffersIndex] == reinterpret_cast<void*>(g_origResize) &&
              vtbl1[kPresent1Index] == reinterpret_cast<void*>(g_origPresent1),
          "all three slots hold their original entries again");
    return true;
}

// ---------------------------------------------------------------------------
// H5 — does patching the real vtable catch a present made through a proxy?
//
// Streamline, ReShade and similar hand the game their own IDXGISwapChain
// implementation. Our dummy-object probe patches the vtable of a REAL DXGI
// swapchain. The proxy has its own vtable that we never touch — so the naive
// expectation is that we miss the present entirely.
// ---------------------------------------------------------------------------
bool ProbeH5_ProxySwapChain(Gfx& g) {
    std::printf("\nH5 — present issued through a proxy swapchain\n");

    auto* proxy = new fl::harness::ProxySwapChain(g.swapChain);

    void** realVtbl = VtableOf(g.swapChain);
    void** proxyVtbl = VtableOf(proxy);
    std::printf("  real vtable  %p\n  proxy vtable %p  (%s)\n", reinterpret_cast<void*>(realVtbl),
                reinterpret_cast<void*>(proxyVtbl), realVtbl == proxyVtbl ? "SAME" : "DIFFERENT — as expected");
    Check(realVtbl != proxyVtbl, "proxy has its own vtable (we never patched it)");

    if (!PatchSlot(realVtbl, kPresentIndex, reinterpret_cast<void*>(&HookPresent),
                   reinterpret_cast<void**>(&g_origPresent))) {
        Check(false, "patch real Present slot");
        proxy->Release();
        return false;
    }

    const int before = g_presentHits.load(std::memory_order_relaxed);
    proxy->Present(0, DXGI_PRESENT_TEST);
    const int after = g_presentHits.load(std::memory_order_relaxed);

    std::printf("  proxy saw %u present(s); our hook on the REAL vtable saw %d\n",
                proxy->proxyPresentCount.load(std::memory_order_relaxed), after - before);

    if (after > before) {
        std::printf("  => the proxy forwards via the real interface, so the hook still fires\n");
    } else {
        std::printf("  => the present did NOT reach our hook: proxies defeat real-vtable patching\n");
    }

    // This was an open observation once. It is now a RECORDED FINDING
    // (docs/spike-notes.md §H5, CHANGELOG "H2/H5 partly answered"), so this
    // probe's job changed from "report whatever happens" to "keep it true".
    //
    // It previously read Check(true, "observation recorded"), which cannot fail
    // — ctest fl_proxy_swapchain was green by construction and would have
    // stayed green if a forwarding proxy ever stopped reaching our hook. That
    // is the same shape as the EnumDeviceDrivers fail-open: a check that
    // succeeds while telling you nothing.
    Check(after > before, "a forwarding proxy's Present reaches our hook on the REAL vtable");

    void* dummy = nullptr;
    PatchSlot(realVtbl, kPresentIndex, reinterpret_cast<void*>(g_origPresent), &dummy);
    proxy->Release();
    return after > before;
}

// ---------------------------------------------------------------------------
// H7 — does our unhook clobber an overlay that hooked after us?
//
// The dev machine already runs RTSS, OBS, Steam Overlay, EOS and GOG Galaxy
// (spike-notes.md §Environment), so this is the DEFAULT state of a gamer's
// machine, not an exotic one. It is simulated here rather than depending on
// RTSS being installed, so it runs on CI too — and so the failure is
// deterministic instead of depending on who hooked first.
// ---------------------------------------------------------------------------
PresentFn        g_foreignOrig = nullptr;
std::atomic<int> g_foreignHits{0};

HRESULT STDMETHODCALLTYPE ForeignHookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    g_foreignHits.fetch_add(1, std::memory_order_relaxed);
    return g_foreignOrig(sc, sync, flags);
}

bool ProbeH7_UnhookPreservesForeign(Gfx& g) {
    std::printf("\nH7 - unhooking must not clobber an overlay that hooked after us\n");

    void** vtbl = VtableOf(g.swapChain);
    void*  pristine = vtbl[kPresentIndex];

    // 1. We hook first.
    if (!PatchSlot(vtbl, kPresentIndex, reinterpret_cast<void*>(&HookPresent),
                   reinterpret_cast<void**>(&g_origPresent))) {
        Check(false, "install our hook");
        return false;
    }

    // 2. Someone else hooks after us, chaining through our detour exactly as a
    //    real overlay would.
    if (!PatchSlot(vtbl, kPresentIndex, reinterpret_cast<void*>(&ForeignHookPresent),
                   reinterpret_cast<void**>(&g_foreignOrig))) {
        Check(false, "install the foreign hook");
        return false;
    }
    Check(reinterpret_cast<void*>(g_foreignOrig) == reinterpret_cast<void*>(&HookPresent),
          "the foreign hook chained through ours (it saved our detour as its original)");

    // 3. We unhook. Compare-and-restore must decline.
    const bool restored = RestoreSlotIfUnchanged(vtbl, kPresentIndex, reinterpret_cast<void*>(&HookPresent),
                                                 reinterpret_cast<void*>(g_origPresent));
    Check(!restored, "our unhook DECLINED to restore, because the slot is no longer ours");
    Check(vtbl[kPresentIndex] == reinterpret_cast<void*>(&ForeignHookPresent),
          "the foreign hook is still installed - we did not silently remove it");

    // 4. And it still works.
    const int before = g_foreignHits.load(std::memory_order_relaxed);
    g.swapChain->Present(0, DXGI_PRESENT_TEST);
    Check(g_foreignHits.load(std::memory_order_relaxed) == before + 1, "the foreign hook still fires after our unhook");

    // 5. The other half of the contract: when the slot IS still ours, we DO
    //    restore. A compare-and-restore that never restores is not a fix.
    void* dummy = nullptr;
    PatchSlot(vtbl, kPresentIndex, reinterpret_cast<void*>(g_origPresent), &dummy);    // undo the foreign hook
    if (!PatchSlot(vtbl, kPresentIndex, reinterpret_cast<void*>(&HookPresent),
                   reinterpret_cast<void**>(&g_origPresent))) {
        Check(false, "re-install our hook");
        return false;
    }
    const bool restored2 = RestoreSlotIfUnchanged(vtbl, kPresentIndex, reinterpret_cast<void*>(&HookPresent),
                                                  reinterpret_cast<void*>(g_origPresent));
    Check(restored2, "with the slot untouched, our unhook DOES restore");
    Check(vtbl[kPresentIndex] == pristine, "the slot holds the pristine entry again");
    return true;
}

// ---------------------------------------------------------------------------
// Per-present cost (NFR-1, 14_TESTING §Hook overhead item 1: <= 1 us).
// The remaining open bullet under spike-notes.md §3. Needs no game.
// ---------------------------------------------------------------------------
double MedianOf(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

double TimePresents(Gfx& g, int iterations) {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    LARGE_INTEGER a{};
    QueryPerformanceCounter(&a);
    for (int i = 0; i < iterations; ++i) {
        g.swapChain->Present(0, DXGI_PRESENT_TEST);
    }
    LARGE_INTEGER b{};
    QueryPerformanceCounter(&b);
    return static_cast<double>(b.QuadPart - a.QuadPart) * 1e9 / static_cast<double>(freq.QuadPart) / iterations;
}

bool ProbeCost_PerPresent(Gfx& g) {
    std::printf("\nPer-present hook cost (NFR-1: <= 1 us)\n");

    constexpr int kIterations = 20000;
    constexpr int kRuns = 5;

    // Interleave hooked and unhooked runs and take medians. Three-of-each in a
    // block would attribute any thermal or scheduler drift during the run to
    // the hook.
    std::vector<double> cold, hot;
    void**              vtbl = VtableOf(g.swapChain);
    for (int r = 0; r < kRuns; ++r) {
        cold.push_back(TimePresents(g, kIterations));

        if (!PatchSlot(vtbl, kPresentIndex, reinterpret_cast<void*>(&HookPresent),
                       reinterpret_cast<void**>(&g_origPresent))) {
            Check(false, "patch Present slot for the hooked run");
            return false;
        }
        hot.push_back(TimePresents(g, kIterations));
        void* dummy = nullptr;
        PatchSlot(vtbl, kPresentIndex, reinterpret_cast<void*>(g_origPresent), &dummy);
    }

    const double c = MedianOf(cold);
    const double h = MedianOf(hot);
    const double delta = h - c;
    std::printf("  %d presents x %d runs, interleaved\n", kIterations, kRuns);
    std::printf("  unhooked median %.1f ns/present\n", c);
    std::printf("  hooked   median %.1f ns/present\n", h);
    std::printf("  delta           %.1f ns/present  (budget 1000 ns)\n", delta);

    // The detour here is an atomic increment plus a call through the saved
    // pointer -- i.e. the floor for ANY vtable hook, not the real Overlay's
    // cost. It bounds the mechanism, not the product. The real budget is
    // 14_TESTING item 2, on a real game, and that stays P1.
    Check(delta < 1000.0, "vtable detour costs under 1 us per present");
    std::printf("       NOTE: this is the empty-detour floor. The Overlay's real per-present\n");
    std::printf("             cost is 14_TESTING item 2, on a real game, and is not this number.\n");
    return true;
}

// DXGI_PRESENT_TEST DOES NOT PRESENT. It is the occlusion probe: it tests
// whether a present WOULD succeed and returns without submitting a frame.
// Measured on this WARP + composition swapchain — 10,000 test-presents leave
// GetLastPresentCount at 0; ten real ones take it to 10.
//
// Every present in this harness used to carry that flag, which made "N presents
// -> N records" a criterion only a writer that counts NON-FRAMES could satisfy.
// The flag is kept, as a separately named mode, because the occlusion path is a
// real thing a title does — the documented recovery from DXGI_STATUS_OCCLUDED is
// a tight Present(0, DXGI_PRESENT_TEST) loop, which is what an alt-tabbed game
// runs, and the Overlay has to be measured against it deliberately rather than
// by accident.
int PresentLoop(Gfx& g, int frames, bool real) {
    const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
    for (int i = 0; i < frames; ++i) {
        g.swapChain->Present(0, flags);
    }
    std::printf("  presented %d frame(s) [%s]\n", frames, real ? "REAL" : "DXGI_PRESENT_TEST — not frames");
    return 0;
}

// A second swapchain on the same device, so a fixture can produce two present
// streams in one process. Patching a vtable slot patches the shared dxgi.dll
// class vtable, so a hook catches BOTH — and FlFrameRecord has no discriminator
// today, which is how "N presents -> N records" can pass while the frame count
// is inflated. This is the fixture that case needs; nothing consumes it yet.
IDXGISwapChain* CreateSecondSwapChain(Gfx& g) {
    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(g.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))) {
        return nullptr;
    }
    IDXGIAdapter* adapter = nullptr;
    HRESULT       hr = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(hr)) {
        return nullptr;
    }
    IDXGIFactory2* factory = nullptr;
    hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    adapter->Release();
    if (FAILED(hr)) {
        return nullptr;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 32;
    desc.Height = 32;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    IDXGISwapChain1* sc1 = nullptr;
    hr = factory->CreateSwapChainForComposition(g.device, &desc, nullptr, &sc1);
    factory->Release();
    if (FAILED(hr)) {
        return nullptr;
    }
    IDXGISwapChain* sc = nullptr;
    if (FAILED(sc1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&sc)))) {
        sc1->Release();
        return nullptr;
    }
    sc1->Release();
    return sc;
}

// D3D12, which this harness did not have at all.
//
// 545 lines and zero D3D12 references, while CLAUDE.md called the harness a
// "D3D11/D3D12/Vulkan" app and 17_HOOK_ENGINE §DLL entry gates hook installation
// on GetModuleHandleW(L"d3d12.dll") — never non-null here. So the acquisition
// path the Overlay will run inside a real game had no fixture, and the P0-exit
// title is D3D12.
//
// The swapchain comes from the COMMAND QUEUE, not the device: that asymmetry is
// the whole of the D3D12 acquisition path in 17_HOOK_ENGINE §Getting vtable
// addresses, and it is what a D3D11-only harness cannot exercise.
bool ProbeD3D12Acquisition() {
    std::printf("\n[d3d12] device + command queue + swapchain, headless\n");

    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) {
        Check(false, "CreateDXGIFactory1(IDXGIFactory4)");
        return false;
    }
    IDXGIAdapter* warp = nullptr;
    HRESULT       hr = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&warp));
    if (FAILED(hr)) {
        factory->Release();
        Check(false, "EnumWarpAdapter — no software adapter, so this cannot run without a GPU");
        return false;
    }

    ID3D12Device* dev = nullptr;
    hr = D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&dev));
    warp->Release();
    if (FAILED(hr)) {
        factory->Release();
        Check(false, "D3D12CreateDevice(WARP)");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    hr = dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue));
    if (FAILED(hr)) {
        dev->Release();
        factory->Release();
        Check(false, "CreateCommandQueue(DIRECT)");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 64;
    desc.Height = 64;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    IDXGISwapChain1* sc = nullptr;
    hr = factory->CreateSwapChainForComposition(queue, &desc, nullptr, &sc);
    factory->Release();
    if (FAILED(hr)) {
        queue->Release();
        dev->Release();
        Check(false, "CreateSwapChainForComposition(command queue) — the D3D12 acquisition path");
        return false;
    }
    Check(sc != nullptr, "a D3D12 swapchain exists, built from the command queue");

    // The point of having it: prove D3D12 presents move the same counter, so a
    // D3D12 fixture can assert frame identity exactly as the D3D11 one does.
    UINT before = 0;
    sc->GetLastPresentCount(&before);
    for (int i = 0; i < 11; ++i) {
        sc->Present(0, 0);
    }
    UINT after = 0;
    sc->GetLastPresentCount(&after);
    const bool counted = (after - before) == 11u;
    Check(counted, "11 real D3D12 presents move GetLastPresentCount by 11");
    std::printf("    counter moved by %u\n", after - before);

    // And that the module gate 17_HOOK_ENGINE §DLL entry step 3 keys on is now
    // satisfiable in this process, which is the thing a D3D11-only harness could
    // never make true.
    const bool loaded = GetModuleHandleW(L"d3d12.dll") != nullptr;
    Check(loaded, "d3d12.dll is loaded, so InitThread's D3D12 branch is reachable here");

    sc->Release();
    queue->Release();
    dev->Release();
    return counted && loaded;
}

// ---------------------------------------------------------------------------
// D3D12 command-list vtable slots, and the DXR pre-flight (docs/HANDOFF.md item 4).
//
// Everything the ray-tracing hooks rest on is an assumption until it is measured,
// and this project's record on vtable assumptions is `ctest fl_vtable_indices`
// proving a fact about dxgi.dll instead of about FrameLedger.Overlay (§S29(b)).
// So these run BEFORE the hooks are written, and their acceptance is four printed
// answers rather than four passing asserts.
// ---------------------------------------------------------------------------

using fl::d3d12::kBuildRaytracingAccelerationStructureIndex;
using fl::d3d12::kDispatchRaysIndex;

std::atomic<int> g_dispatchRaysHits{0};
std::atomic<int> g_asBuildHits{0};

// THESE DELIBERATELY DO NOT FORWARD, and that is a safety property rather than a
// shortcut. Forwarding would call the real method with a zeroed descriptor on a
// list with no ray-tracing state object set, which is exactly what happens if a
// slot index is WRONG -- i.e. in the one case this fixture exists to detect. The
// detour swallowing the call bounds the blast radius of its own failure, and the
// list is never passed to ExecuteCommandLists, so nothing reaches the GPU either
// way. Proving the index needs only that our stub ran.
void STDMETHODCALLTYPE StubDispatchRays(ID3D12GraphicsCommandList4*, const D3D12_DISPATCH_RAYS_DESC*) {
    g_dispatchRaysHits.fetch_add(1, std::memory_order_relaxed);
}

void STDMETHODCALLTYPE StubBuildAs(ID3D12GraphicsCommandList4*,
                                   const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC*, UINT,
                                   const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC*) {
    g_asBuildHits.fetch_add(1, std::memory_order_relaxed);
}

void ReleaseIf(IUnknown* p) {
    if (p != nullptr) {
        p->Release();
    }
}

// Which module a code address belongs to. An address alone says where, not whose.
const wchar_t* ModuleOf(void* addr) {
    static wchar_t path[MAX_PATH]{};
    HMODULE        mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &mod) ||
        GetModuleFileNameW(mod, path, MAX_PATH) == 0) {
        return L"<not in a module>";
    }
    const wchar_t* slash = wcsrchr(path, L'\\');
    return slash != nullptr ? slash + 1 : path;
}

struct ListBits {
    ID3D12CommandAllocator*     alloc = nullptr;
    ID3D12GraphicsCommandList*  list = nullptr;
    ID3D12GraphicsCommandList4* list4 = nullptr;

    void Release() {
        if (list4 != nullptr) {
            list4->Release();
            list4 = nullptr;
        }
        if (list != nullptr) {
            list->Release();
            list = nullptr;
        }
        if (alloc != nullptr) {
            alloc->Release();
            alloc = nullptr;
        }
    }
};

// A command list of `type`, upgraded to ID3D12GraphicsCommandList4.
//
// The QI is the part that can legitimately fail: ID3D12GraphicsCommandList4 is a
// RUNTIME interface (Windows 10 1809+), not a device capability, so it succeeds on
// an adapter with no ray-tracing support at all -- which is why the slot probe and
// the capability probe are separate questions.
bool MakeList(ID3D12Device* dev, D3D12_COMMAND_LIST_TYPE type, ListBits& out, bool reset = true) {
    if (FAILED(dev->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator),
                                           reinterpret_cast<void**>(&out.alloc)))) {
        return false;
    }
    if (FAILED(dev->CreateCommandList(0, type, out.alloc, nullptr, __uuidof(ID3D12GraphicsCommandList),
                                      reinterpret_cast<void**>(&out.list)))) {
        return false;
    }
    if (FAILED(out.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), reinterpret_cast<void**>(&out.list4)))) {
        return false;
    }
    // RESET BY DEFAULT, because that is the state a GAME'S command list is in and
    // the vtable is NOT the same one -- see Q5. A probe that reads an unreset list
    // measures a vtable no title records through.
    if (!reset) {
        return true;
    }
    return SUCCEEDED(out.list4->Close()) && SUCCEEDED(out.alloc->Reset()) &&
           SUCCEEDED(out.list4->Reset(out.alloc, nullptr));
}

// A device on `adapter` (null = the default adapter), with its raytracing tier.
//
// A FAILED CheckFeatureSupport reports tier 0, which is D3D12's own
// NOT_SUPPORTED -- fine HERE, because this is a probe and prints the tier it read.
// The Overlay must NOT do that: fl_shm.h's FlRtTier keeps "could not ask" and
// "asked, and this device cannot" apart precisely because the vendor enum's zero
// collides with our own.
ID3D12Device* MakeDevice(IDXGIAdapter* adapter, unsigned& tierOut) {
    ID3D12Device* dev = nullptr;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                 reinterpret_cast<void**>(&dev)))) {
        return nullptr;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    tierOut = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)))
                  ? static_cast<unsigned>(options.RaytracingTier)
                  : 0u;
    return dev;
}

// The first adapter that reports DXR, WARP included, and it PRINTS which one.
//
// Not "always WARP", and the difference is not academic: this machine's WARP
// D3D12 path was broken for a fortnight by a Windows Insider build, which took
// two ctests down with it and told the reader nothing about the code
// (docs/HANDOFF.md §Traps). A fixture that can only ever run on CI is a fixture
// that cannot be developed against. Whichever adapter answers, the choice is on
// stdout so a result is never read against the wrong hardware.
ID3D12Device* BestDxrDevice(unsigned& tierOut, bool& isWarpOut) {
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) {
        return nullptr;
    }

    ID3D12Device* chosen = nullptr;
    tierOut = 0;
    isWarpOut = false;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        unsigned      tier = 0;
        ID3D12Device* dev = MakeDevice(adapter, tier);
        std::printf("  adapter %u: %-40ls d3d12=%s raytracingTier=%u\n", i, desc.Description,
                    dev != nullptr ? "yes" : "no ", tier);
        if (dev != nullptr && chosen == nullptr && tier >= static_cast<unsigned>(D3D12_RAYTRACING_TIER_1_0)) {
            chosen = dev;
            tierOut = tier;
            isWarpOut = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        } else if (dev != nullptr) {
            dev->Release();
        }
        adapter->Release();
    }

    IDXGIAdapter* warp = nullptr;
    if (SUCCEEDED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&warp)))) {
        unsigned      tier = 0;
        ID3D12Device* dev = MakeDevice(warp, tier);
        std::printf("  adapter W: %-40s d3d12=%s raytracingTier=%u\n", "WARP (software)",
                    dev != nullptr ? "yes" : "no ", tier);
        if (dev != nullptr && chosen == nullptr && tier >= static_cast<unsigned>(D3D12_RAYTRACING_TIER_1_0)) {
            chosen = dev;
            tierOut = tier;
            isWarpOut = true;
        } else if (dev != nullptr) {
            dev->Release();
        }
        warp->Release();
    }

    factory->Release();
    return chosen;
}

// Any D3D12 device at all, and it prints nothing.
//
// The slot probe does not need DXR: ID3D12GraphicsCommandList4 is a runtime
// interface, so its vtable has the ray-tracing slots on a device that cannot
// trace a ray. Separating this from BestDxrDevice is what keeps the slot fixture
// runnable on a machine with no ray-tracing hardware, and keeps the adapter table
// from being printed twice in one run.
ID3D12Device* AnyD3D12Device(bool& isWarpOut) {
    unsigned tier = 0;
    isWarpOut = false;
    ID3D12Device* dev = MakeDevice(nullptr, tier);
    if (dev != nullptr) {
        return dev;
    }

    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) {
        return nullptr;
    }
    IDXGIAdapter* warpAdapter = nullptr;
    if (FAILED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&warpAdapter)))) {
        factory->Release();
        return nullptr;
    }
    dev = MakeDevice(warpAdapter, tier);
    isWarpOut = dev != nullptr;
    warpAdapter->Release();
    factory->Release();
    return dev;
}

// Slot identity on ONE list's vtable, proved the same way §H4 proves the DXGI
// ones: patch, call the method BY NAME through the interface, and see whether the
// detour ran. An address printed beside a slot number says where, not which.
bool ProveSlotsOn(ListBits& bits, const char* listType) {
    void**                   vtbl = VtableOf(bits.list4);
    MEMORY_BASIC_INFORMATION mbi{};
    const bool inImage = VirtualQuery(vtbl[kDispatchRaysIndex], &mbi, sizeof(mbi)) != 0 && (mbi.Type == MEM_IMAGE);
    std::printf("  %s vtable %p: slot %u -> %p, slot %u -> %p (in a mapped image: %s)\n", listType,
                reinterpret_cast<void*>(vtbl), kBuildRaytracingAccelerationStructureIndex,
                vtbl[kBuildRaytracingAccelerationStructureIndex], kDispatchRaysIndex, vtbl[kDispatchRaysIndex],
                inImage ? "yes" : "no");

    void* origDispatch = nullptr;
    void* origBuild = nullptr;
    if (!PatchSlot(vtbl, kDispatchRaysIndex, reinterpret_cast<void*>(&StubDispatchRays), &origDispatch) ||
        !PatchSlot(vtbl, kBuildRaytracingAccelerationStructureIndex, reinterpret_cast<void*>(&StubBuildAs),
                   &origBuild)) {
        Check(false, "VirtualProtect + patch both command-list slots");
        return false;
    }

    const int                dispatchBefore = g_dispatchRaysHits.load(std::memory_order_relaxed);
    D3D12_DISPATCH_RAYS_DESC rays{};
    bits.list4->DispatchRays(&rays);
    const bool dispatched = g_dispatchRaysHits.load(std::memory_order_relaxed) == dispatchBefore + 1;

    const int                                          buildBefore = g_asBuildHits.load(std::memory_order_relaxed);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    bits.list4->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const bool built = g_asBuildHits.load(std::memory_order_relaxed) == buildBefore + 1;

    // Restore FIRST, then report. A failed Check must not leave the SHARED class
    // vtable detoured: every command list of that type in the process would keep
    // our stub, and the next probe would measure it and call it a result.
    void* dummy = nullptr;
    PatchSlot(vtbl, kDispatchRaysIndex, origDispatch, &dummy);
    PatchSlot(vtbl, kBuildRaytracingAccelerationStructureIndex, origBuild, &dummy);

    std::printf("  on a %s list:\n", listType);
    Check(dispatched, "    slot 76 IS ID3D12GraphicsCommandList4::DispatchRays");
    Check(built, "    slot 72 IS ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure");
    Check(vtbl[kDispatchRaysIndex] == origDispatch && vtbl[kBuildRaytracingAccelerationStructureIndex] == origBuild,
          "    both slots hold their original entries again");
    return dispatched && built;
}

// BOTH LIST TYPES, and that is not belt-and-braces.
//
// --probe-dxr MEASURED that DIRECT and COMPUTE command lists do NOT share a
// vtable, so proving the indices on a DIRECT list establishes nothing about the
// COMPUTE one by observation. The COM ABI says the two must agree -- same
// interface, same declaration order -- and "the ABI says so" is exactly the
// confidence this fixture exists to replace with a measurement. It costs one more
// command list, and item 4's hook has to patch both vtables anyway.
bool ProbeD3D12VtableIndices() {
    std::printf("\n[d3d12-vtable] ID3D12GraphicsCommandList4 slot identity, proved by behaviour\n");

    bool          warp = false;
    ID3D12Device* dev = AnyD3D12Device(warp);
    if (dev == nullptr) {
        Check(false, "a D3D12 device on any adapter — nothing here can run without one");
        return false;
    }
    std::printf("  device: %s adapter\n", warp ? "WARP (software)" : "hardware");

    bool     ok = true;
    ListBits direct{};
    if (MakeList(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, direct)) {
        ok = ProveSlotsOn(direct, "DIRECT") && ok;
    } else {
        Check(false, "a DIRECT command list upgraded to ID3D12GraphicsCommandList4");
        ok = false;
    }
    direct.Release();

    ListBits compute{};
    if (MakeList(dev, D3D12_COMMAND_LIST_TYPE_COMPUTE, compute)) {
        ok = ProveSlotsOn(compute, "COMPUTE") && ok;
    } else {
        Check(false, "a COMPUTE command list upgraded to ID3D12GraphicsCommandList4");
        ok = false;
    }
    compute.Release();
    dev->Release();
    return ok;
}

// The two functions a ray-tracing hook has to reach, off one list.
struct RtTargets {
    void* build = nullptr;
    void* dispatch = nullptr;
};

RtTargets TargetsOf(const ListBits& bits) {
    void* const* v = *reinterpret_cast<void* const* const*>(bits.list4);
    return {v[kBuildRaytracingAccelerationStructureIndex], v[kDispatchRaysIndex]};
}

// Q4: does a command list from one KIND of adapter resolve to the same FUNCTIONS
// as one from the other? It decides whether the Overlay could take its targets off
// a throwaway device instead of the game's own.
void ProbeDxrCrossDevice(bool chosenIsWarp, const ListBits& chosenList) {
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) {
        std::printf("  Q4 UNANSWERED: no DXGI factory.\n");
        return;
    }

    unsigned      tier = 0;
    ID3D12Device* other = nullptr;
    if (chosenIsWarp) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; other == nullptr && factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
                other = MakeDevice(adapter, tier);
            }
            adapter->Release();
        }
    } else {
        IDXGIAdapter* warpAdapter = nullptr;
        if (SUCCEEDED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&warpAdapter)))) {
            other = MakeDevice(warpAdapter, tier);
            warpAdapter->Release();
        }
    }
    factory->Release();

    if (other == nullptr) {
        // A HOSTED CI RUNNER WITH NO GPU IS IN EXACTLY THIS STATE, and it must not
        // read as an answer. Nothing was compared.
        std::printf("  Q4 UNANSWERED: no %s adapter on this machine to compare against.\n",
                    chosenIsWarp ? "hardware" : "software");
        return;
    }

    ListBits otherList{};
    if (MakeList(other, D3D12_COMMAND_LIST_TYPE_DIRECT, otherList)) {
        const RtTargets mine = TargetsOf(chosenList);
        const RtTargets theirs = TargetsOf(otherList);
        const bool      same = mine.build == theirs.build && mine.dispatch == theirs.dispatch;
        std::printf("  Q4 %s: a %s device and a %s device resolve to %s functions\n", same ? "SHARED" : "SEPARATE",
                    chosenIsWarp ? "WARP" : "hardware", chosenIsWarp ? "hardware" : "WARP",
                    same ? "the SAME" : "DIFFERENT");
        std::printf("      slot 72 %ls / %ls, slot 76 %ls / %ls\n", ModuleOf(mine.build), ModuleOf(theirs.build),
                    ModuleOf(mine.dispatch), ModuleOf(theirs.dispatch));
        std::printf("      A throwaway-device acquisition would therefore %s.\n",
                    same ? "have worked" : "SILENTLY MISS EVERY CALL — which is why the shipped hook does not use one");
    } else {
        std::printf("  Q4 UNANSWERED: could not build a command list on the other device.\n");
    }
    otherList.Release();
    other->Release();
}

// Q5: does Reset() change which code the ray-tracing slots point at?
//
// THIS QUESTION COST THE MOST AND WAS NOT ON THE ORIGINAL LIST. The first version
// of the Overlay's installer read its vtable off a freshly created throwaway list,
// which is what every "create a dummy object, read the vtable" recipe says to do.
// The hook installed, published its family bit, and NEVER FIRED -- the injected
// fixture recorded `withDispatch = 0` beside `hooks = RT_DISPATCH | RT_AS_BUILD`,
// a mask bit with nothing behind it, which is the exact honesty failure the whole
// entitlement machinery exists to prevent.
//
// The reason, measured on an RTX 5080: the FIRST Reset swaps a command list's
// class vtable for a PER-OBJECT one in which the vendor driver has taken methods
// over. DispatchRays moves from D3D12Core.dll into nvwgf2umx.dll;
// BuildRaytracingAccelerationStructure stays where it was, which is why one hook
// worked and the other did not, and why the failure looked like a bug in the
// dispatch detour rather than in the acquisition.
//
// Every game resets its command lists every frame. So the fix is one call -- put
// the throwaway through the same lifecycle -- and this probe is what keeps it
// honest: it prints the module on each side of the Reset, so a runtime that stops
// swapping, or starts swapping the other slot, says so instead of being discovered
// by a silent zero on a real title.
void ProbeDxrResetSwap(ID3D12Device* dev) {
    ListBits fresh{};
    ListBits reset{};
    if (!MakeList(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, fresh, false) ||
        !MakeList(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, reset, true)) {
        std::printf("  Q5 UNANSWERED: could not build both a fresh and a reset command list.\n");
        fresh.Release();
        reset.Release();
        return;
    }

    void* const* freshVtbl = *reinterpret_cast<void* const* const*>(fresh.list4);
    void* const* resetVtbl = *reinterpret_cast<void* const* const*>(reset.list4);
    std::printf("  Q5 %s: Reset() %s the vtable\n", freshVtbl == resetVtbl ? "NO" : "YES",
                freshVtbl == resetVtbl ? "leaves" : "REPLACES");
    std::printf("      slot 72 fresh=%ls reset=%ls\n", ModuleOf(freshVtbl[kBuildRaytracingAccelerationStructureIndex]),
                ModuleOf(resetVtbl[kBuildRaytracingAccelerationStructureIndex]));
    std::printf("      slot 76 fresh=%ls reset=%ls\n", ModuleOf(freshVtbl[kDispatchRaysIndex]),
                ModuleOf(resetVtbl[kDispatchRaysIndex]));
    std::printf("      The Overlay reads its targets off a RESET list. A hook taken from the fresh\n");
    std::printf("      column is live, publishes its family, and never fires on a real title.\n");

    fresh.Release();
    reset.Release();
}

// The four questions item 4 must not guess at.
//
// Question 1 decides whether a DXR fixture can run here AT ALL; questions 2-4
// decide how many vtables the hook has to patch and whether it may take them off
// a throwaway device. A "skip" here is a printed unanswered question, never a
// pass -- HANDOFF item 4 says so in as many words about the WARP case.
//
// Q2-Q4 DO NOT NEED RAY TRACING and run whatever Q1 says, because
// ID3D12GraphicsCommandList4 is a runtime interface rather than a device
// capability. Gating them on Q1 would leave a machine without DXR running a
// fixture that asserts nothing at all and reports success — which is the shape
// this repository keeps recording as a gate that cannot fail.
bool ProbeDxr() {
    std::printf("\n[dxr] pre-flight — four questions, answered or explicitly not\n");

    unsigned      dxrTier = 0;
    bool          dxrIsWarp = false;
    ID3D12Device* dxr = BestDxrDevice(dxrTier, dxrIsWarp);
    if (dxr != nullptr) {
        std::printf("  Q1 YES: raytracingTier=%u on the %s adapter (chosen)\n", dxrTier,
                    dxrIsWarp ? "WARP" : "hardware");
    } else {
        std::printf("  Q1 NO: no adapter here reports D3D12_RAYTRACING_TIER_1_0 or better, so a DXR\n");
        std::printf("      fixture cannot run on this machine. That is an ANSWER, not a skip and not coverage.\n");
    }

    bool          warp = dxrIsWarp;
    ID3D12Device* dev = dxr;
    if (dev == nullptr) {
        dev = AnyD3D12Device(warp);
    }
    if (dev == nullptr) {
        Check(false, "a D3D12 device on any adapter — Q2 to Q4 cannot be asked without one");
        return false;
    }

    ListBits   a{};
    ListBits   b{};
    ListBits   compute{};
    const bool madeA = MakeList(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, a);
    const bool madeB = MakeList(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, b);
    const bool madeCompute = MakeList(dev, D3D12_COMMAND_LIST_TYPE_COMPUTE, compute);

    if (madeA && madeB) {
        // FUNCTIONS, NOT VTABLE POINTERS, and the difference is the entire finding.
        // After Reset every list has its OWN vtable array, so comparing the arrays
        // answers "would a slot patch cover both" -- which is not what the Overlay
        // does. It MinHooks the function the slot points AT, so what decides whether
        // one patch covers every list is whether the ADDRESSES agree.
        const RtTargets ta = TargetsOf(a);
        const RtTargets tb = TargetsOf(b);
        Check(ta.build == tb.build && ta.dispatch == tb.dispatch,
              "Q2: two DIRECT lists resolve both slots to the SAME functions — one inline patch covers every list");
        // THE CONTROL, and without it Q2 is satisfied by a machine on which every
        // address happens to coincide. A different interface must not share, exactly
        // as fl_vtable_identity_control asserts for DXGI.
        Check(VtableOf(a.alloc) != VtableOf(a.list4),
              "Q2 control: a command ALLOCATOR does not share the command list's vtable");
        std::printf("      per-object vtables: %s — a SLOT patch would have to be repeated per list, which is\n",
                    VtableOf(a.list4) == VtableOf(b.list4) ? "no" : "YES");
        std::printf("      why the Overlay patches the function and not the slot.\n");
    } else {
        Check(false, "Q2: two DIRECT command lists");
    }

    if (madeA && madeCompute) {
        const RtTargets ta = TargetsOf(a);
        const RtTargets tc = TargetsOf(compute);
        const bool      same = ta.build == tc.build && ta.dispatch == tc.dispatch;
        std::printf("  Q3 %s: DIRECT and COMPUTE lists resolve to %s functions\n", same ? "ONE" : "TWO",
                    same ? "the SAME" : "DIFFERENT");
        std::printf("      slot 72 %ls / %ls, slot 76 %ls / %ls\n", ModuleOf(ta.build), ModuleOf(tc.build),
                    ModuleOf(ta.dispatch), ModuleOf(tc.dispatch));
        std::printf("      Consequence: one detour per method %s.\n",
                    same ? "covers both list types" : "CANNOT serve both — the installer refuses rather than guessing");
    } else {
        std::printf("  Q3 UNANSWERED: could not create both a DIRECT and a COMPUTE list.\n");
    }

    if (madeA) {
        ProbeDxrCrossDevice(warp, a);
    } else {
        std::printf("  Q4 UNANSWERED: no command list on the chosen device to compare.\n");
    }

    ProbeDxrResetSwap(dev);

    a.Release();
    b.Release();
    compute.Release();
    dev->Release();
    return true;
}

// ---------------------------------------------------------------------------
// The dispatch-volume arithmetic, against the shapes a caller can actually send.
//
// A UNIT PROBE rather than an injected case, exactly as --probe-sl-inputs is: the
// hazard is a wrapped product, not a crash, and a game fixture could only
// approximate these extents. Here they are exact and cost microseconds.
//
// The header's static_asserts already pin the identities in every TU that
// includes it, so this deliberately does NOT re-check them -- a runtime loop
// re-proving a constant expression is a gate that cannot fail. What it covers is
// the part that is behaviour: that the ACCUMULATION saturates rather than wrapping
// across a sequence of adds, which is the shape a long capture produces and no
// single expression states.
// ---------------------------------------------------------------------------
bool ProbeDxrInputs() {
    std::printf("\n[dxr-inputs] dispatchRaysVolume against malformed and extreme extents\n");

    // The fixture's own dispatch, accumulated the way a session accumulates it.
    uint32_t acc = 0;
    for (int i = 0; i < 1000; ++i) {
        acc = fl::rtaccum::AddedTo(acc, fl::rtaccum::VolumeOf(FL_DXR_DISPATCH_W, FL_DXR_DISPATCH_H, FL_DXR_DISPATCH_D));
    }
    Check(acc == 1000u * FL_DXR_DISPATCH_W * FL_DXR_DISPATCH_H * FL_DXR_DISPATCH_D,
          "1000 fixture dispatches accumulate exactly, with no rounding and no drift");

    // A 4K primary-ray dispatch is 8,294,400 rays, so 518 of them overflow a
    // uint32. THE SEQUENCE IS WHAT MATTERS: each add is individually fine and the
    // total is not, which is precisely the case a per-add check would miss.
    uint32_t big = 0;
    for (int i = 0; i < 4096; ++i) {
        big = fl::rtaccum::AddedTo(big, fl::rtaccum::VolumeOf(3840u, 2160u, 1u));
    }
    Check(big == fl::rtaccum::kVolumeMax, "4096 4K dispatches SATURATE rather than wrapping to a small number");

    // And the direction is one-way: once at the ceiling nothing brings it down.
    Check(fl::rtaccum::AddedTo(big, fl::rtaccum::VolumeOf(3840u, 2160u, 1u)) == fl::rtaccum::kVolumeMax,
          "a saturated accumulator stays saturated — a wrap would read LOW, and this value is a NUMERATOR");

    // A hostile descriptor. Not reachable from D3D12's own validation, which is why
    // it is here: the detour reads these three fields before anything else does.
    Check(fl::rtaccum::VolumeOf(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu) == UINT64_MAX,
          "three UINT_MAX dimensions saturate the 64-bit product instead of wrapping into a plausible number");
    Check(fl::rtaccum::AddedTo(0u, fl::rtaccum::VolumeOf(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu)) ==
              fl::rtaccum::kVolumeMax,
          "and clamping that into the record's 32 bits lands on the ceiling, not on its low word");
    return true;
}

// ---------------------------------------------------------------------------
// The DXR hold modes: a D3D12 target that RECORDS ray-tracing work every frame,
// so an injected Overlay's RT detours have something to see.
//
// TWO MODES, AND THE SECOND ONE IS THE POINT. --hold-presenting-dxr records an
// acceleration-structure build AND a DispatchRays; --hold-presenting-rayquery
// records the build and NEVER dispatches. 03_METRICS:226 says a writer with only
// the DispatchRays hook sees nothing on an inline-RayQuery title and its silence
// is indistinguishable from a real negative -- and the rayquery mode is what makes
// that claim falsifiable rather than a sentence in a document.
//
// WHAT THE RAYQUERY MODE IS AND IS NOT. It reproduces the OBSERVABLE SIGNATURE of
// a RayQuery-only title -- acceleration structures built, no DispatchRays ever --
// and it does not run a RayQuery shader. No shader is compiled and none is needed:
// the claim under test is "AS-build catches a title DispatchRays misses", and the
// absence of a dispatch is exactly what tests it. Said out loud because a fixture
// named `rayquery` that traces no ray would otherwise read as more than it is.
//
// NOTHING IS EVER EXECUTED. The command list is recorded and closed, and
// ExecuteCommandLists is never called, so no work reaches the GPU. What the hooks
// count is RECORDED work (§H6), which is what the record's unit says.
// ---------------------------------------------------------------------------

ID3D12Resource* MakeBuffer(ID3D12Device* dev, UINT64 bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                           D3D12_RESOURCE_FLAGS flags) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heap;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bytes;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    ID3D12Resource* res = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, __uuidof(ID3D12Resource),
                                            reinterpret_cast<void**>(&res)))) {
        return nullptr;
    }
    return res;
}

struct DxrFixture {
    ID3D12Device5*              device5 = nullptr;
    ID3D12CommandAllocator*     alloc = nullptr;
    ID3D12GraphicsCommandList*  list = nullptr;
    ID3D12GraphicsCommandList4* list4 = nullptr;
    ID3D12Resource*             vertices = nullptr;
    ID3D12Resource*             scratch = nullptr;
    ID3D12Resource*             result = nullptr;
    ID3D12RootSignature*        globalRoot = nullptr;
    ID3D12StateObject*          state = nullptr;
    ID3D12Resource*             shaderTable = nullptr;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    D3D12_RAYTRACING_GEOMETRY_DESC                     geometry{};

    void Release() {
        ReleaseIf(list4);
        ReleaseIf(list);
        ReleaseIf(alloc);
        ReleaseIf(shaderTable);
        ReleaseIf(state);
        ReleaseIf(globalRoot);
        ReleaseIf(result);
        ReleaseIf(scratch);
        ReleaseIf(vertices);
        ReleaseIf(device5);
    }
};

// An EMPTY global root signature. The raygen shader binds nothing, and a
// raytracing state object still needs one.
ID3D12RootSignature* MakeEmptyRootSignature(ID3D12Device* dev) {
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ID3DBlob*     blob = nullptr;
    ID3DBlob*     err = nullptr;
    const HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ReleaseIf(err);
    if (FAILED(hr) || blob == nullptr) {
        ReleaseIf(blob);
        return nullptr;
    }
    ID3D12RootSignature* sig = nullptr;
    dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature),
                             reinterpret_cast<void**>(&sig));
    blob->Release();
    return sig;
}

// The smallest raytracing pipeline that DispatchRays will record against.
//
// MEASURED, NOT PRECAUTIONARY. Without a bound state object DispatchRays
// access-violates at RECORD time -- with a well-formed shader table and with a
// zeroed one alike, so it is the state object the runtime dereferences and not the
// descriptor. That is why this fixture carries a DXIL library at all.
ID3D12StateObject* MakeRtStateObject(ID3D12Device5* dev5, ID3D12RootSignature* globalRoot) {
    D3D12_EXPORT_DESC exported{};
    exported.Name = L"RayGen";

    D3D12_DXIL_LIBRARY_DESC lib{};
    lib.DXILLibrary.pShaderBytecode = fl::harness::kDxrRayGenDxil;
    lib.DXILLibrary.BytecodeLength = sizeof(fl::harness::kDxrRayGenDxil);
    lib.NumExports = 1;
    lib.pExports = &exported;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
    shaderConfig.MaxPayloadSizeInBytes = 4;
    shaderConfig.MaxAttributeSizeInBytes = 8;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    D3D12_GLOBAL_ROOT_SIGNATURE global{};
    global.pGlobalRootSignature = globalRoot;

    D3D12_STATE_SUBOBJECT subobjects[4]{};
    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &lib;
    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[1].pDesc = &shaderConfig;
    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[2].pDesc = &pipelineConfig;
    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[3].pDesc = &global;

    D3D12_STATE_OBJECT_DESC desc{};
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    desc.NumSubobjects = 4;
    desc.pSubobjects = subobjects;

    ID3D12StateObject* state = nullptr;
    if (FAILED(dev5->CreateStateObject(&desc, __uuidof(ID3D12StateObject), reinterpret_cast<void**>(&state)))) {
        return nullptr;
    }
    return state;
}

// A WELL-FORMED bottom-level build, sized from the runtime's own prebuild info.
//
// A zeroed descriptor would have been shorter and is not safe: the recording path
// reads the inputs, and a malformed one is a fault inside D3D12Core -- past the
// point where the Overlay's FL_HOOK_GUARD can help, since our detour has already
// forwarded. Malformed input belongs in --probe-dxr-inputs, where it is aimed at
// OUR code and never at the runtime's.
bool BuildDxrFixture(ID3D12Device* dev, DxrFixture& fx) {
    if (FAILED(dev->QueryInterface(__uuidof(ID3D12Device5), reinterpret_cast<void**>(&fx.device5)))) {
        return false;
    }

    // One triangle, in an UPLOAD buffer so it has a real GPU virtual address.
    const float verts[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    fx.vertices = MakeBuffer(dev, sizeof(verts), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                             D3D12_RESOURCE_FLAG_NONE);
    if (fx.vertices == nullptr) {
        return false;
    }
    void*       mapped = nullptr;
    D3D12_RANGE none{0, 0};
    if (SUCCEEDED(fx.vertices->Map(0, &none, &mapped)) && mapped != nullptr) {
        std::memcpy(mapped, verts, sizeof(verts));
        fx.vertices->Unmap(0, nullptr);
    }

    fx.geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    fx.geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    fx.geometry.Triangles.VertexBuffer.StartAddress = fx.vertices->GetGPUVirtualAddress();
    fx.geometry.Triangles.VertexBuffer.StrideInBytes = 3 * sizeof(float);
    fx.geometry.Triangles.VertexCount = 3;
    fx.geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

    fx.build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    fx.build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    fx.build.Inputs.NumDescs = 1;
    fx.build.Inputs.pGeometryDescs = &fx.geometry;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    fx.device5->GetRaytracingAccelerationStructurePrebuildInfo(&fx.build.Inputs, &info);
    if (info.ResultDataMaxSizeInBytes == 0 || info.ScratchDataSizeInBytes == 0) {
        return false;
    }

    fx.scratch = MakeBuffer(dev, info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    fx.result =
        MakeBuffer(dev, info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                   D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (fx.scratch == nullptr || fx.result == nullptr) {
        return false;
    }
    fx.build.ScratchAccelerationStructureData = fx.scratch->GetGPUVirtualAddress();
    fx.build.DestAccelerationStructureData = fx.result->GetGPUVirtualAddress();

    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                           reinterpret_cast<void**>(&fx.alloc))) ||
        FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, fx.alloc, nullptr,
                                      __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&fx.list))) ||
        FAILED(fx.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), reinterpret_cast<void**>(&fx.list4)))) {
        return false;
    }
    fx.list4->Close();

    // The raytracing pipeline and its one-record shader table. Built even for the
    // rayquery mode, which never dispatches: sharing one setup keeps the two modes
    // differing in exactly ONE call, which is the property the pair exists to
    // isolate. A fixture whose two arms differ in five ways proves nothing about
    // the one that matters.
    fx.globalRoot = MakeEmptyRootSignature(dev);
    if (fx.globalRoot == nullptr) {
        return false;
    }
    fx.state = MakeRtStateObject(fx.device5, fx.globalRoot);
    if (fx.state == nullptr) {
        return false;
    }

    ID3D12StateObjectProperties* props = nullptr;
    if (FAILED(fx.state->QueryInterface(__uuidof(ID3D12StateObjectProperties), reinterpret_cast<void**>(&props)))) {
        return false;
    }
    const void* identifier = props->GetShaderIdentifier(L"RayGen");
    // A shader-table record must start on a 64-byte boundary; a committed buffer's
    // GPU virtual address is far more aligned than that.
    fx.shaderTable = MakeBuffer(dev, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, D3D12_HEAP_TYPE_UPLOAD,
                                D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    bool tabled = false;
    if (identifier != nullptr && fx.shaderTable != nullptr) {
        void*       slot = nullptr;
        D3D12_RANGE nothing{0, 0};
        if (SUCCEEDED(fx.shaderTable->Map(0, &nothing, &slot)) && slot != nullptr) {
            std::memcpy(slot, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            fx.shaderTable->Unmap(0, nullptr);
            tabled = true;
        }
    }
    props->Release();
    return tabled;
}

// One frame's worth of recorded ray-tracing work.
void RecordDxrFrame(DxrFixture& fx, bool dispatch) {
    fx.alloc->Reset();
    fx.list4->Reset(fx.alloc, nullptr);
    fx.list4->BuildRaytracingAccelerationStructure(&fx.build, 0, nullptr);
    if (dispatch) {
        // THE ONE CALL THE TWO MODES DIFFER BY. Everything above and below is
        // identical, so a difference in what the Overlay records is attributable to
        // this and to nothing else.
        fx.list4->SetComputeRootSignature(fx.globalRoot);
        fx.list4->SetPipelineState1(fx.state);
        D3D12_DISPATCH_RAYS_DESC rays{};
        rays.RayGenerationShaderRecord.StartAddress = fx.shaderTable->GetGPUVirtualAddress();
        rays.RayGenerationShaderRecord.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        rays.Width = FL_DXR_DISPATCH_W;
        rays.Height = FL_DXR_DISPATCH_H;
        rays.Depth = FL_DXR_DISPATCH_D;
        fx.list4->DispatchRays(&rays);
    }
    fx.list4->Close();
}

bool HoldPresentingDxr(int seconds, bool real, bool dispatch, int presentIntervalMs) {
    std::printf("\n[dxr-hold] recording %s for %d second(s) [%s]\n",
                dispatch ? "AS builds AND DispatchRays" : "AS builds ONLY (the RayQuery signature)", seconds,
                real ? "REAL" : "DXGI_PRESENT_TEST");

    unsigned      tier = 0;
    bool          warp = false;
    ID3D12Device* dev = BestDxrDevice(tier, warp);
    if (dev == nullptr) {
        Check(false, "a DXR-capable adapter — this fixture cannot run on this machine");
        return false;
    }

    IDXGIFactory4*      factory = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1*    sc = nullptr;
    DxrFixture          fx{};
    bool                built = false;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (SUCCEEDED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue)))) {
            DXGI_SWAP_CHAIN_DESC1 d{};
            d.Width = 64;
            d.Height = 64;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            d.BufferCount = 2;
            d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
            built = SUCCEEDED(factory->CreateSwapChainForComposition(queue, &d, nullptr, &sc)) && sc != nullptr;
        }
    }
    if (built) {
        built = BuildDxrFixture(dev, fx);
        Check(built, "a well-formed bottom-level acceleration structure, sized from the runtime's prebuild info");
    } else {
        Check(false, "a D3D12 swapchain for the DXR hold");
    }

    if (built) {
        const UINT      flags = real ? 0u : DXGI_PRESENT_TEST;
        const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
        std::fflush(stdout);
        while (GetTickCount64() < until) {
            RecordDxrFrame(fx, dispatch);
            sc->Present(0, flags);
            if (presentIntervalMs > 0) {
                Sleep(static_cast<DWORD>(presentIntervalMs));
            }
        }
    }

    fx.Release();
    ReleaseIf(sc);
    ReleaseIf(queue);
    ReleaseIf(factory);
    dev->Release();
    return built;
}

// ---------------------------------------------------------------------------
// Module-scoped resolution, against two modules exporting the SAME name.
//
// It calls fl::inventory::ResolveScoped -- the OVERLAY'S OWN RESOLVER, out of
// the Overlay's own header -- rather than a copy. That is the §S29(b) rule:
// `ctest fl_vtable_indices` once proved a fact about dxgi.dll instead of a fact
// about FrameLedger.Overlay because the harness kept its own constants, and a
// probe with its own resolver would prove a fact about GetProcAddress.
// ---------------------------------------------------------------------------

// Load a stub by ABSOLUTE PATH, never by name.
//
// Loading "sl.interposer.dll" by name would search the loader's paths, and on a
// developer machine with a game installed that can find A REAL STREAMLINE
// INTERPOSER. The fixture would then load vendor code into this process, and
// whatever it proved would be about NVIDIA's build rather than about ours.
// An absolute path performs no search at all.
HMODULE LoadStubExactly(const wchar_t* absolutePath) {
    const HMODULE h = LoadLibraryExW(absolutePath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (h == nullptr) {
        return nullptr;
    }
    // Belt and braces: confirm the module we got is the file we named. If a real
    // interposer were already resident under that name, this is what says so.
    //
    // Separators are normalised before comparing, because the two sides spell
    // the same path differently: CMake's $<TARGET_FILE:...> yields forward
    // slashes and GetModuleFileNameW returns backslashes. Comparing raw made
    // this check fail on a correct load, which is a check that cries wolf --
    // nearly as bad as one that cannot fire.
    wchar_t got[MAX_PATH]{};
    if (GetModuleFileNameW(h, got, MAX_PATH) == 0) {
        std::printf("  [FAIL] GetModuleFileNameW failed on the module we just loaded\n");
        ++g_failures;
        return nullptr;
    }
    wchar_t want[MAX_PATH]{};
    wcsncpy_s(want, absolutePath, _TRUNCATE);
    for (wchar_t* p = want; *p != L'\0'; ++p) {
        if (*p == L'/') {
            *p = L'\\';
        }
    }
    for (wchar_t* p = got; *p != L'\0'; ++p) {
        if (*p == L'/') {
            *p = L'\\';
        }
    }
    if (_wcsicmp(got, want) != 0) {
        // PRINT BOTH. A bare "did not match" sent the last diagnosis of this
        // exact check down the wrong path.
        std::printf("  [FAIL] loaded module is not the file we named\n         wanted %ls\n         got    %ls\n", want,
                    got);
        ++g_failures;
        return nullptr;
    }
    return h;
}

// The render extent the fixtures use. Declared here, above every user, and
// sourced from src/native/CMakeLists.txt so guard_test.cpp asserts against the
// SAME number -- a fixture and its assertion holding separate copies of the
// value under test is the §S29(b) defect in miniature.
//
// 1280x720 is arbitrary, and that is the point: a writer that hardcoded a
// plausible render resolution must FAIL rather than coincide.
constexpr unsigned kTaggedRenderW = FL_TAGGED_RENDER_W;
constexpr unsigned kTaggedRenderH = FL_TAGGED_RENDER_H;

// The inputs walk, against input a hostile or buggy title could produce.
//
// WHY THIS IS A UNIT PROBE AND NOT AN INJECTED CASE. Every branch here
// dereferences pointers the caller supplied, and a fault in the real thing lands
// in FL_HOOK_GUARD and burns one of the three that self-disable the Overlay --
// so a malformed input that faults is a BUG, not degradation. Driving those
// shapes through an injected game fixture would take seconds per case and could
// only produce them approximately. Here they are exact and take microseconds.
bool ProbeSlInputs() {
    std::printf("\n[upscaler] the slEvaluateFeature inputs walk, against malformed input\n");

    const sl::Extent zero{};
    sl::Extent       real{};
    real.width = kTaggedRenderW;
    real.height = kTaggedRenderH;

    sl::ResourceTag tagWithExtent(nullptr, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent,
                                  &real);
    sl::ResourceTag tagWholeResource(nullptr, sl::kBufferTypeScalingInputColor,
                                     sl::ResourceLifecycle::eValidUntilPresent, &zero);
    sl::ResourceTag tagOtherBuffer(nullptr, sl::kBufferTypeScalingOutputColor,
                                   sl::ResourceLifecycle::eValidUntilPresent, &real);

    // --- the cases that must FIND it -------------------------------------
    {
        const sl::BaseStructure* one[] = {&tagWithExtent};
        const auto               r = fl::slinputs::FindScalingInputExtent(one, 1);
        Check(r.found && r.renderW == kTaggedRenderW && r.renderH == kTaggedRenderH,
              "a tagged scaling-input extent is found, with the exact values");
    }
    {
        // A null element BEFORE the tag: skipped, not treated as the end.
        const sl::BaseStructure* withHole[] = {nullptr, &tagWithExtent};
        const auto               r = fl::slinputs::FindScalingInputExtent(withHole, 2);
        Check(r.found, "a null element mid-array is skipped rather than ending the walk");
    }
    {
        // Reached through `next` rather than as an array element.
        sl::ResourceTag head = tagOtherBuffer;
        head.next = &tagWithExtent;
        const sl::BaseStructure* chained[] = {&head};
        const auto               r = fl::slinputs::FindScalingInputExtent(chained, 1);
        Check(r.found && r.renderW == kTaggedRenderW, "a tag reached through the next chain is found");
    }

    // --- the cases that must NOT find it, and must not misbehave ----------
    Check(!fl::slinputs::FindScalingInputExtent(nullptr, 4).found, "a null inputs array finds nothing");
    {
        const sl::BaseStructure* one[] = {&tagWithExtent};
        Check(!fl::slinputs::FindScalingInputExtent(one, 0).found, "numInputs 0 finds nothing even with a real array");
    }
    {
        // The whole-resource case: extent defaults to all-zero, which the vendor
        // documents as "use the entire resource". Honest unknown, NOT a
        // resolution of zero.
        const sl::BaseStructure* one[] = {&tagWholeResource};
        const auto               r = fl::slinputs::FindScalingInputExtent(one, 1);
        Check(!r.found && r.renderW == 0 && r.renderH == 0,
              "a whole-resource tag with NO size on the Resource yields the honest unknown, not a 0x0 resolution");
    }
    {
        // THE WHOLE-RESOURCE SHAPE WITH THE SIZE ON THE RESOURCE: extent zero, and
        // the Resource the tag points at declares width/height. That is the title's
        // statement of the input buffer's size, read from the argument it passed.
        sl::Resource sized(sl::ResourceType::eTex2d, nullptr);
        sized.width = kTaggedRenderW;
        sized.height = kTaggedRenderH;
        sl::ResourceTag whole(&sized, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent,
                              &zero);
        const sl::BaseStructure* one[] = {&whole};
        const auto               r = fl::slinputs::FindScalingInputExtent(one, 1);
        Check(r.found && r.renderW == kTaggedRenderW && r.renderH == kTaggedRenderH,
              "a whole-resource tag takes the exact size the Resource declares");
    }
    {
        // A non-zero extent WINS over the Resource's size: the extent is the area in
        // use, the Resource may be the larger dynamic-resolution target.
        sl::Resource bigger(sl::ResourceType::eTex2d, nullptr);
        bigger.width = kTaggedRenderW * 2u;
        bigger.height = kTaggedRenderH * 2u;
        sl::ResourceTag sub(&bigger, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent,
                            &real);
        const sl::BaseStructure* one[] = {&sub};
        const auto               r = fl::slinputs::FindScalingInputExtent(one, 1);
        Check(r.found && r.renderW == kTaggedRenderW && r.renderH == kTaggedRenderH,
              "an extent takes precedence over the Resource's own size");
    }
    {
        // The Resource pointer names something that is NOT a Resource: GUID-checked
        // and skipped, never read as one.
        sl::ViewportHandle       notAResource{9u};
        sl::ResourceTag          bad(reinterpret_cast<sl::Resource*>(&notAResource), sl::kBufferTypeScalingInputColor,
                                     sl::ResourceLifecycle::eValidUntilPresent, &zero);
        const sl::BaseStructure* one[] = {&bad};
        Check(!fl::slinputs::FindScalingInputExtent(one, 1).found,
              "a whole-resource tag whose Resource fails the GUID check yields nothing");
    }
    {
        // TagSize is the SAME reading the global-tag detours use, so the direct
        // call is asserted too: a Resource with one zero dimension is not a size.
        sl::Resource half(sl::ResourceType::eTex2d, nullptr);
        half.width = kTaggedRenderW;
        sl::ResourceTag whole(&half, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent,
                              &zero);
        uint32_t        w = 0;
        uint32_t        h = 0;
        Check(!fl::slinputs::TagSize(whole, w, h), "a Resource declaring only a width is not a size");
    }
    {
        const sl::BaseStructure* one[] = {&tagOtherBuffer};
        Check(!fl::slinputs::FindScalingInputExtent(one, 1).found,
              "a tag for a DIFFERENT buffer type is not mistaken for the scaling input");
    }
    {
        // GUID matches nothing we know: skipped rather than read as a ResourceTag.
        sl::ViewportHandle       viewport{7u};
        const sl::BaseStructure* one[] = {&viewport};
        Check(!fl::slinputs::FindScalingInputExtent(one, 1).found, "a structure of another type is skipped");
    }
    {
        // structVersion below what our headers describe: we cannot place the
        // fields, so we do not read them.
        sl::ResourceTag stale = tagWithExtent;
        stale.structVersion = 0;
        const sl::BaseStructure* one[] = {&stale};
        Check(!fl::slinputs::FindScalingInputExtent(one, 1).found, "a structVersion below kStructVersion1 is skipped");
    }

    // --- the shapes that would hang or run away ---------------------------
    {
        // A SELF-REFERENTIAL next. Without the depth cap this does not fault --
        // it spins forever on the present thread, which is worse: no exception,
        // no self-disable, just a frozen game with our DLL in it.
        sl::ResourceTag loop = tagOtherBuffer;
        loop.next = &loop;
        const sl::BaseStructure* one[] = {&loop};
        Check(!fl::slinputs::FindScalingInputExtent(one, 1).found,
              "a self-referential next terminates and finds nothing");
    }
    {
        // A TWO-NODE CYCLE, which a self-reference check alone would miss.
        sl::ResourceTag a = tagOtherBuffer;
        sl::ResourceTag b = tagOtherBuffer;
        a.next = &b;
        b.next = &a;
        const sl::BaseStructure* one[] = {&a};
        Check(!fl::slinputs::FindScalingInputExtent(one, 1).found, "a two-node cycle terminates too");
    }
    {
        // numInputs FAR beyond the array, with the array long enough to absorb
        // the cap. This is the case the cap exists for: 4 billion becomes 32.
        // It does NOT cover a count that merely exceeds a short array -- nothing
        // in the ABI carries the allocation length, and the header says so.
        const sl::BaseStructure* wide[fl::slinputs::kMaxInputs] = {};
        wide[fl::slinputs::kMaxInputs - 1] = &tagWithExtent;
        const auto r = fl::slinputs::FindScalingInputExtent(wide, 0xFFFFFFFFu);
        Check(r.found, "a lying numInputs is capped, and the walk still reads up to the cap");
    }
    // --- the quality preset, and the zero that must never reach the record ---
    {
        sl::DLSSOptions opts;
        opts.mode = sl::DLSSMode::eMaxQuality;
        const sl::BaseStructure* one[] = {&opts};
        const auto               r = fl::slinputs::FindScalingInputExtent(one, 1);
        Check(r.qualityFound && r.quality == static_cast<uint8_t>(sl::DLSSMode::eMaxQuality),
              "a chained DLSSOptions yields the vendor's own DLSSMode value");
    }
    {
        // THE COLLISION THIS MAPPING EXISTS FOR. DLSSMode::eOff is 0, and
        // upscalerQuality reserves 0 for "nobody looked" -- so a verbatim copy
        // would make "the title turned DLSS off" indistinguishable from an
        // unhooked writer, and 0 would decode as NGX MaxPerf, "DLSS Performance".
        sl::DLSSOptions off;
        off.mode = sl::DLSSMode::eOff;
        const sl::BaseStructure* one[] = {&off};
        const auto               r = fl::slinputs::FindScalingInputExtent(one, 1);
        Check(r.qualityFound && r.quality == 0xFFu, "eOff maps to 0xFF, never to 0");
    }
    {
        // A mode from a newer SDK than these headers describe.
        sl::DLSSOptions future;
        future.mode = static_cast<sl::DLSSMode>(9999);
        const sl::BaseStructure* one[] = {&future};
        const auto               r = fl::slinputs::FindScalingInputExtent(one, 1);
        Check(r.quality == 0xFFu, "a mode at or beyond eCount maps to 0xFF, not to a preset we invented");
    }
    {
        // EVERY mode, exhaustively: none may produce 0. A single mapping mistake
        // here publishes a wrong preset rather than an absence.
        bool anyZero = false;
        for (uint32_t m = 0; m <= static_cast<uint32_t>(sl::DLSSMode::eCount) + 2; ++m) {
            if (fl::slinputs::QualityFromMode(static_cast<sl::DLSSMode>(m)) == 0) {
                anyZero = true;
            }
        }
        Check(!anyZero, "NO DLSSMode value, in range or out, ever maps to 0");
    }
    {
        // Extent and quality arrive in DIFFERENT structures, and a title may
        // chain them in either order. Returning early on the first hit would have
        // made quality depend on chain order -- a property of the title, not of
        // what it is doing.
        sl::DLSSOptions opts;
        opts.mode = sl::DLSSMode::eBalanced;
        sl::ResourceTag tagFirst = tagWithExtent;
        tagFirst.next = &opts;
        const sl::BaseStructure* one[] = {&tagFirst};
        const auto               r = fl::slinputs::FindScalingInputExtent(one, 1);
        Check(r.found && r.qualityFound, "extent and quality are both found when chained together");
    }
    return g_failures == 0;
}

// The word RecordPresent drains: features in the low byte, the frame-generation
// count above them.
//
// WHAT THIS PROBE IS FOR, and what it deliberately is NOT. The field boundaries are
// integer arithmetic and are pinned by static_asserts inside fl_sl_seen.h, so they
// are checked in every translation unit that includes the header and no test can
// forget to run them. Spending a 16-million-iteration loop here to "prove" a carry
// cannot propagate downward would be a gate that cannot fail. What IS behaviour,
// and what this drives, is the saturation direction and the free-bit property --
// the two places a plausible edit changes an answer.
bool ProbeSlSeen() {
    std::printf("\n[upscaler] the g_slSeen word: features in the low byte, the FG count above\n");

    using namespace fl::slseen;

    // --- the free bits, which is the case a literal kFeatureMask gets wrong -----
    //
    // Today FlSlSeen occupies bits 0-4 and the next enumerator lands on bit 5. With
    // kFeatureMask written as 0x1Fu that bit is set by the hook and masked off by
    // Features(), so RecordPresent's two `feature field is non-zero` gates go false
    // and a title silently loses render resolution and gains a fabricated Ray
    // Reconstruction answer. Derived from kCountShift it cannot happen, and this is
    // the assertion that says so.
    // ACCUMULATED, NOT SHORT-CIRCUITED. An early return here would let the first
    // failing case hide every case after it -- the same shape as the Catch2
    // terminate-on-REQUIRE defect this repo fixed on 2026-08-09, where "1 test
    // failed" was true and "and 2 never ran" was the part that mattered.
    bool freeBits = true;
    for (uint32_t bit = 0; bit < kCountShift; ++bit) {
        const uint32_t w = 1u << bit;
        if (Features(w) != w || FgEvals(w) != 0u) {
            freeBits = false;
        }
    }
    Check(freeBits, "every bit below kCountShift is a feature bit and reads back as one");

    // --- one increment does not disturb the features, and vice versa ------------
    Check(Features(kFeatureMask | kCountOne) == kFeatureMask && FgEvals(kFeatureMask | kCountOne) == 1u,
          "a count and a full feature field coexist in one word without either reading the other");

    // --- accumulation, in the shape the hook actually produces ------------------
    {
        uint32_t w = 0;
        w |= 1u << 3;    // some feature bit
        for (int i = 0; i < 7; ++i) {
            w += kCountOne;    // what fetch_add(kCountOne) does
        }
        Check(FgEvals(w) == 7u && Features(w) == (1u << 3),
              "seven increments read back as seven, with the feature bit untouched");
    }

    // --- saturation, and the DIRECTION is the whole point -----------------------
    //
    // A wrapped count reads LOW, and fg_factor divides by it -- so a wrap inflates
    // the factor without bound. Saturating reads HIGH, which a consumer can see and
    // refuse. 255 is a sentinel as much as a value: no configuration evaluates frame
    // generation 255 times between two presents.
    Check(SaturateToByte(0u) == 0u, "zero evaluations is a real count, not a sentinel");
    Check(SaturateToByte(1u) == 1u && SaturateToByte(4u) == 4u, "an ordinary count passes through unchanged");
    Check(SaturateToByte(255u) == 255u, "the last representable count is not treated as overflow");
    Check(SaturateToByte(256u) == 255u && SaturateToByte(kCountMax) == 255u,
          "an over-large count saturates HIGH rather than wrapping low, which would inflate fg_factor");

    return g_failures == 0;
}

// The ABI check: right module name, right symbol name, WRONG GENERATION.
//
// This case is invisible to module scoping, which is why it needs its own
// fixture. The decoy in ProbeUpscalerResolve has the wrong module NAME, so
// scoping catches it. Streamline 1.5.6 -- which The Witcher 3 ships -- has the
// RIGHT name and a different slEvaluateFeature signature, and scoping cannot see
// a signature. Measured 2026-08-14 by fl-probe-interposer, which access-violated
// on it.
//
// LOADS ONLY THE V1 STUB. Both stubs are called sl.interposer.dll, and
// GetModuleHandleExW resolves by name, so a process holding both would answer
// whichever loaded first and this probe would silently be testing the other
// fixture. That is why the two live in different directories and why this mode
// is separate from --probe-upscaler-resolve rather than an extra section of it.
bool ProbeSlAbi() {
    std::printf("\n[upscaler] a Streamline 1.x module has the right name and the wrong ABI\n");

    const HMODULE v1 = LoadStubExactly(FL_STUB_SL_INTERPOSER_V1);
    Check(v1 != nullptr, "the Streamline 1.x sl.interposer.dll fixture loaded from its absolute path");
    if (v1 == nullptr) {
        return false;
    }

    // VACUITY GUARD FIRST. If this fixture did not actually export the symbol,
    // "the Overlay refused it" would be true for the wrong reason and this whole
    // probe would prove nothing -- the exact defect stub_sl_common.cpp records
    // against its own first version.
    Check(GetProcAddress(v1, "slEvaluateFeature") != nullptr,
          "the 1.x fixture really DOES export slEvaluateFeature - so refusing it is about the ABI, not the name");
    Check(GetProcAddress(v1, "slInit") != nullptr, "and slInit, which both generations have");
    Check(GetProcAddress(v1, "slGetHooks") != nullptr, "and slGetHooks, which only 1.x has");

    // The three SL2 markers are absent, which is the signal.
    Check(GetProcAddress(v1, "slSetD3DDevice") == nullptr, "it does NOT export slSetD3DDevice");
    Check(GetProcAddress(v1, "slIsFeatureLoaded") == nullptr, "it does NOT export slIsFeatureLoaded");
    Check(GetProcAddress(v1, "slGetNewFrameToken") == nullptr, "it does NOT export slGetNewFrameToken");
    Check(!fl::inventory::SpeaksStreamline2(v1), "so SpeaksStreamline2 says no");

    // THE PROPERTY UNDER TEST. The Overlay's own resolver -- the one
    // InstallUpscalerHooks calls -- must return nothing, so no detour is
    // installed, FL_HOOK_UPSCALER_IDENTITY is never published, and the record
    // says FL_UPSCALER_NOT_REPORTED instead of a name read out of the wrong
    // argument.
    Check(fl::inventory::ResolveScoped(L"sl.interposer.dll", "slEvaluateFeature") == nullptr,
          "ResolveScoped REFUSES it - a wrong-generation module is the same answer as no module");
    return g_failures == 0;
}

bool ProbeUpscalerResolve() {
    std::printf("\n[upscaler] module-scoped symbol resolution, with a decoy exporting the same name\n");

    const HMODULE interposer = LoadStubExactly(FL_STUB_SL_INTERPOSER);
    const HMODULE common = LoadStubExactly(FL_STUB_SL_COMMON);
    Check(interposer != nullptr, "the sl.interposer.dll stub loaded from its absolute path");
    Check(common != nullptr, "the sl.common.dll decoy loaded from its absolute path");
    if (interposer == nullptr || common == nullptr) {
        return false;
    }

    // Both modules really do export the same name -- otherwise the discrimination
    // below is vacuous and would pass against a resolver that ignores the module
    // entirely.
    void* fromInterposer = fl::inventory::ResolveScoped(L"sl.interposer.dll", "slEvaluateFeature");
    void* fromCommon = fl::inventory::ResolveScoped(L"sl.common.dll", "slEvaluateFeature");
    Check(fromInterposer != nullptr, "sl.interposer.dll exports slEvaluateFeature");
    Check(fromCommon != nullptr, "sl.common.dll ALSO exports it - so the decoy is real and scoping is falsifiable");
    // BOTH non-null before comparing, or this passes on a decoy that exports
    // nothing: nullptr differs from a real address, and that is how the first
    // version of this fixture reported success while the decoy was empty.
    Check(fromInterposer != nullptr && fromCommon != nullptr && fromInterposer != fromCommon,
          "the two are DIFFERENT addresses - a name-only resolver could not tell them apart");

    // And the addresses are the ones the loader reports for each module, not
    // merely 'some address'.
    Check(fromInterposer == reinterpret_cast<void*>(GetProcAddress(interposer, "slEvaluateFeature")),
          "ResolveScoped returned sl.interposer.dll's own export");
    Check(fromCommon == reinterpret_cast<void*>(GetProcAddress(common, "slEvaluateFeature")),
          "ResolveScoped returned sl.common.dll's own export");

    // A module that is not loaded resolves to nothing, and that is an ANSWER.
    // ResolveScoped must never LoadLibrary a module the process does not have --
    // mapping a module the game did not load changes the host to suit us.
    Check(fl::inventory::ResolveScoped(L"fl_not_a_real_module.dll", "slEvaluateFeature") == nullptr,
          "an absent module resolves to nullptr rather than being loaded");
    Check(GetModuleHandleW(L"fl_not_a_real_module.dll") == nullptr, "and it really was not loaded");

    // A symbol that does not exist resolves to nothing. This is the wrong-name
    // case 17_HOOK_ENGINE calls the highest false-confidence risk in the spike:
    // it must produce NOTHING, so the Overlay installs nothing and claims
    // nothing, rather than silently resolving something else.
    Check(fl::inventory::ResolveScoped(L"sl.interposer.dll", "slEvaluateFeatureX") == nullptr,
          "a misspelt symbol resolves to nullptr - it cannot silently find a neighbour");
    return g_failures == 0;
}

// ---------------------------------------------------------------------------
// AMD FidelityFX: LEAF-scoped resolution of ffxDispatch, with the LOADER in the
// process and forwarding.
//
// Four modules export the same five names here, exactly as on a loader-shipping
// title, and the property under test is fl_hook_inventory.h's: the Overlay's own
// resolver returns each module's OWN export -- four distinct addresses, the loader's
// among them -- and refuses a module of a hooked name that does not speak the
// five-export ABI. The second half is a VACUITY GUARD for the [ffx] injected cases: a
// dispatch pushed through the loader stand-in must arrive at exactly one leaf, once,
// through the leaf's DIRECT entry and not its export (the measured shape) -- or the
// K = 1 control in those cases would be proving something about a forwarder that
// did not forward, or that forwarded the way the real one does not.
// ---------------------------------------------------------------------------
using StubFfxCountFn = unsigned int(STDMETHODCALLTYPE*)(uint64_t);
using StubFfxPlainCountFn = unsigned int(STDMETHODCALLTYPE*)();
using StubLoaderBindFn = void(STDMETHODCALLTYPE*)(HMODULE, HMODULE);

bool ProbeFfxResolve() {
    std::printf("\n[ffx] module-scoped resolution of ffxDispatch on all four AMD modules, and the loader's forward\n");

    const HMODULE monolith = LoadStubExactly(FL_STUB_FFX_DX12);
    const HMODULE upscaler = LoadStubExactly(FL_STUB_FFX_UPSCALER);
    const HMODULE fg = LoadStubExactly(FL_STUB_FFX_FG);
    const HMODULE loader = LoadStubExactly(FL_STUB_FFX_LOADER);
    const HMODULE decoy = LoadStubExactly(FL_STUB_SL_COMMON);
    Check(monolith != nullptr,
          "the amd_fidelityfx_dx12.dll stub (the SDK 1.1.x monolith) loaded from its absolute path");
    Check(upscaler != nullptr, "the amd_fidelityfx_upscaler_dx12.dll stub loaded");
    Check(fg != nullptr, "the amd_fidelityfx_framegeneration_dx12.dll stub loaded");
    Check(loader != nullptr, "the amd_fidelityfx_loader_dx12.dll forwarding decoy loaded");
    Check(decoy != nullptr, "the sl.common.dll decoy loaded, for the ABI arm's negative");
    if (monolith == nullptr || upscaler == nullptr || fg == nullptr || loader == nullptr || decoy == nullptr) {
        return false;
    }

    // VACUITY FIRST. All four really export the name -- the loader included -- so
    // "the loader is not a row" is a decision the inventory took, not an accident
    // of the fixture exporting nothing (stub_sl_common.cpp records that exact
    // accident against its own first version).
    Check(GetProcAddress(monolith, "ffxDispatch") != nullptr, "the monolith stub exports ffxDispatch");
    Check(GetProcAddress(upscaler, "ffxDispatch") != nullptr, "the upscaler stub exports ffxDispatch");
    Check(GetProcAddress(fg, "ffxDispatch") != nullptr, "the frame-generation stub exports ffxDispatch");
    Check(GetProcAddress(loader, "ffxDispatch") != nullptr,
          "the LOADER stub exports ffxDispatch too - so leaving it out of the inventory is scoping, not luck");
    Check(fl::inventory::SpeaksFfxApi(loader),
          "and the loader speaks all five names - refusing it is not an ABI verdict");

    // The leaf table agrees with itself at runtime, case-insensitively as the loader is.
    Check(fl::inventory::FfxLeafOf(L"amd_fidelityfx_dx12.dll") == static_cast<int>(fl::inventory::kFfxLeafMonolith),
          "amd_fidelityfx_dx12.dll is the monolith leaf");
    Check(fl::inventory::FfxLeafOf(L"AMD_FIDELITYFX_UPSCALER_DX12.DLL") ==
              static_cast<int>(fl::inventory::kFfxLeafUpscaler),
          "leaf lookup is case-insensitive, as GetModuleHandleExW is");
    Check(fl::inventory::FfxLeafOf(L"amd_fidelityfx_framegeneration_dx12.dll") ==
              static_cast<int>(fl::inventory::kFfxLeafFrameGeneration),
          "amd_fidelityfx_framegeneration_dx12.dll is the frame-generation leaf");
    Check(fl::inventory::FfxLeafOf(fl::inventory::kFfxLoaderModule) == static_cast<int>(fl::inventory::kFfxLeafLoader),
          "the loader has its own slot: it is the game's entry on a loader-shipping title (measured 2026-09-04)");
    Check(fl::inventory::FfxLeafOf(nullptr) < 0, "a null module name is not a leaf");

    // An inventory row names the loader -- walked at runtime here as well as
    // asserted at compile time in the header, so the property has two witnesses.
    bool loaderIsARow = false;
#define FL_PROBE_ROW(mod, sym, family)                                                                                 \
    if (_wcsicmp(mod, fl::inventory::kFfxLoaderModule) == 0) {                                                         \
        loaderIsARow = true;                                                                                           \
    }
    FL_HOOK_INVENTORY(FL_PROBE_ROW)
#undef FL_PROBE_ROW
    Check(loaderIsARow, "an FL_HOOK_INVENTORY row names amd_fidelityfx_loader_dx12.dll");

    // THE PROPERTY: the Overlay's own resolver returns each leaf's OWN export.
    void* fromMonolith = fl::inventory::ResolveScoped(L"amd_fidelityfx_dx12.dll", "ffxDispatch");
    void* fromUpscaler = fl::inventory::ResolveScoped(L"amd_fidelityfx_upscaler_dx12.dll", "ffxDispatch");
    void* fromFg = fl::inventory::ResolveScoped(L"amd_fidelityfx_framegeneration_dx12.dll", "ffxDispatch");
    Check(fromMonolith == reinterpret_cast<void*>(GetProcAddress(monolith, "ffxDispatch")),
          "ResolveScoped returned the monolith's own export");
    Check(fromUpscaler == reinterpret_cast<void*>(GetProcAddress(upscaler, "ffxDispatch")),
          "ResolveScoped returned the upscaler leaf's own export");
    Check(fromFg == reinterpret_cast<void*>(GetProcAddress(fg, "ffxDispatch")),
          "ResolveScoped returned the frame-generation leaf's own export");
    Check(fromMonolith != nullptr && fromUpscaler != nullptr && fromFg != nullptr && fromMonolith != fromUpscaler &&
              fromUpscaler != fromFg && fromMonolith != fromFg,
          "three leaves, three DIFFERENT addresses - a name-only resolver could not tell them apart");
    void* fromLoader = fl::inventory::ResolveScoped(fl::inventory::kFfxLoaderModule, "ffxDispatch");
    Check(fromLoader == reinterpret_cast<void*>(GetProcAddress(loader, "ffxDispatch")),
          "ResolveScoped returned the loader's own export");
    Check(fromLoader != nullptr && fromLoader != fromUpscaler && fromLoader != fromFg && fromLoader != fromMonolith,
          "and it is a fourth address - the loader is patched at its own export, not at a leaf's");

    // The ABI arm, both directions.
    Check(fl::inventory::SpeaksExpectedAbi(L"amd_fidelityfx_upscaler_dx12.dll", upscaler),
          "a leaf module exporting all five names passes the AMD arm");
    Check(!fl::inventory::SpeaksFfxApi(decoy), "the sl.common.dll decoy does not speak ffx-api");
    Check(!fl::inventory::SpeaksExpectedAbi(L"amd_fidelityfx_upscaler_dx12.dll", decoy),
          "a leaf's NAME on a module without the five exports is REFUSED");

    // Absent and misspelt resolve to nothing, and nothing is loaded to find out.
    Check(fl::inventory::ResolveScoped(L"amd_fidelityfx_denoiser_dx12.dll", "ffxDispatch") == nullptr,
          "a module not in this process resolves to nullptr rather than being loaded");
    Check(GetModuleHandleW(L"amd_fidelityfx_denoiser_dx12.dll") == nullptr, "and it really was not loaded");
    Check(fl::inventory::ResolveScoped(L"amd_fidelityfx_dx12.dll", "ffxDispatchX") == nullptr,
          "a misspelt symbol resolves to nullptr - it cannot silently find a neighbour");

    // THE FORWARDING FIXTURE WORKS, which the injected [ffx] cases depend on.
    auto bind = reinterpret_cast<StubLoaderBindFn>(reinterpret_cast<void*>(GetProcAddress(loader, "FlStubLoaderBind")));
    auto loaderDispatch =
        reinterpret_cast<PfnFfxDispatch>(reinterpret_cast<void*>(GetProcAddress(loader, "ffxDispatch")));
    auto forwarded =
        reinterpret_cast<StubFfxPlainCountFn>(reinterpret_cast<void*>(GetProcAddress(loader, "FlStubLoaderForwarded")));
    auto countUp =
        reinterpret_cast<StubFfxCountFn>(reinterpret_cast<void*>(GetProcAddress(upscaler, "FlStubFfxDispatchCount")));
    auto countFg =
        reinterpret_cast<StubFfxCountFn>(reinterpret_cast<void*>(GetProcAddress(fg, "FlStubFfxDispatchCount")));
    auto countMono =
        reinterpret_cast<StubFfxCountFn>(reinterpret_cast<void*>(GetProcAddress(monolith, "FlStubFfxDispatchCount")));
    auto exportsUp = reinterpret_cast<StubFfxPlainCountFn>(
        reinterpret_cast<void*>(GetProcAddress(upscaler, "FlStubFfxExportCalls")));
    auto exportsFg =
        reinterpret_cast<StubFfxPlainCountFn>(reinterpret_cast<void*>(GetProcAddress(fg, "FlStubFfxExportCalls")));
    Check(bind != nullptr && loaderDispatch != nullptr && forwarded != nullptr && countUp != nullptr &&
              countFg != nullptr && countMono != nullptr && exportsUp != nullptr && exportsFg != nullptr,
          "the fixtures export their binding and counting entry points");
    if (bind == nullptr || loaderDispatch == nullptr || forwarded == nullptr || countUp == nullptr ||
        countFg == nullptr || countMono == nullptr || exportsUp == nullptr || exportsFg == nullptr) {
        return false;
    }

    // The descriptors come from the vendored structs, so the fixture and the
    // detour agree about the layout by construction.
    ffxDispatchDescUpscale up{};
    up.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    up.renderSize.width = kTaggedRenderW;
    up.renderSize.height = kTaggedRenderH;
    Check(loaderDispatch(nullptr, &up.header) == FFX_API_RETURN_NO_PROVIDER,
          "an UNBOUND loader answers NO_PROVIDER rather than forwarding into nothing");

    bind(upscaler, fg);
    Check(loaderDispatch(nullptr, &up.header) == FFX_API_RETURN_OK, "an UPSCALE dispatch through the loader succeeds");
    Check(countUp(FFX_API_DISPATCH_DESC_TYPE_UPSCALE) == 1u, "...and reached the UPSCALER leaf exactly once");
    Check(countFg(FFX_API_DISPATCH_DESC_TYPE_UPSCALE) == 0u, "not the frame-generation leaf");
    Check(countMono(FFX_API_DISPATCH_DESC_TYPE_UPSCALE) == 0u,
          "and not the monolith, which is outside the loader's chain");

    ffxDispatchDescFrameGenerationPrepareV2 prep{};
    prep.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prep.frameID = 1u;
    prep.renderSize.width = kTaggedRenderW;
    prep.renderSize.height = kTaggedRenderH;
    Check(loaderDispatch(nullptr, &prep.header) == FFX_API_RETURN_OK,
          "a PREPARE_V2 dispatch through the loader succeeds");
    Check(countFg(FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2) == 1u,
          "...and reached the FRAME-GENERATION leaf exactly once");
    Check(countUp(FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2) == 0u, "not the upscaler leaf");
    Check(countUp(FFX_API_DISPATCH_DESC_TYPE_UPSCALE) == 1u, "and the upscaler's own count did not move");
    Check(forwarded() == 2u, "the loader forwarded exactly the two calls that had a provider");
    // THE MEASURED SHAPE, reproduced: a forward through the loader does NOT enter the
    // leaf's ffxDispatch export. With all four modules hooked, that is what keeps a
    // dispatch on a loader-shipping title counted once rather than twice.
    Check(exportsUp() == 0u && exportsFg() == 0u,
          "neither leaf's EXPORT was entered by the forward - the loader reached the direct entry, as the real "
          "one was measured to bypass the leaf exports");

    // THE FSR 3.0 HOST, the fifth module: its own export, resolved by ITS name through
    // the host ABI arm, and the negatives that keep it from being mistaken for a leaf.
    std::printf("\n[ffx] the FSR 3.0 host facade: resolved by name through its own ABI arm, and not a leaf\n");
    const HMODULE host = LoadStubExactly(FL_STUB_FFX_FSR3_HOST);
    Check(host != nullptr, "the ffx_fsr3_x64.dll host stub loaded from its absolute path");
    if (host == nullptr) {
        return false;
    }
    Check(fl::inventory::SpeaksFsr3Host(host), "the host stub speaks the four FSR 3.0 host names");
    Check(!fl::inventory::SpeaksFsr3Host(decoy), "the sl.common.dll decoy does not");
    Check(!fl::inventory::SpeaksFsr3Host(monolith),
          "and neither does the 1.1.x monolith - the host's names are its own, so a monolith of the wrong NAME cannot "
          "pass the host arm");
    Check(!fl::inventory::SpeaksFfxApi(host), "the host does not speak ffx-api: it exports no ffxDispatch");
    Check(fl::inventory::SpeaksExpectedAbi(fl::inventory::kModuleFfxFsr3Host, host),
          "the ABI arm approves the host by its own name");
    Check(!fl::inventory::SpeaksExpectedAbi(fl::inventory::kModuleFfxFsr3Host, monolith),
          "and refuses a module of the host's name that does not speak the host ABI");
    Check(fl::inventory::FfxLeafOf(fl::inventory::kModuleFfxFsr3Host) < 0,
          "the host is NOT a leaf slot - the installer keys it by module name");
    void* fromHost =
        fl::inventory::ResolveScoped(fl::inventory::kModuleFfxFsr3Host, fl::inventory::kSymbolFfxFsr3DispatchUpscale);
    Check(fromHost != nullptr &&
              fromHost == reinterpret_cast<void*>(GetProcAddress(host, fl::inventory::kSymbolFfxFsr3DispatchUpscale)),
          "ResolveScoped returned the host's own ffxFsr3ContextDispatchUpscale");
    Check(fl::inventory::ResolveScoped(fl::inventory::kModuleFfxFsr3Host, "ffxDispatch") == nullptr,
          "and the host has no ffxDispatch to resolve");
    // The non-row, stated at runtime as well as at compile time: the export EXISTS on the
    // module and no inventory row names it (20_OPEN_QUESTIONS §H11's reversal condition).
    Check(GetProcAddress(host, "ffxFsr3DispatchFrameGeneration") != nullptr,
          "the host exports ffxFsr3DispatchFrameGeneration");
    bool fgIsARow = false;
    bool hostIsARow = false;
#define FL_PROBE_HOST_ROW(mod, sym, family)                                                                            \
    if (std::strcmp(sym, "ffxFsr3DispatchFrameGeneration") == 0) {                                                     \
        fgIsARow = true;                                                                                               \
    }                                                                                                                  \
    if (_wcsicmp(mod, fl::inventory::kModuleFfxFsr3Host) == 0) {                                                       \
        hostIsARow = true;                                                                                             \
    }
    FL_HOOK_INVENTORY(FL_PROBE_HOST_ROW)
#undef FL_PROBE_HOST_ROW
    Check(hostIsARow, "an FL_HOOK_INVENTORY row names ffx_fsr3_x64.dll");
    Check(!fgIsARow, "and no row names ffxFsr3DispatchFrameGeneration - deliberately (dllmain.cpp)");
    return g_failures == 0;
}

// ---------------------------------------------------------------------------
// A target that EVALUATES AN UPSCALER while it presents.
//
// This is what turns the hook from "installs" into "fires". --probe-upscaler-resolve
// proves the Overlay can FIND sl.interposer.dll!slEvaluateFeature; only a live
// target calling it, with the Overlay injected, proves the detour runs and the
// record carries the answer.
//
// THE CALL SHAPE. sl::FrameToken is abstract with a protected constructor
// (sl_core_types.h uses SL_STRUCT_PROTECTED_BEGIN and a pure virtual
// `operator uint32_t`), so this process cannot instantiate one -- and does not
// need to. Every parameter of PFun_slEvaluateFeature is integer-class, so a
// pointer in that slot is ABI-identical to the reference, and NOTHING
// dereferences it: the stub ignores it and the Overlay's detour reads only
// `feature` before forwarding all five arguments untouched. A dummy buffer is
// passed rather than a null so the argument is a valid address either way.
// ---------------------------------------------------------------------------
using StubSetTagFn = sl::Result(STDMETHODCALLTYPE*)(const sl::ViewportHandle&, const sl::ResourceTag*, uint32_t,
                                                    sl::CommandBuffer*);
// The token slot is integer-class (a reference is a pointer) and nothing on either
// side dereferences it, so the dummy buffer stands in exactly as it does for
// slEvaluateFeature's token below.
using StubSetTagForFrameFn = sl::Result(STDMETHODCALLTYPE*)(const void*, const sl::ViewportHandle&,
                                                            const sl::ResourceTag*, uint32_t, sl::CommandBuffer*);

using StubEvaluateFn = sl::Result(STDMETHODCALLTYPE*)(sl::Feature, const void*, const void*, uint32_t, void*);
using StubTokenFn = sl::Result(STDMETHODCALLTYPE*)(sl::FrameToken*&, const uint32_t*);

alignas(16) unsigned char g_dummyFrameToken[64]{};

int HoldPresentingUpscaled(Gfx& g, int seconds, bool real, sl::Feature feature, bool tagForFrame, bool wholeResource) {
    const HMODULE stub = LoadStubExactly(FL_STUB_SL_INTERPOSER);
    if (stub == nullptr) {
        Check(false, "the sl.interposer.dll stub loaded for the upscaled hold");
        return 1;
    }
    auto eval = reinterpret_cast<StubEvaluateFn>(reinterpret_cast<void*>(GetProcAddress(stub, "slEvaluateFeature")));
    if (eval == nullptr) {
        Check(false, "the stub exports slEvaluateFeature");
        return 1;
    }

    // TAG PER FRAME, and the first version of this tagged once at startup and
    // was VACUOUS -- 0 records carried the params bit out of 43.
    //
    // Two reasons, and both are the fixture being wrong rather than the writer.
    // The injection happens ~800 ms after this process starts, so a single tag
    // call before the loop lands before the hook exists and is never seen again:
    // the same shape main.cpp already records twice, where a fixture presented
    // before injection and every assertion went quietly vacuous.
    //
    // And it is what a real title does. sl_core_api.h's lifecycle here is
    // eValidUntilPresent -- the tag is valid UNTIL the frame is presented, so an
    // integration re-tags every frame. Tagging once was not the faithful choice
    // dressed up as a shortcut; it was simply wrong about the vendor's contract.
    auto setTag = reinterpret_cast<StubSetTagFn>(reinterpret_cast<void*>(GetProcAddress(stub, "slSetTag")));
    if (setTag == nullptr) {
        Check(false, "the stub exports slSetTag");
        return 1;
    }
    // THE 2.8 ROUTE: the same tag list through slSetTagForFrame, and NEVER through
    // slSetTag in that mode -- so an extent in the record can only have come from the
    // frame-based row, which is what the injected case for it asserts.
    auto setTagForFrame =
        reinterpret_cast<StubSetTagForFrameFn>(reinterpret_cast<void*>(GetProcAddress(stub, "slSetTagForFrame")));
    if (setTagForFrame == nullptr) {
        Check(false, "the stub exports slSetTagForFrame");
        return 1;
    }
    // Two shapes of the same statement. The extent shape is Cyberpunk's; the
    // whole-resource shape (zero extent, size on the Resource) is the one a title
    // that tags entire buffers produces, and under it an extent in the record can
    // only have come from the Resource's declared size.
    sl::Extent extent{};
    sl::Extent zero{};
    extent.width = kTaggedRenderW;
    extent.height = kTaggedRenderH;
    sl::Resource sized(sl::ResourceType::eTex2d, nullptr);
    sized.width = kTaggedRenderW;
    sized.height = kTaggedRenderH;
    sl::ResourceTag    tag(wholeResource ? &sized : nullptr, sl::kBufferTypeScalingInputColor,
                           sl::ResourceLifecycle::eValidUntilPresent, wholeResource ? &zero : &extent);
    sl::ViewportHandle viewport{0u};

    const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
    std::printf("  presenting for %d second(s) [%s], evaluating feature %u before each present\n", seconds,
                real ? "REAL" : "DXGI_PRESENT_TEST", static_cast<unsigned>(feature));
    std::printf("  tagged kBufferTypeScalingInputColor %s %ux%u through %s\n",
                wholeResource ? "whole resource, Resource size" : "extent", kTaggedRenderW, kTaggedRenderH,
                tagForFrame ? "slSetTagForFrame (Streamline 2.8, frame-based)" : "slSetTag");
    std::fflush(stdout);

    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
    long long       presented = 0;
    long long       evaluated = 0;
    while (GetTickCount64() < until) {
        // Tag, then evaluate, then present -- the order a Streamline title uses,
        // and the order that matters: the extent must be in place before the
        // evaluation the Overlay attributes it to.
        if (tagForFrame) {
            setTagForFrame(g_dummyFrameToken, viewport, &tag, 1, nullptr);
        } else {
            setTag(viewport, &tag, 1, nullptr);
        }
        // ONE EVALUATION PER PRESENT, which is what a real upscaled title does
        // and what makes the drained record's identity checkable frame by frame.
        eval(feature, g_dummyFrameToken, nullptr, 0, nullptr);
        ++evaluated;
        g.swapChain->Present(0, flags);
        ++presented;
        Sleep(8);
    }
    // Both counts to stdout so a test can compare them against records drained
    // from the ring rather than asserting "more than zero".
    std::printf("  presented=%lld evaluated=%lld\n", presented, evaluated);
    std::fflush(stdout);
    return 0;
}

// A target that generates frames: ONE kFeatureDLSS_G evaluation, then K presents.
//
// THIS IS THE SHAPE THE METRIC IS BUILT ON, and it is the one no fixture produced
// before. `--hold-presenting-upscaled` evaluates once per present, so a writer that
// counted evaluations and a writer that counted presents are indistinguishable in
// it -- every ratio is 1. Multi-frame generation is exactly the case where they
// diverge: slEvaluateFeature(kFeatureDLSS_G) fires once per APPLICATION frame and
// the vendor's swapchain emits K presents from it, so `presents / Σ evaluations`
// is K. Driving K = 1 and K = 4 through the same assertion is what makes the
// counter falsifiable rather than merely exercised.
//
// The evaluation comes BEFORE its presents, which is both what a title does and
// what the drain requires: RecordPresent consumes the word, so an evaluation
// reaches the FIRST present after it and the other K-1 legitimately carry zero.
// Those zeros are the point — a consumer that filtered them would recover
// presents == Σ, i.e. fg_factor 1.0.
int HoldPresentingFg(Gfx& g, int seconds, bool real, int presentsPerEval) {
    const HMODULE stub = LoadStubExactly(FL_STUB_SL_INTERPOSER);
    if (stub == nullptr) {
        Check(false, "the sl.interposer.dll stub loaded for the frame-generation hold");
        return 1;
    }
    auto eval = reinterpret_cast<StubEvaluateFn>(reinterpret_cast<void*>(GetProcAddress(stub, "slEvaluateFeature")));
    if (eval == nullptr) {
        Check(false, "the stub exports slEvaluateFeature");
        return 1;
    }
    // THE APPLICATION-FRAME MARKER, and since 2026-09-03 the thing the count is OF.
    // One token per application frame, as the vendor's contract says, then K
    // presents from it; the evaluation beside it is what still names DLSS-G.
    auto newToken = reinterpret_cast<StubTokenFn>(reinterpret_cast<void*>(GetProcAddress(stub, "slGetNewFrameToken")));
    if (newToken == nullptr) {
        Check(false, "the stub exports slGetNewFrameToken");
        return 1;
    }

    // THE LOCAL TAG, PASSED THROUGH slEvaluateFeature'S OWN `inputs`, and this
    // fixture is the ONLY place that route is exercised end to end.
    //
    // fl::slinputs::FindScalingInputExtent has exactly one production call site --
    // inside the detour, after the feature decode -- and until now nothing reached
    // it: every test calls the header directly, and every other injected fixture
    // passes inputs = nullptr. So the WIRING could be deleted outright and the whole
    // suite would stay green while a locally-tagging title silently lost renderW/H.
    // That mattered less when the decode arms all fell through to it; it matters now,
    // because frame generation takes a different arm and "fall through" became a
    // property somebody could reasonably tidy away.
    //
    // AND IT DISCRIMINATES: this hold NEVER calls slSetTag, so a render resolution
    // arriving in the record can only have come from this array. The global route
    // cannot produce it.
    sl::Extent extent{};
    extent.width = kTaggedRenderW;
    extent.height = kTaggedRenderH;
    sl::ResourceTag localTag(nullptr, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent,
                             &extent);
    const sl::BaseStructure* inputs[] = {&localTag};

    const int  k = presentsPerEval < 1 ? 1 : presentsPerEval;
    const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
    std::printf(
        "  presenting for %d second(s) [%s], one frame token + one kFeatureDLSS_G evaluation per %d present(s)\n",
        seconds, real ? "REAL" : "DXGI_PRESENT_TEST", k);
    std::printf("  LOCAL tag only (no slSetTag call): kBufferTypeScalingInputColor extent %ux%u\n", kTaggedRenderW,
                kTaggedRenderH);
    std::fflush(stdout);

    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
    long long       presented = 0;
    long long       evaluated = 0;
    uint32_t        frame = 0;
    while (GetTickCount64() < until) {
        // THREE REQUESTS PER FRAME, INTERLEAVED WITH THE PREVIOUS FRAME'S, as a title
        // with frames in flight does (Cyberpunk asks 3 to 4.6 times per frame from
        // more than one thread). The stub hands back a distinct object each time, so
        // a pointer-keyed writer reads three frames here; and the middle request is
        // for the PREVIOUS index, so a writer keyed on "differs from the last index"
        // reads two. Only a monotone maximum reads one, which is what the K = 1
        // control demands.
        ++frame;
        const uint32_t  previous = frame > 1u ? frame - 1u : frame;
        sl::FrameToken* token = nullptr;
        newToken(token, &frame);
        newToken(token, &previous);
        newToken(token, &frame);
        eval(sl::kFeatureDLSS_G, g_dummyFrameToken, inputs, 1u, nullptr);
        ++evaluated;
        for (int i = 0; i < k; ++i) {
            g.swapChain->Present(0, flags);
            ++presented;
            Sleep(8);
        }
    }
    std::printf("  presented=%lld evaluated=%lld presentsPerEval=%d\n", presented, evaluated, k);
    std::fflush(stdout);
    return 0;
}

// ---------------------------------------------------------------------------
// A target that dispatches through the ffx-api the way an FSR title does.
//
// TWO TOPOLOGIES, because the vendor ships two. `2x` is the SDK 2.x shape: the two
// effect-DLL leaves behind the LOADER decoy, every dispatch entering through the
// loader's export and reaching a leaf through the leaf's -- the loader-shipping
// titles, and the UE5 shape once the plugin's built-in loader is stood in for by
// ours. `1x` is the SDK 1.1.x monolith: one module, no loader, PREPARE in its pre-V2
// shape. The Overlay must read the same numbers off both, and the K = 1 control must
// read 1.0 on both -- with the loader in the chain, a writer that hooked the loader
// as well as the leaf reads 2.0 there.
//
// PER APPLICATION FRAME: one UPSCALE (renderSize = the shared tagged extent), the
// PREPARE issued TWICE with the same frameID -- the re-issue trap, the way the token
// fixture asks three times -- and, at K > 1, one FRAMEGENERATION dispatch carrying
// numGeneratedFrames = K-1 as the real swapchain's callback would, then K presents.
// --ffx-no-prepare drops the PREPARE entirely: the frame-generation-off shape, where
// the Overlay must fall back to the UPSCALE count and K = 1 must still read 1.0.
// ---------------------------------------------------------------------------
// Which module the fixture calls: 1x = the monolith, 2x = the loader (which forwards to
// the two leaves through their DIRECT entry, as measured), ue = the two leaves called
// directly with no loader in the process, the UE5 shape, fsr3host = the FSR 3.0 HOST
// DLL alone (its named UPSCALE export, no PREPARE, no frame generation -- the K = 1
// double-count control for that row), fsr3host+mono = Cyberpunk 2077's measured shape:
// the host upscales while the 1.1.x monolith prepares and generates.
enum class FfxTopology { Monolith, Loader, Ue, Fsr3Host, Fsr3HostMonolith };

using PfnFsr3DispatchUpscale = decltype(&fsr3host::ffxFsr3ContextDispatchUpscale);

int HoldPresentingFfx(Gfx& g, int seconds, bool real, int presentsPerEval, FfxTopology topology, bool prepare) {
    HMODULE upEntry = nullptr;      // where UPSCALE goes (an ffx-api module)
    HMODULE fgEntry = nullptr;      // where PREPARE and FRAMEGENERATION go
    HMODULE hostEntry = nullptr;    // where UPSCALE goes on the FSR 3.0 host topologies
    if (topology == FfxTopology::Fsr3Host || topology == FfxTopology::Fsr3HostMonolith) {
        hostEntry = LoadStubExactly(FL_STUB_FFX_FSR3_HOST);
        if (hostEntry == nullptr) {
            Check(false, "the ffx_fsr3_x64.dll host stub loaded for the ffx hold");
            return 1;
        }
        if (topology == FfxTopology::Fsr3HostMonolith) {
            fgEntry = LoadStubExactly(FL_STUB_FFX_DX12);
            if (fgEntry == nullptr) {
                Check(false, "the SDK 1.1.x monolith stub loaded beside the host for the ffx hold");
                return 1;
            }
        } else {
            // The host alone prepares nothing and generates nothing: FSR 3.0's frame
            // generation lives in ffx_frameinterpolation_x64.dll, which no row hooks. The
            // count must come from the host's UPSCALE, so PREPARE is off whatever was asked.
            prepare = false;
        }
    } else if (topology == FfxTopology::Monolith) {
        upEntry = fgEntry = LoadStubExactly(FL_STUB_FFX_DX12);
        if (upEntry == nullptr) {
            Check(false, "the SDK 1.1.x monolith stub loaded for the ffx hold");
            return 1;
        }
    } else {
        const HMODULE upscaler = LoadStubExactly(FL_STUB_FFX_UPSCALER);
        const HMODULE fg = LoadStubExactly(FL_STUB_FFX_FG);
        if (upscaler == nullptr || fg == nullptr) {
            Check(false, "the SDK 2.x leaf stubs loaded for the ffx hold");
            return 1;
        }
        if (topology == FfxTopology::Loader) {
            const HMODULE loader = LoadStubExactly(FL_STUB_FFX_LOADER);
            if (loader == nullptr) {
                Check(false, "the loader stub loaded for the ffx hold");
                return 1;
            }
            auto bind =
                reinterpret_cast<StubLoaderBindFn>(reinterpret_cast<void*>(GetProcAddress(loader, "FlStubLoaderBind")));
            if (bind == nullptr) {
                Check(false, "the loader stub exports FlStubLoaderBind");
                return 1;
            }
            bind(upscaler, fg);
            upEntry = fgEntry = loader;
        } else {
            upEntry = upscaler;
            fgEntry = fg;
        }
    }
    auto dispatchUp =
        upEntry != nullptr
            ? reinterpret_cast<PfnFfxDispatch>(reinterpret_cast<void*>(GetProcAddress(upEntry, "ffxDispatch")))
            : nullptr;
    auto dispatchFg =
        fgEntry != nullptr
            ? reinterpret_cast<PfnFfxDispatch>(reinterpret_cast<void*>(GetProcAddress(fgEntry, "ffxDispatch")))
            : nullptr;
    auto dispatchHost = hostEntry != nullptr
                            ? reinterpret_cast<PfnFsr3DispatchUpscale>(reinterpret_cast<void*>(
                                  GetProcAddress(hostEntry, fl::inventory::kSymbolFfxFsr3DispatchUpscale)))
                            : nullptr;
    if ((upEntry != nullptr && dispatchUp == nullptr) || (fgEntry != nullptr && dispatchFg == nullptr) ||
        (hostEntry != nullptr && dispatchHost == nullptr)) {
        Check(false, "the entry module(s) export their dispatch entry point");
        return 1;
    }
    // The pre-V2 PREPARE is the 1.1.x monolith's, on both topologies that use it.
    const bool  topology2x = topology == FfxTopology::Loader || topology == FfxTopology::Ue;
    const char* topologyName = topology == FfxTopology::Monolith   ? "SDK 1.1.x (monolith)"
                               : topology == FfxTopology::Loader   ? "SDK 2.x (two leaves behind the loader)"
                               : topology == FfxTopology::Ue       ? "UE5 (two leaves, no loader)"
                               : topology == FfxTopology::Fsr3Host ? "FSR 3.0 host DLL alone"
                                                                   : "FSR 3.0 host DLL + SDK 1.1.x monolith";

    const int  k = presentsPerEval < 1 ? 1 : presentsPerEval;
    const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
    std::printf("  presenting for %d second(s) [%s], ffx-api %s, one UPSCALE%s per %d present(s)%s\n", seconds,
                real ? "REAL" : "DXGI_PRESENT_TEST", topologyName, prepare ? " + PREPARE x2" : " (no PREPARE)", k,
                k > 1 ? " + one FRAMEGENERATION" : "");
    std::printf("  renderSize %ux%u on every UPSCALE%s\n", kTaggedRenderW, kTaggedRenderH,
                prepare ? " and PREPARE" : "");
    std::fflush(stdout);

    // Built from the vendored structs, so the fixture and the detour agree about the
    // layout by construction. The stub reads only the head; the Overlay reads the body.
    ffxDispatchDescUpscale up{};
    up.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    up.renderSize.width = kTaggedRenderW;
    up.renderSize.height = kTaggedRenderH;
    up.upscaleSize.width = 1920u;
    up.upscaleSize.height = 1080u;

    // PREPARE in the shape each generation sends -- V2 for SDK 2.x, the original type
    // for the 1.1.x monolith -- built in the V2 struct either way, because the two
    // share their layout through renderSize (the Overlay asserts that at compile time).
    ffxDispatchDescFrameGenerationPrepareV2 prep{};
    prep.header.type = topology2x ? FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2
                                  : FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
    prep.renderSize = up.renderSize;

    ffxDispatchDescFrameGeneration gen{};
    gen.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    gen.numGeneratedFrames = static_cast<uint32_t>(k - 1);

    // The FSR 3.0 HOST descriptor, from the vendored fsr3-v3.0.4 struct: the same tagged
    // extent, in the field the Overlay pins at offset 1256. Nothing else is set, and the
    // stub reads nothing at all, so a record carrying this size proves the Overlay read it
    // off the argument.
    fsr3host::FfxFsr3DispatchUpscaleDescription hostUp{};
    hostUp.renderSize.width = kTaggedRenderW;
    hostUp.renderSize.height = kTaggedRenderH;

    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
    long long       presented = 0;
    long long       frames = 0;
    uint64_t        frameId = 0;
    while (GetTickCount64() < until) {
        ++frameId;
        if (dispatchHost != nullptr) {
            dispatchHost(nullptr, &hostUp);
        } else {
            dispatchUp(nullptr, &up.header);
        }
        if (prepare && dispatchFg != nullptr) {
            // TWICE, SAME frameID: the SDK's own sample re-issues the prepare when its
            // configuration changes, and the vendor's contract is about the INDEX. A
            // writer that counted calls reads 2.0 at K = 1 here.
            prep.frameID = frameId;
            dispatchFg(nullptr, &prep.header);
            dispatchFg(nullptr, &prep.header);
        }
        if (k > 1 && dispatchFg != nullptr) {
            gen.frameID = frameId;
            dispatchFg(nullptr, &gen.header);
        }
        ++frames;
        for (int i = 0; i < k; ++i) {
            g.swapChain->Present(0, flags);
            ++presented;
            Sleep(8);
        }
    }
    std::printf("  presented=%lld frames=%lld presentsPerEval=%d\n", presented, frames, k);
    std::fflush(stdout);
    return 0;
}

// What a frame IS, asserted against DXGI's own counter.
//
// GetLastPresentCount is the one oracle in this area that does not share the
// writer's assumption: DXGI computes it, we do not. Both directions are checked,
// because a writer that counts everything and a writer that counts nothing are
// each wrong in one of them.
bool ProbeFrameIdentity(Gfx& g) {
    std::printf("\n[frames] what counts as a frame\n");

    UINT before = 0;
    if (FAILED(g.swapChain1->GetLastPresentCount(&before))) {
        Check(false, "GetLastPresentCount is unavailable — the oracle this probe rests on");
        return false;
    }

    constexpr int kTest = 500;
    PresentLoop(g, kTest, /*real=*/false);
    UINT afterTest = 0;
    g.swapChain1->GetLastPresentCount(&afterTest);
    const bool testIsNotAFrame = (afterTest == before);
    Check(testIsNotAFrame, "DXGI_PRESENT_TEST submits no frames");
    std::printf("    %d test-present(s) moved the counter by %u\n", kTest, afterTest - before);

    constexpr int kReal = 37;
    PresentLoop(g, kReal, /*real=*/true);
    UINT afterReal = 0;
    g.swapChain1->GetLastPresentCount(&afterReal);
    const bool realIsAFrame = (afterReal - afterTest) == static_cast<UINT>(kReal);
    Check(realIsAFrame, "a real present submits exactly one frame");
    std::printf("    %d real present(s) moved the counter by %u\n", kReal, afterReal - afterTest);

    // The two together are the contract. Separately, each passes against a
    // wrong writer: a writer that ignores everything satisfies the first, and
    // one that counts everything satisfies the second.
    return testIsNotAFrame && realIsAFrame;
}

}    // namespace

int main(int argc, char** argv) {
    std::printf("FrameLedger hook-harness (WARP, headless — no GPU or window required)\n");

    Gfx g;
    if (!CreateGfx(g)) {
        std::printf("FAILED: could not create the graphics objects\n");
        return 2;
    }

    // Probe return values are propagated, not discarded. Today every early
    // return is preceded by a Check(false, ...) so g_failures would catch it
    // anyway — but relying on that makes the exit code depend on each probe
    // remembering to log its own failure. A probe that returns false must fail
    // the ctest whether or not it said so.
    bool ok = true;
    bool ranSomething = false;
    // --real applies to --present and --hold, wherever it appears on the line.
    bool real = false;
    int  plusUi = 0;

    // MILLISECONDS BETWEEN PRESENTS in the --hold-presenting modes. 8 is ~120/s and is the default for
    // the reason the mode's own comment gives: an uncapped WARP loop pegs a core and starves the very
    // injector under test on a shared runner.
    //
    // IT IS A KNOB BECAUSE ONE TEST CANNOT BE WRITTEN WITHOUT IT. The Agent's drop accounting fires
    // when the writer laps the reader -- writeIndex - readIndex > FL_SHM_DEFAULT_CAPACITY, i.e. 8192
    // records -- and at 120/s that is 68 SECONDS of a reader deliberately not draining. So the branch
    // 04_CAPTURE calls "the Agent stalled for over ~16 s" had never been driven against the real
    // Overlay in either language; the only tests that touch it build their own writer.
    //
    // MEASURED, not assumed: `hook-harness --real --present 20000` completes in 0.62 s INCLUDING
    // device creation, so an uncapped hold laps the ring in well under a second and the stall the test
    // has to sit through is seconds rather than minutes.
    int presentIntervalMs = 8;

    // Presents per kFeatureDLSS_G evaluation for --hold-presenting-fg. 1 is the
    // no-frame-generation shape and is the control: it and K = 4 have to be told
    // apart by one assertion, or the assertion is not measuring the counter.
    int presentsPerEval = 1;

    // Which vendor shape --hold-presenting-ffx stands in, and whether the title
    // issues a PREPARE at all. Pre-pass arguments, like --presents-per-eval, so one
    // hold mode drives every combination and the tests assert one expression.
    FfxTopology ffxTopology = FfxTopology::Loader;
    bool        ffxPrepare = true;
    bool        slTagForFrame = false;
    bool        slTagWholeResource = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--real") == 0) {
            real = true;
        } else if (std::strcmp(argv[i], "--plus-ui") == 0 && i + 1 < argc) {
            plusUi = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--present-interval-ms") == 0 && i + 1 < argc) {
            presentIntervalMs = std::atoi(argv[++i]);
            if (presentIntervalMs < 0) {
                presentIntervalMs = 0;
            }
        } else if (std::strcmp(argv[i], "--presents-per-eval") == 0 && i + 1 < argc) {
            // The FG factor the fixture EMITS. A pre-pass argument rather than a
            // second positional, so one hold mode drives both K = 1 and K = 4 and a
            // test can assert the SAME expression against both -- which is what
            // makes the pair discriminating instead of two separate acceptances.
            presentsPerEval = std::atoi(argv[++i]);
            if (presentsPerEval < 1) {
                presentsPerEval = 1;
            }
        } else if (std::strcmp(argv[i], "--ffx-topology") == 0 && i + 1 < argc) {
            const char* t = argv[++i];
            ffxTopology = std::strcmp(t, "1x") == 0              ? FfxTopology::Monolith
                          : std::strcmp(t, "ue") == 0            ? FfxTopology::Ue
                          : std::strcmp(t, "fsr3host") == 0      ? FfxTopology::Fsr3Host
                          : std::strcmp(t, "fsr3host+mono") == 0 ? FfxTopology::Fsr3HostMonolith
                                                                 : FfxTopology::Loader;
        } else if (std::strcmp(argv[i], "--ffx-no-prepare") == 0) {
            ffxPrepare = false;
        } else if (std::strcmp(argv[i], "--sl-tag-for-frame") == 0) {
            slTagForFrame = true;
        } else if (std::strcmp(argv[i], "--sl-tag-whole-resource") == 0) {
            slTagWholeResource = true;
        }
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--real") == 0 || std::strcmp(argv[i], "--plus-ui") == 0 ||
            std::strcmp(argv[i], "--present-interval-ms") == 0 || std::strcmp(argv[i], "--presents-per-eval") == 0 ||
            std::strcmp(argv[i], "--ffx-topology") == 0 || std::strcmp(argv[i], "--ffx-no-prepare") == 0 ||
            std::strcmp(argv[i], "--sl-tag-for-frame") == 0 || std::strcmp(argv[i], "--sl-tag-whole-resource") == 0) {
            if (std::strcmp(argv[i], "--plus-ui") == 0 || std::strcmp(argv[i], "--present-interval-ms") == 0 ||
                std::strcmp(argv[i], "--presents-per-eval") == 0 || std::strcmp(argv[i], "--ffx-topology") == 0) {
                ++i;
            }
            continue;    // consumed above
        } else if (std::strcmp(argv[i], "--probe-frames") == 0) {
            ok = ProbeFrameIdentity(g) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-fg") == 0 && i + 1 < argc) {
            ok = HoldPresentingFg(g, std::atoi(argv[++i]), real, presentsPerEval) == 0 && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-ffx") == 0 && i + 1 < argc) {
            ok = HoldPresentingFfx(g, std::atoi(argv[++i]), real, presentsPerEval, ffxTopology, ffxPrepare) == 0 && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-upscaled") == 0 && i + 1 < argc) {
            ok = HoldPresentingUpscaled(g, std::atoi(argv[++i]), real, sl::kFeatureDLSS, slTagForFrame,
                                        slTagWholeResource) == 0 &&
                 ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-upscaled-unknown") == 0 && i + 1 < argc) {
            // The SAME target, evaluating a feature id the Overlay does not
            // decode. It must report FL_UPSCALER_UNKNOWN with FL_MEASURED_UPSCALER
            // SET -- "a hook ran and could not identify what it saw" -- and must
            // never report FL_UPSCALER_NONE, which is the only state that may be
            // aggregated as a negative. 0xF00D is not a Streamline feature.
            ok = HoldPresentingUpscaled(g, std::atoi(argv[++i]), real, static_cast<sl::Feature>(0xF00Du), slTagForFrame,
                                        slTagWholeResource) == 0 &&
                 ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-upscaler-resolve") == 0) {
            ok = ProbeUpscalerResolve() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-ffx-resolve") == 0) {
            ok = ProbeFfxResolve() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-sl-inputs") == 0) {
            ok = ProbeSlInputs() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-sl-seen") == 0) {
            ok = ProbeSlSeen() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-sl-abi") == 0) {
            ok = ProbeSlAbi() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-d3d12") == 0) {
            ok = ProbeD3D12Acquisition() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-d3d12-vtable") == 0) {
            ok = ProbeD3D12VtableIndices() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-dxr") == 0) {
            ok = ProbeDxr() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-dxr-inputs") == 0) {
            ok = ProbeDxrInputs() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-dxr") == 0 && i + 1 < argc) {
            ok = HoldPresentingDxr(std::atoi(argv[++i]), real, true, presentIntervalMs) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-rayquery") == 0 && i + 1 < argc) {
            // Builds acceleration structures and NEVER dispatches -- the observable
            // signature of an inline-RayQuery title, which is what makes
            // 03_METRICS:226's claim about the AS-build hook falsifiable.
            ok = HoldPresentingDxr(std::atoi(argv[++i]), real, false, presentIntervalMs) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-vtable") == 0) {
            ok = ProbeH4_VtableIndices(g) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-proxy") == 0) {
            ok = ProbeH5_ProxySwapChain(g) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-unhook") == 0) {
            ok = ProbeH7_UnhookPreservesForeign(g) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-cost") == 0) {
            ok = ProbeCost_PerPresent(g) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold") == 0 && i + 1 < argc) {
            // A live target for the injection tests. --present alone is not
            // enough: 100,000 WARP presents take ~30 ms, so the process is gone
            // long before anything can inject into it, and the test then fails
            // for a reason that has nothing to do with injection.
            ok = PresentLoop(g, 240, real) == 0 && ok;
            std::printf("  holding for %d second(s)\n", std::atoi(argv[i + 1]));
            std::fflush(stdout);
            Sleep(static_cast<DWORD>(std::atoi(argv[++i])) * 1000);
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-d3d12") == 0 && i + 1 < argc) {
            // The same shape as --hold-presenting, but the swapchain is created
            // from a D3D12 COMMAND QUEUE rather than a D3D11 device.
            //
            // That difference is the whole point. One hook on the shared
            // dxgi.dll class vtable catches both, so the present call cannot tell
            // them apart -- the Overlay has to ask the swapchain which device
            // made it, and this is the fixture that proves it answers D3D12
            // rather than the D3D11 it used to hardcode.
            const int           seconds = std::atoi(argv[++i]);
            IDXGIFactory4*      f = nullptr;
            IDXGIAdapter*       warp = nullptr;
            ID3D12Device*       dev = nullptr;
            ID3D12CommandQueue* queue = nullptr;
            IDXGISwapChain1*    sc = nullptr;
            bool                built = false;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&f))) &&
                SUCCEEDED(f->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&warp))) &&
                SUCCEEDED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                            reinterpret_cast<void**>(&dev)))) {
                D3D12_COMMAND_QUEUE_DESC qd{};
                qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                if (SUCCEEDED(
                        dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue)))) {
                    DXGI_SWAP_CHAIN_DESC1 d{};
                    d.Width = 64;
                    d.Height = 64;
                    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    d.SampleDesc.Count = 1;
                    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                    d.BufferCount = 2;
                    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                    d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
                    built = SUCCEEDED(f->CreateSwapChainForComposition(queue, &d, nullptr, &sc)) && sc != nullptr;
                }
            }
            if (!built) {
                Check(false, "d3d12 swapchain for --hold-presenting-d3d12");
                ok = false;
            } else {
                const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
                std::printf("  presenting D3D12 for %d second(s) [%s]\n", seconds, real ? "REAL" : "DXGI_PRESENT_TEST");
                std::fflush(stdout);
                const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
                long long       presented = 0;
                while (GetTickCount64() < until) {
                    sc->Present(0, flags);
                    ++presented;
                    Sleep(8);
                }
                std::printf("  presented=%lld\n", presented);
                std::fflush(stdout);
            }
            if (sc != nullptr) {
                sc->Release();
            }
            if (queue != nullptr) {
                queue->Release();
            }
            if (dev != nullptr) {
                dev->Release();
            }
            if (warp != nullptr) {
                warp->Release();
            }
            if (f != nullptr) {
                f->Release();
            }
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting") == 0 && i + 1 < argc) {
            // WHAT --hold CANNOT DO, and why the injection tests needed this.
            //
            // --hold presents 240 frames and THEN sleeps. Those 240 are over in
            // milliseconds, while fl_guard_test injects ~800 ms after spawn -- so
            // an Overlay injected into --hold installs its present hook into a
            // process that has already stopped presenting and observes exactly
            // ZERO frames. Every "N presents -> N records" assertion written
            // against --hold would have been vacuous, which is the same shape as
            // the DXGI_PRESENT_TEST defect this harness was fixed for once
            // already.
            //
            // This mode presents for the WHOLE hold, at a deliberately modest
            // cadence by default: an uncapped WARP loop pegs a core and starves the
            // very injector under test on a shared runner.
            //
            // --present-interval-ms 0 removes the cap, and exists for exactly one
            // test: the Agent's drop accounting needs the writer to LAP the reader,
            // which at ~120/s means 68 s of not draining. See the flag's own note.
            const int  seconds = std::atoi(argv[++i]);
            const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
            std::printf("  presenting for %d second(s) [%s] every %d ms\n", seconds,
                        real ? "REAL" : "DXGI_PRESENT_TEST", presentIntervalMs);
            std::fflush(stdout);
            const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
            long long       presented = 0;
            while (GetTickCount64() < until) {
                g.swapChain->Present(0, flags);
                ++presented;
                if (presentIntervalMs > 0) {
                    Sleep(static_cast<DWORD>(presentIntervalMs));
                } else {
                    // Yield rather than spin: an uncapped loop that never gives up its
                    // quantum starves the reader we are trying to make fall behind, which
                    // would make the test slower rather than faster.
                    Sleep(0);
                }
            }
            // The count goes to stdout so a test can compare it against records
            // drained from the ring rather than asserting "more than zero".
            std::printf("  presented=%lld\n", presented);
            std::fflush(stdout);
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-overflow") == 0 && i + 1 < argc) {
            // Drives the Overlay's swapchain table PAST its fixed capacity, which
            // is the one path where the writer knows it has no output size and
            // used to claim one anyway (20_OPEN_QUESTIONS §S29(g)).
            //
            // dllmain.cpp's FindOrAdd is a fixed 16-slot linear scan and returns
            // nullptr once they are taken; RecordPresent then leaves outputW/H at
            // zero. It set FL_MEASURED_OUTPUT_RES regardless, so the record said
            // "output resolution MEASURED: 0 x 0" -- and 03_METRICS computes the
            // upscale ratio from exactly those two fields.
            //
            // WHY A NEW MODE AND NOT --plus-ui: that flag creates ONE extra
            // swapchain, which is a second stream, not an overflow. Nothing in
            // the harness could reach slot 17 before this.
            //
            // The extra chains are kept ALIVE for the whole hold on purpose. The
            // Overlay keys its table on the raw pointer and never evicts, so a
            // released chain whose address a later allocation reuses would alias
            // an existing slot and quietly un-overflow the test.
            const int  seconds = std::atoi(argv[++i]);
            const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
            // One more than the Overlay's kMaxSwapChains, minus the primary that
            // already occupies a slot: 15 fill the table, the 16th overflows.
            constexpr int kExtras = 16;

            std::vector<IDXGISwapChain*> extras;
            extras.reserve(kExtras);
            for (int k = 0; k < kExtras; ++k) {
                IDXGISwapChain* sc = CreateSecondSwapChain(g);
                if (sc == nullptr) {
                    Check(false, "could not create an extra swapchain for the overflow mode");
                    ok = false;
                    break;
                }
                extras.push_back(sc);
            }

            if (extras.size() == static_cast<size_t>(kExtras)) {
                // ROUND-ROBIN FOR THE WHOLE HOLD, and the first version of this
                // mode got it wrong in a way worth keeping written down.
                //
                // It filled the table once at startup and then held on the 17th
                // chain. But the Overlay is injected ~800 ms LATER and only ever
                // sees presents that happen after it hooks — so it observed an
                // empty table, gave the "overflowed" chain slot 1, and every
                // record came back with a real id. The test's own vacuity guard
                // caught it: 206 records drained, 0 from an overflowed stream.
                //
                // Presenting on all 17 for the whole hold means the Overlay sees
                // 17 distinct chains whenever it attaches, fills its 16 slots in
                // the order it meets them, and the last one it meets can never
                // get one.
                std::vector<IDXGISwapChain*> chains;
                chains.reserve(extras.size() + 1);
                chains.push_back(g.swapChain);
                for (auto* sc : extras) {
                    chains.push_back(sc);
                }
                std::printf("  presenting on %zu swapchains for %d second(s) [%s]; the Overlay holds 16\n",
                            chains.size(), seconds, real ? "REAL" : "DXGI_PRESENT_TEST");
                std::fflush(stdout);

                const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
                long long       presented = 0;
                while (GetTickCount64() < until) {
                    for (auto* sc : chains) {
                        sc->Present(0, flags);
                        ++presented;
                    }
                    Sleep(8);
                }
                std::printf("  presented=%lld\n", presented);
                std::fflush(stdout);
            }

            for (auto* sc : extras) {
                sc->Release();
            }
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--present") == 0 && i + 1 < argc) {
            const int frames = std::atoi(argv[++i]);
            ok = PresentLoop(g, frames, real) == 0 && ok;
            // A SECOND present stream in the same process. The hook patches the
            // shared dxgi.dll class vtable, so it sees both — and the ring has no
            // field that says which. The fixture exists so that assertion can be
            // written; nothing consumes it until the record carries a
            // discriminator.
            if (plusUi > 0) {
                IDXGISwapChain* ui = CreateSecondSwapChain(g);
                if (ui == nullptr) {
                    Check(false, "could not create the second swapchain");
                    ok = false;
                } else {
                    for (int k = 0; k < plusUi; ++k) {
                        ui->Present(0, real ? 0u : DXGI_PRESENT_TEST);
                    }
                    std::printf("  a SECOND swapchain presented %d frame(s) - same process, same vtable\n", plusUi);
                    ui->Release();
                }
            }
            ranSomething = true;
        } else {
            std::printf("FAILED: unrecognised argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if (!ranSomething) {
        ok = ProbeH4_VtableIndices(g) && ok;
        ok = ProbeH5_ProxySwapChain(g) && ok;
        ok = ProbeH7_UnhookPreservesForeign(g) && ok;
    }

    const bool passed = ok && g_failures == 0;
    std::printf("\n%s (%d failure(s))\n", passed ? "HARNESS OK" : "HARNESS FAILURES", g_failures);
    return passed ? 0 : 1;
}
