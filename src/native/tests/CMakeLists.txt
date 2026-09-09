# Native unit tests (docs/14_TESTING.md §Native unit tests).
#
# The record and header layout contract needs no test framework — it is enforced
# by static_assert in fl_shm.h, so simply compiling this target proves it.
# enable_testing() is called by the top-level CMakeLists; calling it here
# instead configures fine and then finds no tests at ctest time.

add_executable(fl_shm_layout_test layout_test.cpp)
target_link_libraries(fl_shm_layout_test PRIVATE FrameLedger.Shm fl_native_flags)
add_test(NAME fl_shm_layout COMMAND fl_shm_layout_test)

# The NVAPI bridge's C ABI (P2 PR-E2). Links the SHIPPED DLL rather than compiling its source in,
# because what the managed side loads is that DLL's export table; the DLL is copied beside the test
# so the loader finds it the way the Agent's directory will. Green on both kinds of machine and it
# says which (the same alternation fl_nvapi_probe pins, for the same reason).
add_executable(fl_nvapi_bridge_test nvapi_bridge_test.cpp)
target_link_libraries(fl_nvapi_bridge_test PRIVATE FrameLedger.NvapiBridge fl_native_flags Catch2::Catch2WithMain)
target_compile_options(fl_nvapi_bridge_test PRIVATE /EHsc)
add_custom_command(TARGET fl_nvapi_bridge_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:FrameLedger.NvapiBridge> $<TARGET_FILE_DIR:fl_nvapi_bridge_test>)
add_test(NAME fl_nvapi_bridge COMMAND fl_nvapi_bridge_test)
set_tests_properties(fl_nvapi_bridge PROPERTIES PASS_REGULAR_EXPRESSION "BRANCH: (AVAILABLE|DEGRADED)")

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
# Shell32/Ole32: fl_ac_rules.cpp resolves the rules path via SHGetKnownFolderPath
# (§S21). fl_ac_floor: the generated blocklist floor it compiles in.
target_link_libraries(fl_guard_test PRIVATE FrameLedger.Shm fl_native_flags fl_jsmn Catch2::Catch2WithMain
    Shell32 Ole32 fl_ac_floor)
add_dependencies(fl_guard_test fl_ac_floor_gen)
add_dependencies(fl_guard_test fl_stub_sl_common fl_stub_sl_interposer)

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
#
# THE PAYLOAD IS STAGED BESIDE THE TEST BINARY, and that is load-bearing rather
# than tidiness (§S22). The guard now refuses a payload that does not resolve
# into the directory the guard's own code was loaded from, and in this target the
# guard IS the test executable — the injector sources are compiled in (above), so
# OwnDirectory() answers with the test's directory. Pointing FL_OVERLAY_DLL at
# $<TARGET_FILE:FrameLedger.Overlay> would name a sibling build directory, and
# every injection test would refuse kPayloadNotOurs. It would also stop matching
# the shipped arrangement, where 12_BUILD publishes the Agent, the guard and the
# Overlay into one out/app.
#
# Staged through a target that DEPENDS ON THE OVERLAY rather than a POST_BUILD
# step on fl_guard_test. A POST_BUILD copy only refreshes when the TEST relinks,
# so a rebuilt Overlay would leave a stale payload in place and the suite would
# keep testing the previous DLL — the stale-artifact defect 20_OPEN_QUESTIONS
# records having already shipped once.
# A plain path, not $<TARGET_FILE_DIR:fl_guard_test>: add_custom_command's OUTPUT
# may not carry a target-dependent generator expression. No target here sets
# RUNTIME_OUTPUT_DIRECTORY, so fl_guard_test lands in CMAKE_CURRENT_BINARY_DIR —
# and that assumption is not left to hold quietly. The "payload check can PASS"
# case REQUIREs that FL_OVERLAY_DLL resolves into the running binary's own
# directory, so if the two ever diverge the suite goes red instead of the staging
# silently landing somewhere the guard does not look.
set(FL_STAGED_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/FrameLedger.Overlay.dll")
add_custom_command(
    OUTPUT "${FL_STAGED_OVERLAY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:FrameLedger.Overlay>" "${FL_STAGED_OVERLAY}"
    DEPENDS FrameLedger.Overlay
    COMMENT "Staging FrameLedger.Overlay.dll beside fl_guard_test (payload identity, §S22)"
    VERBATIM
)
add_custom_target(fl_stage_overlay_payload DEPENDS "${FL_STAGED_OVERLAY}")

target_compile_definitions(fl_guard_test PRIVATE
    FL_HARNESS_EXE=L"$<TARGET_FILE:hook-harness>"
    # The extent hook-harness tags, from src/native/CMakeLists.txt. Shared rather
    # than retyped: a fixture and its assertion holding separate copies of the
    # number under test is how a test starts proving something else.
    FL_TAGGED_RENDER_W=${FL_TAGGED_RENDER_W}
    FL_TAGGED_RENDER_H=${FL_TAGGED_RENDER_H}
    FL_DXR_DISPATCH_W=${FL_DXR_DISPATCH_W}
    FL_DXR_DISPATCH_H=${FL_DXR_DISPATCH_H}
    FL_DXR_DISPATCH_D=${FL_DXR_DISPATCH_D}
    # The Streamline stubs, for the LoadLibrary detour's late-load fixture (P1 item
    # 1): sl.common is an inert decoy exporting nothing (also copied under an
    # anti-cheat name for the early-stop case), sl.interposer is the module whose
    # late arrival must be hooked within milliseconds rather than on the next tick.
    FL_STUB_SL_COMMON=L"$<TARGET_FILE:fl_stub_sl_common>"
    FL_STUB_SL_INTERPOSER=L"$<TARGET_FILE:fl_stub_sl_interposer>"
    FL_OVERLAY_DLL=L"${FL_STAGED_OVERLAY}"
    # The un-staged original, so a test can prove the check discriminates on
    # DIRECTORY rather than on filename: same bytes, same name, wrong place.
    FL_OVERLAY_DLL_ELSEWHERE=L"$<TARGET_FILE:FrameLedger.Overlay>"
)
add_dependencies(fl_guard_test hook-harness FrameLedger.Overlay fl_stage_overlay_payload)

add_test(NAME fl_guard COMMAND fl_guard_test)

# The Vulkan layer's capture side (P1 item 3), driven by a FAKE loader chain --
# no registry, no loader, no GPU -- so it is merge-gated. Three ctests, three
# processes, because one process carries one ring and each DLL flavour makes one.
# fl_ac_rules.cpp is compiled in for LocalAppDataDir / RulesFilePath, the same
# functions the layer reads through, so the files the test writes land exactly
# where the layer looks.
add_executable(fl_vklayer_test vklayer_test.cpp "${CMAKE_SOURCE_DIR}/FrameLedger.Injector/src/fl_ac_rules.cpp")
target_include_directories(fl_vklayer_test PRIVATE "${CMAKE_SOURCE_DIR}/FrameLedger.Injector/include")
target_link_libraries(fl_vklayer_test PRIVATE FrameLedger.Shm fl_native_flags fl_jsmn fl_vulkan_headers
    Catch2::Catch2WithMain Shell32 Ole32 fl_ac_floor)
add_dependencies(fl_vklayer_test fl_ac_floor_gen FrameLedger.VkLayer fl_vklayer_shortdeadline hook-harness)
target_compile_options(fl_vklayer_test PRIVATE /EHsc)
target_compile_definitions(fl_vklayer_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    FL_VKLAYER_DLL=L"$<TARGET_FILE:FrameLedger.VkLayer>"
    FL_VKLAYER_SHORTDEADLINE_DLL=L"$<TARGET_FILE:fl_vklayer_shortdeadline>"
    # The directory the loader is pointed at through VK_ADD_IMPLICIT_LAYER_PATH:
    # it holds the generated manifest beside the DLL.
    FL_VKLAYER_MANIFEST_DIR=L"$<TARGET_FILE_DIR:FrameLedger.VkLayer>"
    FL_HARNESS_EXE=L"$<TARGET_FILE:hook-harness>"
    FL_REPO_RULES=L"${CMAKE_SOURCE_DIR}/../../rules/detection-rules.json"
)
# Compare-and-restore for INLINE patches (P1 item 4; 17_HOOK_ENGINE §Unhooking):
# the predicate the Overlay consults before MH_DisableHook, against a real
# MinHook patch and a simulated foreign one. fl_unhook_preserves_foreign covers
# the vtable form in the harness; the Overlay's hooks are this form.
add_executable(fl_unhook_inline_test unhook_inline_test.cpp)
target_include_directories(fl_unhook_inline_test PRIVATE "${CMAKE_SOURCE_DIR}/FrameLedger.Overlay/include")
target_link_libraries(fl_unhook_inline_test PRIVATE fl_native_flags minhook Catch2::Catch2WithMain)
target_compile_options(fl_unhook_inline_test PRIVATE /EHsc)
target_compile_definitions(fl_unhook_inline_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME fl_unhook_inline COMMAND fl_unhook_inline_test)

add_test(NAME fl_vklayer COMMAND fl_vklayer_test "[vklayer]")
add_test(NAME fl_vklayer_supervision COMMAND fl_vklayer_test "[vklayer-deadline]")
# The real-loader leg SKIPS (Catch2 exit 4) where there is no Vulkan loader or no
# presentable device -- CI, per the §S29(d) census -- and runs on a dev box.
add_test(NAME fl_vklayer_real COMMAND fl_vklayer_test "[vklayer-real]")
set_tests_properties(fl_vklayer_real PROPERTIES SKIP_RETURN_CODE 4)

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
target_link_libraries(fl_rules_budget_test PRIVATE fl_native_flags fl_jsmn Catch2::Catch2WithMain Shell32 Ole32 fl_ac_floor)
add_dependencies(fl_rules_budget_test fl_ac_floor_gen)
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

# The SPSC ring (14_TESTING §Native unit tests). Half of the TODO that used to
# sit here; the fault-policy suite lands with the hook bodies it guards, since a
# fault policy with no hook to fault in is a gate on something that does not
# exist.
#
# /EHsc because Catch2 reports failures by throwing, and <thread> for the
# hammering-writer case. FrameLedger.Shm only — the ring has no business near the
# injector, and keeping it out means this target cannot become a second place
# that reaches the injection primitive.
add_executable(fl_ring_test ring_test.cpp)
target_link_libraries(fl_ring_test PRIVATE FrameLedger.Shm fl_native_flags Catch2::Catch2WithMain)
target_compile_options(fl_ring_test PRIVATE /EHsc)
add_test(NAME fl_ring COMMAND fl_ring_test)

# STILL NO FAULT-POLICY SUITE, and the reason is worth more than the TODO it
# replaces. The three-fault self-disable is NFR-3's entire mechanism and the
# clause 20_OPEN_QUESTIONS §H8 was closed on, and nothing exercises it.
#
# What blocks it is the VEHICLE, not the assertions. To count a fault we must
# make our own hook body fault, in a live injected target, deterministically:
#
#   - A fault seam compiled into FrameLedger.Overlay is rejected on sight. It
#     ships into game processes, and "an export that makes the DLL crash on
#     demand" is a worse thing to hand an anti-cheat vendor than the bug.
#     §S15's evidence-seam precedent (compiled out of everything that ships,
#     verified with dumpbin) is the standard, and a fault seam cannot meet it
#     because the fault has to happen on the REAL path to be counted by it.
#   - VirtualProtectEx'ing the target's mapped ring to PAGE_NOACCESS was
#     considered and is not safe as stated: the test cannot locate the view's
#     base address in the target, and protecting the whole view makes
#     StopObserving's own status store fault -- inside the __except handler,
#     where there is no guard, which crashes the harness instead of testing it.
#     Protecting the ring region ALONE would work, if the base address problem
#     were solved.
#
# So the honest state is: FL_HOOK_GUARD's structure is reviewed and not
# exercised, and NoteFault's routing through StopObserving (2026-08-05) is a fix
# made WITHOUT a regression test. Recorded here rather than left as a bare TODO
# so the next reader knows the gap is known and why, per build.ps1's
# skip-loudly discipline. Tracked in 20_OPEN_QUESTIONS §S24.
