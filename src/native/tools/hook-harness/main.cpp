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

#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <gl/GL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "proxy_swapchain.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "opengl32.lib")

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
constexpr unsigned kPresentIndex = 8;
constexpr unsigned kResizeBuffersIndex = 13;
constexpr unsigned kPresent1Index = 22;

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

// OpenGL hold. Returns false only for a real failure; a missing GL stack exits
// the process with 3 so a caller can tell "not available" from "broken".
bool RunGlHold(int seconds, bool real) {
    std::printf("\n[gl] hidden-window OpenGL context\n");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FlHarnessGL";
    RegisterClassExW(&wc);

    // WS_POPUP with no WS_VISIBLE: created, never shown. It still needs a window
    // station, which is the part that can be absent on a hosted runner.
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance,
                                nullptr);
    if (hwnd == nullptr) {
        std::printf("  OpenGL UNAVAILABLE: CreateWindowExW failed (%lu)\n", GetLastError());
        std::exit(3);
    }

    HDC hdc = GetDC(hwnd);
    if (hdc == nullptr) {
        std::printf("  OpenGL UNAVAILABLE: GetDC failed\n");
        std::exit(3);
    }

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    const int fmt = ChoosePixelFormat(hdc, &pfd);
    if (fmt == 0 || !SetPixelFormat(hdc, fmt, &pfd)) {
        std::printf("  OpenGL UNAVAILABLE: no usable pixel format (%lu)\n", GetLastError());
        std::exit(3);
    }

    HGLRC rc = wglCreateContext(hdc);
    if (rc == nullptr || !wglMakeCurrent(hdc, rc)) {
        std::printf("  OpenGL UNAVAILABLE: wglCreateContext/MakeCurrent failed (%lu)\n", GetLastError());
        std::exit(3);
    }
    std::printf("  GL_VERSION = %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // wglSwapBuffers, NOT the GDI SwapBuffers. 17_HOOK_ENGINE §Hook inventory
    // names the wgl entry point, and that is what the Overlay hooks; presenting
    // through the GDI one would exercise a different symbol and the count would
    // be a lie about what we intercept.
    std::printf("  presenting GL for %d second(s) [%s]\n", seconds, real ? "REAL" : "no-op");
    std::fflush(stdout);
    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
    long long       presented = 0;
    // Resolved by name rather than by import: wglSwapBuffers is declared behind
    // header guards that WIN32_LEAN_AND_MEAN-style builds do not always expose,
    // and going through GetProcAddress is exactly what the Overlay does to hook
    // it -- so the harness and the hook are talking about the same symbol by
    // construction rather than by assumption.
    using PFN_wglSwapBuffers = BOOL(WINAPI*)(HDC);
    auto swap = reinterpret_cast<PFN_wglSwapBuffers>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"), "wglSwapBuffers")));
    if (swap == nullptr) {
        std::printf("  OpenGL UNAVAILABLE: opengl32 exports no wglSwapBuffers\n");
        std::exit(3);
    }
    while (GetTickCount64() < until) {
        glClear(GL_COLOR_BUFFER_BIT);
        swap(hdc);
        ++presented;
        Sleep(8);
    }
    std::printf("  presented=%lld\n", presented);
    std::fflush(stdout);

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return true;
}

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
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--real") == 0) {
            real = true;
        } else if (std::strcmp(argv[i], "--plus-ui") == 0 && i + 1 < argc) {
            plusUi = std::atoi(argv[++i]);
        }
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--real") == 0 || std::strcmp(argv[i], "--plus-ui") == 0) {
            if (std::strcmp(argv[i], "--plus-ui") == 0) {
                ++i;
            }
            continue;    // consumed above
        } else if (std::strcmp(argv[i], "--probe-frames") == 0) {
            ok = ProbeFrameIdentity(g) && ok;
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
        } else if (std::strcmp(argv[i], "--hold-presenting-gl") == 0 && i + 1 < argc) {
            // OpenGL cannot use the two tricks that make the D3D modes headless.
            // There is no WARP equivalent to force, and wglCreateContext needs a
            // real HDC with a pixel format, which means a real HWND. So this
            // creates a HIDDEN window: no WS_VISIBLE, never shown, 64x64.
            //
            // That is a genuine environment dependency, and it is reported rather
            // than hidden: exit code 3 means "OpenGL is not available here",
            // which is a different fact from "the hook is broken". A test that
            // could not tell those apart would pass on a machine with no GL at
            // all, which is the shape this repository keeps finding.
            const int seconds = std::atoi(argv[++i]);
            ok = RunGlHold(seconds, real) && ok;
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
            // cadence: an uncapped WARP loop pegs a core and starves the very
            // injector under test on a shared runner.
            const int  seconds = std::atoi(argv[++i]);
            const UINT flags = real ? 0u : DXGI_PRESENT_TEST;
            std::printf("  presenting for %d second(s) [%s]\n", seconds, real ? "REAL" : "DXGI_PRESENT_TEST");
            std::fflush(stdout);
            const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
            long long       presented = 0;
            while (GetTickCount64() < until) {
                g.swapChain->Present(0, flags);
                ++presented;
                Sleep(8);    // ~120/s, enough to be measurable without pegging a core
            }
            // The count goes to stdout so a test can compare it against records
            // drained from the ring rather than asserting "more than zero".
            std::printf("  presented=%lld\n", presented);
            std::fflush(stdout);
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
