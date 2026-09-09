# FrameLedger native layer (docs/12_BUILD.md, docs/17_HOOK_ENGINE.md).
#
# Scaffold. Targets are declared with their build profiles so the flags are
# established before any hook code exists — the flags are the part that is
# hard to retrofit, and two of them are open questions (see below).

cmake_minimum_required(VERSION 3.28)
project(FrameLedgerNative VERSION 0.1.0 LANGUAGES CXX RC)

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

# ---------------------------------------------------------------------------
# VERSIONINFO — a safety requirement, not packaging polish.
#
# docs/19_SAFETY_AND_ANTICHEAT.md: the DLL ships with its real filename, real
# exports, and a populated version block, because an anti-cheat vendor looking
# at a module inside their game must be able to tell instantly what it is and
# who wrote it. The Overlay CMakeLists claimed "CI fails the build without it"
# while no .rc file existed anywhere in the repository and nothing checked —
# so the claim was true only as an intention.
#
# tools/versioninfo-check.ps1 now makes it true for real, and every shipped
# native binary calls this.
# ---------------------------------------------------------------------------
# The render extent hook-harness tags, and that guard_test asserts the Overlay
# published. ONE DEFINITION, handed to both targets, because a fixture and its
# assertion holding separate copies of the number under test is the §S29(b)
# defect in miniature: `ctest fl_vtable_indices` once proved a fact about
# dxgi.dll instead of about the Overlay for exactly that reason.
set(FL_TAGGED_RENDER_W 1280)
set(FL_TAGGED_RENDER_H 720)

# The ray dispatch extent hook-harness records, and that guard_test asserts the
# Overlay summed into dispatchRaysVolume. Same rule as the two above: 64*32*1 =
# 2048 rays per DispatchRays, and neither side may hold its own copy of it -- an
# assertion that recomputes the fixture's arithmetic from a second literal is a
# test of nothing.
set(FL_DXR_DISPATCH_W 64)
set(FL_DXR_DISPATCH_H 32)
set(FL_DXR_DISPATCH_D 1)

set(FL_VER_MAJOR ${PROJECT_VERSION_MAJOR})
set(FL_VER_MINOR ${PROJECT_VERSION_MINOR})
set(FL_VER_PATCH ${PROJECT_VERSION_PATCH})
set(FL_COMPANY_NAME  "FrameLedger")
set(FL_PRODUCT_NAME  "FrameLedger")
set(FL_COPYRIGHT     "GPL-3.0-or-later")

# Optional third argument: the REAL output filename, when it differs from the
# CMake target name. The vendor-name stubs need it — a target called
# fl_stub_sl_interposer produces sl.interposer.dll, and a version block claiming
# the target name would describe a file that does not exist. 19_SAFETY's posture
# is that our binaries say plainly what they are, and that has to survive a
# fixture whose FILENAME is deliberately somebody else's.
function(fl_add_version_resource target description)
    set(FL_TARGET_NAME "${target}")
    if(ARGC GREATER 2)
        set(FL_TARGET_FILENAME "${ARGV2}")
    else()
        set(FL_TARGET_FILENAME "${target}.dll")
    endif()
    set(FL_FILE_DESCRIPTION "${description}")
    set(FL_COMMENTS "Performance measurement tool. Identifies itself deliberately; see https://github.com/poli0981/frameledger")
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/${target}.version.rc")
    configure_file("${CMAKE_SOURCE_DIR}/version.rc.in" "${_out}" @ONLY)
    target_sources(${target} PRIVATE "${_out}")
endfunction()

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

# ---------------------------------------------------------------------------
# The compiled-in blocklist floor (§S21), GENERATED from the shipped rules file.
#
# The floor is what a rules file may extend and can never shrink. §S21 shipped a
# hand-written one covering the three required families, kept minimal because a
# larger hand-written table would be a second copy of the blocklist that drifts
# from the data. Measured afterwards, that bought 4 of the seed's 22 values, 2 of
# its 5 groups and none of its 5 name fragments — so a crafted rules file could
# still delete most of the blocklist.
#
# Generating it removes the objection that kept it small: a table derived from
# rules/detection-rules.json at build time cannot drift from it.
#
# Output goes in the BUILD tree. Generated code in src/native would be swept by
# clang-format, and a generated file under version control is a copy somebody can
# edit independently — the exact thing this exists to prevent.
# ---------------------------------------------------------------------------
find_program(FL_PWSH pwsh REQUIRED
    DOC "PowerShell 7+, used to generate the blocklist floor from the rules file")

set(FL_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(FL_AC_FLOOR_DIR "${CMAKE_BINARY_DIR}/generated")
set(FL_AC_FLOOR_HEADER "${FL_AC_FLOOR_DIR}/fl_ac_floor.generated.h")

add_custom_command(
    OUTPUT "${FL_AC_FLOOR_HEADER}"
    COMMAND "${FL_PWSH}" -NoProfile -File "${FL_REPO_ROOT}/tools/gen-ac-floor.ps1"
            -RulesPath "${FL_REPO_ROOT}/rules/detection-rules.json"
            -OutPath "${FL_AC_FLOOR_HEADER}"
    DEPENDS "${FL_REPO_ROOT}/rules/detection-rules.json"
            "${FL_REPO_ROOT}/tools/gen-ac-floor.ps1"
    COMMENT "Generating the anti-cheat blocklist floor from rules/detection-rules.json"
    VERBATIM)

add_custom_target(fl_ac_floor_gen DEPENDS "${FL_AC_FLOOR_HEADER}")

# Carries only the include directory. Every target that compiles fl_ac_rules.cpp
# links this AND depends on fl_ac_floor_gen — the dependency is stated per target
# rather than hidden here, so a new target that forgets it fails to compile
# immediately and loudly rather than picking up a stale header.
add_library(fl_ac_floor INTERFACE)
target_include_directories(fl_ac_floor INTERFACE "${FL_AC_FLOOR_DIR}")

add_subdirectory(third_party)
add_subdirectory(FrameLedger.Shm)
add_subdirectory(FrameLedger.Overlay)
add_subdirectory(FrameLedger.Injector)
add_subdirectory(FrameLedger.NvapiBridge)
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
