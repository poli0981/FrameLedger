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

# NOTE: docs/20_OPEN_QUESTIONS.md §S9 is closed — the thin CLI does NOT exist
# and does not ship. A user-runnable LoadLibraryW injector is a path into a game
# process that the guard does not stand in front of, and no invocation contract
# makes that safe. The Agent links this static lib; the guard owns the
# chokepoint inside it.
