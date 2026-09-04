// The shared body of the three AMD FidelityFX LEAF stubs -- a module that answers
// to one of the three names in fl::inventory::kFfxLeafModules and exports the five
// ffx-api entry points.
//
// ONE BODY, THREE MODULES, because that is the vendor's shape: the SDK 1.1.x
// monolith and the two SDK 2.x effect DLLs export the same five names and differ
// only in which descriptor types they answer. The fixture does not model providers;
// it COUNTS DISPATCHES BY DESCRIPTOR TYPE, which is the one thing the Overlay's
// detour reads, so a test can assert how many UPSCALE / PREPARE / FRAMEGENERATION
// dispatches reached THIS leaf -- and, with the loader decoy in the chain, that a
// dispatch forwarded through the loader is counted once, at the leaf, and not
// twice.
//
// THE SIGNATURES AND THE EXPORT LINKAGE COME FROM THE VENDORED MIT HEADER.
// ffx_api.h declares every entry point FFX_API_ENTRY, which it defines as
// __declspec(dllexport) with no import switch, so defining the function here
// exports it under exactly the declaration the Overlay's detour is typed by. The
// same header, included by FrameLedger.Overlay for types only, exports nothing
// there because nothing there defines these names -- hookinventory-check Pass C
// reads the built Overlay's export table to keep that true.
//
// The including file defines FL_STUB_FFX_MODULE (a wide literal, one of the leaf
// names) before including this, and nothing else.

#ifndef FL_STUB_FFX_MODULE
#error "define FL_STUB_FFX_MODULE (the leaf's module name, as a wide literal) before including stub_ffx_leaf.inl"
#endif

#include <ffx_api.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>

#include "stub_common.h"

// Bound to the Overlay's table, both halves: the NAME must be a leaf the
// inventory hooks, and the SYMBOL must be the one it hooks there. Renaming a row
// is a compile error here rather than a fixture that passes while proving nothing.
static_assert(fl::inventory::FfxLeafOfExact(FL_STUB_FFX_MODULE) >= 0,
              "this stub's module name is not in fl::inventory::kFfxLeafModules, so it stands in for a module the "
              "Overlay does not hook");
static_assert(fl::stub::InventoryHasRow(FL_STUB_FFX_MODULE, "ffxDispatch"),
              "fl_hook_inventory.h has no ffxDispatch row for this module, so the fixture would exercise a name the "
              "Overlay does not install (20_OPEN_QUESTIONS S29(b), with a vendor's name on it)");

namespace {

// Per-descriptor-type counters, one per type the Overlay decodes plus one bucket
// for everything else. Named counters rather than a table so a test reads them by
// the vendor's own constant and a typo in the constant is a compile error.
volatile LONG g_upscale = 0;
volatile LONG g_prepare = 0;
volatile LONG g_prepareV2 = 0;
volatile LONG g_frameGeneration = 0;
volatile LONG g_other = 0;
volatile LONG g_contexts = 0;
// Calls that entered through the EXPORT, as opposed to the direct entry below. The
// loader decoy forwards through the direct entry -- the shape measured on the
// loader-shipping titles, whose leaf exports stayed silent -- so a test can assert
// that a dispatch pushed through the loader was counted once, at the leaf, and did
// NOT re-enter this export (which is the double count the Overlay's loader row
// would produce if the real loader forwarded that way).
volatile LONG g_exportCalls = 0;

ffxReturnCode_t DispatchBody(const ffxDispatchDescHeader* desc) {
    if (desc == nullptr) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    switch (desc->type) {
    case FFX_API_DISPATCH_DESC_TYPE_UPSCALE:
        InterlockedIncrement(&g_upscale);
        break;
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE:
        InterlockedIncrement(&g_prepare);
        break;
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2:
        InterlockedIncrement(&g_prepareV2);
        break;
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION:
        InterlockedIncrement(&g_frameGeneration);
        break;
    default:
        InterlockedIncrement(&g_other);
        break;
    }
    return FFX_API_RETURN_OK;
}

}    // namespace

extern "C" {

FFX_API_ENTRY ffxReturnCode_t ffxCreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                               const ffxAllocationCallbacks* memCb) {
    (void)memCb;
    if (context == nullptr || desc == nullptr) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    // An opaque non-null handle. Nothing dereferences it -- not the harness, not the
    // Overlay, whose detour reads the descriptor and never the context.
    *context = reinterpret_cast<ffxContext>(static_cast<uintptr_t>(InterlockedIncrement(&g_contexts)));
    return FFX_API_RETURN_OK;
}

FFX_API_ENTRY ffxReturnCode_t ffxDestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb) {
    (void)memCb;
    if (context != nullptr) {
        *context = nullptr;
    }
    return FFX_API_RETURN_OK;
}

FFX_API_ENTRY ffxReturnCode_t ffxConfigure(ffxContext* context, const ffxConfigureDescHeader* desc) {
    (void)context;
    return desc != nullptr ? FFX_API_RETURN_OK : FFX_API_RETURN_ERROR_PARAMETER;
}

FFX_API_ENTRY ffxReturnCode_t ffxQuery(ffxContext* context, ffxQueryDescHeader* desc) {
    (void)context;
    return desc != nullptr ? FFX_API_RETURN_OK : FFX_API_RETURN_ERROR_PARAMETER;
}

// THE ROW'S TARGET. Counts by head type and does nothing else -- the descriptor's
// body is never read here, so a test that sees renderW/H in a record knows the
// Overlay read it from the argument and not from anything this stub produced.
FFX_API_ENTRY ffxReturnCode_t ffxDispatch(ffxContext* context, const ffxDispatchDescHeader* desc) {
    (void)context;
    InterlockedIncrement(&g_exportCalls);
    return DispatchBody(desc);
}

// The DIRECT entry: the same counting, NOT through the ffx-api export, which is how
// the loader decoy reaches this leaf -- the shape the real loader was measured to
// have. FrameLedger-named, so nothing here pretends to be part of a vendor API, and
// never an inventory row.
__declspec(dllexport) ffxReturnCode_t FlStubFfxDispatchDirect(ffxContext* context, const ffxDispatchDescHeader* desc) {
    (void)context;
    return DispatchBody(desc);
}

// How many dispatches entered through the EXPORT (the hooked one), whichever type.
__declspec(dllexport) unsigned int FlStubFfxExportCalls() {
    return static_cast<unsigned int>(InterlockedCompareExchange(&g_exportCalls, 0, 0));
}

// FrameLedger-named observers, so nothing here pretends to be part of a vendor API.
// How many dispatches of THIS descriptor type reached this leaf; 0 for a type the
// stub does not bucket (the "other" bucket is FlStubFfxDispatchOther).
__declspec(dllexport) unsigned int FlStubFfxDispatchCount(uint64_t type) {
    volatile LONG* counter = nullptr;
    switch (type) {
    case FFX_API_DISPATCH_DESC_TYPE_UPSCALE:
        counter = &g_upscale;
        break;
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE:
        counter = &g_prepare;
        break;
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2:
        counter = &g_prepareV2;
        break;
    case FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION:
        counter = &g_frameGeneration;
        break;
    default:
        return 0u;
    }
    return static_cast<unsigned int>(InterlockedCompareExchange(counter, 0, 0));
}

__declspec(dllexport) unsigned int FlStubFfxDispatchOther() {
    return static_cast<unsigned int>(InterlockedCompareExchange(&g_other, 0, 0));
}

}    // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
