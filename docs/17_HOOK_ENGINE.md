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
- `/GS /guard:cf /Qspectre`, `/O2`, no RTTI (`/GR-`), C++ exceptions disabled in this target (`-D_HAS_EXCEPTIONS=0`); error handling is return codes + SEH. **Both of the contentious flags are verified, not assumed** (`20_OPEN_QUESTIONS` §H1/§H3, `spike-notes.md` §3): `/guard:cf` coexists with MinHook trampolines because Windows treats `VirtualAlloc`'d executable memory as a valid CFG call target, and `std::atomic_ref` is lock-free at both widths under `-D_HAS_EXCEPTIONS=0`. The `fl_hook_profile` ctest re-checks both on every build.

  > ⚠ Know what `-D_HAS_EXCEPTIONS=0` actually buys. MSVC's STL does not delete its failure paths under that define — it rewrites `_THROW(x)` to `(x)._Raise()` and `_RAISE` to `__fastfail(5)`. Inside an injected DLL that means **a would-be exception kills the host game**, uncatchably, which SEH cannot intercept. It is safe here only because the hot path uses no throwing STL at all (`<atomic>`, `<cstdint>` and `<type_traits>` have zero throw-sites). That makes "no STL containers that allocate in hook paths" a load-bearing rule, not a style preference.
  >
  > ⚠ The CFG result was measured with **strict mode off**. A host that enables CFG strict mode does not auto-validate dynamically generated code and could still `__fastfail`. Rare, but real, and not something the fault policy can catch — see §Fault policy and `20_OPEN_QUESTIONS` §H8.
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

> ⚠ **"Verified at runtime against the dummy object" is not implementable, and
> must not be reintroduced.** A vtable slot holds a bare function pointer: no
> name, no signature, nothing identifying which method it is. Reading `vtbl[8]`
> tells you an address, not that the address is `Present`. The only runtime
> checks available are structural — non-null, inside a mapped image, table long
> enough — and none of them distinguishes slot 8 from slot 9.
>
> **What validates the indices is behaviour, and it is now a test.** `hook-harness
> --probe-vtable` (ctest `fl_vtable_indices`) patches each slot, calls the
> method, and asserts the detour ran. Verified on MSVC 19.51 / Windows 11
> 26300: **slot 8 = `Present`, slot 13 = `ResizeBuffers`, slot 22 = `Present1`**.
> `IDXGISwapChain` and `IDXGISwapChain1` return the same vtable pointer, so slot
> 22 is reachable from either interface pointer.
>
> The harness runs **headless on a GPU-less CI runner** — WARP for the device
> and `CreateSwapChainForComposition` so no `HWND` or interactive window station
> is needed. That was the obstacle `20_OPEN_QUESTIONS` §H4 raised to testing
> this anywhere but a dev machine, and it is closed.
>
> Release the dummy objects immediately after reading vtables.

> **Patching a vtable entry patches every instance.** The table lives in
> `dxgi.dll`'s read-only data and is shared by all objects of that concrete
> class, so `VirtualProtect` + write affects the whole process. That is *why*
> the dummy-object technique works at all — and it is also why §H7's
> restore-only-if-unchanged rule matters, since another overlay may have
> patched the same shared slot after us.

Prefer hooking the **vtable entry** over inline-patching for COM methods (cleaner uninstall, no trampoline hazards); use MinHook inline hooks for flat C exports (`wglSwapBuffers`, NGX/FFX/XeSS entry points).

## Hook inventory

Every hook must be listed here with a purpose. Anything not on this list is not allowed to exist (`19_SAFETY` review checklist).

> **This table is a SPECIFICATION, and as of 2026-08-05 exactly three of its rows are
> built.** `FrameLedger.Overlay` installs `Present` (slot 8), `ResizeBuffers` (13) and
> `Present1` (22) via MinHook on the shared `dxgi.dll` class vtable — indices proved by
> behaviour, never hardcoded (ctest `fl_vtable_indices`). **Every other row below is
> unwritten**, including `SetFullscreenState`, `SetColorSpace1`, `CreateSwapChain*`,
> `wglSwapBuffers`, and all of §Upscaling, §Ray tracing, §Pipeline and §Memory/latency.
>
> Stated here because the distinction is invisible from the table and it is what the
> record honestly reports: a present-only writer sets `measuredMask =
> FL_MEASURED_OUTPUT_RES | FL_MEASURED_PRESENT_ARGS` and `rtFlags = 0`
> (v3: the bits are *observed*, so zero is honest), so the fields those
> unwritten rows would fill are marked *not measured* rather than defaulted to "none"
> (`fl_shm.h` §FlMeasured, CLAUDE.md rules 6 and 7).
>
> Marked ✅ per row below. When a row is built, flip it in the same PR.

### Presentation
| Hook | Purpose |
|---|---|
| ✅ `IDXGISwapChain::Present`, `Present1` | Frame boundary, QPC, sync interval, present flags. **`DXGI_PRESENT_TEST` calls are dropped without a record** — the occlusion probe submits nothing, and a minimised game emits a stream of them (`07_IPC` §Protocol rules) |
| ✅ `IDXGISwapChain::ResizeBuffers` · ⏳ `ResizeTarget` | Output resolution changes mid-session. `ResizeBuffers` re-reads the swapchain description *after* the original returns; `ResizeTarget` is not hooked |
| ⏳ `IDXGISwapChain::SetFullscreenState` | Fullscreen ↔ borderless transitions |
| ⏳ `IDXGISwapChain3::SetColorSpace1` | HDR output detection. **`IDXGISwapChain3`, not 4** — 4 adds only `SetHDRMetaData` (`20_OPEN_QUESTIONS` §H9). Unbuilt, which is why `colorSpace` reads `NOT_REPORTED` and `FL_MEASURED_HDR` stays clear. **When it is built, the writer must initialise `colorSpace = FL_COLOR_SPACE_SDR` at swapchain identification** — an SDR title never calls `SetColorSpace1`, so "hook live, no call" would otherwise sit at `NOT_REPORTED` forever and HDR's definite `No` would be unreachable. DXGI documents G22/Rec.709 as the default, which makes SDR a *measured* default rather than an affirmative negative |
| ⏳ `IDXGIFactory::CreateSwapChain*` | Capture swapchain desc (format, buffer count, swap effect, flags) at creation. Unbuilt — the swapchain description is currently read on demand in the present hook via `GetDesc` |
| ⏳ `wglSwapBuffers` | OpenGL titles. Unbuilt, and deliberately not attempted before `hook-harness` has an OpenGL mode: the hook is small (a flat export, no vtable) but shipping an unexercised hook into a game process is not something this project does. Measured 2026-08-05: `opengl32!wglSwapBuffers` is a `jmp` thunk (`E9 <rel32>`) into the vendor ICD, which is already mapped before the first call |
| ⏳ Vulkan `vkQueuePresentKHR` | via layer, not hook (below). Unbuilt — P1, and §S2's in-layer supervision check lands with it. The layer today loads, gates and self-scans, and intercepts nothing |

### Upscaling / frame generation (the accuracy problem this rewrite exists to solve)
| Hook | Yields |
|---|---|
| NGX: `NVSDK_NGX_D3D11/D3D12/VULKAN_CreateFeature`, `EvaluateFeature`, `ReleaseFeature` | Which NGX feature is *actually created and evaluated per frame*: SuperSampling (DLSS), RayReconstruction (DLSS-D), FrameGeneration (DLSS-G) |
| NGX parameter accessors (`NVSDK_NGX_Parameter_SetI/GetI/SetUI`) — **exported by `sl.common.dll` only; see below** | `Width`/`Height` (render) vs `OutWidth`/`OutHeight` (output), `PerfQualityValue` (quality preset), sharpness |
| ✅ Streamline: `slEvaluateFeature` · ⏳ `slInit`, `slSetFeatureLoaded`, `slSetConstants`, `slGetFeatureRequirements` | Feature set actually active when the game goes through SL rather than NGX directly (`kFeatureDLSS`, `DLSS_G`, `DLSS_RR`, `Reflex`, `NIS`). **Built 2026-08-09**, identity only: one MinHook detour on `sl.interposer.dll!slEvaluateFeature`, resolved **module-scoped** and installed lazily by the watchdog. Sets `FL_MEASURED_UPSCALER` + `FL_HOOK_UPSCALER_IDENTITY`; decodes `kFeatureDLSS`/`kFeatureNIS` and reports `FL_UPSCALER_UNKNOWN` for anything else. **Never `FL_UPSCALER_NONE`** — a Streamline-only writer cannot see FFX, XeSS or NGX-direct, and `NONE` is the only one of the three states that may be aggregated as a negative |
| FidelityFX: `ffxFsr2ContextCreate` / `ffxFsr3*` / unified `ffxCreateContext` (`ffx_api`) | `maxRenderSize` vs `displaySize`/`maxUpscaleSize`, FSR version, frame-interpolation context presence |
| XeSS: `xessD3D12CreateContext`, `xessD3D12Init`, `xessD3D12Execute` | `outputResolution`, `qualitySetting`, XeSS version; `xess_fg` variants for XeFG |
| NGX/SL/FFX/XeSS **FG feature evaluations per present** (`fgEvaluations`) | Native vs Displayed frame counts at Tier 1 — see `03_METRICS` §Frame Generation. This is what separates the two counts: generated frames go out through the same swapchain we hooked, so the present count alone is Displayed, not Native |

> `GetFrameStatistics().PresentCount` is **not** in this inventory and must not be
> re-added as an FG signal. It counts presents the application submitted through
> the swapchain — the same events our present hook already intercepts — so the
> difference is structurally zero, not merely unreliable. An earlier revision
> used it to claim driver-level FG (AMD AFMF) detection; that claim was wrong and
> would have read as "no frame generation" rather than as a failure.

> ⚠ **Symbol names above were vendor SDK conventions. They are now measured** — `tools/vendor-exports.ps1` resolves them against the DLLs installed titles actually ship, and the result is committed as `docs/vendor-exports.json` (34 distinct modules across 162 files on the dev machine). Resolve everything dynamically by name with a null-check + capability flag; a missing export must degrade to "unknown", never crash. **A wrong name degrades silently to `unknown`, which reads as "working, no upscaler detected" — the highest false-confidence risk in the spike, which is why this is data and not prose.**

### The NGX parameter surface splits into two hook classes — measured

This is the finding P0 item 5 existed to produce, and it changes what the feature-hook phase has to build.

| Module | Exports the parameter **accessors** (`NVSDK_NGX_Parameter_SetI/GetI/…`) | Exports the parameter-object **factories** (`AllocateParameters`, `GetCapabilityParameters`, `GetParameters`) |
|---|---|---|
| `sl.common.dll` (Streamline) | **yes** — all 16 | yes |
| `nvngx.dll` / driver-store `_nvngx.dll` (NGX core) | **no** | yes, for D3D11/D3D12/Vulkan/CUDA |
| `nvngx_dlss.dll`, `nvngx_dlssg.dll`, `nvngx_dlssd.dll` | no | no |

So:

- **Streamline-shimmed titles** (9 of the installed titles here) expose the accessors as ordinary exported functions on `sl.common.dll`. An inline hook on `NVSDK_NGX_Parameter_SetUI` yields `PerfQualityValue` and the render/output pair directly. This is the case §Hook inventory already describes.

  > ⛔ **AND IT IS BLOCKED ON LICENCE GROUNDS — measured 2026-08-09, and this
  > document recommended it for months without noticing.** Hooking
  > `NVSDK_NGX_Parameter_SetUI` needs NGX declarations: the `NVSDK_NGX_Parameter`
  > handle, the parameter name strings, the `PerfQuality` enum. The NGX/DLSS SDK
  > ships under the **NVIDIA RTX SDKs License**, which hits three of
  > `18_GPU_VENDOR_APIS` §Checklist step 2's four needles verbatim — *"defend,
  > **indemnify** and hold harmless"*, *"may not **reverse engineer**, decompile or
  > disassemble"*, *"will **terminate** automatically without notice"*. Step 3
  > therefore binds: **do not vendor it, and do not work around it by re-declaring
  > the API.**
  >
  > **The trap is that the symbol lives in a Streamline module.** `sl.common.dll`
  > is Streamline's, Streamline is MIT, and neither fact relicenses the NGX API
  > those exports implement — upstream keeps it in `external/ngx-sdk/` under the
  > RTX licence.
  >
  > So `FL_MEASURED_UPSCALER_PARAMS` has **no in-policy producer on this route**.
  > The licence-clean alternative is Streamline's own MIT surface — `slSetTag`'s
  > resource extents (`kBufferTypeScalingInputColor` / `…OutputColor` give
  > render → output directly) and the DLSS options struct reached through
  > `slGetFeatureFunction`. That is the params PR's job, and it needs `sl_dlss.h`
  > vendored alongside the nine headers already taken.
  >
  > Reversing this is an owner decision about the checklist, not a coding task.
- **NGX-direct titles** call the core, which exports only the **factories**. The accessors are function pointers *inside* the `NVSDK_NGX_Parameter` object those factories return — so there is no symbol to hook. Reaching them means hooking `NVSDK_NGX_D3D12_AllocateParameters` / `GetCapabilityParameters` and reading or wrapping the returned object's function-pointer table.

**The second path needs its own justification against CLAUDE.md rule 4 before it is built, and it survives one.** Rule 4 permits "arguments passed to APIs we hooked and COM/handle objects we legitimately own". The parameter object is a handle returned by an API we hooked, in the same sense as an `IDXGISwapChain` — reading its function-pointer table is the same operation as reading a COM vtable, which §Getting vtable addresses already does. What it is **not** is pattern-scanning for game internals, and the distinction must stay in writing.

> `nvngx_dlss.dll` itself is a dead end for this purpose and should not be hooked for it: 59 exports, none of them parameter-related. It is the feature payload, not the API surface.

> ~~**Not built, and not gated.**~~ **Gated 2026-08-09.** `tools/hookinventory-check.ps1`
> checks every symbol the Overlay resolves by name against `vendor-exports.json`,
> **module-scoped** — "does *this* module export *this* symbol", never "does
> anything export it" — and runs in `build.ps1` in both halves (`-SelfTest` and a
> live pass). It is **prevention and it fixed nothing**: no drift existed when it
> was written, and saying so matters, because a gate whose write-up implies it
> caught something cannot be audited later.
>
> Five red cases are proven and restored: a misspelt symbol, the right symbol from
> the wrong module, an emptied inventory, a stray vendor literal in an Overlay
> source (a second resolver written outside the table), and a broken oracle
> lookup. The last is the important one — every failure mode of a lookup like this
> produces the same answer as "absent", so it proves the oracle discriminates
> *before* forming any verdict.
>
> A misspelt symbol is caught **earlier still**, by the compile-time binding
> between the inventory and the stub fixtures: `static_assert(InventoryHas(...))`
> makes it a build error, so the typo never reaches the gate. Stronger, and not
> the script's credit to take.
>
> **A THIRD PASS landed 2026-08-14, and it covers the failure the other two
> structurally cannot see.** Passes A and B are source checks: they see what the
> Overlay *resolves*. Neither sees what it *links*. Taking the address of an
> `SL_API` declaration in evaluated code makes `sl.interposer.dll` a **load-time
> dependency**, and the Overlay then fails to load in every game that ships no
> Streamline — inside the loader, before `DllMain`, with no message anywhere and
> nothing in the ring to explain it. **Pass C reads the built binary's own
> dependency list** (`dumpbin /dependents`) and fails on any module matching
> `^(sl\.|_?nvngx|libxess|ffx_|amd_fidelityfx)`.
>
> Two properties worth stating because both were mistakes first. It **refuses
> rather than passes** when it cannot look — a zero-length import list, a missing
> binary under `-RequireBinaries`, or an absent `dumpbin` are all failures, and
> the list must contain `kernel32.dll` before any verdict is formed, for the same
> discrimination reason as the oracle probe above. And it runs **only** under
> `-RequireBinaries`, because without it any binary in the build tree is stale by
> definition, and a gate reporting on the wrong artefact is worse than one that
> says it did not look.
>
> **`third_party/streamline/README.md` asserted this pass existed for five days
> before it did**, which is why it is described here rather than only there.

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
- **`enable_environment` (`FRAMELEDGER_ENABLE_VK_LAYER=1`)**, plus
  `disable_environment` so a user can force us off — the latter is Vulkan
  convention and stays. Measured against loader 1.4.357 (`spike-notes.md` §2):
  with the variable unset the loader locates our manifest and **never maps the
  DLL**, and it compares the variable's **value**, so a stray
  `FRAMELEDGER_ENABLE_VK_LAYER=0` does not enable us. **This makes Vulkan
  Tier 1 launch-mode-only** — the Agent sets the variable when it starts an
  opted-in game, so a Vulkan title launched from Steam or GOG is not hooked.
  It is a *loading* gate, not a security gate: anything running as the user can
  set the variable.

### Never decline to load — accept, then be inert

> **Measured, and it would have been catastrophic.** Returning
> `VK_ERROR_INITIALIZATION_FAILED` from
> `vkNegotiateLoaderLayerInterfaceVersion` — the obvious way to say "this
> process did not opt in, skip me" — does **not** make loader 1.4.357 skip the
> layer. It **access-violates the application** (`spike-notes.md` §2,
> reproduced every time). Every Vulkan application on the machine outside our
> enable-list would have crashed: a far larger blast radius than the one we
> were reducing, inflicted on programs with nothing to do with FrameLedger.

So the layer **always accepts negotiation and always forwards**. Returning
`nullptr` from `vkGetInstanceProcAddr` as a way to opt out is the same class of
failure and is equally forbidden — handing the loader a null `vkCreateInstance`
breaks the application just as thoroughly.

The enable-list decides **what we intercept**, never **whether we load**. Being
present and inert costs a pointer forward per call; being absent by erroring out
is not something this loader supports.

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
- The layer respects the same guard, in two steps at init: it checks the enable-list, and it scans **its own process** against the anti-cheat blocklist. Either one saying no means fully passthrough. **A layer is machine-wide by nature — these checks are mandatory, not optional.**
- The self-scan uses the **same matcher and the same rules file as the injection guard** (`fl_ac_rules.h`, compiled into both targets). Not a copy: a layer with its own blocklist would be a second matcher that can disagree with the first. Every uncertainty — rules unreadable, malformed, enumeration failed, a truncated list — resolves to passthrough, which is the opposite polarity from the injection guard and the same principle: leave the host alone.
- Registered only while at least one Vulkan game has hooking enabled; unregistered on uninstall (Velopack hook) and when the last such game is disabled. **Never at install time** — `12_BUILD` §The Vulkan layer is not registered at install time.

### The enable-list

Referenced everywhere, specified nowhere until now (`20_OPEN_QUESTIONS` §S4).
It is read inside a process we do not own, by code that runs before we know
anything, so its failure modes matter more than its format.

| | |
|---|---|
| **Location** | `%LOCALAPPDATA%\FrameLedger\vklayer\enabled.txt` — per-user, no admin, same trust boundary as the rules copy |
| **Format** | UTF-8, LF, one lowercased process image name per line (`witchfire.exe`), `#` comments, blank lines ignored |
| **Bounds** | ≤ 64 KiB and ≤ 1024 entries. A file larger than that is treated as corrupt |
| **Matching** | Exact, case-insensitive, on the image name only — never a path, never a prefix, never a substring |
| **Sole writer** | The Agent. The layer only ever reads it |
| **ACL** | Inherited from `%LOCALAPPDATA%`: the current user's SID. **The Agent must not widen it** |

**Every failure is passthrough.** File missing, unreadable, oversized, malformed,
or the current process simply absent from it ⇒ the layer does nothing and
forwards. That is the opposite direction from the injection guard, and
deliberately so: here "do nothing" *is* the safe outcome, because the risk being
managed is our code running somewhere it was not invited.

> **The ACL is the whole mechanism, and it is not a strong one.** Anything
> running as the user can add a line to this file, exactly as anything running
> as the user could once redirect the rules path (§S3). The difference is what
> it buys an attacker: a line here causes FrameLedger to *observe* a Vulkan
> process it would otherwise ignore. It grants no injection — the Vulkan path
> has no injection — and it cannot disable the blocklist scan the layer runs on
> itself. Treat it as reducing blast radius, not as authorisation.

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
    uint8_t  upscaler;               // @27 0 = NOT_REPORTED; dlss|fsr2..4|xess|nis|none(8)|unknown(0xFF)
    uint8_t  upscalerQuality;        // @28 vendor enum; 0xFF = a hook ran and could not tell
    uint8_t  fgMode;                 // @29 0 = NOT_REPORTED; dlss_g|fsr_fg|xefg|none(4)|unknown(0xFF)
    uint8_t  rtFlags;                // @30 bit0 asBuildOBSERVED, bit1 dispatchOBSERVED, bit2 psoCreatedEver
    uint8_t  colorSpace;             // @31 0 = NOT_REPORTED, 1 SDR, 2 HDR10, 3 scRGB
    uint32_t dispatchRaysVolume;     // @32 Σ (W×H×D) over DispatchRays calls this frame
    uint16_t psoCreatedThisFrame;    // @36
    uint8_t  maxTraceRecursionDepth; // @38 from the live RT PSO config, 0 = none
    uint8_t  featureFlags;           // @39 FlFeatureFlags: facts + their OBSERVED companions
    uint16_t measuredMask;           // @40 FlMeasured, 16-bit since v3
    uint8_t  upscalerSharpness;      // @42 percent; 0xFF = the API reports none
    uint8_t  fgEvaluations;          // @43 FG feature evaluations observed this frame
    uint32_t vramUsedMb;             // @44 MiB, matching vramBudgetMb
    uint32_t reflexLatencyUs;        // @48 0 = unavailable
    uint32_t reserved;               // @52 must be zero
    uint32_t seq;                    // @56 seqlock counter (see 07_IPC §Protocol rules)
    uint32_t swapchainId;            // @60 which swapchain this present came through; 0 = unidentified
};
static_assert(sizeof(FlFrameRecord) == 64);
```

> **Layout version 3, 2026-08-05 — and the reason for the whole revision is the
> zero value.** `FlFrameRecord rec{}` zero-initialises, so whatever 0 means is
> what a writer publishes when it FORGETS. In v2, 0 meant `FL_UPSCALER_NONE`,
> `FL_FG_NONE` and an `rtFlags` with no evidence bits — three measured negatives
> about a title nobody had examined. `measuredMask` made that safe by CONVENTION;
> v3 makes it safe by CONSTRUCTION. Every enum's 0 is now `NOT_REPORTED`, and
> `rtFlags`' polarity is flipped so its bits mean *observed*.
>
> Four answers items 4/6/7 owe had no home and now do: DLSS-SR **and** Ray
> Reconstruction concurrently (`featureFlags`, since RR was a mutually exclusive
> `upscaler` value), `upscalerSharpness`, and — in `FlWriterState`, because they
> are session facts and not per-frame — the device **RT tier** without which
> `03_METRICS`' definite RT `No` had no producer at all, plus `rtStateObjectsCreated`
> and `rasterPsoCreated`.
>
> Paid for by narrowing `vramUsedBytes` (u64 bytes → u32 MiB, matching the
> `vramBudgetMb` it is compared against and the `vram_mb` every consumer exports)
> and `fgEvaluations` (u32 → u8; ×4 multi-frame generation is 3). `seq` @56 and
> `swapchainId` @60 did not move, so `fl_ring.h`'s two pins and the seqlock's
> payload spans are untouched.

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
- **`_pad0` and `_pad1` were explicit holes and are now carrying data.** Both were
  named only so every offset could be asserted; spending them costs nothing,
  because both bytes ranges were already inside the record and already written
  every frame. Neither needs a `FL_SHM_LAYOUT_VERSION` bump — and both had to be
  decided BEFORE a writer or a C# mirror exists, because after that the same
  change is user-visible: the Agent refuses to attach and tells the user to
  restart the game.
  - **`measuredMask` (@39)** distinguishes *"we looked and there was none"* from
    *"we did not look"*. The zero-defaults are affirmative negatives —
    `FL_UPSCALER_NONE`, `FL_FG_NONE`, `rtFlags = 0` — so a present-only writer
    with no feature hooks would otherwise assert "no upscaler, no FG, no ray
    tracing" as measured fact, 118 times a second, on the exact title chosen to
    prove ADR-7. `03_METRICS` would then produce `fg_factor 1.0`, the single
    inflated number CLAUDE.md rule 6 forbids, and map a whole session to a
    definite RT `No`, which rule 7 forbids.
  - **`swapchainId` (@60)** says which swapchain the present came through.
    Patching a vtable slot patches the SHARED `dxgi.dll` class vtable — measured
    across five configurations, D3D11 and D3D12, WARP and hardware, composition
    and HWND, all identical — so one hook sees every swapchain in the process. A
    title with a separate UI or video swapchain inflates `F_disp`, and
    `03_METRICS` has no way to tell the streams apart without this.
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
  `overlayEnabled`, `guardTicks`). The control flags are *not* in the Overlay's
  header — they are written by the other process, and mixing them into an
  Overlay-written line reintroduces exactly the false sharing the split exists to
  prevent. Layout is normative in `07_IPC` §A + B.
- **The hot path performs: one QPC read, a few field reads from cached state, one 60-byte store, two relaxed atomic stores and two compiler fences.** No syscall, no allocation, no lock, no logging. Target ≤ 1 µs per present.
- Per-frame mutable state (current upscaler, render res, dispatch counts) lives in a small struct updated by the feature hooks and *read* by the present hook; counters reset at present.

## The watchdog thread — the only thread we add to the game

One thread, started on the init thread *after* the hooks are installed, sleeping
`1000 ms` at a time. It evaluates `unhookRequested` and the `guardTicks` deadline,
calls `StopObserving` when either fires, and **exits** once stopped.

**Why it exists.** Both stops used to be evaluated only on the present path, and
`MayObserve` is reachable only from `RecordPresent`, which is reachable only from
the two present hooks. In a process that had stopped presenting — hung,
alt-tabbed, sitting in a menu — neither check ever ran and the hooks stayed
patched in for the life of the process. `fl_shm.h` says over
`FL_GUARD_TICK_DEADLINE_MS`, in capitals, that the deadline must **not** be driven
by the present hook, *"because the clock would stop when presents stop, which is
the exact scenario this exists for"*. The code did what its own normative comment
forbade. Measured: `unhookRequested` set on a `hook-harness --hold` target left
`status` at `READY` indefinitely.

**It supplements the present path, it does not replace it.** `07_IPC` requires the
safety stop within one frame, and a one-second watchdog cannot promise that, so
the present path keeps its `unhookRequested` check. Same shape as §S6's
`LoadLibrary` hook supplementing the 30 s poll.

**Why a thread here, when `20_OPEN_QUESTIONS` §S2 rejected one for the Vulkan
layer.** All three of §S2's reasons are properties of the *layer*: the Vulkan
loader owns the layer's mapping, so a thread outliving it access-violates a host
we do not own; the re-scan it would have run allocates ~1.15 MB transiently; and
it would have called `NtQuerySystemInformation` and probed the SCM from inside a
game, which is the behavioural signature of anti-analysis code (CLAUDE.md rule 3).
None applies to the Overlay, which is loaded by documented `LoadLibraryW` and is
never `FreeLibrary`'d from a live process. **This thread enumerates nothing,
probes nothing and allocates nothing** — it reads two `uint32`s from our own
mapping and sleeps. That distinction is the whole justification and must survive
any future change: a watchdog that starts scanning is a different object under
rule 3.

Failing to create it is **not** fatal — the present-path checks still work, and an
Overlay that reacts only while presenting is strictly better than none. The
failure is recorded in `faultCount` so the Agent can see it rather than infer it.

`legal/DISCLAIMER.md` §2 discloses this thread to the user in plain language,
because "what runs inside the game" is exactly what that document exists to state.

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

`FlRequestUnhook()` (or the control flag) ⇒ `MH_DisableHook(MH_ALL_HOOKS)`, restore vtable entries **only where they are still ours**, flush the ring, set status. **The DLL is not `FreeLibrary`'d from the live process** — a thread could still be inside a trampoline. It goes dormant and unloads with the process. This is deliberate and documented.

### Compare-and-restore, never unconditional restore

This section used to call vtable swapping a "cleaner uninstall" than inline
patching. **In the multi-overlay case that is backwards** (`20_OPEN_QUESTIONS`
§H7), and the multi-overlay case is the normal state of a gamer's machine — the
dev box alone has RTSS, OBS, Steam Overlay, Steam Fossilize, EOS and GOG Galaxy
resident (`spike-notes.md` §Environment).

If another overlay hooked the same slot *after* us, it saved **our detour** as
its original and chains through it. Writing the pristine address back then
removes their hook silently: their overlay stops working, with no error
anywhere, and we caused it.

So the rule is **compare-and-restore**:

```
if (slot != our_detour) -> leave it alone, go dormant
else                    -> restore the original
```

Re-check the comparison *under* the `VirtualProtect` write window, not only
before it. And when the slot has changed, going dormant costs nothing: we stay
loaded anyway, and our detour remains in someone else's chain doing nothing
harmful.

Verified by `hook-harness --probe-unhook` (ctest `fl_unhook_preserves_foreign`),
which simulates the foreign hooker rather than depending on RTSS being
installed, so it is deterministic and runs on CI. Both halves are asserted: we
decline when the slot changed, **and** we do restore when it did not — a
compare-and-restore that never restores is not a fix.

## Native logging

No logging in hook bodies. A small fixed-size in-memory ring of structured events (hook installed, symbol missing, fault, unhook) is flushed to `logs\overlay-<pid>-*.log` by the init thread at session end, on unhook, or on Agent request — never mid-frame.

## Test harness

`src/native/tools/hook-harness` — a minimal D3D11 app that presents at a controlled rate. It lets CI and local dev exercise hook paths with **no game and no anti-cheat surface at all** (`14_TESTING`).

Two choices make it run on a hosted CI runner, which is what made vtable-index
verification a dev-machine-only affair before:

- **WARP** (`D3D_DRIVER_TYPE_WARP`) — a software rasteriser, so no adapter is required.
- **`CreateSwapChainForComposition`** — no `HWND`, so no dependency on a window station or an interactive session.

Current modes: `--probe-vtable` (§H4, ctest `fl_vtable_indices`), `--probe-proxy` (§H5, ctest `fl_proxy_swapchain`), `--probe-unhook` (§H7, ctest `fl_unhook_preserves_foreign`), `--probe-cost` (NFR-1, measurement only), **`--probe-frames`** (ctest `fl_frame_identity` — what counts as a frame, against `GetLastPresentCount`), **`--probe-d3d12`** (ctest `fl_d3d12_acquisition` — device → command queue → swapchain), `--present N`, `--hold N`, **`--real`** and **`--plus-ui K``**.

> **Every present here used to carry `DXGI_PRESENT_TEST`, which submits nothing.** Measured: 500 of them leave `GetLastPresentCount` at 0 while 37 real presents move it by 37. "N presents → N records" was therefore satisfiable only by a writer counting non-frames. `--real` issues real presents; the test-present path is kept as a named mode because it is what an alt-tabbed title actually runs.

Still to add as the hook layer grows: Vulkan devices, RT PSO creation and ray dispatch, stub upscaler exports with the real vendor names, a PSO-compile spike generator, and a fault-injecting hook body for the self-disable path (`14_TESTING` §Integration tests).
