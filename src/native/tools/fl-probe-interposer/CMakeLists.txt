# Does a vtable-slot present hook see a Streamline title's presents?
# (docs/20_OPEN_QUESTIONS.md §H5 case 3, docs/spike-notes.md §5.)
#
# docs/03_METRICS.md §Counting native vs displayed rests on the assumption that
# the object a game calls Present on belongs to the class whose shared dxgi.dll
# vtable we patch. If a Streamline title's swapchain is a different class, a
# real-vtable hook sees nothing, F_disp == F_app, and fg_factor is STRUCTURALLY
# 1.0 — the failure mode the GetFrameStatistics rung was deleted for.
#
# hook-harness cannot answer it: --probe-proxy builds our own forwarding wrapper,
# which is the easy case by construction, and §H5 names the interposer as the
# case the harness structurally cannot reach.
#
# fl_native_flags only, NOT fl_hostile_env_flags: this runs in our own process,
# so the -D_HAS_EXCEPTIONS=0 / no-allocation rules that shape the Overlay do not
# apply. Same reasoning as fl-probe-guard.
add_executable(fl-probe-interposer main.cpp)
target_link_libraries(fl-probe-interposer PRIVATE fl_native_flags d3d11 dxgi)
# psapi: the engagement check enumerates OUR OWN modules to prove the interposer
# actually loaded its plugins. Without that, a passthrough result reads as an
# answer — which is exactly how the first run of this probe was wrong.
target_link_libraries(fl-probe-interposer PRIVATE Psapi)
target_compile_definitions(fl-probe-interposer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# ONLY THE CONTROL IS A CTEST, and that is deliberate.
#
# Part 2 needs a real sl.interposer.dll from a title the user has installed. We
# do not ship one, do not download one, and a ctest that skips on every machine
# without a game is a gate that cannot fail — this file's own §S2 precedent.
#
# What IS registered is the control, which is meaningful everywhere: two
# swapchains created by different routes through the real dxgi.dll must share a
# vtable (the property 17_HOOK_ENGINE §swapchainId depends on, measured across
# five configurations), and two different interfaces must not (proving the
# comparison discriminates at all). If either direction ever stops holding, the
# swapchainId design and the whole vtable-hook premise are wrong, and that should
# fail the build rather than wait to be rediscovered inside a game.
#
# Run part 2 deliberately, like --probe-cost:
#   fl-probe-interposer --interposer "D:\...\Cyberpunk 2077\bin\x64"
add_test(NAME fl_vtable_identity_control COMMAND fl-probe-interposer)
