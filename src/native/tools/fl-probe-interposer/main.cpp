// fl-probe-interposer — does a vtable-slot present hook see a Streamline title's
// presents? (docs/20_OPEN_QUESTIONS.md §H5 case 3, docs/spike-notes.md §5.)
//
// WHY THIS EXISTS, and why it runs in OUR OWN PROCESS.
//
// docs/03_METRICS.md §Counting native vs displayed defines F_disp as "presents
// observed by the hook" and F_app as "presents - sum(fgEvaluations)". Both rest on
// one unverified assumption: that the object a game calls Present on is an
// instance of the class whose shared dxgi.dll vtable we patched. If a Streamline
// title's swapchain is a DIFFERENT class, a real-vtable hook sees nothing, F_disp
// == F_app, and fg_factor is STRUCTURALLY 1.0 -- the same failure the
// GetFrameStatistics rung was deleted for (03_METRICS "not conservative; silently
// wrong"), landing on the exact metric CLAUDE.md rule 6 exists to protect.
//
// hook-harness cannot answer it. Its --probe-proxy builds OUR OWN forwarding
// wrapper, which is the easy case by construction; §H5 lists the interposer as
// the case the harness structurally cannot reach.
//
// So this probe loads the real sl.interposer.dll -- from a title the user
// already has installed, named on the command line, never shipped by us -- into
// a process we own, and compares vtables. No game runs, no injection happens, no
// guard is involved, and nothing is written anywhere.
//
// IT NEEDS NO VENDOR HEADERS. Everything here is GetProcAddress plus DXGI types
// from the Windows SDK, so it does not touch the question
// legal/THIRD_PARTY_NOTICES.md settles for Intel IGCL -- "re-declaring the API by
// hand is explicitly NOT an approved workaround". Nothing is re-declared: the
// interposer's CreateDXGIFactory* have the same signatures as dxgi.dll's, which
// is the whole point of an interposer.

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <cwchar>
#include <psapi.h>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
}

// The vtable pointer of a COM object: the first machine word of the instance.
// This is the same read docs/17_HOOK_ENGINE.md §Getting vtable addresses makes,
// and the same one MinHook would patch through.
const void* VtableOf(void* comObject) {
    if (comObject == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<void* const*>(comObject);
}

struct Gfx {
    ID3D11Device*        device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    ~Gfx() {
        if (context != nullptr) {
            context->Release();
        }
        if (device != nullptr) {
            device->Release();
        }
    }
};

// WARP, so this needs no GPU; composition, so it needs no HWND or interactive
// window station. Same two choices hook-harness makes, and for the same reason.
bool CreateDevice(Gfx& g) {
    D3D_FEATURE_LEVEL got{};
    const HRESULT     hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                             nullptr, 0, D3D11_SDK_VERSION, &g.device, &got, &g.context);
    if (FAILED(hr)) {
        std::printf("  D3D11CreateDevice(WARP) failed: 0x%08lX\n", static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

DXGI_SWAP_CHAIN_DESC1 CompositionDesc() {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 64;
    desc.Height = 64;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    return desc;
}

using PFN_CreateDXGIFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);

// Which sl.* plugins are mapped into THIS process right now.
//
// This is the difference between an answer and a confident wrong answer. The
// first run of this probe reported "same vtable" for two titles and the result
// was worthless: the FACTORY vtable was identical too, which a genuinely
// interposing Streamline cannot produce — it has to wrap the factory to reach
// the swapchain. The interposer loads, exports CreateDXGIFactory2, and forwards
// straight to dxgi.dll until slInit() has run and a feature is loaded.
//
// So "same vtable" from an unengaged interposer means only that passthrough is
// passthrough. Reading it as "our hook is safe on Streamline titles" would be a
// gate going green without looking — this file's own reason for existing.
int CountLoadedSlPlugins(bool print) {
    HMODULE   mods[512];
    DWORD     needed = 0;
    const int loaded = EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed) ? 1 : 0;
    if (loaded == 0) {
        std::printf("  (could not enumerate our own modules; treating engagement as unknown)\n");
        return -1;
    }
    const DWORD count = needed / sizeof(HMODULE);
    int         plugins = 0;
    for (DWORD i = 0; i < count && i < 512; ++i) {
        wchar_t name[MAX_PATH]{};
        if (GetModuleBaseNameW(GetCurrentProcess(), mods[i], name, MAX_PATH) == 0) {
            continue;
        }
        if (_wcsnicmp(name, L"sl.", 3) == 0 || _wcsnicmp(name, L"nvngx", 5) == 0) {
            if (print) {
                std::wprintf(L"    %ls\n", name);
            }
            if (_wcsicmp(name, L"sl.interposer.dll") != 0) {
                ++plugins;
            }
        }
    }
    return plugins;
}

// Create a composition swapchain through a caller-supplied factory entry point.
// Returns the swapchain, or nullptr with the HRESULT printed.
IDXGISwapChain1* MakeSwapChain(PFN_CreateDXGIFactory2 create, ID3D11Device* device, const char* label) {
    IDXGIFactory2* factory = nullptr;
    HRESULT        hr = create(0, __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || factory == nullptr) {
        std::printf("  %s: CreateDXGIFactory2 failed 0x%08lX\n", label, static_cast<unsigned long>(hr));
        return nullptr;
    }

    const DXGI_SWAP_CHAIN_DESC1 desc = CompositionDesc();
    IDXGISwapChain1*            sc = nullptr;
    hr = factory->CreateSwapChainForComposition(device, &desc, nullptr, &sc);
    std::printf("  %s: factory %p (vtable %p), swapchain hr=0x%08lX\n", label, static_cast<void*>(factory),
                VtableOf(factory), static_cast<unsigned long>(hr));
    factory->Release();
    if (FAILED(hr)) {
        return nullptr;
    }
    return sc;
}

// -------------------------------------------------------------------------
// Part 1 — the CONTROL. Always runs, and is what makes part 2 readable.
//
// A comparison tool that has never been shown to detect a DIFFERENCE carries no
// information when it reports "same" — that is this project's most-recorded
// defect shape. So this asserts BOTH directions before any vendor DLL is
// involved: two swapchains built by different routes through the real dxgi.dll
// must share a vtable, and two DIFFERENT interfaces must not.
// -------------------------------------------------------------------------
bool RunControl(Gfx& g) {
    std::printf("\nPart 1 - control (real dxgi.dll only)\n");

    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    if (dxgi == nullptr) {
        dxgi = LoadLibraryW(L"dxgi.dll");
    }
    if (dxgi == nullptr) {
        Check(false, "dxgi.dll is loadable");
        return false;
    }
    auto realCreate =
        reinterpret_cast<PFN_CreateDXGIFactory2>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")));
    if (realCreate == nullptr) {
        Check(false, "dxgi.dll exports CreateDXGIFactory2");
        return false;
    }

    IDXGISwapChain1* a = MakeSwapChain(realCreate, g.device, "real/A");
    IDXGISwapChain1* b = MakeSwapChain(realCreate, g.device, "real/B");
    if (a == nullptr || b == nullptr) {
        Check(false, "two swapchains created through the real factory");
        if (a != nullptr) {
            a->Release();
        }
        if (b != nullptr) {
            b->Release();
        }
        return false;
    }

    const void* va = VtableOf(a);
    const void* vb = VtableOf(b);
    std::printf("  real/A vtable %p\n  real/B vtable %p\n", va, vb);

    // POSITIVE control: the shared class vtable is why one hook sees every
    // swapchain in the process (17_HOOK_ENGINE §swapchainId, measured across
    // five configurations).
    Check(va != nullptr && va == vb, "two independently created swapchains share one vtable");

    // NEGATIVE control: the comparison can tell things apart at all. Without
    // this, "same vtable" below would be indistinguishable from a probe that
    // always says same.
    Check(VtableOf(g.device) != va, "a different interface has a different vtable (the check discriminates)");

    a->Release();
    b->Release();
    return g_failures == 0;
}

// -------------------------------------------------------------------------
// Part 2 — the question. Needs a real sl.interposer.dll, so it is opt-in.
// -------------------------------------------------------------------------
int RunInterposer(Gfx& g, const wchar_t* dir) {
    std::printf("\nPart 2 - Streamline interposer\n");
    std::wprintf(L"  directory: %ls\n", dir);

    wchar_t path[MAX_PATH];
    if (swprintf_s(path, L"%ls\\sl.interposer.dll", dir) < 0) {
        std::printf("  path too long\n");
        return 2;
    }

    // ALTERED_SEARCH_PATH so the interposer resolves sl.common.dll and its
    // plugins from beside itself, the way the game's own loader would.
    HMODULE sl = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (sl == nullptr) {
        std::printf("  INCONCLUSIVE: LoadLibraryExW failed, GetLastError=%lu\n", GetLastError());
        std::printf("  Reported as inconclusive, never as 'same vtable'. A load failure says\n"
                    "  nothing about the question, and reading it as a pass is exactly how a\n"
                    "  gate goes green without looking.\n");
        return 2;
    }

    auto slCreate =
        reinterpret_cast<PFN_CreateDXGIFactory2>(reinterpret_cast<void*>(GetProcAddress(sl, "CreateDXGIFactory2")));
    if (slCreate == nullptr) {
        std::printf("  INCONCLUSIVE: sl.interposer.dll does not export CreateDXGIFactory2\n");
        return 2;
    }

    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    auto    realCreate =
        reinterpret_cast<PFN_CreateDXGIFactory2>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")));

    // Prove we are genuinely on a different code path before trusting a verdict.
    std::printf("  real CreateDXGIFactory2   = %p\n", reinterpret_cast<void*>(realCreate));
    std::printf("  interposer CreateDXGIFactory2 = %p\n", reinterpret_cast<void*>(slCreate));
    Check(reinterpret_cast<void*>(slCreate) != reinterpret_cast<void*>(realCreate),
          "the interposer's entry point is not just dxgi.dll's (we are on a different path)");

    IDXGISwapChain1* real = MakeSwapChain(realCreate, g.device, "real");
    IDXGISwapChain1* shim = MakeSwapChain(slCreate, g.device, "interposer");
    if (real == nullptr || shim == nullptr) {
        std::printf("  INCONCLUSIVE: could not create both swapchains\n");
        if (real != nullptr) {
            real->Release();
        }
        if (shim != nullptr) {
            shim->Release();
        }
        return 2;
    }

    const void* vreal = VtableOf(real);
    const void* vshim = VtableOf(shim);
    std::printf("\n  real       swapchain vtable = %p\n", vreal);
    std::printf("  interposer swapchain vtable = %p\n", vshim);

    const bool same = (vreal == vshim);

    std::printf("\n  sl.* / nvngx* modules mapped into this process:\n");
    const int plugins = CountLoadedSlPlugins(true);

    // ENGAGEMENT CHECK. Without it the verdict below is unreadable.
    if (plugins == 0) {
        std::printf("\n  ==> INCONCLUSIVE — THE INTERPOSER NEVER ENGAGED.\n"
                    "      Only sl.interposer.dll is mapped: no sl.common, no feature plugin.\n"
                    "      Streamline forwards straight to dxgi.dll until slInit() has run and a\n"
                    "      feature is loaded, so the vtables matching here says nothing except\n"
                    "      that passthrough is passthrough. The swapchain vtables were %s and the\n"
                    "      FACTORY vtables matched too, which a genuinely interposing Streamline\n"
                    "      cannot produce — it must wrap the factory to reach the swapchain.\n"
                    "\n"
                    "      Answering the real question needs slInit(), whose sl::Preferences\n"
                    "      struct is vendor ABI — the licence question in\n"
                    "      legal/THIRD_PARTY_NOTICES.md, unanswered. Recorded as unanswered\n"
                    "      rather than closed on a passthrough measurement.\n",
                    same ? "identical" : "different");
        real->Release();
        shim->Release();
        return 2;
    }
    if (plugins < 0) {
        real->Release();
        shim->Release();
        return 2;
    }

    std::printf("\n  ==> VERDICT (interposer engaged, %d plugin module(s) loaded): "
                "the swapchain vtable is %s\n",
                plugins, same ? "THE SAME" : "DIFFERENT");
    if (same) {
        std::printf("      A vtable-slot present hook DOES catch presents made through the\n"
                    "      Streamline interposer. 03_METRICS Counting native vs displayed holds\n"
                    "      at the mechanism level.\n");
    } else {
        std::printf("      §H5 case 3 IS LIVE. The game calls Present on an object whose class\n"
                    "      vtable we never patched, so a real-vtable hook misses Streamline\n"
                    "      titles and fg_factor would be structurally 1.0. 03_METRICS §Frame\n"
                    "      Generation has to be reopened before feature hooks are designed.\n");
    }

    // Whether the wrapper will hand back the object underneath is the other half
    // of the answer if the vtables differ, so it is recorded either way.
    void*         native = nullptr;
    const HRESULT qi = shim->QueryInterface(__uuidof(IDXGISwapChain), &native);
    std::printf("\n  interposer QueryInterface(IDXGISwapChain) hr=0x%08lX, vtable %p\n", static_cast<unsigned long>(qi),
                SUCCEEDED(qi) ? VtableOf(native) : nullptr);
    if (SUCCEEDED(qi) && native != nullptr) {
        static_cast<IUnknown*>(native)->Release();
    }
    std::printf("  sl.interposer exports slGetNativeInterface: %s\n",
                GetProcAddress(sl, "slGetNativeInterface") != nullptr ? "yes" : "no");

    real->Release();
    shim->Release();
    return same ? 0 : 1;
}

}    // namespace

int wmain(int argc, wchar_t** argv) {
    std::printf("fl-probe-interposer (WARP, headless - no GPU, no window, no game running)\n");

    Gfx g;
    if (!CreateDevice(g)) {
        return 2;
    }

    const bool controlOk = RunControl(g);
    if (!controlOk) {
        std::printf("\nCONTROL FAILED - part 2 is not run, because a comparison that cannot be\n"
                    "trusted to detect sameness AND difference answers nothing.\n");
        return 1;
    }

    const wchar_t* dir = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--interposer") == 0 && i + 1 < argc) {
            dir = argv[++i];
        }
    }

    if (dir == nullptr) {
        std::printf("\nPart 2 NOT RUN - no --interposer <dir> given.\n"
                    "  Pass the directory of an installed title that ships sl.interposer.dll.\n"
                    "  We never ship or download one; it is read from what the user already has.\n");
        std::printf("\ncontrol: %d/%d checks passed\n", g_checks - g_failures, g_checks);
        return 0;
    }

    const int rc = RunInterposer(g, dir);
    std::printf("\nchecks: %d/%d passed\n", g_checks - g_failures, g_checks);
    if (g_failures != 0) {
        return 1;
    }
    return rc;
}
