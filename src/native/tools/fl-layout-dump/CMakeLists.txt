# Emits the native struct offsets as JSON. FrameLedger.Infrastructure.Tests
# (ShmLayoutMirrorTests) runs this and asserts the C# [StructLayout] mirrors
# match field for field. Struct drift between the two layers is the most
# dangerous silent bug in this architecture (CLAUDE.md §Struct mirroring) --
# this is what catches it.
#
# True since 2026-08-05, and it was written here in the present tense long
# before it was: this was one of nine sites claiming a mechanism that did not
# exist (build.ps1:324-329), and one of the two strongest, because a build file
# reads as a wiring fact rather than as prose.
#
# NO add_test HERE, deliberately. fl_shm.h's static_asserts already pin every
# size and offset at compile time and layout_test.cpp says compiling IS the
# test, so a ctest over this tool's stdout would be a second hand-maintained
# copy of the same constants -- the exact thing the mirror exists to prevent.
# The consumer that changes a verdict is the managed test.
#
# FrameLedger.LayoutDump.targets stages the exe beside that test. It is NOT
# shipped: nothing in the product runs it, and an executable that dumps internal
# layout has no business in a release.
add_executable(fl-layout-dump main.cpp)
target_link_libraries(fl-layout-dump PRIVATE FrameLedger.Shm fl_native_flags)
