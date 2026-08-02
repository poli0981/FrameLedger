# Native unit tests (docs/14_TESTING.md §Native unit tests).
#
# The record and header layout contract needs no test framework — it is enforced
# by static_assert in fl_shm.h, so simply compiling this target proves it.
# enable_testing() is called by the top-level CMakeLists; calling it here
# instead configures fine and then finds no tests at ctest time.

add_executable(fl_shm_layout_test layout_test.cpp)
target_link_libraries(fl_shm_layout_test PRIVATE FrameLedger.Shm fl_native_flags)
add_test(NAME fl_shm_layout COMMAND fl_shm_layout_test)

# The safety-guard matrix. 20_OPEN_QUESTIONS §S13(a) put the guard in C++, which
# made Catch2 a PREREQUISITE of the guard rather than a later nicety: 14_TESTING
# requires forcing EnumProcessModulesEx failures, partial module lists and
# unreadable processes, and none of that is reachable without seams and a
# framework to drive them.
add_executable(fl_guard_test guard_test.cpp)
target_link_libraries(fl_guard_test PRIVATE FrameLedger.Injector fl_native_flags Catch2::Catch2WithMain)

# /EHsc on the TEST BINARY only. Catch2 reports failures by throwing, and the
# top-level CMakeLists strips the compiler's default /EHsc so every target has
# to state its own exception model deliberately.
#
# FrameLedger.Injector itself does not get this and does not want it: the guard
# must always reach a verdict, and a throw crossing it would be an unstructured
# exit from the one function in the codebase where that is unacceptable. The
# asymmetry is the point, not an oversight.
target_compile_options(fl_guard_test PRIVATE /EHsc)

add_test(NAME fl_guard COMMAND fl_guard_test)

# TODO(P1): ring-buffer and fault-policy suites, now that Catch2 is here.
