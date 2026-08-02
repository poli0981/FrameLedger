# Launch/attach injection and the anti-cheat guard probe.
#
# The guard is a hard gate: no code path here may open a process with
# CREATE_THREAD|VM_WRITE without a passing AntiCheatGuard result
# (CLAUDE.md rule 2, docs/19_SAFETY_AND_ANTICHEAT.md).
add_library(FrameLedger.Injector STATIC src/injector.cpp)
target_link_libraries(FrameLedger.Injector PUBLIC FrameLedger.Shm fl_native_flags)

# NOTE: docs/20_OPEN_QUESTIONS.md §S9 asks whether the thin CLI should ship to
# users at all. A standalone LoadLibraryW injector outside the managed guard is
# both a bypass and a bad look for a project whose position is "we refuse where
# we are not welcome". No executable target until that is decided.
