// A stub that answers to the name `ffx_fsr3_x64.dll` -- the FSR 3.0 HOST DLL Cyberpunk
// 2077 ships beside its 1.1.x monolith -- and exports the four names
// fl::inventory::SpeaksFsr3Host probes for, one of which is the row the Overlay hooks.
//
// NOT A LEAF: it exports no ffxDispatch and shares nothing with stub_ffx_leaf.inl. The
// one export the Overlay detours takes the host API's own descriptor, whose renderSize
// sits 1256 bytes in; the stub reads none of it, so renderW/H in a record can only have
// come from the Overlay reading the argument.
//
// THE SIGNATURES AND THE EXPORT LINKAGE COME FROM THE VENDORED MIT HEADER (tag
// fsr3-v3.0.4). FFX_API there is __declspec(dllexport) with no switch, so defining
// the function here exports it under exactly the declaration the Overlay's detour is
// typed by -- and the same header, included by FrameLedger.Overlay for types only,
// exports nothing there because nothing there defines these names. This DLL is Pass C's
// second positive control: its export table MUST show ffxFsr3ContextDispatchUpscale.

#include <FidelityFX/host/ffx_fsr3.h>

#include "stub_common.h"

static_assert(
    fl::stub::InventoryHasRow(fl::inventory::kModuleFfxFsr3Host, fl::inventory::kSymbolFfxFsr3DispatchUpscale),
    "fl_hook_inventory.h has no ffxFsr3ContextDispatchUpscale row for ffx_fsr3_x64.dll, so this fixture would "
    "exercise a name the Overlay does not install");
static_assert(
    !fl::stub::InventoryHas("ffxFsr3DispatchFrameGeneration"),
    "ffxFsr3DispatchFrameGeneration is deliberately NOT a row (dllmain.cpp, fl_hook_inventory.h): on the one "
    "title that ships the host DLL frame generation goes through the monolith, and the pre-committed reversal "
    "condition in 20_OPEN_QUESTIONS §H11 has not fired");
static_assert(!fl::stub::InventoryHas("ffxFsr3SkipPresent"), "probe-only, never a row");

namespace {

volatile LONG g_upscales = 0;
volatile LONG g_contexts = 0;

}    // namespace

extern "C" {

FFX_API FfxErrorCode ffxFsr3ContextCreate(FfxFsr3Context* context, FfxFsr3ContextDescription* contextDescription) {
    if (context == nullptr || contextDescription == nullptr) {
        return FFX_ERROR_INVALID_POINTER;
    }
    InterlockedIncrement(&g_contexts);
    return FFX_OK;
}

// THE ROW'S TARGET. Counts the call and reads nothing of the descriptor.
FFX_API FfxErrorCode ffxFsr3ContextDispatchUpscale(FfxFsr3Context*                          context,
                                                   const FfxFsr3DispatchUpscaleDescription* dispatchParams) {
    (void)context;
    if (dispatchParams == nullptr) {
        return FFX_ERROR_INVALID_POINTER;
    }
    InterlockedIncrement(&g_upscales);
    return FFX_OK;
}

FFX_API FfxErrorCode ffxFsr3DispatchFrameGeneration(const FfxFrameGenerationDispatchDescription* desc) {
    return desc != nullptr ? FFX_OK : FFX_ERROR_INVALID_POINTER;
}

FFX_API FfxErrorCode ffxFsr3SkipPresent(FfxFsr3Context* context) {
    (void)context;
    return FFX_OK;
}

// FrameLedger-named observer, so nothing here pretends to be part of a vendor API.
__declspec(dllexport) unsigned int FlStubFsr3UpscaleCalls() {
    return static_cast<unsigned int>(InterlockedCompareExchange(&g_upscales, 0, 0));
}

}    // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
