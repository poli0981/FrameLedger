# The DXGI vtable slots the Overlay hooks, as an INTERFACE target so
# tools/hook-harness can consume the header without linking the DLL.
#
# It is a separate target for exactly that reason: the harness must read the
# SAME constants the Overlay hooks with, or `ctest fl_vtable_indices` proves a
# fact about dxgi.dll rather than about FrameLedger.Overlay — which is what it
# was doing (20_OPEN_QUESTIONS §S29(b)).
add_library(fl_dxgi_vtable INTERFACE)
target_include_directories(fl_dxgi_vtable INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# The injected DLL. Install hooks, write records into the ring, stay invisible in
# the frame budget, never take the game down (docs/17_HOOK_ENGINE.md).
add_library(FrameLedger.Overlay SHARED src/dllmain.cpp)
# Advapi32: the mapping is created with a DACL granting only the current user's
# SID (07_IPC §Security), which needs the token's user SID and the SDDL
# converters. Not a convenience — the Agent's control block lives in that
# mapping, and unhookRequested is the safety stop.
# d3d11/dxgi: the present hook reads the SHARED class vtable off a throwaway WARP
# composition swapchain (17_HOOK_ENGINE §Getting vtable addresses) — never a
# hardcoded pointer, and the dummy object is released immediately.
# fl_streamline_headers and fl_fidelityfx_headers carry INCLUDE DIRECTORIES ONLY --
# each is an INTERFACE target with no target_link_libraries of its own,
# deliberately, so that naming it here cannot make sl.interposer.dll or an AMD
# module a load-time dependency of this DLL. See third_party/streamline/README.md
# and third_party/fidelityfx/README.md: an Overlay that imports a vendor module
# fails to load in every game that ships no such module, in the loader, silently.
# hookinventory-check Pass C reads the built DLL's import AND export tables.
target_link_libraries(FrameLedger.Overlay PRIVATE FrameLedger.Shm fl_dxgi_vtable fl_native_flags fl_hostile_env_flags minhook
    fl_streamline_headers fl_fidelityfx_headers fl_fsr3_host_headers
    Advapi32 d3d11 d3d12 dxgi)
set_target_properties(FrameLedger.Overlay PROPERTIES OUTPUT_NAME "FrameLedger.Overlay")

# VERSIONINFO is mandatory and the build fails without it: being plainly
# identifiable to anti-cheat is a requirement, not a nicety (docs/19_SAFETY).
# This used to be a TODO next to a comment asserting CI enforced it. Enforced
# for real now by tools/versioninfo-check.ps1, which reads the built binary.
fl_add_version_resource(FrameLedger.Overlay "FrameLedger capture overlay (injected, opt-in per game)")
