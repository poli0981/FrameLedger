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

// A POOL OF DISTINCT TOKENS, because the Overlay counts POINTER CHANGES. The real
// interposer hands out one FrameToken object per frame from a small ring; a stub
// that returned nullptr, or one static object, would make every call look like the
// same frame and the count would read 1 forever -- a fixture green for a writer that
// never counted. Storage only: sl::FrameToken is abstract and nothing here or in
// the Overlay ever dereferences one.
// CONCRETE, because the Overlay may READ the index through the vendor's accessor.
// Each call hands back a DIFFERENT object carrying the requested frame's index --
// which is what the real interposer was measured doing on 2026-09-03 (Cyberpunk
// 2077: 3 to 4.6 requests per frame, a distinct object each) -- so a writer that
// counted pointer changes is red here and one that counts index changes is green.
struct StubFrameToken final : sl::FrameToken {
    uint32_t index = 0;
             operator uint32_t() const override { return index; }
};
volatile LONG  g_tokenCalls = 0;
StubFrameToken g_tokenPool[8];

}    // namespace

// The inventory must still spell it this way. Renaming the row, or misspelling
// it, is a compile error here rather than a silently passing fixture.
static_assert(fl::stub::InventoryHas("slGetNewFrameToken"),
              "fl_hook_inventory.h no longer contains \"slGetNewFrameToken\", so the frame-token fixture would "
              "exercise a row the Overlay does not install.");
static_assert(fl::stub::InventoryHas("slSetTagForFrame"),
              "fl_hook_inventory.h no longer contains \"slSetTagForFrame\", so the frame-based tag fixture would "
              "exercise a row the Overlay does not install.");
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

// The GLOBAL TAG entry point, and the second inventory row's target. The harness
// calls it with a known extent so an injected test can assert the exact
// renderW/H the Overlay published -- a hardcoded resolution in the writer would
// then fail rather than coincide.
//
// Signature from the vendored header, like slEvaluateFeature's, so the stub and
// the detour cannot disagree about the ABI. Note it is NOT [[deprecated]] here:
// that attribute is behind `#if __cplusplus >= 201402L` and this build sets
// /std:c++20 without /Zc:__cplusplus, so MSVC reports 199711L. Measured, not
// assumed.
SL_API sl::Result slSetTag(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags,
                           sl::CommandBuffer* cmdBuffer) {
    (void)viewport;
    (void)tags;
    (void)numTags;
    (void)cmdBuffer;
    return sl::Result::eOk;
}

// THE FRAME-BASED TAG ENTRY POINT (Streamline 2.8: Dying Light: The Beast ships
// 2.8.0, which exports this beside the deprecated slSetTag). Signature from the
// vendored header, as above. The stub records nothing here either; the harness
// drives it with a known extent so the injected case can assert the Overlay read
// renderW/H off THIS route rather than off slSetTag.
SL_API sl::Result slSetTagForFrame(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
                                   const sl::ResourceTag* resources, uint32_t numResources,
                                   sl::CommandBuffer* cmdBuffer) {
    (void)frame;
    (void)viewport;
    (void)resources;
    (void)numResources;
    (void)cmdBuffer;
    return sl::Result::eOk;
}

// THE ABI MARKERS. Three SL2-only entry points, exported so this stub actually
// looks like the generation it stands in for.
//
// The Overlay refuses to hook an sl.* module that does not export all three
// (fl_hook_inventory.h SpeaksStreamline2), because Streamline 1.x keeps the NAME
// slEvaluateFeature and changes its SIGNATURE -- measured on The Witcher 3's
// 1.5.6. Without these, this fixture would be an SL1-shaped module wearing an SL2
// name, the Overlay would correctly decline to hook it, and every [upscaler] test
// would fail for a reason that has nothing to do with what it is testing.
//
// Bodies are deliberately trivial: nothing calls them, and their existence is the
// entire signal.
SL_API sl::Result slSetD3DDevice(void* d3dDevice) {
    (void)d3dDevice;
    return sl::Result::eOk;
}

SL_API sl::Result slIsFeatureLoaded(sl::Feature feature, bool& loaded) {
    (void)feature;
    loaded = true;
    return sl::Result::eOk;
}

SL_API sl::Result slGetNewFrameToken(sl::FrameToken*& token, const uint32_t* frameIndex) {
    // The vendor's contract in miniature: an explicit index returns THAT frame's
    // token (the same object on a re-request), no index advances to a new one.
    const LONG      n = InterlockedIncrement(&g_tokenCalls);
    StubFrameToken& slot = g_tokenPool[static_cast<unsigned>(n) % 8u];
    slot.index = frameIndex != nullptr ? *frameIndex : static_cast<uint32_t>(n);
    token = &slot;
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
