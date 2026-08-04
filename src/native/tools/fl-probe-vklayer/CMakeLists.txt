# Proves the Vulkan layer's blocklist self-scan discriminates (§S2 second half).
#
# Loads the layer as an ordinary DLL and asks it a question — it never registers
# the layer with the Vulkan loader and never calls a Vulkan entry point, so this
# puts nothing machine-wide anywhere. Registration is tools/vklayer-blastradius.ps1,
# which is deliberately outside `build.ps1 check` because it writes to HKCU.
#
# fl_ac_rules.cpp is compiled in for ONE symbol: fl::guard::RulesFilePath. The
# probe used to expand %LOCALAPPDATA% itself, which made it a THIRD resolver of
# the path the guard and the layer share — and §S21 is precisely the bug where
# resolvers of "the ONE location" disagree. It now plants the seed by asking the
# same function the layer reads through, so the two cannot drift: if the
# resolution changes, this probe follows it or fails to compile.
add_executable(fl-probe-vklayer main.cpp
    "${CMAKE_SOURCE_DIR}/FrameLedger.Injector/src/fl_ac_rules.cpp")
target_include_directories(fl-probe-vklayer PRIVATE "${CMAKE_SOURCE_DIR}/FrameLedger.Injector/include")
target_link_libraries(fl-probe-vklayer PRIVATE fl_native_flags fl_jsmn Shell32 Ole32 fl_ac_floor)
add_dependencies(fl-probe-vklayer fl_ac_floor_gen)
target_compile_definitions(fl-probe-vklayer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    # The repository seed, installed to %LOCALAPPDATA% only when nothing is
    # there and removed afterwards. The layer reads from the ONE product
    # location and there is deliberately no override (S3) — so to exercise the
    # scan at all, the file has to actually be where the product reads it.
    FL_REPO_RULES=L"${CMAKE_SOURCE_DIR}/../../rules/detection-rules.json"
)
add_dependencies(fl-probe-vklayer FrameLedger.VkLayer)

# The layer is loaded from ITS OWN build output, passed in as a generator
# expression — never from a copy placed beside the probe.
#
# It was a copy, via a POST_BUILD command, and that was a real bug: POST_BUILD
# runs only when the PROBE target relinks, so editing only the layer left a
# STALE DLL beside the probe. A red-green canary run this way left the broken
# layer in place and the next clean run kept failing against it. The same
# mechanism could just as easily have kept a WORKING copy around and reported a
# broken layer as passing.
target_compile_definitions(fl-probe-vklayer PRIVATE
    FL_VKLAYER_DLL=L"$<TARGET_FILE:FrameLedger.VkLayer>"
)

# Skips honestly rather than passing vacuously when no rules file is installed:
# with nothing to match against, the scan correctly says "stay inert" and the
# blocked case would then pass for the wrong reason. The probe says so and
# exits 0.
add_test(NAME fl_vklayer_selfscan COMMAND fl-probe-vklayer)
