# The injected DLL. Install hooks, write 64-byte records into the ring, stay
# invisible in the frame budget, never take the game down (docs/17_HOOK_ENGINE.md).
add_library(FrameLedger.Overlay SHARED src/dllmain.cpp)
# Advapi32: the mapping is created with a DACL granting only the current user's
# SID (07_IPC §Security), which needs the token's user SID and the SDDL
# converters. Not a convenience — the Agent's control block lives in that
# mapping, and unhookRequested is the safety stop.
target_link_libraries(FrameLedger.Overlay PRIVATE FrameLedger.Shm fl_native_flags fl_hostile_env_flags minhook
    Advapi32)
set_target_properties(FrameLedger.Overlay PROPERTIES OUTPUT_NAME "FrameLedger.Overlay")

# VERSIONINFO is mandatory and the build fails without it: being plainly
# identifiable to anti-cheat is a requirement, not a nicety (docs/19_SAFETY).
# This used to be a TODO next to a comment asserting CI enforced it. Enforced
# for real now by tools/versioninfo-check.ps1, which reads the built binary.
fl_add_version_resource(FrameLedger.Overlay "FrameLedger capture overlay (injected, opt-in per game)")
