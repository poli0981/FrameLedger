# Build-profile probes for 20_OPEN_QUESTIONS H1 and H3.
#
# Compiled with the EXACT flags 17_HOOK_ENGINE mandates for the Overlay. That
# is the whole point: a probe built with different flags would prove nothing
# about those flags. In particular /guard:cf and -D_HAS_EXCEPTIONS=0 are
# applied HERE while they are still withheld from the Overlay target itself,
# because the Overlay must not adopt them until these probes pass.
add_executable(fl-probe-hookprofile main.cpp)
# fl_hostile_env_flags carries exactly what the Overlay carries, so the probe
# cannot drift away from the configuration it is supposed to be validating.
# /guard:cf must reach the LINK line — the linker emits the guard tables that
# make the mitigation real. Compile-only would be a silent no-op and the probe
# would pass for the wrong reason; verified via dumpbin /loadconfig.
target_link_libraries(fl-probe-hookprofile PRIVATE FrameLedger.Shm fl_native_flags fl_hostile_env_flags minhook)
target_compile_definitions(fl-probe-hookprofile PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_test(NAME fl_hook_profile COMMAND fl-probe-hookprofile)
