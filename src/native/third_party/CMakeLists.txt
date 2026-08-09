# Third-party native dependencies, fetched and pinned by commit.
#
# MinHook (BSD-2-Clause) — inline hooking. docs/12_BUILD.md says "vendored as a
# submodule (or FetchContent) and built from source, pinned by commit".
# FetchContent chosen over a submodule so a fresh clone needs no extra step and
# CI has nothing to remember.
#
# Pinned to the v1.3.4 TAG'S COMMIT, not the tag name: a tag can be moved, a
# commit cannot. Bumping this is a deliberate act — MinHook is the component
# that rewrites instructions inside somebody else's game process.

include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        c3fcafdc10146beb5919319d0683e44e3c30d537    # v1.3.4, 2025-03-28
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(minhook)

# MinHook is C, built by upstream's own CMakeLists. Do not apply our /W4 /WX
# to it: warnings in third-party code are upstream's to fix, and failing our
# build on them would only tempt someone to patch a vendored copy — which
# would drag it out of "consumed unmodified" and into MPL/BSD modification
# territory for no benefit.
if(TARGET minhook)
    set_target_properties(minhook PROPERTIES FOLDER "third_party")
endif()

# ---------------------------------------------------------------------------
# jsmn (MIT) — the JSON parser the anti-cheat guard reads detection-rules.json
# with (20_OPEN_QUESTIONS §S15 item 3).
#
# Chosen for what it does NOT do. jsmn is a single header, no allocation, no
# exceptions, no recursion beyond a bounded token array we own, and it reports
# failure as a return code rather than by throwing. In the guard those are not
# stylistic preferences: this is the one component where a bug costs somebody
# their account, so a parser whose entire failure surface is three enum values
# is worth the slightly more verbose extraction code.
#
# Explicitly rejected: parsers that throw. -D_HAS_EXCEPTIONS=0 is Overlay-only,
# so the Injector *could* have exceptions — but a throw crossing the guard would
# be an unstructured exit from the one function that must always reach a verdict.
FetchContent_Declare(
    jsmn
    GIT_REPOSITORY https://github.com/zserge/jsmn.git
    GIT_TAG        fdcef3ebf886fa210d14956d3c068a653e76a24e    # v1.1.0
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(jsmn)

add_library(fl_jsmn INTERFACE)
target_include_directories(fl_jsmn SYSTEM INTERFACE "${jsmn_SOURCE_DIR}")
# JSMN_STATIC keeps the implementation internal to each TU that asks for it;
# JSMN_PARENT_LINKS is what makes object-key lookup possible without a second
# pass over the token array.
target_compile_definitions(fl_jsmn INTERFACE JSMN_STATIC JSMN_PARENT_LINKS)

# ---------------------------------------------------------------------------
# Catch2 (BSL-1.0) — 20_OPEN_QUESTIONS §S15 item 2 made this a prerequisite of
# the guard rather than a P1 nicety: §S13(a) put the guard in C++, so
# 14_TESTING's fail-closed matrix (forced EnumProcessModulesEx failures, partial
# module lists, unreadable processes) is a NATIVE test suite, and none existed.
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        fa43b77429ba76c462b1898d6cd2f2d7a9416b14    # v3.7.1
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(catch2)
if(TARGET Catch2)
    set_target_properties(Catch2 Catch2WithMain PROPERTIES FOLDER "third_party")

    # /EHsc ON CATCH2 ITSELF, and this is a FIX, not tidying.
    #
    # tests/CMakeLists.txt adds /EHsc to each test BINARY, with a comment whose
    # premise is right — "Catch2 reports failures by throwing" — and whose effect
    # was not: the flag reached guard_test.cpp and never reached the Catch2
    # LIBRARY, which is what decides the mode. The top-level CMakeLists strips
    # /EHsc from CMAKE_CXX_FLAGS (deliberately, so every target states its own
    # exception model), FetchContent builds Catch2 with whatever is left, and
    # Catch2 auto-detects the absence and compiles itself with
    # CATCH_CONFIG_DISABLE_EXCEPTIONS.
    #
    # MEASURED, not inferred, 2026-08-09. The generated command line for
    # _deps/catch2-build/.../Catch2.dir/*.obj was
    #   /DWIN32 /D_WINDOWS /O2 /Ob1 /DNDEBUG -std:c++20 -MT -Zi
    # with no /EH flag at all. In that mode a failed REQUIRE does not fail a test
    # case, it TERMINATES THE PROCESS:
    #   "Catch will terminate because it needed to throw an exception."
    #
    # WHAT THAT COST, on a dev box where WARP's D3D12 path is broken (HANDOFF
    # §Traps). fl_guard_test's D3D12 case fails at guard_test.cpp:1298 and takes
    # every test case DECLARED AFTER IT down with it -- including "the injected
    # Overlay records real presents into the ring", which 20_OPEN_QUESTIONS
    # §S29(a) names as the merge gate's honesty coverage. Run on its own that
    # case PASSES here, 250 assertions. So the coverage was not broken, it was
    # ABSENT, and ctest reported "1 test failed" rather than "and 2 never ran" --
    # a green-looking count over missing coverage, which is the defect this
    # repository keeps finding in other clothes.
    #
    # It also removes a real ABI hazard: the library and its consumers disagreed
    # about CATCH_CONFIG_DISABLE_EXCEPTIONS, so Catch2's macros expanded one way
    # in guard_test.cpp and the other way inside the archive it linked against.
    #
    # PRIVATE, and appended after CMAKE_CXX_FLAGS, so it wins over the strip.
    target_compile_options(Catch2 PRIVATE /EHsc)
    target_compile_options(Catch2WithMain PRIVATE /EHsc)
endif()

# ---------------------------------------------------------------------------
# Vulkan headers (Apache-2.0 OR MIT) — VENDORED, not fetched.
#
# Deliberately the opposite choice from everything above, for two reasons:
#
#   1. CI must not need the Vulkan SDK. FrameLedger.VkLayer cannot compile
#      without these, and the alternative was a ~1 GB SDK install on every run.
#   2. These are the exact headers matching the loader the blast-radius test
#      runs against (instance 1.4.357, docs/spike-notes.md §Environment).
#      Fetching a different revision would mean compiling against one contract
#      and testing against another.
#
# Only the C closure a Windows layer needs; the C++ bindings are excluded
# because they allocate and throw, which CLAUDE.md forbids in the layer.
# See third_party/vulkan-headers/README.md for provenance.
# ---------------------------------------------------------------------------
add_library(fl_vulkan_headers INTERFACE)
target_include_directories(fl_vulkan_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan-headers/include"
)
# SYSTEM: these are consumed unmodified, so /W4 /WX must not fail our build on
# somebody else's warnings. Patching a vendored header would drag it out of
# "consumed unmodified" and into modification territory for no benefit.
#
# (The same reasoning applies to fl_streamline_headers below.)

# ---------------------------------------------------------------------------
# NVAPI (MIT) — VENDORED, same reasoning as the Vulkan headers and one more.
#
# CLAUDE.md's pinned stack claimed "headers vendored" in the present tense while
# this directory held only vulkan-headers. legal/THIRD_PARTY_NOTICES.md had
# already caught the identical claim about ITSELF and gained a bidirectional
# licence gate for it; nothing propagated to CLAUDE.md. Vendored 2026-08-05 —
# see nvapi/README.md for provenance, the commit, and what was deliberately
# left behind.
#
# WHY THE .lib IS SAFE TO VENDOR AND THE SDK'S IS NOT: amd64/nvapi64.lib is a
# tracked file in the MIT repository and License.txt names the import libraries
# as the subject of the grant (docs/spike-notes.md §0, M3). The same file from
# the SDK installer would carry the SDK agreement instead.
#
# x64 ONLY. x86/nvapi.lib is not taken: FrameLedger is x64 only, so a 32-bit
# import library would be material we can never link and must still disclose.
add_library(fl_nvapi INTERFACE)
target_include_directories(fl_nvapi SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/nvapi"
)
target_link_libraries(fl_nvapi INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/nvapi/amd64/nvapi64.lib"
)
# NOT a load-time dependency, which is the property L3's degradation rests on.
# nvapi64.lib is a STATIC library of stubs that reach nvapi64.dll through
# nvapi_QueryInterface at first call, so a machine with no NVIDIA driver still
# loads our binaries and NvAPI_Initialize returns an error instead. That is
# asserted by ctest fl_nvapi_probe rather than assumed — a vendored dependency
# nothing compiles against is a dependency whose header closure could be
# incomplete with nobody the wiser.

# ---------------------------------------------------------------------------
# NVIDIA Streamline headers (MIT) — VENDORED, not fetched, like the Vulkan
# headers and for the same two reasons: CI must not need a vendor SDK install,
# and we must compile against the exact revision the hook was written for.
#
# WHAT THIS BUYS, stated because the alternative was seriously proposed and
# nearly built. The Overlay hooks sl.interposer.dll!slEvaluateFeature. Without a
# header, its signature has to be guessed, and a guess that is wrong by one
# argument corrupts the stack INSIDE THE ORIGINAL FUNCTION — outside
# FL_HOOK_GUARD's __try, which cannot help there, in somebody's game. With the
# header it is not a guess: sl_core_api.h publishes
#
#   using PFun_slEvaluateFeature = sl::Result(sl::Feature, const sl::FrameToken&,
#                                             const sl::BaseStructure**, uint32_t,
#                                             sl::CommandBuffer*);
#
# Five parameters, every one integer-class (Feature is uint32_t, CommandBuffer is
# void, a reference is a pointer), and an enum return. No float, no double, so
# nothing travels in XMM — which was the exact residual hazard nobody could rule
# out while the signature was unknown.
#
# LICENCE: MIT, verified against upstream on 2026-08-09 — none of
# docs/18_GPU_VENDOR_APIS.md §Checklist step 2's needles, so step 1 terminates on
# a pass. NGX/DLSS is a DIFFERENT licence (proprietary RTX SDKs Licence) and is
# neither vendored nor re-declarable; see third_party/streamline/README.md, which
# also records what is excluded and why.
add_library(fl_streamline_headers INTERFACE)
target_include_directories(fl_streamline_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/streamline/include"
)
# NEVER target_link_libraries anything here. These are declarations; SL_API
# entry points are extern "C" imports, and linking one would make
# sl.interposer.dll a LOAD-TIME dependency of FrameLedger.Overlay.dll — which
# would stop the Overlay loading in every game that ships no Streamline, in the
# loader, before any of our code runs and with no message anywhere. The Overlay
# resolves the symbol at runtime, module-scoped, on a module the game loaded.
