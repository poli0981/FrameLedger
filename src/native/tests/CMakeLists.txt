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

# Check 4's fail-closed matrix, as its own ctest name so a regression in the
# static pre-scan is legible in the ctest summary rather than buried in
# fl_guard's total. Same binary, selected by tag — duplicating the evidence
# fakes into a second target would be a second set of seams to keep in step.
add_test(NAME fl_prescan COMMAND fl_guard_test "[prescan]")

# The rules file we actually SHIP, against the parser that has to read it.
#
# Only fl_ac_rules.cpp is compiled in — not the whole injector. This target has
# no business near the injection primitive, and keeping it out means the test
# cannot accidentally become a second place that reaches it.
#
# Deliberately NOT FL_GUARD_TESTABLE: there is no evidence seam here to need.
add_executable(fl_rules_budget_test
    rules_budget_test.cpp
    "${CMAKE_SOURCE_DIR}/FrameLedger.Injector/src/fl_ac_rules.cpp"
)
target_include_directories(fl_rules_budget_test PRIVATE "${CMAKE_SOURCE_DIR}/FrameLedger.Injector/include")
target_link_libraries(fl_rules_budget_test PRIVATE fl_native_flags fl_jsmn Catch2::Catch2WithMain)
target_compile_options(fl_rules_budget_test PRIVATE /EHsc)

# The repository seed, by absolute path at compile time.
#
# NOT the %LOCALAPPDATA% lookup fl-probe-vklayer uses. That probe reads the
# product's one location because the layer has no override (§S3) and it skips
# honestly when nothing is installed. This test asks a different question — does
# the file IN THIS COMMIT parse — so reading anything but the repository copy
# would let an unrelated file on the developer's machine answer it.
target_compile_definitions(fl_rules_budget_test PRIVATE
    FL_SEED_RULES="${CMAKE_SOURCE_DIR}/../../rules/detection-rules.json"
)

add_test(NAME fl_rules_budget COMMAND fl_rules_budget_test)

# TODO(P1): ring-buffer and fault-policy suites, now that Catch2 is here.
