// A stub that answers to the name `sl.interposer.dll` and exports
// `slEvaluateFeature`.
//
// This is the module the Overlay's inventory says that symbol may be taken from,
// and the one whose calls must be counted.
//
// The signature comes from the VENDORED MIT HEADER, not from a second hand-typed
// declaration, so the stub and the Overlay's detour cannot disagree about the
// ABI they are both compiled against. If they disagreed the fixture would still
// pass -- the harness would call one shape and the hook would forward another --
// which is the class of green-but-wrong this file exists to prevent.

#include "stub_common.h"

// The vendor header's OWN export switch, which is what an implementer of this
// ABI is expected to define: SL_INTERPOSER turns SL_API into
// `extern "C" __declspec(dllexport)`. Using it rather than bolting dllexport
// onto our own definition means the exported name and linkage come from the
// header both sides compile against, so the stub cannot drift from the
// declaration the Overlay's detour is typed by.
//
// FrameLedger.Overlay must NEVER define this. It would turn every SL_API
// declaration into an export and our injected DLL would start exporting
// `slEvaluateFeature` — announcing an API it does not implement, inside somebody
// else's game.
#define SL_INTERPOSER 1

#include <sl.h>

namespace {

// Which feature ids this stub was asked to evaluate, as the same bitmask shape
// the Overlay keeps, so a test can assert WHAT was seen and not merely a count.
volatile LONG g_calls = 0;
volatile LONG g_lastFeature = -1;

}    // namespace

// The inventory must still spell it this way. Renaming the row, or misspelling
// it, is a compile error here rather than a silently passing fixture.
static_assert(fl::stub::InventoryHas("slEvaluateFeature"),
              "fl_hook_inventory.h no longer contains \"slEvaluateFeature\", so this stub would export a name "
              "the Overlay does not look for -- the fixture would pass while proving nothing "
              "(20_OPEN_QUESTIONS S29(b), with a vendor's name on it).");

extern "C" {

// The measured name, undecorated. `extern "C"` on x64 means the export is
// exactly `slEvaluateFeature`, which is what docs/vendor-exports.json records
// for sl.interposer.dll and therefore what the Overlay resolves.
SL_API sl::Result slEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame, const sl::BaseStructure** inputs,
                                    uint32_t numInputs, sl::CommandBuffer* cmdBuffer) {
    (void)frame;
    (void)inputs;
    (void)numInputs;
    (void)cmdBuffer;
    InterlockedIncrement(&g_calls);
    InterlockedExchange(&g_lastFeature, static_cast<LONG>(feature));
    return sl::Result::eOk;
}

// FrameLedger-named, so nothing here pretends to be part of a vendor API.
__declspec(dllexport) unsigned int FlStubEvaluateCount() {
    return static_cast<unsigned int>(InterlockedCompareExchange(&g_calls, 0, 0));
}

__declspec(dllexport) int FlStubLastFeature() {
    return static_cast<int>(InterlockedCompareExchange(&g_lastFeature, 0, 0));
}

}    // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
