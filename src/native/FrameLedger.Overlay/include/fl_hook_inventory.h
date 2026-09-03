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
// and 03_METRICS computes F_app = sum(fgEvaluations) with
// fg_factor = presents / F_app -- so an inflated count INFLATES Native FPS and
// DEFLATES fg_factor, reporting a title as generating fewer frames than it does.
// Either direction is the same class of defect: a number arrived at by arithmetic
// rather than by measurement, which is what CLAUDE.md rule 6 exists to forbid.
// (The polarity here is the 2026-08-14 owner ruling; this comment described the
// pre-ruling subtraction until the producer landed.) (docs/HANDOFF.md says four modules and names
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
// TWO ROWS, TWO FAMILIES, TWO DETOURS. dllmain's installer binds each row by its
// family bit; a row whose family has no detour installs nothing rather than
// borrowing a neighbour's body.
//
// slSetTag carries the GLOBAL resource tags, which is where a title states the
// size of the buffer it is upscaling FROM. sl_core_api.h documents local tags as
// merely *allowed* in slEvaluateFeature's `inputs` ("they do NOT interact with
// same tags sent in the global scope"), so a global-tagging title yields nothing
// from the inputs walk -- and only four of the ten Streamline titles measured on
// this machine route DLSS super-resolution through Streamline at all. Deferring
// this row would have shipped a producer whose hit rate could be zero.
// ONE DETOUR, TWO FAMILIES, and the constant exists so a gate can read its VALUE.
//
// `slEvaluateFeature` is the single call a Streamline title routes every feature
// evaluation through, so the one detour on it both IDENTIFIES the upscaler and
// COUNTS frame-generation evaluations. Those are two FlHookFamily bits, and
// hooksInstalledMask has to carry both or `MeasuredFacts.EntitledBy` refuses the
// FG_COUNTS claim the same hook is entitled to make.
//
// WHY A NAMED CONSTANT WITH A static_assert RATHER THAN AN INLINE `A | B`.
// tools/hookinventory-check.ps1 captures the family column as an OPAQUE IDENTIFIER
// (`([A-Za-z_][A-Za-z0-9_:]*)`) and never evaluates it -- Pass A checks module and
// symbol against the measured oracle and nothing checks the third column at all. So
// a plausible mistake here, `| FL_HOOK_RT_PSO` copied from item 4's rows landing
// next, would compile, parse, install, and publish an RT hook family that does not
// exist: `EntitledBy` would then grant FlMeasured.Rt and `IsHonest` would stop
// catching an RT over-claim, with every gate in the tree green. A compile error is
// the right verdict for a family the detour does not honour, and this is the only
// check in the chain that can produce one.
inline constexpr uint32_t kFamilyEvaluateFeature =
    static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_IDENTITY) | static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS);

// EVERY FAMILY THIS DETOUR DOES NOT IMPLEMENT, NAMED. The obvious assertion --
// `kFamilyEvaluateFeature == (IDENTITY | FG_EVALUATIONS)` -- is a TAUTOLOGY against
// the line above it and can never fail, which is this project's signature defect
// wearing a static_assert. Stating the complement cannot be tautological: it is a
// second, independent list, and a bit added to the constant has to be REMOVED from
// here before the build goes green, which is precisely the moment someone has to
// decide whether the detour actually implements that family.
//
// It also fails usefully when FlHookFamily GAINS a member: the new bit is in
// neither list, the compile still passes, and that is the one case worth
// tolerating -- a family nobody has wired cannot be wrongly claimed by this row.
inline constexpr uint32_t kFamiliesEvaluateFeatureDoesNotImplement =
    static_cast<uint32_t>(fl::FL_HOOK_PRESENT) | static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS) |
    static_cast<uint32_t>(fl::FL_HOOK_RT_DISPATCH) | static_cast<uint32_t>(fl::FL_HOOK_RT_AS_BUILD) |
    static_cast<uint32_t>(fl::FL_HOOK_RT_PSO) | static_cast<uint32_t>(fl::FL_HOOK_PSO) |
    static_cast<uint32_t>(fl::FL_HOOK_COLOR_SPACE) | static_cast<uint32_t>(fl::FL_HOOK_REFLEX) |
    static_cast<uint32_t>(fl::FL_HOOK_VRAM);

static_assert((kFamilyEvaluateFeature & kFamiliesEvaluateFeatureDoesNotImplement) == 0u,
              "the slEvaluateFeature row claims a family its detour does not implement -- hookinventory-check "
              "reads this column as an opaque identifier and cannot see it, so a compile error is the only "
              "verdict available");
static_assert((kFamilyEvaluateFeature & static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_IDENTITY)) != 0u,
              "the detour decodes sl::Feature, so it must claim upscaler identity");
static_assert((kFamilyEvaluateFeature & static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS)) != 0u,
              "the detour counts kFeatureDLSS_G evaluations, so it must claim FG evaluations -- without this "
              "bit EntitledBy refuses FL_MEASURED_FG_COUNTS and every record over-claims");

#define FL_HOOK_INVENTORY(X)                                                                                           \
    X(L"sl.interposer.dll", "slEvaluateFeature", fl::inventory::kFamilyEvaluateFeature)                                \
    X(L"sl.interposer.dll", "slSetTag", fl::FL_HOOK_UPSCALER_PARAMS)

// Does this module speak the Streamline 2 ABI the vendored headers describe?
//
// MEASURED 2026-08-14, and it cost an access violation to find. The Witcher 3
// ships sl.interposer.dll 1.5.6 -- a different API generation. It exports
// slInit, slEvaluateFeature, slSetTag and slShutdown, so a name check passes;
// it also exports slGetHooks, slIsFeatureEnabled and slSetFeatureConstants,
// which SL2 does not have, and it does NOT export the three below.
//
// THE NAMES SURVIVED THE VERSION BUMP AND THE SIGNATURES DID NOT, which is the
// whole hazard. docs/vendor-exports.json records ONE COPY PER MODULE NAME, so
// hookinventory-check Pass A resolves `slEvaluateFeature` against one machine's
// 2.7.4 and says nothing about the 1.5.6 a title ships. A detour typed with
// PFun_slEvaluateFeature -- SL2's (Feature, const FrameToken&,
// const BaseStructure**, uint32_t, CommandBuffer*) -- would then be called with
// SL1's argument list. Reading argument 1 as a feature id when it is not one
// yields a WRONG UPSCALER NAME, not a crash, which is the failure class
// 17_HOOK_ENGINE calls the highest false-confidence risk in the spike.
//
// So we refuse the module rather than hook it, and the record then says nothing
// about the upscaler -- which is true. FL_UPSCALER_NOT_REPORTED, not a guess.
//
// WHY THESE THREE NAMES ARE NOT INVENTORY ROWS. A row is a symbol we DETOUR;
// these are symbols we probe for existence and never call, so they carry no
// signature risk of their own. They live in this header because it is the file
// that owns which vendor names the Overlay speaks -- and because
// hookinventory-check Pass B excludes exactly this file from its stray-literal
// sweep, so putting them anywhere else in the Overlay would fail that gate for a
// reason unrelated to what the gate is for.
inline bool SpeaksStreamline2(HMODULE h) noexcept {
    return h != nullptr && GetProcAddress(h, "slSetD3DDevice") != nullptr &&
           GetProcAddress(h, "slIsFeatureLoaded") != nullptr && GetProcAddress(h, "slGetNewFrameToken") != nullptr;
}

// Is this module's ABI one we compiled against?
//
// SCOPED TO THE INTERPOSER BY NAME, not to `sl.*`, and the first version of this
// got that wrong. The three markers above are exports of the INTERPOSER; the
// Streamline plugins are a different shape entirely -- a real sl.common.dll
// exports the NGX C API and slGetPluginFunction and none of the three. A `sl.`
// prefix test therefore refuses modules whose generation it has no business
// judging, and the decoy fixture in --probe-upscaler-resolve caught it doing
// exactly that.
//
// Scoping to sl.interposer.dll is also all that is needed: slEvaluateFeature has
// exactly ONE exporter in the measured data, and it is this module. Anything else
// passes, because "no known ABI hazard" is the honest default for a module class
// nobody has measured a break in -- and inventing a check for one would be
// guessing, not guarding.
inline bool SpeaksExpectedAbi(const wchar_t* module, HMODULE h) noexcept {
    if (module == nullptr) {
        return false;
    }
    if (_wcsicmp(module, L"sl.interposer.dll") == 0) {
        return SpeaksStreamline2(h);
    }
    return true;
}

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
    // A module of the right NAME whose ABI is not ours is the same answer as no
    // module: we cannot read its arguments, so we do not hook it. See
    // SpeaksExpectedAbi -- this is the Witcher 3 case, and refusing here is what
    // keeps a wrong upscaler name out of the record.
    if (!SpeaksExpectedAbi(module, h)) {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(h, symbol));
}

// ---------------------------------------------------------------------------
// THE RUNTIME CENSUS. Not inventory rows: nothing here is detoured, resolved or
// called. Each row is a module NAME the loader is asked about once a second on
// the watchdog, and the FlRuntimeCensus bit that name publishes.
//
// The names are the measured ones in docs/vendor-exports.json, and
// hookinventory-check Pass D fails the build when a row names a module that
// data has never seen -- for the same reason Pass A exists: a misspelt name here
// degrades silently to "not loaded", which reads as the 2D-title case and would
// print the very qualifier this census exists to withhold on a DLSS-G title.
//
// Lives in this header, not dllmain.cpp, because Pass B sweeps every other
// Overlay source for vendor-shaped literals and this is the one file it exempts.
// ---------------------------------------------------------------------------
#define FL_RUNTIME_CENSUS(X)                                                                                           \
    X(L"sl.dlss_g.dll", fl::FL_CENSUS_SL_DLSS_G)                                                                       \
    X(L"nvngx_dlssg.dll", fl::FL_CENSUS_NVNGX_DLSSG)                                                                   \
    X(L"libxess_fg.dll", fl::FL_CENSUS_LIBXESS_FG)                                                                     \
    X(L"ffx_frameinterpolation_x64.dll", fl::FL_CENSUS_FFX_FRAMEINTERPOLATION)                                         \
    X(L"ffx_fsr3_x64.dll", fl::FL_CENSUS_FFX_FSR3)                                                                     \
    X(L"amd_fidelityfx_framegeneration_dx12.dll", fl::FL_CENSUS_AMD_FFX_FRAMEGENERATION)                               \
    X(L"sl.interposer.dll", fl::FL_CENSUS_SL_INTERPOSER)                                                               \
    X(L"sl.dlss.dll", fl::FL_CENSUS_SL_DLSS)                                                                           \
    X(L"sl.nis.dll", fl::FL_CENSUS_SL_NIS)                                                                             \
    X(L"nvngx.dll", fl::FL_CENSUS_NVNGX_CORE)                                                                          \
    X(L"_nvngx.dll", fl::FL_CENSUS_NVNGX_CORE)                                                                         \
    X(L"nvngx_dlss.dll", fl::FL_CENSUS_NVNGX_DLSS)                                                                     \
    X(L"nvngx_dlssd.dll", fl::FL_CENSUS_NVNGX_DLSSD)                                                                   \
    X(L"libxess.dll", fl::FL_CENSUS_LIBXESS)                                                                           \
    X(L"libxess_dx11.dll", fl::FL_CENSUS_LIBXESS)                                                                      \
    X(L"ffx_fsr2_api_x64.dll", fl::FL_CENSUS_FFX_FSR2)                                                                 \
    X(L"ffx_fsr2_api_dx12_x64.dll", fl::FL_CENSUS_FFX_FSR2)                                                            \
    X(L"ffx_fsr3upscaler_x64.dll", fl::FL_CENSUS_FFX_FSR3_UPSCALER)                                                    \
    X(L"amd_fidelityfx_upscaler_dx12.dll", fl::FL_CENSUS_AMD_FFX_UPSCALER)                                             \
    X(L"amd_fidelityfx_dx12.dll", fl::FL_CENSUS_AMD_FFX_DX12)

// Is a module of this name in the process right now? UNCHANGED_REFCOUNT, unlike
// ResolveScoped: the census patches nothing, so it has no reason to keep the
// module alive and takes the smaller claim. Allocation-free, one documented call.
inline bool IsModuleLoaded(const wchar_t* module) noexcept {
    HMODULE h = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, module, &h) && h != nullptr;
}

// One census pass. Always carries FL_CENSUS_RAN, so a word without it means the
// census never ran rather than "ran and saw nothing".
inline uint32_t ObserveRuntimeModules() noexcept {
    uint32_t seen = static_cast<uint32_t>(fl::FL_CENSUS_RAN);
#define FL_CENSUS_ROW(name, bit)                                                                                       \
    if (IsModuleLoaded(name)) {                                                                                        \
        seen |= static_cast<uint32_t>(bit);                                                                            \
    }
    FL_RUNTIME_CENSUS(FL_CENSUS_ROW)
#undef FL_CENSUS_ROW
    return seen;
}

}    // namespace fl::inventory

#endif    // FRAMELEDGER_FL_HOOK_INVENTORY_H
