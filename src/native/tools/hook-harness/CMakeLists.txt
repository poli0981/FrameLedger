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
add_test(NAME fl_proxy_swapchain COMMAND hook-harness --probe-proxy)
