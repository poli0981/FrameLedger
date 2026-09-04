// Shared body for the vendor-name stub DLLs.
//
// WHAT THESE ARE FOR. 17_HOOK_ENGINE §Hook inventory calls a wrong symbol name
// degrading silently to `unknown` "the highest false-confidence risk in the
// spike", and until now nothing tested it: a typo in the Overlay's symbol string
// produces a writer that resolves nothing, installs nothing, and reports exactly
// what an honest writer on a title with no upscaler reports. Green everywhere.
//
// A stub DLL carrying the MEASURED vendor name turns that into an observable
// event -- the Overlay either finds this function or it does not, and the
// difference is a counter.
//
// WHAT THEY ARE NOT. No vendor code, no vendor behaviour, and no claim about the
// real ABI beyond the one thing the vendored MIT header already tells us: the
// signature. These export a name and count calls. They are fixtures, they live
// under tools/ and never under third_party/ (tools/license-check.ps1 keys on
// that directory and would demand a licence file for our own code), and nothing
// ships them -- 12_BUILD publishes FrameLedger.App and FrameLedger.Agent only.
//
// THE NAME IS BOUND TO THE OVERLAY'S, NOT TYPED TWICE. InventoryHas() below is a
// constexpr walk of FL_HOOK_INVENTORY, so a stub asserting a name the inventory
// no longer contains FAILS TO COMPILE. That is the §S29(b) lesson applied before
// it bites: `ctest fl_vtable_indices` once proved a fact about dxgi.dll rather
// than about FrameLedger.Overlay, because the harness kept its own copy of the
// constants. A stub exporting its own hand-typed "slEvaluateFeature" would be
// the same defect wearing a vendor's name -- the fixture would pass while the
// Overlay looked for something else.

#ifndef FRAMELEDGER_STUB_COMMON_H
#define FRAMELEDGER_STUB_COMMON_H

#include <windows.h>

#include <fl_hook_inventory.h>

namespace fl::stub {

// constexpr string compare, so the binding below is a COMPILE error rather than
// a runtime disagreement nobody runs.
constexpr bool Same(const char* a, const char* b) noexcept {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

// Does the Overlay's inventory contain this symbol name?
constexpr bool InventoryHas(const char* name) noexcept {
#define FL_STUB_MATCH_ROW(mod, sym, family)                                                                            \
    if (Same(sym, name)) {                                                                                             \
        return true;                                                                                                   \
    }
    FL_HOOK_INVENTORY(FL_STUB_MATCH_ROW)
#undef FL_STUB_MATCH_ROW
    return false;
}

// Does the Overlay's inventory contain this symbol FROM THIS MODULE? The
// module-scoped form, for a symbol several modules export: `InventoryHas("ffxDispatch")`
// is true whichever AMD module a stub stands in for, and a stub for the LOADER --
// which must NOT be a row -- needs to assert the negative about its own name.
constexpr bool InventoryHasRow(const wchar_t* module, const char* symbol) noexcept {
#define FL_STUB_MATCH_MODULE_ROW(mod, sym, family)                                                                     \
    if (fl::inventory::SameW(mod, module) && Same(sym, symbol)) {                                                      \
        return true;                                                                                                   \
    }
    FL_HOOK_INVENTORY(FL_STUB_MATCH_MODULE_ROW)
#undef FL_STUB_MATCH_MODULE_ROW
    return false;
}

}    // namespace fl::stub

#endif    // FRAMELEDGER_STUB_COMMON_H
