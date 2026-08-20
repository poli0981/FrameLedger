// THE DECOY. A second module, named `sl.common.dll`, exporting the SAME symbol
// name -- and it must never be hooked.
//
// WHY A DECOY IS THE POINT OF THIS FIXTURE. docs/HANDOFF.md item 2 says module
// scoping matters because a name-resolved hook double-counts, and the whole
// argument is arithmetic: 03_METRICS computes F_app = sum(fgEvaluations) with
// fg_factor = presents / F_app (owner ruling 2026-08-14; this comment carried the
// pre-ruling subtraction until the producer landed), so counting one logical
// evaluation twice INFLATES Native FPS and DEFLATES fg_factor -- reporting a title
// as generating fewer frames than it does. Either direction is the same class of
// defect: a number reached by arithmetic rather than by measurement, which is what
// CLAUDE.md rule 6 exists to forbid.
//
// Without a second exporter in the process, "we resolve module-scoped" is
// unfalsifiable: a name-only resolver and a module-scoped one behave
// identically, every test passes, and the property is a code comment. This DLL
// is what makes the two distinguishable, and `--probe-upscaler-resolve` asserts
// the Overlay's own resolver picks sl.interposer.dll's address and NOT this one.
//
// IT IS A FIXTURE, NOT A CLAIM ABOUT THE REAL sl.common.dll. Measured, the real
// one exports the sixteen NVSDK_NGX_Parameter_* accessors and the NGX
// CreateFeature/EvaluateFeature family -- NOT slEvaluateFeature, which exactly
// one measured module exports. The multi-exporter hazard is real but lives on
// the NGX names (NVSDK_NGX_D3D12_EvaluateFeature: seven modules), and those are
// the next PR's. This stub reproduces the SHAPE of that hazard on the symbol the
// Overlay actually resolves today, so the guarantee is tested now rather than
// when it first matters.
//
// tools/hookinventory-check.ps1 checks the OVERLAY's inventory against
// docs/vendor-exports.json. It does not check this file, and must not: a fixture
// that could only export names the vendor really exports could not build a
// decoy at all.

#include "stub_common.h"

// The vendor header's own export switch; see stub_sl_interposer.cpp for why this
// is the right mechanism and why FrameLedger.Overlay must never define it.
//
// LEAVING IT OUT IS HOW THIS FILE FIRST SHIPPED, and the failure is worth
// recording because it was invisible from the passing assertions. Without it
// SL_API is a bare `extern "C"`, so the decoy compiled, linked and exported
// NOTHING -- and the probe still reported "the two are DIFFERENT addresses",
// because nullptr differs from a real address. Three checks were green against a
// decoy that did not exist. Only the explicit "sl.common.dll ALSO exports it"
// assertion failed, which is precisely why a fixture needs a vacuity guard
// rather than only the assertions it was written for.
#define SL_INTERPOSER 1

#include <sl.h>

namespace {

volatile LONG g_calls = 0;

}    // namespace

extern "C" {

// Same name, different module, different address. If the Overlay ever hooks
// this one, `--probe-upscaler-resolve` goes red.
SL_API sl::Result slEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame, const sl::BaseStructure** inputs,
                                    uint32_t numInputs, sl::CommandBuffer* cmdBuffer) {
    (void)feature;
    (void)frame;
    (void)inputs;
    (void)numInputs;
    (void)cmdBuffer;
    InterlockedIncrement(&g_calls);
    return sl::Result::eOk;
}

__declspec(dllexport) unsigned int FlStubEvaluateCount() {
    return static_cast<unsigned int>(InterlockedCompareExchange(&g_calls, 0, 0));
}

}    // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
