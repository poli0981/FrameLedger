// The vendor symbols FrameLedger.Overlay resolves by name, and the module each
// one must come from.
//
// ONE TABLE, THREE CONSUMERS, and that is the whole point of the file:
//
//   1. dllmain.cpp installs from it.
//   2. tools/hookinventory-check.ps1 parses it and asserts every row appears in
//      docs/vendor-exports.json -- measured data, so a symbol name that exists
//      only in somebody's memory fails the build.
//   3. tools/vendor-stubs generates its exported names from it, so the fixture
//      exercises the SHIPPED names rather than a second copy of them.
//
// That third consumer is the §S29(b) lesson applied before it can bite again:
// `ctest fl_vtable_indices` once proved a fact about dxgi.dll instead of a fact
// about FrameLedger.Overlay, because the harness declared its own copy of the
// slot numbers. A stub DLL exporting its own hand-typed spelling of
// "slEvaluateFeature" would be exactly that defect wearing a vendor's name --
// the test would pass while the Overlay looked for something else.
//
// WHY MODULE SCOPE IS A COLUMN AND NOT A COMMENT. Resolving a vendor symbol by
// name alone is not "slightly imprecise", it is wrong by a factor of seven.
// Measured from docs/vendor-exports.json on the dev machine:
//
//   NVSDK_NGX_D3D12_EvaluateFeature -> SEVEN modules  (nvngx_dlss, nvngx_dlssd,
//       nvngx_dlssg, nvngx_deepdvc, sl.common, _nvngx, nvngx)
//   slEvaluateFeature               -> ONE module     (sl.interposer)
//
// A name-resolved hook that catches the first match, or worse every match,
// counts one logical evaluation several times. That number is `fgEvaluations`,
// and 03_METRICS computes F_app = presents - sum(fgEvaluations), so an inflated
// count deflates Native FPS and inflates fg_factor -- the single inflated number
// CLAUDE.md rule 6 exists to forbid, arrived at by arithmetic rather than by a
// guess. (docs/HANDOFF.md says four modules and names
// `NVSDK_NGX_EvaluateFeature`, which no measured module exports at all. Both are
// corrected there in the PR that adds this file.)
//
// The Streamline tier has no such ambiguity, and that is a reason to start here
// rather than a lucky accident: one exporter, so scoping is free to hold.

#ifndef FRAMELEDGER_FL_HOOK_INVENTORY_H
#define FRAMELEDGER_FL_HOOK_INVENTORY_H

#include <windows.h>

#include <fl_shm.h>

namespace fl::inventory {

// X(module, symbol, family) -- module is the ONLY module this symbol may be
// taken from; family is the FlHookFamily bit published in
// FlWriterState::hooksInstalledMask once the hook is live.
//
// KEEP THE SPELLING EXACT. tools/hookinventory-check.ps1 checks each row against
// docs/vendor-exports.json, module-scoped: "does THIS module export THIS
// symbol", never "does anything export it".
#define FL_HOOK_INVENTORY(X) X(L"sl.interposer.dll", "slEvaluateFeature", fl::FL_HOOK_UPSCALER_IDENTITY)

// Resolve a symbol from ONE named module, or nothing.
//
// GetModuleHandleExW, NEVER LoadLibraryW, and the difference is not stylistic.
// LoadLibraryW would MAP A MODULE THE GAME DID NOT LOAD -- changing the host's
// module set to suit our measurement, which is both a lie about what the title
// is doing and a modification of a process we were invited to observe. If the
// game has not loaded Streamline, the honest answer is that there is no
// Streamline here.
//
// It takes a REFERENCE (no UNCHANGED_REFCOUNT) and never releases it, and that
// is deliberate. MinHook writes its patch into the target module's own code and
// keeps the hook in its table; if that module unloaded, MH_DisableHook(ALL) --
// which is the SAFETY STOP -- would write to unmapped memory and access-violate
// inside the one path in this codebase that must never misbehave. Holding a
// reference costs one refcount on a module the game loaded itself, hides
// nothing, and is visible to anyone listing our handles. GET_MODULE_HANDLE_EX_FLAG_PIN
// would do the same thing irreversibly; a plain reference is the smaller claim.
//
// Allocation-free and syscall-cheap: two documented calls, no enumeration.
inline void* ResolveScoped(const wchar_t* module, const char* symbol) noexcept {
    if (module == nullptr || symbol == nullptr) {
        return nullptr;
    }
    HMODULE h = nullptr;
    if (!GetModuleHandleExW(0, module, &h) || h == nullptr) {
        return nullptr;    // the game has not loaded it; that is an answer, not a failure
    }
    return reinterpret_cast<void*>(GetProcAddress(h, symbol));
}

}    // namespace fl::inventory

#endif    // FRAMELEDGER_FL_HOOK_INVENTORY_H
