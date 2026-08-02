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
# The guard sources are COMPILED INTO THE TEST, not linked from the shipped
# static lib. That is what makes FL_GUARD_TESTABLE real rather than advisory:
# FrameLedger.Injector.lib is built without it, so the evidence seam is not
# merely unused there — the symbols do not exist. Linking the lib as well would
# duplicate every symbol, which is the compiler telling us the same thing.
add_executable(fl_guard_test guard_test.cpp ${FL_INJECTOR_SOURCES})
target_include_directories(fl_guard_test PRIVATE "${CMAKE_SOURCE_DIR}/FrameLedger.Injector/include")
target_link_libraries(fl_guard_test PRIVATE FrameLedger.Shm fl_native_flags fl_jsmn Catch2::Catch2WithMain)

# /EHsc on the TEST BINARY only. Catch2 reports failures by throwing, and the
# top-level CMakeLists strips the compiler's default /EHsc so every target has
# to state its own exception model deliberately.
#
# FrameLedger.Injector itself does not get this and does not want it: the guard
# must always reach a verdict, and a throw crossing it would be an unstructured
# exit from the one function in the codebase where that is unacceptable. The
# asymmetry is the point, not an oversight.
target_compile_options(fl_guard_test PRIVATE /EHsc)

# FL_GUARD_TESTABLE exposes the evidence seam (EvaluateWithSources /
# GuardedInjectWithSources). THIS IS THE ONLY TARGET THAT MAY DEFINE IT.
#
# The seam is necessary — 14_TESTING's matrix is unwritable without injectable
# evidence, and an input whose failure path cannot be exercised is an input
# whose failure path is unverified. But it lets a caller hand the guard
# all-clean fakes, which was harmless while the injection primitive was a stub
# and is a way into a game process now that it is real. So it is compiled out
# of everything that ships, and tools/chokepoint-check.ps1 fails the build if
# any other CMakeLists defines it.
target_compile_definitions(fl_guard_test PRIVATE FL_GUARD_TESTABLE)

# Real cross-process injection, against our own dummy D3D11 app — no game and
# no anti-cheat surface (14_TESTING §Integration tests).
target_compile_definitions(fl_guard_test PRIVATE
    FL_HARNESS_EXE=L"$<TARGET_FILE:hook-harness>"
    FL_OVERLAY_DLL=L"$<TARGET_FILE:FrameLedger.Overlay>"
)
add_dependencies(fl_guard_test hook-harness FrameLedger.Overlay)

add_test(NAME fl_guard COMMAND fl_guard_test)

# TODO(P1): ring-buffer and fault-policy suites, now that Catch2 is here.
