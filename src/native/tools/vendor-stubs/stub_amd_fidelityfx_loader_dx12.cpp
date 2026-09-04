// THE FORWARDING DECOY. A module named `amd_fidelityfx_loader_dx12.dll`, exporting
// the SAME five names as the leaves -- and it must never be hooked.
//
// WHY A FORWARDER AND NOT A PLAIN DECOY. stub_sl_common.cpp's decoy proves module
// scoping by exporting a name from the wrong module. The AMD hazard is one step
// further along: on a loader-shipping title (Dying Light: The Beast, Kingdom Come II,
// Black Myth: Wukong) the game calls the LOADER's ffxDispatch, and the loader
// calls the EFFECT DLL's ffxDispatch -- the only route it has, since the effect
// DLLs export nothing else. Every logical dispatch therefore crosses TWO exports of
// the same name, and a writer that hooked both would count each one twice, straight
// into fg_factor (03_METRICS: fg_factor = presents / F_app, so a doubled count
// HALVES the factor -- reporting x2 frame generation as none). The inventory hooks
// the leaves only, and this stub is what makes "the leaf count reads 1x with a
// loader in the chain" a property a test can falsify: the harness binds it to the
// two SDK 2.x leaf stubs and dispatches THROUGH it, and the leaf counters and the
// Overlay's records had better agree with the number of calls the harness made.
//
// The negative is asserted at compile time, not merely by the fixture: the loader's
// name is bound to fl::inventory::kFfxLoaderModule, and both fl_hook_inventory.h
// and this file refuse a row for it.
//
// IT IS A FIXTURE, NOT A CLAIM ABOUT THE REAL LOADER'S INTERNALS. What is measured
// is the export tables (docs/vendor-exports.json: five names, all four modules) and
// the SDK's own description of the loader as containing no effect code; how the
// signed binary routes internally is not ours to read, and it does not matter
// here: with only those five exports, forwarding is the only way through.

#include <ffx_api.h>

#include "stub_common.h"

static_assert(!fl::stub::InventoryHasRow(fl::inventory::kFfxLoaderModule, "ffxDispatch"),
              "amd_fidelityfx_loader_dx12.dll became an inventory row -- hooking the loader AND a leaf counts every "
              "dispatch twice on a loader-shipping title, straight into fg_factor");
static_assert(fl::inventory::FfxLeafOfExact(fl::inventory::kFfxLoaderModule) < 0,
              "the loader is not a leaf, and the leaf table must not say otherwise");

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
    b.dispatch = reinterpret_cast<PfnFfxDispatch>(reinterpret_cast<void*>(GetProcAddress(h, "ffxDispatch")));
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

// The same name the leaves export, one hop earlier. If the Overlay ever hooks this
// one, the K = 1 control in the [ffx] tests reads 2x and goes red.
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
