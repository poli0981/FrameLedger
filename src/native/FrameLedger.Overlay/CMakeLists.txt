# The injected DLL. Install hooks, write 64-byte records into the ring, stay
# invisible in the frame budget, never take the game down (docs/17_HOOK_ENGINE.md).
add_library(FrameLedger.Overlay SHARED src/dllmain.cpp)
target_link_libraries(FrameLedger.Overlay PRIVATE FrameLedger.Shm fl_native_flags fl_hostile_env_flags minhook)
set_target_properties(FrameLedger.Overlay PROPERTIES OUTPUT_NAME "FrameLedger.Overlay")

# VERSIONINFO is mandatory and CI fails the build without it: being plainly
# identifiable to anti-cheat is a requirement, not a nicety (docs/19_SAFETY).
# TODO(P1): add version.rc once the target has real content.
