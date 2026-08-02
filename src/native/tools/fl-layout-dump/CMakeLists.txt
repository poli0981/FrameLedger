# Emits the native struct offsets as JSON. FrameLedger.Infrastructure.Tests
# reads this and asserts the C# [StructLayout] mirrors match field for field.
# Struct drift between the two layers is the most dangerous silent bug in this
# architecture (CLAUDE.md §Struct mirroring) — this is what catches it.
add_executable(fl-layout-dump main.cpp)
target_link_libraries(fl-layout-dump PRIVATE FrameLedger.Shm fl_native_flags)
