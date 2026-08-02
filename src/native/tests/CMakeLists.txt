# Native unit tests (docs/14_TESTING.md §Native unit tests): ring buffer SPSC
# correctness under a hammering writer, seqlock torn-read detection, the
# one-full-lap case, wrap semantics, and the fault policy.
#
# The record and header layout contract needs no test framework — it is enforced
# by static_assert in fl_shm.h, so simply compiling this target proves it.
include(CTest)

add_executable(fl_shm_layout_test layout_test.cpp)
target_link_libraries(fl_shm_layout_test PRIVATE FrameLedger.Shm fl_native_flags)
add_test(NAME fl_shm_layout COMMAND fl_shm_layout_test)

# TODO(P1): Catch2 via FetchContent for the ring-buffer and fault-policy suites.
