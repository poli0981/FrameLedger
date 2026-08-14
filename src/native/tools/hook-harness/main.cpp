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

#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fl_dxgi_vtable.h"
#include "fl_hook_inventory.h"
#include "fl_sl_inputs.h"
#include "proxy_swapchain.h"

// Vendored MIT Streamline headers, for the feature ids and the ABI of the call
// the upscaled-hold mode makes. Types only, never linked -- the same rule the
// Overlay follows (third_party/streamline/README.md).
#include <sl.h>

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
              "a whole-resource tag yields the honest unknown, not a 0x0 resolution");
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
<<<<<<< HEAD
=======
// The render extent the upscaled hold tags. A constant here rather than a
// literal typed twice: guard_test.cpp asserts against it, and a fixture and its
// assertion holding separate copies of the number under test is the §S29(b)
// defect the inventory macro exists to prevent, in miniature.
//
// 1280x720 is arbitrary, and that is the point -- a writer that hardcoded a
// plausible render resolution must FAIL here rather than coincide.
constexpr unsigned kTaggedRenderW = FL_TAGGED_RENDER_W;
constexpr unsigned kTaggedRenderH = FL_TAGGED_RENDER_H;

>>>>>>> origin/main
using StubSetTagFn = sl::Result(STDMETHODCALLTYPE*)(const sl::ViewportHandle&, const sl::ResourceTag*, uint32_t,
                                                    sl::CommandBuffer*);

using StubEvaluateFn = sl::Result(STDMETHODCALLTYPE*)(sl::Feature, const void*, const void*, uint32_t, void*);

alignas(16) unsigned char g_dummyFrameToken[64]{};

int HoldPresentingUpscaled(Gfx& g, int seconds, bool real, sl::Feature feature) {
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
    sl::Extent extent{};
    extent.width = kTaggedRenderW;
    extent.height = kTaggedRenderH;
    sl::ResourceTag tag(nullptr, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &extent);
    sl::ViewportHandle viewport{0u};

    const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
    std::printf("  presenting for %d second(s) [%s], evaluating feature %u before each present\n", seconds,
                real ? "REAL" : "DXGI_PRESENT_TEST", static_cast<unsigned>(feature));
    std::printf("  tagged kBufferTypeScalingInputColor extent %ux%u\n", kTaggedRenderW, kTaggedRenderH);
    std::fflush(stdout);

    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
    long long       presented = 0;
    long long       evaluated = 0;
    while (GetTickCount64() < until) {
        // Tag, then evaluate, then present -- the order a Streamline title uses,
        // and the order that matters: the extent must be in place before the
        // evaluation the Overlay attributes it to.
        setTag(viewport, &tag, 1, nullptr);
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
        }
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--real") == 0 || std::strcmp(argv[i], "--plus-ui") == 0 ||
            std::strcmp(argv[i], "--present-interval-ms") == 0) {
            if (std::strcmp(argv[i], "--plus-ui") == 0 || std::strcmp(argv[i], "--present-interval-ms") == 0) {
                ++i;
            }
            continue;    // consumed above
        } else if (std::strcmp(argv[i], "--probe-frames") == 0) {
            ok = ProbeFrameIdentity(g) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-upscaled") == 0 && i + 1 < argc) {
            ok = HoldPresentingUpscaled(g, std::atoi(argv[++i]), real, sl::kFeatureDLSS) == 0 && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--hold-presenting-upscaled-unknown") == 0 && i + 1 < argc) {
            // The SAME target, evaluating a feature id the Overlay does not
            // decode. It must report FL_UPSCALER_UNKNOWN with FL_MEASURED_UPSCALER
            // SET -- "a hook ran and could not identify what it saw" -- and must
            // never report FL_UPSCALER_NONE, which is the only state that may be
            // aggregated as a negative. 0xF00D is not a Streamline feature.
            ok = HoldPresentingUpscaled(g, std::atoi(argv[++i]), real, static_cast<sl::Feature>(0xF00Du)) == 0 && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-upscaler-resolve") == 0) {
            ok = ProbeUpscalerResolve() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-sl-inputs") == 0) {
            ok = ProbeSlInputs() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-sl-abi") == 0) {
            ok = ProbeSlAbi() && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-d3d12") == 0) {
            ok = ProbeD3D12Acquisition() && ok;
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
