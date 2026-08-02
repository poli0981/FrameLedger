# 17 — Hook engine (`FrameLedger.Overlay`, C++20)

The injected DLL. Its whole job: install hooks, write 64-byte records into a ring, stay invisible in the frame budget, and never take the game down.

## Build profile

- C++20, MSVC v143+, x64 only, `/MT` (static CRT — never drag a VC runtime dependency into a host process we don't control).
- `/GS /guard:cf /Qspectre`, `/O2`, no RTTI (`/GR-`), C++ exceptions disabled in this target (`-D_HAS_EXCEPTIONS=0`); error handling is return codes + SEH.
- No STL container that allocates in a hook path. `std::atomic` and fixed arrays are fine.
- VERSIONINFO populated (`ProductName=FrameLedger`, real company/version) — being identifiable is required by `19_SAFETY`.
- Exports: `FlGetBuildId()`, `FlRequestUnhook()`, `FlGetStatus()`. Real names, no ordinal-only tricks.

## DLL entry

`DllMain(DLL_PROCESS_ATTACH)` does **only**: `DisableThreadLibraryCalls`, then `CreateThread` → `InitThread`. Nothing else — loader lock rules. All real work (dummy-device creation, MinHook init, hook installation, shm creation) happens on `InitThread`.

`InitThread` order:
1. Open/create shared memory `Local\FrameLedger.Ring.<pid>`; write the handshake header (layout version, build id, pid, api=unknown).
2. `MH_Initialize()`.
3. Resolve which graphics APIs are present (`GetModuleHandleW` on `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `d3d9.dll`, `opengl32.dll`) — and register a `LoadLibrary` hook so APIs loaded **later** still get hooked (many games load D3D12 lazily).
4. Install present hooks for what exists (below).
5. Install feature hooks (upscaler / RT / PSO) lazily — the first time their module appears.
6. Publish `status = ready` in the handshake block.

## Getting vtable addresses (COM)

Never hardcode vtable indices. Create a throwaway object, read the pointer, hook the pointer:

```cpp
// D3D11/DXGI: create a 1x1 swapchain on a dummy HWND (or DXGI_SWAP_EFFECT_FLIP_DISCARD w/ CreateSwapChainForComposition)
// D3D12: D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, ...) then dummy command queue + swapchain
void** vtbl = *reinterpret_cast<void***>(dummySwapChain);
MH_CreateHook(vtbl[kPresentIndex], &Hook_Present, reinterpret_cast<void**>(&Orig_Present));
```

Index constants (`IDXGISwapChain::Present = 8`, `Present1 = 22` on `IDXGISwapChain1`, `ResizeBuffers = 13`, `IDirect3DDevice9::Present = 17`, `EndScene = 42`) are written down as *expectations* in `HookIndices.h` and **verified at runtime** against the dummy object before hooking; a mismatch aborts hook installation and reports rather than corrupting the vtable. Release the dummy objects immediately after reading vtables.

Prefer hooking the **vtable entry** over inline-patching for COM methods (cleaner uninstall, no trampoline hazards); use MinHook inline hooks for flat C exports (`wglSwapBuffers`, NGX/FFX/XeSS entry points).

## Hook inventory

Every hook must be listed here with a purpose. Anything not on this list is not allowed to exist (`19_SAFETY` review checklist).

### Presentation
| Hook | Purpose |
|---|---|
| `IDXGISwapChain::Present`, `Present1` | Frame boundary, QPC, sync interval, present flags |
| `IDXGISwapChain::ResizeBuffers`, `ResizeTarget` | Output resolution changes mid-session |
| `IDXGISwapChain::SetFullscreenState` | Fullscreen ↔ borderless transitions |
| `IDXGISwapChain4::SetColorSpace1` | HDR output detection |
| `IDXGIFactory::CreateSwapChain*` | Capture swapchain desc (format, buffer count, swap effect, flags) at creation |
| `IDirect3DDevice9::Present` / `EndScene` | D3D9 titles (very common in VN/JRPG/older indie) |
| `wglSwapBuffers` | OpenGL titles |
| Vulkan `vkQueuePresentKHR` | via layer, not hook (below) |

### Upscaling / frame generation (the accuracy problem this rewrite exists to solve)
| Hook | Yields |
|---|---|
| NGX: `NVSDK_NGX_D3D11/D3D12/VULKAN_CreateFeature`, `EvaluateFeature`, `ReleaseFeature` | Which NGX feature is *actually created and evaluated per frame*: SuperSampling (DLSS), RayReconstruction (DLSS-D), FrameGeneration (DLSS-G) |
| NGX parameter accessors (`NVSDK_NGX_Parameter_SetI/GetI/SetUI`) | `Width`/`Height` (render) vs `OutWidth`/`OutHeight` (output), `PerfQualityValue` (quality preset), sharpness |
| Streamline: `slInit`, `slSetFeatureLoaded`, `slEvaluateFeature`, `slSetConstants`, `slGetFeatureRequirements` | Feature set actually active when the game goes through SL rather than NGX directly (`kFeatureDLSS`, `DLSS_G`, `DLSS_RR`, `Reflex`, `NIS`) |
| FidelityFX: `ffxFsr2ContextCreate` / `ffxFsr3*` / unified `ffxCreateContext` (`ffx_api`) | `maxRenderSize` vs `displaySize`/`maxUpscaleSize`, FSR version, frame-interpolation context presence |
| XeSS: `xessD3D12CreateContext`, `xessD3D12Init`, `xessD3D12Execute` | `outputResolution`, `qualitySetting`, XeSS version; `xess_fg` variants for XeFG |
| `IDXGISwapChain` present-count vs `GetFrameStatistics().PresentCount` | Driver-inserted (generated) frames — see `03_METRICS` §FG ground truth |

> ⚠ **Symbol names above are from vendor SDK conventions and must be verified against the actual exports on the dev machine during the P0 spike.** These SDKs rename and restructure between versions (especially the FidelityFX move to `ffx_api`, and NGX/Streamline dual paths). Resolve everything dynamically by name with a null-check + capability flag; a missing export must degrade to "unknown", never crash.

### Ray tracing
| Hook | Yields |
|---|---|
| `ID3D12Device5::CreateStateObject` | RT pipeline creation; read `D3D12_RAYTRACING_PIPELINE_CONFIG.MaxTraceRecursionDepth`, shader counts |
| `ID3D12GraphicsCommandList4::DispatchRays` | **Per-frame proof that rays are dispatched**, plus dispatch W×H×D (→ rays-per-pixel ratio, the main PT heuristic input) |
| `ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure` | AS build/update activity — **catches inline RayQuery too**, which `DispatchRays` alone misses |
| Vulkan `vkCmdTraceRaysKHR`, `vkCmdBuildAccelerationStructuresKHR` | Same, via layer |

### Pipeline / stutter attribution
| Hook | Yields |
|---|---|
| `ID3D12Device::CreateGraphicsPipelineState` / `CreateComputePipelineState`, `ID3D12Device2::CreatePipelineState` | PSO compilations per frame → correlate with frame-time spikes = **shader-compilation stutter attribution** (a Tier-1-only feature; ETW cannot attribute this) |
| `ID3D11Device::CreateVertexShader`/`CreatePixelShader` | D3D11 equivalent, coarser |

### Memory / latency
| Hook / call | Yields |
|---|---|
| `IDXGIAdapter3::QueryVideoMemoryInfo` (we **call** it, not hook it, once per second from our own thread) | **Per-process VRAM** `CurrentUsage` + `Budget` — previously impossible; now exact |
| NVAPI Reflex: hook `NvAPI_D3D_SetSleepMode`, `NvAPI_D3D_SetLatencyMarker`; call `NvAPI_D3D_GetLatency` | Reflex on/off + PC latency breakdown. Declarations come from the vendored MIT NVAPI headers (`18_GPU_VENDOR_APIS` §L3); the Overlay links them like any other header. NVIDIA-only — the whole block compiles out to a no-op capability flag elsewhere |

### Explicitly not hooked
Input APIs, file I/O, network, memory allocators, window messages beyond what the overlay needs, anything game-specific. Rule 4 in CLAUDE.md.

## Vulkan: layer, not hook

Vulkan gets an **implicit layer** (`FrameLedger.VkLayer`), not injection — it is the mechanism Khronos supports, it is far more robust than hooking dispatch tables, and it is how OBS and RTSS do it.

- Manifest JSON registered under `HKCU\SOFTWARE\Khronos\Vulkan\ImplicitLayers` (per-user, no admin).
- Layer name `VK_LAYER_frameledger_overlay`, with `disable_environment` so a user can turn it off with an env var — and so it is well-behaved by Vulkan convention.
- Intercepts `vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkCmdTraceRaysKHR`, `vkCmdBuildAccelerationStructuresKHR`, `vkCreateGraphicsPipelines`.
- The layer respects the same guard: on init it checks the enable-list written by the Agent (a small per-user config the Agent maintains) and stays fully passthrough for any process not opted in. **A layer is machine-wide by nature — this check is mandatory, not optional.**
- Registered only while at least one Vulkan game has hooking enabled; unregistered on uninstall (Velopack hook) and when the last such game is disabled.

## Ring writer (hot path)

```cpp
struct alignas(64) FlFrameRecord {   // 64 bytes exactly, static_assert'd
    uint64_t qpc;                    // present entry timestamp
    uint32_t frameIndex;
    uint32_t presentFlags;
    uint16_t syncInterval;
    uint16_t renderW, renderH;       // 0 = unknown
    uint16_t outputW, outputH;
    uint8_t  api;                    // d3d9|d3d11|d3d12|vulkan|opengl
    uint8_t  upscaler;               // none|dlss|dlss_rr|fsr2|fsr3|fsr4|xess|nis|unknown
    uint8_t  upscalerQuality;        // vendor enum, 0xFF unknown
    uint8_t  fgMode;                 // none|dlss_g|fsr_fg|xefg|afmf|unknown
    uint8_t  rtFlags;                // bit0 asBuild, bit1 dispatchRays, bit2 rtPsoAlive
    uint8_t  hdr;
    uint16_t dispatchRaysCount;
    uint16_t psoCreatedThisFrame;
    uint64_t vramUsedBytes;
    uint32_t reflexLatencyUs;        // 0 = unavailable
    uint32_t gpuBusyUs;              // 0 = unavailable
    uint32_t seq;                    // seqlock counter for torn-read detection
    uint32_t _pad;
};
static_assert(sizeof(FlFrameRecord) == 64);
```

- **SPSC lock-free ring**, capacity a power of two (default 8192 records = 512 KB), overwrite-oldest.
- Writer: bump `seq` (odd = writing), fill, bump `seq` (even = complete), `store(writeIndex, release)`. Reader validates `seq` before/after copy and skips torn records.
- Header block (separate cache line): layout version, build id, pid, status, api mask, `droppedRecords`, `faultCount`, control flags (`pauseRequested`, `unhookRequested`, `overlayEnabled`).
- **The hot path performs: one QPC read, a few field reads from cached state, one 64-byte store, two atomics.** No syscall, no allocation, no lock, no logging. Target ≤ 1 µs per present.
- Per-frame mutable state (current upscaler, render res, dispatch counts) lives in a small struct updated by the feature hooks and *read* by the present hook; counters reset at present.

## Fault policy

Every hook body is wrapped:

```cpp
#define FL_HOOK_GUARD(body, fallback)                    \
    __try { body }                                       \
    __except (FlFilter(GetExceptionCode())) { fallback }
```

- Any SEH exception inside our code ⇒ increment `faultCount`, record which hook, **return control to the original function** so the game continues.
- 3 faults total ⇒ set `status = self_disabled`, uninstall all hooks (`MH_DisableHook(MH_ALL_HOOKS)`), stop writing, stay dormant. The Agent sees the flag and finalizes the session as `degraded`.
- Never `__try` around the call to the original function — only around *our* code, so we never mask a game bug as ours or vice versa.
- The trampoline call to the original is always executed exactly once, on every path, including error paths.

## Unhooking

`FlRequestUnhook()` (or the control flag) ⇒ `MH_DisableHook(MH_ALL_HOOKS)`, restore vtable entries, flush the ring, set status. **The DLL is not `FreeLibrary`'d from the live process** — a thread could still be inside a trampoline. It goes dormant and unloads with the process. This is deliberate and documented.

## Native logging

No logging in hook bodies. A small fixed-size in-memory ring of structured events (hook installed, symbol missing, fault, unhook) is flushed to `logs\overlay-<pid>-*.log` by the init thread at session end, on unhook, or on Agent request — never mid-frame.

## Test harness

`tools/hook-harness` — a minimal D3D11 / D3D12 / Vulkan / D3D9 app that presents at a controlled rate, optionally creates RT PSOs and dispatches rays, and can simulate an upscaler by calling stub exports with the same names. It lets CI and local dev exercise every hook path with **no game and no anti-cheat surface at all** (`14_TESTING`).
