// THE LOADER STAND-IN. A module named `amd_fidelityfx_loader_dx12.dll`, exporting
// the SAME five names as the leaves, forwarding to the two SDK 2.x leaf stubs --
// and, since the evening of 2026-09-04, an inventory row like them.
//
// WHAT WAS MEASURED, AND WHAT IT REVERSED. This file first shipped as a decoy that
// must never be hooked, on the argument that the effect DLLs export only the five
// ffx-api names, so the real loader could reach them only through their exports,
// and hooking both would count each dispatch twice. Then three loader-shipping
// titles ran FSR under the leaf-only build -- Dying Light: The Beast (FSR + FSR
// frame generation), Kingdom Come: Deliverance II, Black Myth: Wukong -- and every
// one produced ZERO dispatches at any leaf export while the leaves sat in the
// census. The game calls the loader's export; however the signed loader reaches
// its providers afterwards, it is not through the leaves' ffxDispatch. So the
// loader is where those titles' calls arrive, and it is a row.
//
// THIS STUB THEREFORE FORWARDS THE WAY THE MEASUREMENT SAYS: through the leaves'
// FrameLedger-named DIRECT entry (FlStubFfxDispatchDirect), never through their
// export. That is what makes the K = 1 control in the [ffx] cases discriminating
// with all four modules hooked: a loader that DID re-enter a leaf's export would
// be counted at both, and the control would read 2x. The leaves also count export
// entries separately (FlStubFfxExportCalls), so --probe-ffx-resolve asserts the
// forward did not touch the export at all.
//
// IT IS A FIXTURE, NOT A CLAIM ABOUT THE REAL LOADER'S INTERNALS beyond the one
// measured fact: the leaf exports stay silent behind it.

#include <ffx_api.h>

#include "stub_common.h"

static_assert(fl::stub::InventoryHasRow(fl::inventory::kFfxLoaderModule, "ffxDispatch"),
              "amd_fidelityfx_loader_dx12.dll lost its inventory row -- on a loader-shipping title the game calls "
              "THIS module's export and the leaves stay silent (measured 2026-09-04), so those titles would read N/A");
static_assert(fl::inventory::FfxLeafOfExact(fl::inventory::kFfxLoaderModule) == fl::inventory::kFfxLeafLoader,
              "the loader's slot in the module table moved");

namespace {

struct Bound {
    PfnFfxCreateContext  create = nullptr;
    PfnFfxDestroyContext destroy = nullptr;
    PfnFfxConfigure      configure = nullptr;
    PfnFfxQuery          query = nullptr;
    PfnFfxDispatch       dispatch = nullptr;
};

Bound         g_upscaler;
Bound         g_frameGeneration;
volatile LONG g_forwarded = 0;

void Bind(Bound& b, HMODULE h) {
    if (h == nullptr) {
        b = Bound{};
        return;
    }
    b.create = reinterpret_cast<PfnFfxCreateContext>(reinterpret_cast<void*>(GetProcAddress(h, "ffxCreateContext")));
    b.destroy = reinterpret_cast<PfnFfxDestroyContext>(reinterpret_cast<void*>(GetProcAddress(h, "ffxDestroyContext")));
    b.configure = reinterpret_cast<PfnFfxConfigure>(reinterpret_cast<void*>(GetProcAddress(h, "ffxConfigure")));
    b.query = reinterpret_cast<PfnFfxQuery>(reinterpret_cast<void*>(GetProcAddress(h, "ffxQuery")));
    // DISPATCH GOES THROUGH THE DIRECT ENTRY, NOT THE EXPORT -- see the header comment.
    b.dispatch =
        reinterpret_cast<PfnFfxDispatch>(reinterpret_cast<void*>(GetProcAddress(h, "FlStubFfxDispatchDirect")));
}

// Route by EFFECT ID, which is what the real loader's provider lookup keys on
// (ffx_provider.h: CanProvide compares descType & FFX_API_EFFECT_MASK). The
// frame-generation swapchain effect lives in the frame-generation DLL.
Bound* Route(ffxStructType_t type) {
    const uint64_t effect = type & FFX_API_EFFECT_MASK;
    if (effect == FFX_API_EFFECT_ID_UPSCALE) {
        return &g_upscaler;
    }
    if (effect == FFX_API_EFFECT_ID_FRAMEGENERATION || effect == FFX_API_EFFECT_ID_FRAMEGENERATIONSWAPCHAIN) {
        return &g_frameGeneration;
    }
    return nullptr;
}

}    // namespace

extern "C" {

// FrameLedger-named. The harness hands over the two leaf modules it loaded by
// absolute path; nothing here searches for a module by name.
__declspec(dllexport) void FlStubLoaderBind(HMODULE upscaler, HMODULE frameGeneration) {
    Bind(g_upscaler, upscaler);
    Bind(g_frameGeneration, frameGeneration);
}

__declspec(dllexport) unsigned int FlStubLoaderForwarded() {
    return static_cast<unsigned int>(InterlockedCompareExchange(&g_forwarded, 0, 0));
}

FFX_API_ENTRY ffxReturnCode_t ffxCreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                               const ffxAllocationCallbacks* memCb) {
    if (desc == nullptr) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    Bound* b = Route(desc->type);
    if (b == nullptr || b->create == nullptr) {
        return FFX_API_RETURN_NO_PROVIDER;
    }
    InterlockedIncrement(&g_forwarded);
    return b->create(context, desc, memCb);
}

FFX_API_ENTRY ffxReturnCode_t ffxDestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb) {
    (void)memCb;
    if (context != nullptr) {
        *context = nullptr;
    }
    return FFX_API_RETURN_OK;
}

FFX_API_ENTRY ffxReturnCode_t ffxConfigure(ffxContext* context, const ffxConfigureDescHeader* desc) {
    if (desc == nullptr) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    Bound* b = Route(desc->type);
    if (b == nullptr || b->configure == nullptr) {
        return FFX_API_RETURN_NO_PROVIDER;
    }
    InterlockedIncrement(&g_forwarded);
    return b->configure(context, desc);
}

FFX_API_ENTRY ffxReturnCode_t ffxQuery(ffxContext* context, ffxQueryDescHeader* desc) {
    if (desc == nullptr) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    Bound* b = Route(desc->type);
    if (b == nullptr || b->query == nullptr) {
        return FFX_API_RETURN_NO_PROVIDER;
    }
    InterlockedIncrement(&g_forwarded);
    return b->query(context, desc);
}

// The same name the leaves export, one hop earlier -- and hooked, since the measurement.
// The forward below reaches the leaf's DIRECT entry, so with all four modules hooked the
// Overlay sees this dispatch exactly once.
FFX_API_ENTRY ffxReturnCode_t ffxDispatch(ffxContext* context, const ffxDispatchDescHeader* desc) {
    if (desc == nullptr) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    Bound* b = Route(desc->type);
    if (b == nullptr || b->dispatch == nullptr) {
        return FFX_API_RETURN_NO_PROVIDER;
    }
    InterlockedIncrement(&g_forwarded);
    return b->dispatch(context, desc);
}

}    // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
