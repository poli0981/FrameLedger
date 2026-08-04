# Dummy D3D11 app for exercising hook paths with no game and no anti-cheat
# surface (17_HOOK_ENGINE §Test harness).
#
# Deliberately NOT built with fl_hostile_env_flags. This is our own process, not
# an injected DLL: it has no reason to give up exceptions, and the COM proxy in
# proxy_swapchain.h is ordinary C++ that benefits from them.
add_executable(hook-harness main.cpp proxy_swapchain.h)
target_link_libraries(hook-harness PRIVATE FrameLedger.Shm fl_native_flags)
target_compile_definitions(hook-harness PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# WARP + CreateSwapChainForComposition means no GPU and no HWND are needed, so
# these run on a hosted CI runner — which is what 20_OPEN_QUESTIONS §H4 said
# was the obstacle to verifying vtable indices anywhere but a dev machine.
add_test(NAME fl_vtable_indices COMMAND hook-harness --probe-vtable)

# WHAT COUNTS AS A FRAME. Every present in this harness used to carry
# DXGI_PRESENT_TEST, which submits nothing — measured, 10,000 of them leave
# GetLastPresentCount at 0. "N presents -> N records" was therefore a criterion
# only a writer that counts NON-frames could satisfy, and a correct Overlay had
# no green path at all. Asserted in both directions against DXGI's own counter,
# which is the one oracle here that does not share the writer's assumption.
add_test(NAME fl_frame_identity COMMAND hook-harness --probe-frames)

# The D3D12 acquisition path. This harness had 545 lines and zero D3D12
# references while CLAUDE.md called it a D3D11/D3D12/Vulkan app, and
# 17_HOOK_ENGINE §DLL entry gates hook installation on GetModuleHandleW(
# L"d3d12.dll") — never non-null here. The swapchain comes from the COMMAND
# QUEUE, which is the whole asymmetry a D3D11-only fixture cannot exercise.
add_test(NAME fl_d3d12_acquisition COMMAND hook-harness --probe-d3d12)
add_test(NAME fl_proxy_swapchain COMMAND hook-harness --probe-proxy)

# §H7 — our unhook must not silently remove an overlay that hooked the same
# slot after us. Simulated rather than depending on RTSS being installed, so it
# is deterministic and runs on CI; the dev machine can then confirm against the
# real thing (spike-notes.md §Environment lists six resident overlays).
add_test(NAME fl_unhook_preserves_foreign COMMAND hook-harness --probe-unhook)

# NFR-1 per-present cost. Not registered as a ctest: it is a MEASUREMENT, and a
# timing threshold on a shared CI runner fails for reasons that have nothing to
# do with the code. Run it deliberately: hook-harness --probe-cost

