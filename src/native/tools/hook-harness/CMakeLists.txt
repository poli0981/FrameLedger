# Dummy D3D11 app for exercising hook paths with no game and no anti-cheat
# surface (17_HOOK_ENGINE §Test harness).
#
# Deliberately NOT built with fl_hostile_env_flags. This is our own process, not
# an injected DLL: it has no reason to give up exceptions, and the COM proxy in
# proxy_swapchain.h is ordinary C++ that benefits from them.
add_executable(hook-harness main.cpp proxy_swapchain.h)
# fl_dxgi_vtable, not a local copy of the slot numbers: this harness proves the
# indices by behaviour, and it must prove the SHIPPED ones (§S29(b)). The same
# reasoning brings in fl_hook_inventory.h from the Overlay's include directory:
# --probe-upscaler-resolve calls the OVERLAY'S resolver, not a copy of it, or it
# would prove a fact about GetProcAddress.
target_link_libraries(hook-harness PRIVATE FrameLedger.Shm fl_dxgi_vtable fl_native_flags fl_streamline_headers)
target_include_directories(hook-harness PRIVATE "${CMAKE_SOURCE_DIR}/FrameLedger.Overlay/include")
target_compile_definitions(hook-harness PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# ABSOLUTE PATHS to the stubs, resolved at build time. The harness loads them by
# full path and never by name: loading "sl.interposer.dll" by name would search
# the loader's paths and, on a developer machine with a game installed, could
# find a REAL Streamline interposer -- after which the fixture would be proving
# something about NVIDIA's build.
target_compile_definitions(hook-harness PRIVATE
    FL_STUB_SL_INTERPOSER=L"$<TARGET_FILE:fl_stub_sl_interposer>"
    FL_STUB_SL_COMMON=L"$<TARGET_FILE:fl_stub_sl_common>"
    FL_STUB_SL_INTERPOSER_V1=L"$<TARGET_FILE:fl_stub_sl_interposer_v1>"
    # Shared with fl_guard_test from src/native/CMakeLists.txt, so the fixture and
    # the assertion cannot hold different numbers.
    FL_TAGGED_RENDER_W=${FL_TAGGED_RENDER_W}
    FL_TAGGED_RENDER_H=${FL_TAGGED_RENDER_H}
)
add_dependencies(hook-harness fl_stub_sl_interposer fl_stub_sl_common fl_stub_sl_interposer_v1)

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

# MODULE-SCOPED RESOLUTION, with a decoy module exporting the same symbol name.
#
# docs/HANDOFF.md item 2's whole argument for scoping is arithmetic — a
# name-resolved hook counts one logical evaluation more than once, and
# 03_METRICS derives F_app by SUBTRACTING that count, so it lands on fg_factor,
# the number CLAUDE.md rule 6 exists to protect. Without a second exporter in the
# process that property is unfalsifiable: a name-only resolver and a
# module-scoped one behave identically and every test passes. The decoy is what
# makes them distinguishable.
add_test(NAME fl_upscaler_resolve COMMAND hook-harness --probe-upscaler-resolve)

# Its own ctest, and its own PROCESS, which is the point. Both fixtures are named
# sl.interposer.dll and GetModuleHandleExW resolves by name, so running this
# inside fl_upscaler_resolve would have the two silently test each other.
add_test(NAME fl_sl_abi_guard COMMAND hook-harness --probe-sl-abi)

# The inputs walk against malformed input. A unit probe rather than an injected
# case: every branch dereferences caller-supplied pointers, a fault in the real
# thing burns one of the three that self-disable the Overlay, and a game fixture
# could only approximate these shapes. Here they are exact and cost microseconds.
add_test(NAME fl_sl_inputs COMMAND hook-harness --probe-sl-inputs)

# The word RecordPresent drains. Its FIELD BOUNDARIES are static_asserts inside
# fl_sl_seen.h -- integer algebra, checked in every TU that includes the header, so
# no test can forget them and a 16-million-iteration loop proving a carry cannot go
# downward would be a gate that cannot fail. What this ctest covers is the part that
# is behaviour: that saturation goes HIGH (a wrapped count reads low, and fg_factor
# DIVIDES by it, so a wrap inflates the number rule 6 exists to protect), and that
# every bit below kCountShift is genuinely free -- the case a literal 0x1Fu gets
# wrong by silently swallowing the next FlSlSeen enumerator.
add_test(NAME fl_sl_seen COMMAND hook-harness --probe-sl-seen)

# §H7 — our unhook must not silently remove an overlay that hooked the same
# slot after us. Simulated rather than depending on RTSS being installed, so it
# is deterministic and runs on CI; the dev machine can then confirm against the
# real thing (spike-notes.md §Environment lists six resident overlays).
add_test(NAME fl_unhook_preserves_foreign COMMAND hook-harness --probe-unhook)

# NFR-1 per-present cost. Not registered as a ctest: it is a MEASUREMENT, and a
# timing threshold on a shared CI runner fails for reasons that have nothing to
# do with the code. Run it deliberately: hook-harness --probe-cost

