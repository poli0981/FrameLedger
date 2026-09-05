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
target_link_libraries(hook-harness PRIVATE FrameLedger.Shm fl_dxgi_vtable fl_native_flags fl_streamline_headers
    fl_fidelityfx_headers fl_fsr3_host_headers)
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
    # The three AMD leaves and the loader stand-in (tools/vendor-stubs, same rule:
    # absolute paths, never a name a game directory could satisfy).
    FL_STUB_FFX_DX12=L"$<TARGET_FILE:fl_stub_ffx_dx12>"
    FL_STUB_FFX_UPSCALER=L"$<TARGET_FILE:fl_stub_ffx_upscaler>"
    FL_STUB_FFX_FG=L"$<TARGET_FILE:fl_stub_ffx_fg>"
    FL_STUB_FFX_LOADER=L"$<TARGET_FILE:fl_stub_ffx_loader>"
    # The FSR 3.0 host stand-in (ffx_fsr3_x64.dll), same rule.
    FL_STUB_FFX_FSR3_HOST=L"$<TARGET_FILE:fl_stub_ffx_fsr3_host>"
    # Shared with fl_guard_test from src/native/CMakeLists.txt, so the fixture and
    # the assertion cannot hold different numbers.
    FL_TAGGED_RENDER_W=${FL_TAGGED_RENDER_W}
    FL_TAGGED_RENDER_H=${FL_TAGGED_RENDER_H}
    FL_DXR_DISPATCH_W=${FL_DXR_DISPATCH_W}
    FL_DXR_DISPATCH_H=${FL_DXR_DISPATCH_H}
    FL_DXR_DISPATCH_D=${FL_DXR_DISPATCH_D}
)
add_dependencies(hook-harness fl_stub_sl_interposer fl_stub_sl_common fl_stub_sl_interposer_v1
    fl_stub_ffx_dx12 fl_stub_ffx_upscaler fl_stub_ffx_fg fl_stub_ffx_loader fl_stub_ffx_fsr3_host)

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

# HANDOFF item 4's pre-flight, and it runs BEFORE the ray-tracing hooks are
# written rather than beside them.
#
# fl_d3d12_vtable_indices is the fl_vtable_indices argument one interface over:
# the slot numbers are derivable from the COM inheritance chain, and derivable is
# exactly the confidence this project distrusts. It patches each slot, calls the
# method BY NAME, and checks the detour ran. It needs D3D12 and NOT ray tracing --
# ID3D12GraphicsCommandList4 is a runtime interface, so its vtable carries the
# ray-tracing slots on an adapter that cannot trace a ray.
add_test(NAME fl_d3d12_vtable_indices COMMAND hook-harness --probe-d3d12-vtable)

# fl_dxr_probe answers four questions the hook design must not guess at: whether
# any adapter here reports DXR at all; whether two command lists from one device
# share a vtable (with the CONTROL that a different interface does not); whether
# DIRECT and COMPUTE lists share one, which decides whether the hook patches one
# vtable or two; and whether a WARP list shares with a hardware list, which is what
# a throwaway-device acquisition would have rested on.
#
# An unanswerable question PRINTS as unanswered and never as a pass -- a hosted CI
# runner has no hardware adapter, so Q4 is genuinely unanswerable there, and
# HANDOFF item 4 is explicit that a skip reported as coverage is the failure to
# avoid. Q2 to Q4 assert regardless of Q1, so a machine with no ray tracing still
# runs a fixture that can go red.
add_test(NAME fl_dxr_probe COMMAND hook-harness --probe-dxr)

# dispatchRaysVolume's arithmetic, against the shapes a caller can send. A unit
# probe rather than an injected case, exactly as fl_sl_inputs is: the hazard is a
# WRAPPED PRODUCT rather than a crash, a game fixture could only approximate these
# extents, and the accumulator's saturation is a property of a SEQUENCE of adds
# that no single expression states. The identities themselves are static_asserts
# in fl_rt_accum.h, checked in every TU that includes it, so this deliberately
# does not re-prove them at runtime.
add_test(NAME fl_dxr_inputs COMMAND hook-harness --probe-dxr-inputs)

# MODULE-SCOPED RESOLUTION, with a decoy module exporting the same symbol name.
#
# docs/HANDOFF.md item 2's whole argument for scoping is arithmetic — a
# name-resolved hook counts one logical evaluation more than once, and 03_METRICS
# sets F_app = sum(fgEvaluations) with fg_factor = presents / F_app (owner ruling
# 2026-08-14; this comment carried the pre-ruling subtraction), so an inflated
# count inflates Native FPS and deflates fg_factor — the number CLAUDE.md rule 6
# exists to protect, reached by arithmetic rather than by measurement. Without a
# second exporter in the process that property is unfalsifiable: a name-only
# resolver and a module-scoped one behave identically and every test passes. The
# decoy is what makes them distinguishable.
add_test(NAME fl_upscaler_resolve COMMAND hook-harness --probe-upscaler-resolve)

# Its own ctest, and its own PROCESS, which is the point. Both fixtures are named
# sl.interposer.dll and GetModuleHandleExW resolves by name, so running this
# inside fl_upscaler_resolve would have the two silently test each other.
add_test(NAME fl_sl_abi_guard COMMAND hook-harness --probe-sl-abi)

# AMD's version of the scoping argument, one step further along. Four modules
# export the same five ffx-api names -- three LEAVES and the SDK 2.x LOADER -- and
# the Overlay hooks all four, because a game calls whichever it shipped against and
# the loader was MEASURED not to re-enter the leaves' exports (three loader titles at
# FSR, zero leaf dispatches, 2026-09-04). This probe drives the Overlay's own resolver
# against all four, then pushes dispatches THROUGH the loader stand-in and asserts
# they reached exactly one leaf, once, and touched no leaf export -- the vacuity guard
# for a K = 1 control that would otherwise be about nothing.
add_test(NAME fl_ffx_resolve COMMAND hook-harness --probe-ffx-resolve)

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

