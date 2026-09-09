# FrameLedger.NvapiBridge — the C ABI the managed Agent reaches NVAPI through
# (docs/18_GPU_VENDOR_APIS.md §L3; P2 PR-E2).
#
# A SHARED library, because P/Invoke needs one and CLAUDE.md puts the native
# boundary in Infrastructure: the second facade after the guard's, and the ONLY
# shipped binary that links nvapi64.lib. Read-only by construction — every call
# in fl_nvapi_bridge.cpp is a query (§Runtime policy: never a setter).
#
# NEVER LOADED INTO A GAME. The Agent loads it beside its own binary by absolute
# path; nothing injects it, and its name carries none of the words the guard's
# §S18 heuristic scans a launcher's ancestors for (guard / hook / inject / cheat).
#
# fl_native_flags only, NOT fl_hostile_env_flags: this runs in our own process.
# It ships, so tools/versioninfo-check.ps1 covers it: a binary that can appear on
# a machine must say what it is (docs/19_SAFETY).
add_library(FrameLedger.NvapiBridge SHARED
    src/fl_nvapi_bridge.cpp
)
target_include_directories(FrameLedger.NvapiBridge PUBLIC include)
target_link_libraries(FrameLedger.NvapiBridge PRIVATE FrameLedger.Shm fl_native_flags fl_nvapi)
target_compile_definitions(FrameLedger.NvapiBridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
fl_add_version_resource(FrameLedger.NvapiBridge "FrameLedger NVAPI telemetry bridge (read-only; loaded by the Agent, never into a game)")
