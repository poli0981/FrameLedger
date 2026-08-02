# FrameLedger native layer (docs/12_BUILD.md, docs/17_HOOK_ENGINE.md).
#
# Scaffold. Targets are declared with their build profiles so the flags are
# established before any hook code exists — the flags are the part that is
# hard to retrofit, and two of them are open questions (see below).

cmake_minimum_required(VERSION 3.28)
project(FrameLedgerNative LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FrameLedger's native layer is Windows-only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    # An x64 DLL cannot be injected into a 32-bit process. This is why D3D9 is
    # not a Tier-1 API in v1 (docs/20_OPEN_QUESTIONS.md §Scope). If a 32-bit
    # Overlay is ever added, it gets its own preset, not a relaxed check here.
    message(FATAL_ERROR "x64 only. Configure with the x64-debug or x64-release preset.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT everywhere: never drag a VC runtime dependency into a host process
# we do not control (docs/17_HOOK_ENGINE.md §Build profile).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(fl_native_flags INTERFACE)
target_compile_options(fl_native_flags INTERFACE
    /W4 /WX
    /GS /guard:cf /Qspectre
    /GR-                      # no RTTI
    /permissive-
    $<$<CONFIG:Release>:/O2>
)
target_link_options(fl_native_flags INTERFACE
    /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA
)

# ---------------------------------------------------------------------------
# Two flags on the Overlay are unresolved and MUST be settled in P0 before hook
# code is written. Both are recorded in docs/20_OPEN_QUESTIONS.md:
#
#   H1  /guard:cf  — an indirect call to a MinHook trampoline is not a
#       registered CFG call target, so in a CFG-enabled host it can
#       __fastfail, i.e. take the game down. Directly contradicts NFR-3.
#
#   H3  -D_HAS_EXCEPTIONS=0 — not a configuration Microsoft's STL supports,
#       and <atomic> is load-bearing here because the ring writer is built on
#       std::atomic_ref.
#
# They are deliberately NOT applied to the Overlay target yet. Applying them
# now would mean discovering the problem as a crash in a real game rather than
# as a decision. Add them once P0 answers the questions.
# ---------------------------------------------------------------------------

add_subdirectory(FrameLedger.Shm)
add_subdirectory(FrameLedger.Overlay)
add_subdirectory(FrameLedger.Injector)
add_subdirectory(FrameLedger.VkLayer)

if(FL_BUILD_TESTS)
    # enable_testing() must be called from the TOP-LEVEL CMakeLists, not from
    # tests/. Calling it only in the subdirectory configures without error and
    # then reports "No tests were found!!!" at ctest time — a green build with
    # a silently empty test gate.
    enable_testing()
    add_subdirectory(tests)
endif()

if(FL_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
