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

# CMake injects /EHsc into CMAKE_CXX_FLAGS by default. Targets that disable
# exceptions then get "/EHsc ... /EHsc-" on one command line: the later flag
# wins so the result is correct, but it is confusing to read and one reorder
# away from being silently wrong. Strip it and let each target state its own.
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

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
# Overlay hardening flags — H1 and H3, both answered by fl-probe-hookprofile
# before being applied here (docs/spike-notes.md §3).
#
#   H1  /guard:cf with MinHook trampolines: VERIFIED SAFE. The probe runs with
#       CFG genuinely enforcing (PE header flag, an 84-entry guard function
#       table, 114 guarded indirect call sites, and the process mitigation
#       query confirming it) and the indirect call into MinHook's runtime-
#       allocated trampoline survives. Windows marks VirtualAlloc'd executable
#       memory as a valid call target by default.
#       CAVEAT: measured with CFG *strict mode* OFF. A host that enables strict
#       mode does not auto-validate dynamic code and could still __fastfail.
#       The fault policy cannot catch that — __fastfail is uncatchable.
#
#   H3  -D_HAS_EXCEPTIONS=0 with <atomic>: VERIFIED. Compiles and runs, and
#       std::atomic_ref is lock-free at both 32- and 64-bit widths — which is
#       the part that matters, since a non-lock-free atomic would take a mutex
#       and violate CLAUDE.md rule 5.
#       Note what the define actually does: MSVC's STL turns _THROW into
#       __fastfail(5) rather than removing the failure path. In an injected
#       DLL that means "kill the host game", uncatchable by SEH. Safe only
#       while the no-throwing-STL-in-hook-paths rule holds — which is why that
#       rule is enforced, not merely stated.
# ---------------------------------------------------------------------------

# Applied to the Overlay and the Vulkan layer: both run inside a host process
# we do not own. NOT applied to the Injector, which is our own code in our own
# process and has no reason to give up exceptions.
add_library(fl_hostile_env_flags INTERFACE)
target_compile_definitions(fl_hostile_env_flags INTERFACE _HAS_EXCEPTIONS=0)
target_compile_options(fl_hostile_env_flags INTERFACE /EHsc-)
target_link_options(fl_hostile_env_flags INTERFACE /guard:cf)

add_subdirectory(third_party)
add_subdirectory(FrameLedger.Shm)
add_subdirectory(FrameLedger.Overlay)
add_subdirectory(FrameLedger.Injector)
add_subdirectory(FrameLedger.VkLayer)

# enable_testing() must be called from the TOP-LEVEL CMakeLists, not from a
# subdirectory. Calling it only in tests/ configures without error and then
# reports "No tests were found!!!" at ctest time — a green build with a
# silently empty test gate.
#
# Unconditional, and before any add_subdirectory that registers a test: tools/
# contributes fl_hook_profile, so gating this on FL_BUILD_TESTS would make
# -DFL_BUILD_TOOLS=ON -DFL_BUILD_TESTS=OFF a configuration error.
enable_testing()

if(FL_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(FL_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
