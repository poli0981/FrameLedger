// A stub that answers to the name `sl.interposer.dll` and is the WRONG
// GENERATION: Streamline 1.x.
//
// WHY THIS FIXTURE EXISTS. `stub_sl_common.cpp` is the decoy that makes
// module-scoping falsifiable — right symbol, wrong module. This is the other
// half, and it is the one a real title actually presents: right module, right
// symbol name, WRONG ABI.
//
// Measured 2026-08-14. The Witcher 3 ships sl.interposer.dll **1.5.6**. It
// exports slInit, slEvaluateFeature, slSetTag and slShutdown — so every name
// check passes — plus slGetHooks, slGetNumHooks, slIsFeatureEnabled,
// slSetFeatureEnabled, slSetFeatureConstants and slGetFeatureConfiguration,
// which Streamline 2 does not have. It exports NONE of slSetD3DDevice,
// slIsFeatureLoaded or slGetNewFrameToken.
//
// THE NAME SURVIVED THE VERSION BUMP AND THE SIGNATURE DID NOT. That is the
// whole hazard, and it is invisible to every gate we have:
// docs/vendor-exports.json records one copy per module NAME, so
// hookinventory-check Pass A resolves `slEvaluateFeature` against one machine's
// 2.7.4 and says nothing about a 1.5.6 in a game. A detour typed with SL2's
// PFun_slEvaluateFeature, called with SL1's argument list, reads argument 1 as a
// feature id when it is not one — producing a WRONG UPSCALER NAME rather than a
// crash, which docs/17_HOOK_ENGINE.md calls the highest false-confidence risk in
// the spike.
//
// So this file exports the SL1 shape and nothing else, and the test asserts the
// Overlay's own resolver REFUSES it. Without this fixture, "we check the ABI" is
// a property no test can falsify — the same argument stub_sl_common.cpp makes
// about scoping.
//
// DELIBERATELY NOT BUILT FROM <sl.h>. The vendored headers describe SL2; there is
// no SL1 header here and there must not be one, because vendoring a second
// generation to build a fixture would be vendoring ahead of a consumer
// (18_GPU_VENDOR_APIS). The exports below are plain dllexport with signatures
// that are deliberately NOT SL2's — that difference is the fixture.

#include "stub_common.h"

// Same binding as the SL2 stub: if the inventory stops naming this symbol, this
// fixture is testing a name nothing looks for and must fail to compile rather
// than pass quietly.
static_assert(fl::stub::InventoryHas("slEvaluateFeature"),
              "fl_hook_inventory.h no longer contains \"slEvaluateFeature\", so this SL1 fixture would prove "
              "nothing about the resolver's ABI check.");

extern "C" {

// SL1's shape, not SL2's — command buffer FIRST. Nothing calls it; the point is
// that the Overlay must never resolve it, so its body is irrelevant and its
// SIGNATURE is the documentation.
__declspec(dllexport) int slEvaluateFeature(void* cmdBuffer, unsigned int feature, unsigned int frameIndex) {
    (void)cmdBuffer;
    (void)feature;
    (void)frameIndex;
    return 0;
}

// Present in both generations, which is exactly why neither is a usable version
// signal on its own.
__declspec(dllexport) int slInit(const void* preferences, unsigned long long sdkVersion) {
    (void)preferences;
    (void)sdkVersion;
    return 0;
}

__declspec(dllexport) int slShutdown() {
    return 0;
}

// SL1-ONLY names, measured on The Witcher 3's 1.5.6. They are here so the
// fixture is recognisably that generation rather than merely an SL2 module with
// three exports missing — a distinction that matters if the check ever grows
// from "SL2 markers present" into "which generation is this".
__declspec(dllexport) int slGetHooks(void** hooks, unsigned int* count) {
    (void)hooks;
    (void)count;
    return 0;
}

__declspec(dllexport) int slIsFeatureEnabled(unsigned int feature) {
    (void)feature;
    return 0;
}

__declspec(dllexport) int slSetFeatureConstants(unsigned int feature, const void* consts) {
    (void)feature;
    (void)consts;
    return 0;
}

}    // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
