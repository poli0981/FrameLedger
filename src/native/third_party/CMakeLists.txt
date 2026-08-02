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
