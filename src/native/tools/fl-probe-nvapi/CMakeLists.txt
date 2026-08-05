# Proves the vendored NVAPI material is usable — and that L3's absence is a
# normal condition rather than a crash (docs/18_GPU_VENDOR_APIS.md §L3).
#
# The COMPILE is half the test. Nine headers were taken out of a nineteen-file
# repository; if the include closure is short by one, this target does not
# build. Nothing else in the tree compiles against fl_nvapi yet, so without this
# the vendoring would be asserted rather than verified.
#
# fl_native_flags only, NOT fl_hostile_env_flags: this runs in our own process.
add_executable(fl-probe-nvapi main.cpp)
target_link_libraries(fl-probe-nvapi PRIVATE fl_native_flags fl_nvapi)
target_compile_definitions(fl-probe-nvapi PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# GREEN ON BOTH KINDS OF MACHINE, BY DESIGN, AND IT SAYS WHICH ONE IT IS ON.
# A hosted CI runner has no NVIDIA driver, so it takes the DEGRADED branch and
# exits 0 having proven the thing §L3 actually requires: NvAPI_Initialize
# failing must disable L3 cleanly. The dev box (RTX 5080) takes the AVAILABLE
# branch and measures driver version, GPU count and name.
#
# The failure mode this guards against is the one that would otherwise hide
# here: a probe that printed the same line in both cases would exit 0 on a
# machine where NVAPI was completely broken. The two branches print different
# words and the enumerate branch refuses a zero GPU count.
add_test(NAME fl_nvapi_probe COMMAND fl-probe-nvapi)
