# 17 — Hook engine (`FrameLedger.Overlay`, C++20)

The injected DLL. Its whole job: install hooks, write 64-byte records into a ring, stay invisible in the frame budget, and never take the game down.

## Build profile

- C++20, MSVC v143+, x64 only, `/MT` (static CRT — never drag a VC runtime dependency into a host process we don't control).

> **x64-only is a scope decision with a user-visible consequence, stated here so
> it is not rediscovered during implementation.** An x64 DLL cannot be loaded
> into a 32-bit process, and Direct3D 9 titles — the visual-novel, JRPG and older
> indie catalogue — are almost entirely 32-bit. **D3D9 is therefore not a Tier-1
> API in v1**, and 32-bit games of any API are Tier 2. Shipping a second 32-bit
> Overlay and injector would restore them, at the cost of doubling the native
> build matrix and adding a second struct-mirror surface; that trade is recorded
> in `20_OPEN_QUESTIONS` §Scope rather than left implicit in a build flag.
- `/GS /guard:cf /Qspectre`, `/O2`, no RTTI (`/GR-`), C++ exceptions disabled in this target (`-D_HAS_EXCEPTIONS=0`); error handling is return codes + SEH.
- No STL container that allocates in a hook path. `std::atomic` and fixed arrays are fine.
- VERSIONINFO populated (`ProductName=FrameLedger`, real company/version) — being identifiable is required by `19_SAFETY`.
- Exports: `FlGetBuildId()`, `FlRequestUnhook()`, `FlGetStatus()`. Real names, no ordinal-only tricks.

## DLL entry

`DllMain(DLL_PROCESS_ATTACH)` does **only**: `DisableThreadLibraryCalls`, then `CreateThread` → `InitThread`. Nothing else — loader lock rules. All real work (dummy-device creation, MinHook init, hook installation, shm creation) happens on `InitThread`.

`InitThread` order:
1. Open/create shared memory `Local\FrameLedger.Ring.<pid>`; write the handshake header (layout version, build id, pid, api=unknown).
2. `MH_Initialize()`.
3. Resolve which graphics APIs are present (`GetModuleHandleW` on `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `opengl32.dll`) — and register a `LoadLibrary` hook so APIs loaded **later** still get hooked (many games load D3D12 lazily). **The `LoadLibrary` hook must not install hooks inline:** it runs under the loader lock, and MinHook suspends all threads to patch, which deadlocks against any thread waiting on that lock. It enqueues the request and signals the init thread, which installs outside the lock (`20_OPEN_QUESTIONS` §H2).
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

Index constants (`IDXGISwapChain::Present = 8`, `Present1 = 22` on `IDXGISwapChain1`, `ResizeBuffers = 13`) live in `HookIndices.h`.

> ⚠ **"Verified at runtime against the dummy object" is not implementable as
> stated, and must not be relied on.** A vtable slot holds a bare function
> pointer; it carries no name, no signature, and nothing that identifies which
> method it is. Reading `vtbl[8]` tells you an address, not that the address is
> `Present`. The only checks actually available are structural — the pointer is
> non-null, lies inside the module that exports the interface, and the vtable has
> at least the expected number of entries — none of which distinguish slot 8 from
> slot 9. What genuinely validates the indices is the `hook-harness` integration
> test on a known runtime plus the D3D headers themselves, not a runtime probe.
> Treat the constants as compile-time facts covered by tests (`20_OPEN_QUESTIONS`
> §H4). Release the dummy objects immediately after reading vtables.

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
- Layer name **`VK_LAYER_FRAMELEDGER_overlay`** — uppercase vendor tag, per the
  loader's `VK_LAYER_<VENDOR>_<name>` convention. This is not pedantry: a
  non-conforming name makes the Vulkan loader emit a policy warning
  (`LLP_LAYER_3`) into every Vulkan application's log on the machine. Observed
  on the dev box, where GOG Galaxy's `GalaxyOverlayVkLayer` does exactly that
  and gets named in the warning three times. `19_SAFETY` requires us to be
  plainly identifiable and well behaved; a layer that announces itself as
  malformed is the wrong kind of visible. Every conforming layer on that
  machine uses an uppercase tag — `VK_LAYER_KHRONOS_*`, `VK_LAYER_LUNARG_*`,
  `VK_LAYER_NV_*`, `VK_LAYER_VALVE_*`, `VK_LAYER_OBS_HOOK`, `VK_LAYER_RTSS`.
- Declare an API version at least as high as the applications we expect to
  layer. The same machine shows the loader warning that `VK_LAYER_OBS_HOOK` and
  `VK_LAYER_RTSS` declare 1.3 against a 1.4 application, "may cause issues".
- `disable_environment` so a user can turn it off with an env var — and so it is
  well-behaved by Vulkan convention. (`20_OPEN_QUESTIONS` §S2 proposes moving to
  `enable_environment`, which is a stronger gate; that decision is open.)

> **We will not be the only layer, and probably not the only present hook.** A
> representative dev machine carries six machine-wide implicit layers already:
> Steam Overlay, Steam Fossilize, EOS Overlay, GOG Galaxy Overlay, OBS, and
> RTSS. RTSS and the Steam overlay also hook D3D presentation in-process. Design
> for coexistence, not for an empty process — see `20_OPEN_QUESTIONS` §H5 and
> §H7, both of which are testable on such a machine today.
>
> Note also that all six of those register under **HKLM** and therefore needed
> admin. Registering under HKCU (below) is the less common choice and the better
> one: no elevation, and per-user scope.
- Intercepts `vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkCmdTraceRaysKHR`, `vkCmdBuildAccelerationStructuresKHR`, `vkCreateGraphicsPipelines`.
- The layer respects the same guard: on init it checks the enable-list written by the Agent (a small per-user config the Agent maintains) and stays fully passthrough for any process not opted in. **A layer is machine-wide by nature — this check is mandatory, not optional.**
- Registered only while at least one Vulkan game has hooking enabled; unregistered on uninstall (Velopack hook) and when the last such game is disabled.

## Ring writer (hot path)

```cpp
struct alignas(64) FlFrameRecord {   // 64 bytes exactly, static_assert'd
    uint64_t qpc;                    // @0  present entry timestamp
    uint32_t frameIndex;             // @8
    uint32_t presentFlags;           // @12
    uint16_t syncInterval;           // @16
    uint16_t renderW, renderH;       // @18 0 = unknown
    uint16_t outputW, outputH;       // @22
    uint8_t  api;                    // @26 d3d11|d3d12|vulkan|opengl
    uint8_t  upscaler;               // @27 none|dlss|dlss_rr|fsr2|fsr3|fsr4|xess|nis|unknown
    uint8_t  upscalerQuality;        // @28 vendor enum, 0xFF unknown
    uint8_t  fgMode;                 // @29 none|dlss_g|fsr_fg|xefg|unknown
    uint8_t  rtFlags;                // @30 bit0 asBuild, bit1 dispatchRays, bit2 rtPsoAlive
    uint8_t  hdr;                    // @31
    uint32_t dispatchRaysVolume;     // @32 Σ (W×H×D) over DispatchRays calls this frame
    uint16_t psoCreatedThisFrame;    // @36
    uint8_t  maxTraceRecursionDepth; // @38 from the live RT PSO config, 0 = none
    uint8_t  _pad0;                  // @39 explicit; keeps vramUsedBytes 8-aligned
    uint64_t vramUsedBytes;          // @40
    uint32_t reflexLatencyUs;        // @48 0 = unavailable
    uint32_t fgEvaluations;          // @52 FG feature evaluations observed this frame
    uint32_t seq;                    // @56 seqlock counter (see 07_IPC §Protocol rules)
    uint32_t _pad1;                  // @60 explicit
};
static_assert(sizeof(FlFrameRecord) == 64);
```

Field notes — each of these was a defect in an earlier revision:

- **`dispatchRaysVolume` is `uint32`, and it is a volume, not a call count.** A
  single 3840×2160 primary-ray dispatch is 8,294,400 rays — 126× a `uint16`'s
  range, so a counter of that width saturates on every RT title at 1080p or
  above. That is precisely the regime `03_METRICS` §RT reads: its path-tracing
  heuristic keys off "rays dispatched per output pixel ≥ ~1.0", which is
  uncomputable from a pinned counter. `rays_per_pixel` divides this by output
  pixels.
- **`maxTraceRecursionDepth`** is the second of the four stated inputs to
  `pt_confidence`. It is read at `CreateStateObject` (§Hook inventory) but
  previously had no transport to the Agent and no column to land in.
- **`_pad0` and `_pad1` are explicit.** Natural alignment would insert padding
  here anyway; naming it is required because CLAUDE.md §Struct mirroring asserts
  *every field offset* on both the C++ and C# sides, and an unnamed hole cannot
  be asserted.
- **There is no `gpuBusyUs`.** Its only possible source is GPU timestamp queries
  injected into the game's command lists, which `15_ROADMAP` defers to v2 as
  "more invasive than anything in v1". A version-locked 64-byte record must not
  reserve space for a field v1 cannot fill; the bytes now carry
  `fgEvaluations`, which v1 genuinely produces.
- **`fgEvaluations`** is what makes Native-vs-Displayed computable at Tier 1 —
  see `03_METRICS` §Frame Generation.
- **`api` no longer lists `d3d9`.** D3D9 titles are almost entirely 32-bit and
  the Overlay is x64-only; they are Tier 2 in v1 (`20_OPEN_QUESTIONS` §Scope).

- **SPSC lock-free ring**, capacity a power of two (default 8192 records = 512 KB), overwrite-oldest.
- Writer/reader seqlock protocol, including the rule that the payload write must
  **exclude** the `seq` field and that `seq` is never reset: `07_IPC` §Protocol rules.
- Header: three separate 64-byte lines before the ring — `FlShmHandshake` (write-once),
  `FlWriterState` (Overlay-written: `writeIndex`, `status`, `apiMask`, `faultCount`),
  and `FlControlBlock` (**Agent**-written: `pauseRequested`, `unhookRequested`,
  `overlayEnabled`, `agentHeartbeat`). The control flags are *not* in the Overlay's
  header — they are written by the other process, and mixing them into an
  Overlay-written line reintroduces exactly the false sharing the split exists to
  prevent. Layout is normative in `07_IPC` §A + B.
- **The hot path performs: one QPC read, a few field reads from cached state, one 60-byte store, two relaxed atomic stores and two compiler fences.** No syscall, no allocation, no lock, no logging. Target ≤ 1 µs per present.
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

`tools/hook-harness` — a minimal D3D11 / D3D12 / Vulkan / OpenGL app that presents at a controlled rate, optionally creates RT PSOs and dispatches rays, and can simulate an upscaler by calling stub exports with the same names. It lets CI and local dev exercise every hook path with **no game and no anti-cheat surface at all** (`14_TESTING`).
