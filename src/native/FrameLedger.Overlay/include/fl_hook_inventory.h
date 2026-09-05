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
// ONE FAMILY, FOUR ROWS, FOUR MODULES, AND EVERY ROW IS THE SAME EXPORT NAME.
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
// WHY THESE FOUR MODULES. Measured on installed titles, 2026-09-04:
//
//   amd_fidelityfx_dx12.dll                  SDK 1.1.x MONOLITH -- Lies of P, Cyberpunk 2077,
//                                            Rune Factory, Black Myth: Wukong (1.0.1.41314)
//   amd_fidelityfx_upscaler_dx12.dll         SDK 2.x effect DLL: the FSR 3.1 AND FSR 4 providers
//   amd_fidelityfx_framegeneration_dx12.dll  SDK 2.x effect DLL: FSR 3.1 / FSR 4 frame generation
//                                            and the frame-generation swapchain
//   amd_fidelityfx_loader_dx12.dll           SDK 2.x LOADER: no effect code, hands the game's
//                                            calls to the two above
//
// THE ROW SET IS "WHATEVER MODULE THE GAME CALLS", AND THAT WAS MEASURED TWICE IN
// ONE DAY, WITH OPPOSITE ANSWERS FOR THE LOADER. The first version of this table
// hooked the three leaves and refused the loader as a row, on the argument that all
// four modules export only the five ffx-api names, so the loader could reach an
// effect DLL only through the effect DLL's own export -- and hooking both would
// count every dispatch twice. The UE5 half of that held: Hell Is Us and Expedition
// 33 ship the two effect DLLs with NO loader (the engine plugin compiles the MIT
// loader source in) and their dispatches arrive at the leaves' exports, 1x. The
// loader half did not: Dying Light: The Beast at FSR + FSR frame generation, Kingdom
// Come: Deliverance II at FSR and Black Myth: Wukong at FSR each produced ZERO
// dispatches at any leaf export while the leaves sat in the census -- the game
// calls the LOADER's export, and however the signed loader reaches its providers
// afterwards (the MIT source shows an "external provider" object, not an export
// call), it is not through the leaves' ffxDispatch. So the loader IS the game's
// entry on a loader-shipping title, and it is a row. The two counts the consumer
// prints against each other (frames/upscale-drained) and the K = 1 control in the
// fixtures are what would show a loader that re-entered a leaf's export: 2x on both.
//
// The effect-DLL split is NOT a guess about the vendor's internals: the FidelityFX
// SDK's own getting-started text says the loader "only manages the loading of
// effect type DLLs" and is "interface- and behavior-compatible with
// amd_fidelityfx_dx12.dll". The monolith IS its own leaf -- SDK 1.1.x compiled the
// providers in.
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

// --- AMD FidelityFX, the FSR 3.0 HOST API (2026-09-05, the route HANDOFF 7c deferred) ---
//
// Cyberpunk 2077 at FSR 3 upscales through ffx_fsr3_x64.dll's NAMED exports -- the
// SDK 1.0.x host API, tag fsr3-v3.0.4 -- while its frame generation goes through the
// 1.1.x monolith's ffxDispatch above. Measured 2026-09-04: `FsrFg` printed, `upscaler:
// N/A`. One row, ffxFsr3ContextDispatchUpscale, whose descriptor carries renderSize
// at an offset the vendored header vouches for; identity is FSR3 as a fact (the 3.0
// host hosts nothing else) and the UPSCALE count feeds the SAME drain word the
// ffx-api rows feed.
//
// THE SAME FAMILY VALUE AS THE ffx-api ROWS, ON PURPOSE. The row's UPSCALE goes into
// g_ffxSeen's count, which RecordPresent drains into fgEvaluations when Streamline has
// never issued a token and no PREPARE ever arrived -- so a row claiming IDENTITY|PARAMS
// alone would feed a field its family does not entitle, and every record on a
// host-only title would be an honesty violation. The installer's ffx arm binds on the
// family and then keys on the MODULE (FfxLeafOf for the four ffxDispatch modules, this
// name for the host), exactly as the params arm keys on the symbol -- so the equality
// collision the asserts above forbid between DIFFERENT detour bodies is not one here:
// one family, one publish point (PublishFfxFamilyIfWhole), one live latch.
//
// ffxFsr3DispatchFrameGeneration IS NOT A ROW. On the one title that ships the host
// DLLs, frame generation is dispatched through the monolith's ffxDispatch (4261
// FRAMEGENERATION batches, 2026-09-04) and the count comes from Streamline tokens; the
// host's FG export fires per GENERATED batch, so it is not a count producer either.
// It becomes a row only if a title appears generating through the host DLL with no
// monolith in the census -- `Active (technology not identified)` beside
// FL_CENSUS_FFX_FSR3. Pre-committed in 20_OPEN_QUESTIONS §H11.
inline constexpr uint32_t kFamilyFsr3Host = kFamilyFfxDispatch;
static_assert(kFamilyFsr3Host == kFamilyFfxDispatch,
              "one family, one publish point, one live latch: the host row shares the ffx-api rows' drain word and "
              "must share their claims, or fgEvaluations would be fed by a row that does not claim FG_EVALUATIONS");

// The leaves, in the order dllmain's per-leaf trampoline and latch arrays are indexed.
enum FfxLeaf : uint32_t {
    kFfxLeafMonolith = 0,           // amd_fidelityfx_dx12.dll -- SDK 1.1.x, FSR 3.1 only
    kFfxLeafUpscaler = 1,           // amd_fidelityfx_upscaler_dx12.dll -- SDK 2.x, FSR 3.1 or FSR 4
    kFfxLeafFrameGeneration = 2,    // amd_fidelityfx_framegeneration_dx12.dll -- SDK 2.x
    kFfxLeafLoader = 3,             // amd_fidelityfx_loader_dx12.dll -- SDK 2.x, the game's entry on
                                    // a loader-shipping title (measured: its leaves' exports stay silent)
    kFfxLeafCount = 4,
};

inline constexpr const wchar_t* kFfxLeafModules[kFfxLeafCount] = {
    L"amd_fidelityfx_dx12.dll",
    L"amd_fidelityfx_upscaler_dx12.dll",
    L"amd_fidelityfx_framegeneration_dx12.dll",
    L"amd_fidelityfx_loader_dx12.dll",
};

// The loader by name, for the fixtures: the decoy in tools/vendor-stubs stands in
// for it and forwards to the two 2.x leaves the way the measurement says the real
// one does -- NOT through their exports.
inline constexpr const wchar_t* kFfxLoaderModule = kFfxLeafModules[kFfxLeafLoader];

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
// THE FOUR ffxDispatch ROWS ARE THE MODULES A GAME CALLS, spelled as literals
// because Pass A's parser reads literals; the static_asserts under the table bind
// these spellings to kFfxLeafModules in both directions, so neither list can gain or
// lose a module alone.
// TWO ROWS, ONE FAMILY, TWO DETOURS -- the params family has a second row since
// 2026-09-04. Streamline 2.8 (Dying Light: The Beast ships 2.8.0) deprecates slSetTag
// in favour of slSetTagForFrame, which takes the frame token first and the same tag
// list after; a 2.8 title that tags per frame never calls slSetTag, and the first
// title measured on 2.8 published DLSS identity on 4,575 of 4,575 batches with
// UpscalerParams on ZERO of them. The two rows share the family bit because they
// produce the same fields from the same ResourceTag list; the installer tells them
// apart by SYMBOL (kSymbolSlSetTagForFrame below), never by family, because their
// signatures differ and the neighbour's body would read a FrameToken as a viewport.
// And the family is published WHEN WHOLE -- once every one of these two symbols the
// loaded interposer exports is patched -- not on the first row: published on the
// first, it entitled records the second row was not yet producing (measured 38 of
// 41 on the frame-based fixture), which is the ffx leaves' defect from #110 again.
#define FL_HOOK_INVENTORY(X)                                                                                           \
    X(L"sl.interposer.dll", "slEvaluateFeature", fl::inventory::kFamilyEvaluateFeature)                                \
    X(L"sl.interposer.dll", "slSetTag", fl::FL_HOOK_UPSCALER_PARAMS)                                                   \
    X(L"sl.interposer.dll", "slSetTagForFrame", fl::FL_HOOK_UPSCALER_PARAMS)                                           \
    X(L"sl.interposer.dll", "slGetNewFrameToken", fl::FL_HOOK_FG_EVALUATIONS)                                          \
    X(L"amd_fidelityfx_dx12.dll", "ffxDispatch", fl::inventory::kFamilyFfxDispatch)                                    \
    X(L"amd_fidelityfx_upscaler_dx12.dll", "ffxDispatch", fl::inventory::kFamilyFfxDispatch)                           \
    X(L"amd_fidelityfx_framegeneration_dx12.dll", "ffxDispatch", fl::inventory::kFamilyFfxDispatch)                    \
    X(L"amd_fidelityfx_loader_dx12.dll", "ffxDispatch", fl::inventory::kFamilyFfxDispatch)                             \
    X(L"ffx_fsr3_x64.dll", "ffxFsr3ContextDispatchUpscale", fl::inventory::kFamilyFsr3Host)

// The FSR 3.0 host module and the one symbol dllmain's ffx arm keys on for it. Named
// constants HERE, for the reason kModuleSlInterposer gives below, and bound to the
// table so the row and the arm cannot drift apart.
inline constexpr const wchar_t* kModuleFfxFsr3Host = L"ffx_fsr3_x64.dll";
inline constexpr const char*    kSymbolFfxFsr3DispatchUpscale = "ffxFsr3ContextDispatchUpscale";

// The module and the two symbols dllmain's params arm keys on: the frame-based
// symbol picks the detour, and all three name what "whole" means when the family is
// published. Named constants HERE, because Pass B forbids the symbol literals
// anywhere else in the Overlay -- and bound to the table below, so a row and the
// arm that installs it cannot drift apart.
inline constexpr const wchar_t* kModuleSlInterposer = L"sl.interposer.dll";
inline constexpr const char*    kSymbolSlSetTag = "slSetTag";
inline constexpr const char*    kSymbolSlSetTagForFrame = "slSetTagForFrame";

constexpr bool InventoryHasRow(const wchar_t* module, const char* symbol) noexcept {
#define FL_ROW_HAS(mod, sym, family)                                                                                   \
    if (SameW(mod, module) && SameA(sym, symbol)) {                                                                    \
        return true;                                                                                                   \
    }
    FL_HOOK_INVENTORY(FL_ROW_HAS)
#undef FL_ROW_HAS
    return false;
}
static_assert(InventoryHasRow(kModuleSlInterposer, kSymbolSlSetTag),
              "the params arm keys on slSetTag by this constant, and the table no longer has that row");
static_assert(InventoryHasRow(kModuleSlInterposer, kSymbolSlSetTagForFrame),
              "the params arm keys on slSetTagForFrame by this constant, and the table no longer has that row");
static_assert(InventoryHasRow(kModuleFfxFsr3Host, kSymbolFfxFsr3DispatchUpscale),
              "the ffx arm keys on the FSR 3.0 host row by these constants, and the table no longer has that row");

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
                  FfxDispatchRowExistsFor(kFfxLeafModules[kFfxLeafFrameGeneration]) &&
                  FfxDispatchRowExistsFor(kFfxLeafModules[kFfxLeafLoader]),
              "every ffx leaf needs its ffxDispatch row, spelled identically -- a leaf with no row is a trampoline "
              "slot nothing installs into");
static_assert(EveryFfxDispatchRowIsALeaf(),
              "an ffxDispatch row names a module the leaf table does not know -- InstallByFamily would resolve it "
              "and find no trampoline slot, and install nothing without saying so");
// The reverse of what this line said on the morning of 2026-09-04, and the evening's
// measurement is why: three loader-shipping titles at FSR produced zero dispatches at
// any leaf export, so the loader is where a game's calls arrive and it MUST be a row.
static_assert(AnyRowNames(kFfxLoaderModule),
              "amd_fidelityfx_loader_dx12.dll is the game's entry on a loader-shipping title (Dying Light: The "
              "Beast, KCD2, Wukong measured 2026-09-04: zero dispatches at any leaf export while FSR ran), so "
              "without a row those titles read N/A");
// The host is NOT a leaf: it has no ffxDispatch, its own detour body, its own latch.
// FfxLeafOf returning -1 for it is what routes the installer's ffx arm to the host
// branch, so a leaf-table entry for it would hand the host the ffxDispatch trampoline.
static_assert(FfxLeafOfExact(kModuleFfxFsr3Host) < 0,
              "the FSR 3.0 host facade is not an ffxDispatch leaf; the installer keys it by module name");
static_assert(!FfxDispatchRowExistsFor(kModuleFfxFsr3Host),
              "the host exports no ffxDispatch; a row claiming one would resolve nothing and install nothing");

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

// Does this module speak the FSR 3.0 HOST ABI the vendored fsr3-v3.0.4 headers describe?
//
// A NAME CHECK, like SpeaksFfxApi's, on four of the twelve ffxFsr3* names the shipped
// module exports (docs/vendor-exports.json): the create, the one export hooked, the
// frame-generation dispatch and SkipPresent -- the last two being the 3.0 host's own
// and exported by no ffx-api module, so a 1.1.x monolith of the wrong NAME cannot pass.
// Deliberately not a generation check: 3.0.3, 3.0.4 and 1.1.4 share the descriptor
// prefix through renderSize, which is the only field read, and dllmain pins that
// offset at compile time; a name a later generation ADDS is not evidence the field
// moved. Probe-only names, never resolved into a row, never called; this file because
// Pass B exempts exactly this file.
inline bool SpeaksFsr3Host(HMODULE h) noexcept {
    return h != nullptr && GetProcAddress(h, "ffxFsr3ContextCreate") != nullptr &&
           GetProcAddress(h, "ffxFsr3ContextDispatchUpscale") != nullptr &&
           GetProcAddress(h, "ffxFsr3DispatchFrameGeneration") != nullptr &&
           GetProcAddress(h, "ffxFsr3SkipPresent") != nullptr;
}

inline bool SpeaksExpectedAbi(const wchar_t* module, HMODULE h) noexcept {
    if (module == nullptr) {
        return false;
    }
    if (_wcsicmp(module, L"sl.interposer.dll") == 0) {
        return SpeaksStreamline2(h);
    }
    if (_wcsicmp(module, kModuleFfxFsr3Host) == 0) {
        return SpeaksFsr3Host(h);
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
