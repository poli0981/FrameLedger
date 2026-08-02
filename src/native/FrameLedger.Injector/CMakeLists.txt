# Launch/attach injection and the anti-cheat guard.
#
# The guard is a hard gate: no code path here may open a process with
# CREATE_THREAD|VM_OPERATION|VM_WRITE without a passing verdict
# (CLAUDE.md rule 2, docs/19_SAFETY_AND_ANTICHEAT.md). That is enforced
# structurally rather than by review — the injection primitive has internal
# linkage inside fl_guard.cpp, so no other translation unit has a symbol to
# call, and tools/chokepoint-check.ps1 fails the build if one names it.
set(FL_INJECTOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/injector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fl_ac_rules.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fl_guard.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fl_guard_sources.cpp"
    PARENT_SCOPE
)

add_library(FrameLedger.Injector STATIC
    src/injector.cpp
    src/fl_ac_rules.cpp
    src/fl_guard.cpp
    src/fl_guard_sources.cpp
)
target_include_directories(FrameLedger.Injector PUBLIC include)
target_link_libraries(FrameLedger.Injector
    PUBLIC FrameLedger.Shm fl_native_flags
    PRIVATE fl_jsmn
)

# ---------------------------------------------------------------------------
# The C ABI the managed Agent reaches the guard through (§S15 item 1).
#
# A SHARED library, because P/Invoke needs one and CLAUDE.md puts the native
# boundary in Infrastructure. This is NOT the thing §S9 refused: §S9 refused a
# user-runnable injector that the guard did not stand in front of. Nothing
# exported here skips a check — FlGuardedInject runs the full guard and returns
# a refusal, and there is no entry point that accepts evidence from a caller.
#
# It ships, so tools/versioninfo-check.ps1 covers it: a binary that can appear
# in a process must say what it is (docs/19_SAFETY).
# ---------------------------------------------------------------------------
add_library(FrameLedger.Guard SHARED
    src/injector.cpp
    src/fl_ac_rules.cpp
    src/fl_guard.cpp
    src/fl_guard_sources.cpp
    src/fl_guard_abi.cpp
)
target_include_directories(FrameLedger.Guard PUBLIC include)
target_link_libraries(FrameLedger.Guard PRIVATE FrameLedger.Shm fl_native_flags fl_jsmn)
target_compile_definitions(FrameLedger.Guard PRIVATE FL_GUARD_ABI_BUILD)
set_target_properties(FrameLedger.Guard PROPERTIES OUTPUT_NAME "FrameLedger.Guard")
fl_add_version_resource(FrameLedger.Guard "FrameLedger anti-cheat guard (hard gate; refuses where anti-cheat is present)")

# NOTE: docs/20_OPEN_QUESTIONS.md §S9 is closed — the thin CLI does NOT exist
# and does not ship. A user-runnable LoadLibraryW injector is a path into a game
# process that the guard does not stand in front of, and no invocation contract
# makes that safe. The Agent links this static lib; the guard owns the
# chokepoint inside it.
