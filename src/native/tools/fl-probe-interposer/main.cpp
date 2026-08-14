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
// WHY IT NOW USES THE VENDORED HEADERS, having deliberately avoided them.
//
// This file used to say "it needs no vendor headers", and that was correct when
// written: reaching the engaged path needs slInit's sl::Preferences, i.e. vendor
// ABI, and legal/THIRD_PARTY_NOTICES.md settles for Intel IGCL that "re-declaring
// the API by hand is explicitly NOT an approved workaround". So the probe could
// only measure PASSTHROUGH and reported INCONCLUSIVE, which is what it did.
//
// PR #64 vendored the Streamline headers under MIT, after this file was written.
// The blocker is gone: sl::Preferences is sl_core_types.h:549 and slInit is
// sl_core_api.h:83, both MIT, neither re-declared here. 20_OPEN_QUESTIONS §S24
// records the unblock; §H5 case 3's own text still said "blocked on a licence
// decision" and is corrected in this PR.
//
// STILL RESOLVED WITH GetProcAddress, AND THAT IS NOT OPTIONAL. The headers are
// for types only. Calling slInit by name would make sl.interposer.dll a
// load-time dependency of this executable -- and Part 1 of this probe is
// `ctest fl_vtable_identity_control`, which runs on CI, where no Streamline
// exists. Linking it would turn the control into a test that cannot start on the
// one machine that runs it. Same rule as the Overlay, for a different reason.

#include <windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <cwchar>
#include <psapi.h>

// TYPES ONLY. Nothing below names an SL_API function in evaluated code; every
// entry point is resolved with GetProcAddress from the module we loaded by
// absolute path. See the header comment.
#include <sl.h>

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
// IUnknown* rather than ID3D11Device*, because a D3D12 swapchain is created from
// the COMMAND QUEUE, not the device -- the same asymmetry ResolveApi in the
// Overlay had to learn. Part 1 still passes a D3D11 device; Part 2 passes a queue.
IDXGISwapChain1* MakeSwapChain(PFN_CreateDXGIFactory2 create, IUnknown* device, const char* label) {
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
// Engagement — what the first version of this probe could not do.
//
// Streamline forwards straight to dxgi.dll until slInit() has run AND a feature
// plugin has loaded. Comparing vtables before that measures passthrough, which
// is why the probe reported INCONCLUSIVE rather than a verdict.
//
// The sequence below is the one a real title performs, in order, because the
// order is what decides whether the interposer is in a position to wrap
// anything: slInit -> D3D12CreateDevice THROUGH THE INTERPOSER -> slSetD3DDevice.
// Creating the device through dxgi/d3d12 directly and then telling Streamline
// about it is not the same arrangement and would answer a different question.
// -------------------------------------------------------------------------

// The vendor's own function TYPES, from the MIT headers. Pointers to these are
// filled by GetProcAddress; none of these names appears in evaluated code.
using PFN_slInit = ::PFun_slInit*;
using PFN_slSetD3DDevice = ::PFun_slSetD3DDevice*;
using PFN_slIsFeatureLoaded = ::PFun_slIsFeatureLoaded*;
using PFN_slShutdown = ::PFun_slShutdown*;
using PFN_D3D12CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

struct Gfx12 {
    ID3D12Device*       device = nullptr;
    ID3D12CommandQueue* queue = nullptr;

    ~Gfx12() {
        if (queue != nullptr) {
            queue->Release();
        }
        if (device != nullptr) {
            device->Release();
        }
    }
};

// NOT WARP, and that is the one place this probe cannot follow hook-harness.
//
// DLSS-G is an NVIDIA feature on a real adapter; WARP will not load the plugin,
// so a WARP device could never reach the engaged state this function exists to
// produce. nullptr = the default adapter. On a machine with no NVIDIA GPU this
// simply fails to engage and the caller reports INCONCLUSIVE, which is correct:
// the question is about what Streamline does when it is working.
bool CreateD3D12(Gfx12& g, PFN_D3D12CreateDevice create) {
    const HRESULT hr =
        create(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&g.device));
    if (FAILED(hr) || g.device == nullptr) {
        std::printf("  D3D12CreateDevice(default adapter) failed: 0x%08lX\n", static_cast<unsigned long>(hr));
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(g.device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&g.queue)))) {
        std::printf("  CreateCommandQueue failed\n");
        return false;
    }
    return true;
}

// Ask Streamline to initialise, naming the title's own directory as the plugin
// search path. Returns false when it could not, and prints why -- never a
// verdict, because "did not engage" is the input to an INCONCLUSIVE report.
bool SlInit(HMODULE sl, const wchar_t* dir) {
    auto init = reinterpret_cast<PFN_slInit>(reinterpret_cast<void*>(GetProcAddress(sl, "slInit")));
    if (init == nullptr) {
        std::printf("  sl.interposer.dll does not export slInit\n");
        return false;
    }

    // BOTH features requested, because the installed corpus splits on exactly
    // this: only four of the ten Streamline titles on this machine ship
    // sl.dlss.dll, while the rest use Streamline for frame generation only. Asking
    // for one would make the answer depend on which title the caller pointed at.
    const sl::Feature features[] = {sl::kFeatureDLSS, sl::kFeatureDLSS_G};
    const wchar_t*    paths[] = {dir};
    sl::Preferences   pref{};
    pref.pathsToPlugins = paths;
    pref.numPathsToPlugins = 1;
    pref.featuresToLoad = features;
    pref.numFeaturesToLoad = 2;
    pref.logLevel = sl::LogLevel::eOff;
    pref.pathToLogsAndData = nullptr;    // no file logging from a probe
    pref.renderAPI = sl::RenderAPI::eD3D12;
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "fl-probe-interposer";
    pref.projectId = "b7f4f0b1-6a0f-4f7e-9d8a-frameledger01";

    const sl::Result r = init(pref, sl::kSDKVersion);
    if (r != sl::Result::eOk) {
        std::printf("  slInit returned %u (not eOk)\n", static_cast<unsigned>(r));
        return false;
    }
    return true;
}

// -------------------------------------------------------------------------
// Part 2 — the question. Needs a real sl.interposer.dll, so it is opt-in.
// -------------------------------------------------------------------------
// NO Gfx PARAMETER any more. Part 1's D3D11 WARP device is the CONTROL's device
// and has no role here: Part 2 now builds its own D3D12 device through the
// interposer, which is the arrangement the question is about. Leaving the
// parameter in place would be an unread argument under /W4 /WX, i.e. a build
// error -- the same C4100/CS9113 shape docs/HANDOFF.md §Traps records.
int RunInterposer(const wchar_t* dir) {
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

    // --- ENGAGE. Everything below this point is what the first version could
    // --- not reach, and without it the comparison measures passthrough.
    std::printf("\n  engaging Streamline (slInit -> D3D12CreateDevice -> slSetD3DDevice)\n");

    auto slD3D12Create =
        reinterpret_cast<PFN_D3D12CreateDevice>(reinterpret_cast<void*>(GetProcAddress(sl, "D3D12CreateDevice")));
    if (slD3D12Create == nullptr) {
        std::printf("  INCONCLUSIVE: sl.interposer.dll does not export D3D12CreateDevice\n");
        return 2;
    }

    // VERSION GUARD, and it is here because the probe CRASHED without it.
    //
    // Measured 2026-08-14: The Witcher 3 ships sl.interposer.dll **1.5.6**, not
    // 2.x. Its export set is a different API generation -- slGetHooks,
    // slGetNumHooks, slIsFeatureEnabled, slSetFeatureEnabled,
    // slSetFeatureConstants, slGetFeatureConfiguration -- and it exports NEITHER
    // slSetD3DDevice NOR slIsFeatureLoaded. slInit exists in both, with a
    // DIFFERENT sl::Preferences layout, so calling it with the vendored 2.x
    // struct passes a wrongly-shaped argument and access-violates (0xC0000005).
    //
    // The SL2-only entry points are the version probe. They cost nothing and are
    // the honest test: "does this module speak the ABI our vendored headers
    // describe", not "is it named sl.interposer.dll".
    const bool sl2 = GetProcAddress(sl, "slSetD3DDevice") != nullptr &&
                     GetProcAddress(sl, "slIsFeatureLoaded") != nullptr &&
                     GetProcAddress(sl, "slGetNewFrameToken") != nullptr;
    if (!sl2) {
        std::printf("\n  ==> SKIPPED - this is a Streamline 1.x interposer.\n"
                    "      It exports slInit but not slSetD3DDevice / slIsFeatureLoaded /\n"
                    "      slGetNewFrameToken, and its sl::Preferences has a different layout,\n"
                    "      so calling slInit with the vendored 2.x struct crashes. Refusing is\n"
                    "      the answer; the first version of this guard was its absence.\n"
                    "\n"
                    "      THIS MATTERS BEYOND THIS PROBE. docs/vendor-exports.json records ONE\n"
                    "      COPY PER MODULE NAME, so 'sl.interposer.dll' there is one machine's\n"
                    "      2.7.4 and says nothing about the 1.5.6 a title may ship.\n"
                    "      FL_HOOK_INVENTORY's oracle would pass slEvaluateFeature against it --\n"
                    "      the NAME exists in both generations -- while the SIGNATURE differs.\n"
                    "      A detour typed with the 2.x PFun_ would then read 1.x arguments.\n");
        return 2;
    }

    if (!SlInit(sl, dir)) {
        std::printf("\n  ==> INCONCLUSIVE - slInit did not succeed, so nothing is wrapped and the\n"
                    "      comparison would measure passthrough. Not a verdict about the vtables.\n");
        return 2;
    }

    // THROUGH THE INTERPOSER, deliberately: this is the call a Streamline title
    // makes, and every one measured on this machine imports D3D12CreateDevice
    // from sl.interposer.dll rather than d3d12.dll.
    Gfx12 g12;
    if (!CreateD3D12(g12, slD3D12Create)) {
        std::printf("\n  ==> INCONCLUSIVE - no D3D12 device, so no D3D12 swapchain and no DLSS-G.\n");
        return 2;
    }

    auto setDevice =
        reinterpret_cast<PFN_slSetD3DDevice>(reinterpret_cast<void*>(GetProcAddress(sl, "slSetD3DDevice")));
    if (setDevice != nullptr) {
        const sl::Result sr = setDevice(g12.device);
        std::printf("  slSetD3DDevice -> %u\n", static_cast<unsigned>(sr));
    }

    // WAIT FOR ENGAGEMENT. Reading this ONCE is a race, and the first run of this
    // version lost it: slIsFeatureLoaded said "no" for both features and the
    // module scan found only sl.interposer.dll, while Streamline's own log --
    // flushed AFTER our output -- showed it verifying and loading sl.common,
    // sl.dlss, sl.dlss_d, sl.dlss_g, sl.pcl and sl.reflex. Plugin load is
    // deferred, so "not engaged yet" and "will not engage" are the same answer
    // read too early. Exactly the shape docs/HANDOFF.md §Traps records for the
    // drain tests: poll for the STATE, bounded by a generous wall clock.
    auto loaded =
        reinterpret_cast<PFN_slIsFeatureLoaded>(reinterpret_cast<void*>(GetProcAddress(sl, "slIsFeatureLoaded")));
    bool anyLoaded = false;
    for (int i = 0; i < 100 && !anyLoaded; ++i) {
        if (loaded != nullptr) {
            for (const auto f : {sl::kFeatureDLSS, sl::kFeatureDLSS_G}) {
                bool on = false;
                loaded(f, on);
                anyLoaded = anyLoaded || on;
            }
        }
        // A plugin can be MAPPED before it reports loaded, so either signal ends
        // the wait -- the verdict below re-checks both.
        if (!anyLoaded && CountLoadedSlPlugins(false) > 0) {
            break;
        }
        if (!anyLoaded) {
            Sleep(100);
        }
    }
    if (loaded != nullptr) {
        for (const auto f : {sl::kFeatureDLSS, sl::kFeatureDLSS_G}) {
            bool on = false;
            loaded(f, on);
            std::printf("  slIsFeatureLoaded(%u) -> %s\n", static_cast<unsigned>(f), on ? "yes" : "no");
        }
    }

    // The swapchain a D3D12 title actually gets: created from the COMMAND QUEUE.
    IDXGISwapChain1* real = MakeSwapChain(realCreate, g12.queue, "real/d3d12");
    IDXGISwapChain1* shim = MakeSwapChain(slCreate, g12.queue, "interposer/d3d12");
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
    // THE VTABLE QUESTION IS ANSWERED BY slInit ALONE, and separating it from the
    // plugin question is the correction this run forced.
    //
    // Measured 2026-08-14 against Alan Wake 2 (SL 2.7.0) on an RTX 5080: with
    // slInit returning eOk and NO feature plugin reporting loaded, the interposer
    // still hands back a swapchain whose vtable is inside sl.interposer.dll's
    // module range while dxgi.dll's own route yields dxgi.dll's. So §H5 case 3's
    // PREMISE is real and does not need DLSS-G to be running to be true.
    //
    // What that does NOT settle is whether we still see the presents. §H5's
    // --probe-proxy result stands: a FORWARDING proxy calls real_->Present(...),
    // an ordinary virtual dispatch, and a hook on the real vtable catches it one
    // layer down. Different class != missed present. Deciding that needs presents
    // driven through the proxy with our hook installed, which is a different
    // fixture and is called out as such rather than guessed at here.
    if (!same) {
        std::printf("\n  ==> MEASURED: THE SWAPCHAIN CLASS IS NOT OURS.\n"
                    "      slInit returned eOk and the interposer's swapchain vtable (%p) is not\n"
                    "      dxgi.dll's (%p). §H5 case 3's premise holds: the object a Streamline\n"
                    "      title calls Present on is not an instance of the class whose shared\n"
                    "      vtable we patch.\n"
                    "\n"
                    "      STILL UNMEASURED, and it is the half that decides fg_factor: whether\n"
                    "      that proxy FORWARDS to the real vtable. hook-harness --probe-proxy\n"
                    "      shows a forwarding proxy is caught one layer down, so 'different\n"
                    "      class' is not by itself 'missed present'. Answering it needs presents\n"
                    "      driven through this proxy with our hook installed.\n"
                    "\n"
                    "      Feature plugins loaded this run: %d. With none, the DLSS-G-specific\n"
                    "      question — do GENERATED presents reach the same vtable — is untouched.\n",
                    vshim, vreal, plugins);
        real->Release();
        shim->Release();
        return 1;
    }

    if (plugins == 0) {
        std::printf("\n  ==> INCONCLUSIVE — THE INTERPOSER NEVER ENGAGED.\n"
                    "      Only sl.interposer.dll is mapped: no sl.common, no feature plugin.\n"
                    "      Streamline forwards straight to dxgi.dll until slInit() has run and a\n"
                    "      feature is loaded, so the vtables matching here says nothing except\n"
                    "      that passthrough is passthrough. The swapchain vtables were %s and the\n"
                    "      FACTORY vtables matched too, which a genuinely interposing Streamline\n"
                    "      cannot produce — it must wrap the factory to reach the swapchain.\n"
                    "\n"
                    "      slInit() DID return eOk above, so this is no longer the licence block\n"
                    "      that used to stop here — it is a plugin that declined to load. Usual\n"
                    "      causes: no NVIDIA adapter, a driver too old for the features asked\n"
                    "      for, or a title directory whose sl.* plugins do not match its\n"
                    "      interposer. Still reported as unanswered rather than closed on a\n"
                    "      passthrough measurement.\n",
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

    const int rc = RunInterposer(dir);
    std::printf("\nchecks: %d/%d passed\n", g_checks - g_failures, g_checks);
    if (g_failures != 0) {
        return 1;
    }
    return rc;
}
