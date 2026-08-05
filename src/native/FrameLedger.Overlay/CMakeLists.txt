# The injected DLL. Install hooks, write 64-byte records into the ring, stay
# invisible in the frame budget, never take the game down (docs/17_HOOK_ENGINE.md).
add_library(FrameLedger.Overlay SHARED src/dllmain.cpp)
# Advapi32: the mapping is created with a DACL granting only the current user's
# SID (07_IPC §Security), which needs the token's user SID and the SDDL
# converters. Not a convenience — the Agent's control block lives in that
# mapping, and unhookRequested is the safety stop.
# d3d11/dxgi: the present hook reads the SHARED class vtable off a throwaway WARP
# composition swapchain (17_HOOK_ENGINE §Getting vtable addresses) — never a
# hardcoded pointer, and the dummy object is released immediately.
target_link_libraries(FrameLedger.Overlay PRIVATE FrameLedger.Shm fl_native_flags fl_hostile_env_flags minhook
    Advapi32 d3d11 d3d12 dxgi)
set_target_properties(FrameLedger.Overlay PROPERTIES OUTPUT_NAME "FrameLedger.Overlay")

# VERSIONINFO is mandatory and the build fails without it: being plainly
# identifiable to anti-cheat is a requirement, not a nicety (docs/19_SAFETY).
# This used to be a TODO next to a comment asserting CI enforced it. Enforced
# for real now by tools/versioninfo-check.ps1, which reads the built binary.
fl_add_version_resource(FrameLedger.Overlay "FrameLedger capture overlay (injected, opt-in per game)")
