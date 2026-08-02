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
