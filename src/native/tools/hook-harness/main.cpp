// hook-harness — a dummy D3D11 app for exercising hook paths with no game and
// no anti-cheat surface at all (17_HOOK_ENGINE §Test harness, 14_TESTING).
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
//   --present N      present N frames (for later overhead measurement)

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "proxy_swapchain.h"

#pragma comment(lib, "d3d11.lib")
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

int PresentLoop(Gfx& g, int frames) {
    for (int i = 0; i < frames; ++i) {
        g.swapChain->Present(0, DXGI_PRESENT_TEST);
    }
    std::printf("  presented %d frames\n", frames);
    return 0;
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
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--probe-vtable") == 0) {
            ok = ProbeH4_VtableIndices(g) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--probe-proxy") == 0) {
            ok = ProbeH5_ProxySwapChain(g) && ok;
            ranSomething = true;
        } else if (std::strcmp(argv[i], "--present") == 0 && i + 1 < argc) {
            ok = PresentLoop(g, std::atoi(argv[++i])) == 0 && ok;
            ranSomething = true;
        } else {
            std::printf("FAILED: unrecognised argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if (!ranSomething) {
        ok = ProbeH4_VtableIndices(g) && ok;
        ok = ProbeH5_ProxySwapChain(g) && ok;
    }

    const bool passed = ok && g_failures == 0;
    std::printf("\n%s (%d failure(s))\n", passed ? "HARNESS OK" : "HARNESS FAILURES", g_failures);
    return passed ? 0 : 1;
}
