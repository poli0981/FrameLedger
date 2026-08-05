# Emits the native struct offsets as JSON.
#
# NOTHING CONSUMES THIS YET. The comment here used to say
# "FrameLedger.Infrastructure.Tests reads this and asserts the C# [StructLayout]
# mirrors match field for field ... this is what catches it" -- present tense, in
# a build file, about a test that does not exist. src/FrameLedger.Shared holds a
# .csproj and no .cs files, and no managed test references FlFrameRecord.
# build.ps1:324-329 enumerates the nine files that made this claim; this was one
# of the two strongest sites, because a build file reads as a wiring fact rather
# than as prose.
#
# Struct drift between the two layers is still the most dangerous silent bug in
# this architecture (CLAUDE.md §Struct mirroring) -- which is the reason the gap
# is named here rather than left implied. 20_OPEN_QUESTIONS §R10 tracks it,
# §S24 schedules it. This tool is the intended input when it lands.
add_executable(fl-layout-dump main.cpp)
target_link_libraries(fl-layout-dump PRIVATE FrameLedger.Shm fl_native_flags)
