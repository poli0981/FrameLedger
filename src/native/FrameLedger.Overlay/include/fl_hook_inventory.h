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
// IDENTITY ONLY, since 2026-09-03. Until then the constant was
// `IDENTITY | FG_EVALUATIONS`, because the same detour counted
// slEvaluateFeature(kFeatureDLSS_G) as the application-frame count -- and five
// real titles measured that count at zero (Cyberpunk, Wukong, Rune Factory, Alan
// Wake 2, Hell Is Us): DLSS-G is not driven through the evaluate entry point on
// Streamline 2.x. The application-frame count moved to its own row,
// slGetNewFrameToken, which every Streamline 2 title calls once per frame BY
// CONTRACT (sl_core_api.h: "obtain unique instance" per frame) -- including the
// ones that never evaluate anything through this export. The family bit moved with
// the count, so EntitledBy still binds FL_MEASURED_FG_COUNTS to the detour that
// actually produces it.
inline constexpr uint32_t kFamilyEvaluateFeature = static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_IDENTITY);

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
    static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS) | static_cast<uint32_t>(fl::FL_HOOK_RT_DISPATCH) |
    static_cast<uint32_t>(fl::FL_HOOK_RT_AS_BUILD) | static_cast<uint32_t>(fl::FL_HOOK_RT_PSO) |
    static_cast<uint32_t>(fl::FL_HOOK_PSO) | static_cast<uint32_t>(fl::FL_HOOK_COLOR_SPACE) |
    static_cast<uint32_t>(fl::FL_HOOK_REFLEX) | static_cast<uint32_t>(fl::FL_HOOK_VRAM);

static_assert((kFamilyEvaluateFeature & kFamiliesEvaluateFeatureDoesNotImplement) == 0u,
              "the slEvaluateFeature row claims a family its detour does not implement -- hookinventory-check "
              "reads this column as an opaque identifier and cannot see it, so a compile error is the only "
              "verdict available");
static_assert((kFamilyEvaluateFeature & static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_IDENTITY)) != 0u,
              "the detour decodes sl::Feature, so it must claim upscaler identity");
static_assert((kFamilyEvaluateFeature & static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS)) == 0u,
              "the evaluate detour no longer produces the application-frame count -- slGetNewFrameToken does -- "
              "so claiming FG_EVALUATIONS here would entitle FL_MEASURED_FG_COUNTS to a detour that writes "
              "nothing into fgEvaluations");

// --- AMD FidelityFX, through the ffx-api facade (2026-09-04, HANDOFF item 7c) ----
//
// ONE FAMILY, THREE ROWS, THREE MODULES, AND EVERY ROW IS THE SAME EXPORT NAME.
// `ffxDispatch` is the single call an ffx-api title routes every per-frame piece
// of work through -- the upscale, the frame-generation PREPARE, and the generated
// batch -- and the DESCRIPTOR it is handed says which (ffxApiHeader::type, a value
// from the vendored MIT headers). So the one detour IDENTIFIES the upscaler (an
// UPSCALE dispatch arrived), publishes the render extent (the same descriptor's
// renderSize: PARAMS) and COUNTS application frames (the PREPARE descriptor's
// frameID, which the vendor documents as "must increment by exactly one for each
// frame" -- this vendor's slGetNewFrameToken). Three families from one body, hence
// a compound constant, and it must differ from all three single-bit values above
// because InstallByFamily binds by EQUALITY (dllmain.cpp).
//
// WHY THESE THREE MODULES, AND WHY NOT A FOURTH. Measured on installed titles,
// 2026-09-04:
//
//   amd_fidelityfx_dx12.dll                  SDK 1.1.x MONOLITH -- Lies of P, Cyberpunk 2077,
//                                            Rune Factory, Black Myth: Wukong (1.0.1.41314)
//   amd_fidelityfx_upscaler_dx12.dll         SDK 2.x effect DLL: the FSR 3.1 AND FSR 4 providers
//   amd_fidelityfx_framegeneration_dx12.dll  SDK 2.x effect DLL: FSR 3.1 / FSR 4 frame generation
//                                            and the frame-generation swapchain
//   amd_fidelityfx_loader_dx12.dll           SDK 2.x LOADER: no effect code, forwards to the two above
//
// All four export the same five names and nothing else (docs/vendor-exports.json),
// so the loader can reach an effect DLL only through the effect DLL's OWN exports
// -- and a UE5 title ships the two effect DLLs with NO loader at all (Hell Is Us,
// Expedition 33: the engine plugin compiles the MIT loader source in and calls the
// leaves directly). Every route therefore ends at a LEAF, and the leaves are what
// is hooked. Hooking the loader as well would count each dispatch TWICE on a
// loader-shipping title, straight into fg_factor -- the exact defect the module
// column exists to prevent, one vendor over. The loader's name is kept here ONLY
// so a static_assert below can refuse it as a row, and so the fixtures can build a
// forwarding decoy that proves the leaf count reads 1x with a loader in the chain.
//
// The effect-DLL split is NOT a guess about the vendor's internals: the FidelityFX
// SDK's own getting-started text says the loader "only manages the loading of
// effect type DLLs" and is "interface- and behavior-compatible with
// amd_fidelityfx_dx12.dll". The monolith IS its own leaf -- SDK 1.1.x compiled the
// providers in -- which is why it is a row and the loader is not.
inline constexpr uint32_t kFamilyFfxDispatch = static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_IDENTITY) |
                                               static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS) |
                                               static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS);

// The complement, as its own list, for the reason kFamiliesEvaluateFeatureDoesNotImplement
// gives: a bit added above has to be REMOVED here before the build goes green, and that
// is the moment somebody decides whether the detour actually implements the family.
inline constexpr uint32_t kFamiliesFfxDispatchDoesNotImplement =
    static_cast<uint32_t>(fl::FL_HOOK_PRESENT) | static_cast<uint32_t>(fl::FL_HOOK_RT_DISPATCH) |
    static_cast<uint32_t>(fl::FL_HOOK_RT_AS_BUILD) | static_cast<uint32_t>(fl::FL_HOOK_RT_PSO) |
    static_cast<uint32_t>(fl::FL_HOOK_PSO) | static_cast<uint32_t>(fl::FL_HOOK_COLOR_SPACE) |
    static_cast<uint32_t>(fl::FL_HOOK_REFLEX) | static_cast<uint32_t>(fl::FL_HOOK_VRAM);

static_assert((kFamilyFfxDispatch & kFamiliesFfxDispatchDoesNotImplement) == 0u,
              "the ffxDispatch rows claim a family their detour does not implement -- hookinventory-check reads "
              "this column as an opaque identifier and cannot see it, so a compile error is the only verdict");
static_assert((kFamilyFfxDispatch & static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_IDENTITY)) != 0u,
              "an UPSCALE dispatch descriptor names the upscaler, so the row must claim identity");
static_assert((kFamilyFfxDispatch & static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS)) != 0u,
              "the UPSCALE descriptor carries renderSize, so the row must claim params");
static_assert((kFamilyFfxDispatch & static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS)) != 0u,
              "the PREPARE descriptor's frameID is the application-frame count, so the row must claim FG counts");
static_assert(kFamilyFfxDispatch != kFamilyEvaluateFeature &&
                  kFamilyFfxDispatch != static_cast<uint32_t>(fl::FL_HOOK_UPSCALER_PARAMS) &&
                  kFamilyFfxDispatch != static_cast<uint32_t>(fl::FL_HOOK_FG_EVALUATIONS),
              "InstallByFamily binds a row by EQUALITY on this value -- a collision would hand a Streamline detour "
              "body to an AMD export, which is the neighbour's-body defect the installer exists to stop");

// The leaves, in the order dllmain's per-leaf trampoline and latch arrays are indexed.
enum FfxLeaf : uint32_t {
    kFfxLeafMonolith = 0,           // amd_fidelityfx_dx12.dll -- SDK 1.1.x, FSR 3.1 only
    kFfxLeafUpscaler = 1,           // amd_fidelityfx_upscaler_dx12.dll -- SDK 2.x, FSR 3.1 or FSR 4
    kFfxLeafFrameGeneration = 2,    // amd_fidelityfx_framegeneration_dx12.dll -- SDK 2.x
    kFfxLeafCount = 3,
};

inline constexpr const wchar_t* kFfxLeafModules[kFfxLeafCount] = {
    L"amd_fidelityfx_dx12.dll",
    L"amd_fidelityfx_upscaler_dx12.dll",
    L"amd_fidelityfx_framegeneration_dx12.dll",
};

// NOT A ROW, and asserted so below. Named here for the static_assert and for the
// forwarding decoy in tools/vendor-stubs, nowhere else.
inline constexpr const wchar_t* kFfxLoaderModule = L"amd_fidelityfx_loader_dx12.dll";

// constexpr string compares, so the bindings below are COMPILE errors rather than
// runtime disagreements nobody runs. Case-sensitive: they compare this file's
// spellings with each other, not a loader's answer with ours.
constexpr bool SameW(const wchar_t* a, const wchar_t* b) noexcept {
    while (*a != L'\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
constexpr bool SameA(const char* a, const char* b) noexcept {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

// Which leaf slot a row's module is, exactly as spelled in the table. -1 if none.
constexpr int FfxLeafOfExact(const wchar_t* module) noexcept {
    for (uint32_t i = 0; i < kFfxLeafCount; ++i) {
        if (SameW(module, kFfxLeafModules[i])) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The runtime twin: the loader is case-insensitive about module names, so this is
// too. dllmain's installer indexes its per-leaf storage with the result.
inline int FfxLeafOf(const wchar_t* module) noexcept {
    if (module == nullptr) {
        return -1;
    }
    for (uint32_t i = 0; i < kFfxLeafCount; ++i) {
        if (_wcsicmp(module, kFfxLeafModules[i]) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// slGetNewFrameToken IS THE APPLICATION-FRAME PRODUCER (2026-09-03, HANDOFF item 3's
// decision). Streamline hands a title one frame token per frame, and the title
// must ask for it; the detour counts DISTINCT tokens between two presents, so a
// title that re-requests the same frame's token (explicit frameIndex) counts once.
// Presents / tokens is fg_factor -- with no premise about Ray Reconstruction
// batches, and on titles that never call slEvaluateFeature at all. The premise it
// DOES carry -- that the DLSS-G plugin does not request tokens for the frames it
// generates -- is what 20_OPEN_QUESTIONS §S31's pre-committed table exists to test
// against a title's own x2 / x4 before a ratio near 1 may be read as "none".
// THE THREE ffxDispatch ROWS ARE THE LEAVES, spelled as literals because Pass A's
// parser reads literals; the static_asserts under the table bind these spellings to
// kFfxLeafModules in both directions, so neither list can gain or lose a module alone.
#define FL_HOOK_INVENTORY(X)                                                                                           \
    X(L"sl.interposer.dll", "slEvaluateFeature", fl::inventory::kFamilyEvaluateFeature)                                \
    X(L"sl.interposer.dll", "slSetTag", fl::FL_HOOK_UPSCALER_PARAMS)                                                   \
    X(L"sl.interposer.dll", "slGetNewFrameToken", fl::FL_HOOK_FG_EVALUATIONS)                                          \
    X(L"amd_fidelityfx_dx12.dll", "ffxDispatch", fl::inventory::kFamilyFfxDispatch)                                    \
    X(L"amd_fidelityfx_upscaler_dx12.dll", "ffxDispatch", fl::inventory::kFamilyFfxDispatch)                           \
    X(L"amd_fidelityfx_framegeneration_dx12.dll", "ffxDispatch", fl::inventory::kFamilyFfxDispatch)

// THE LEAF TABLE AND THE ROWS ARE BOUND TO EACH OTHER AT COMPILE TIME, BOTH WAYS.
// A leaf without a row would be a trampoline slot nothing installs into; a row
// without a leaf would resolve, reach InstallByFamily, and find no slot -- and the
// installer's answer to that is to install nothing, silently, which is the shape
// this whole file exists to make loud.
constexpr bool FfxDispatchRowExistsFor(const wchar_t* module) noexcept {
#define FL_FFX_ROW_FOR(mod, sym, family)                                                                               \
    if (SameW(mod, module) && SameA(sym, "ffxDispatch")) {                                                             \
        return true;                                                                                                   \
    }
    FL_HOOK_INVENTORY(FL_FFX_ROW_FOR)
#undef FL_FFX_ROW_FOR
    return false;
}
constexpr bool EveryFfxDispatchRowIsALeaf() noexcept {
#define FL_FFX_ROW_IS_LEAF(mod, sym, family)                                                                           \
    if (SameA(sym, "ffxDispatch") && FfxLeafOfExact(mod) < 0) {                                                        \
        return false;                                                                                                  \
    }
    FL_HOOK_INVENTORY(FL_FFX_ROW_IS_LEAF)
#undef FL_FFX_ROW_IS_LEAF
    return true;
}
constexpr bool AnyRowNames(const wchar_t* module) noexcept {
#define FL_ANY_ROW_NAMES(mod, sym, family)                                                                             \
    if (SameW(mod, module)) {                                                                                          \
        return true;                                                                                                   \
    }
    FL_HOOK_INVENTORY(FL_ANY_ROW_NAMES)
#undef FL_ANY_ROW_NAMES
    return false;
}
static_assert(FfxDispatchRowExistsFor(kFfxLeafModules[kFfxLeafMonolith]) &&
                  FfxDispatchRowExistsFor(kFfxLeafModules[kFfxLeafUpscaler]) &&
                  FfxDispatchRowExistsFor(kFfxLeafModules[kFfxLeafFrameGeneration]),
              "every ffx leaf needs its ffxDispatch row, spelled identically -- a leaf with no row is a trampoline "
              "slot nothing installs into");
static_assert(EveryFfxDispatchRowIsALeaf(),
              "an ffxDispatch row names a module the leaf table does not know -- InstallByFamily would resolve it "
              "and find no trampoline slot, and install nothing without saying so");
static_assert(!AnyRowNames(kFfxLoaderModule),
              "amd_fidelityfx_loader_dx12.dll is a FORWARDER and must never be a row: it reaches the effect DLLs "
              "through their own ffxDispatch exports, so hooking it as well counts every dispatch twice, straight "
              "into fg_factor");

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
// Does this module speak the ffx-api ABI the vendored headers describe?
//
// A NAME CHECK, AND SAID SO. The five entry points are the whole of the ffx-api
// surface -- a module of a leaf's name that lacks any of them is not an ffx-api
// module at all, whatever else it is. It is deliberately NOT a version check: ffx-api
// is designed for ABI stability across SDK generations (the same five names, the same
// header-typed descriptors, from the 1.1.x monolith through the 2.x effect DLLs), and
// the vendored 2.3.0 declarations decode every generation measured. The guard that
// actually protects the read is in the detour: a descriptor is reinterpreted only
// AFTER its head type matched a constant whose layout is identical in every tag
// consulted, and nothing past the head is touched otherwise.
//
// Probe-only names, like SpeaksStreamline2's: nothing resolves or calls them, and
// they live in this file because Pass B exempts exactly this file.
inline bool SpeaksFfxApi(HMODULE h) noexcept {
    return h != nullptr && GetProcAddress(h, "ffxCreateContext") != nullptr &&
           GetProcAddress(h, "ffxDestroyContext") != nullptr && GetProcAddress(h, "ffxConfigure") != nullptr &&
           GetProcAddress(h, "ffxQuery") != nullptr && GetProcAddress(h, "ffxDispatch") != nullptr;
}

inline bool SpeaksExpectedAbi(const wchar_t* module, HMODULE h) noexcept {
    if (module == nullptr) {
        return false;
    }
    if (_wcsicmp(module, L"sl.interposer.dll") == 0) {
        return SpeaksStreamline2(h);
    }
    // THE AMD ARM, by leaf name, for the same reason the Streamline arm is by name:
    // the loader also exports all five and is not a leaf, so a prefix test would
    // approve a module the inventory deliberately does not hook.
    if (FfxLeafOf(module) >= 0) {
        return SpeaksFfxApi(h);
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
