# AMD FidelityFX SDK — FSR 3.0 host API headers (vendored)

Copied from <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>, tag
**`fsr3-v3.0.4`** (commit `55ff22bb6981a9b9c087b9465101769fc0acd447`). **Ten headers and the
licence file.** A second vendoring directory beside `../fidelityfx/` (tag `v2.3.0`, the ffx-api
facade), because the two tags carry two different licence *shapes* and `tools/license-check.ps1`
asserts each per file: §2d walks `fidelityfx/Kits/` against the exception list inside its
`license.md`; **§2e walks this directory's `sdk/` against the root `LICENSE.txt`**, which at this
tag is the MIT grant verbatim, with every header carrying the same grant in its own banner.

## Why this tag — and why not `v1.1.4`, which three documents named

The one installed title that ships the FSR 3.0 **host** DLLs is Cyberpunk 2077
(`ffx_fsr3_x64.dll`, `ffx_fsr3upscaler_x64.dll`, `ffx_frameinterpolation_x64.dll`,
`ffx_opticalflow_x64.dll`, `ffx_backend_dx12_x64.dll`). `docs/vendor-exports.json` records
`ffx_fsr3_x64.dll` exporting **twelve** `ffxFsr3*` names: `ContextCreate`, `ContextDestroy`,
`ContextDispatchUpscale`, `ContextGenerateReactiveMask`, `DispatchFrameGeneration`,
`ConfigureFrameGeneration`, `GetJitterOffset`, `GetJitterPhaseCount`,
`GetRenderResolutionFromQualityMode`, `GetUpscaleRatioFromQualityMode`, `ResourceIsNull`,
`SkipPresent`. Read off the upstream tags through the GitHub API on 2026-09-05:

- **`fsr3-v3.0.4`** (and `fsr3-v3.0.3`) declares exactly those twelve. Its
  `FfxFsr3DispatchUpscaleDescription` ends at `viewSpaceToMetersFactor`.
- **`v1.1.4`** declares fifteen — it adds `ContextGetGpuMemoryUsage`,
  `ContextDispatchFrameGenerationPrepare` and `GetEffectVersion`, none of which the shipped
  module exports — and its `FfxFsr3DispatchUpscaleDescription` appends `upscaleSize`, `flags`
  and `frameID` **after `renderSize`**. A detour typed against 1.1.4 would read
  `enableSharpening`/`sharpness` bytes as an output size on Cyberpunk's 3.0 module.
- The prefix through `renderSize` — `commandList`, seven `FfxResource`s, `jitterOffset`,
  `motionVectorScale`, `renderSize` — is **identical** in 3.0.3, 3.0.4 and 1.1.4, and it is the
  only field the Overlay reads. `dllmain.cpp` pins `offsetof(FfxFsr3DispatchUpscaleDescription,
  renderSize)` to a literal so a re-vendoring that moved it fails to compile rather than reading
  the wrong bytes.

So the declarations vendored are the ones the shipped module exports, and the corrections to
`docs/18_GPU_VENDOR_APIS.md`, `docs/HANDOFF.md` and `docs/20_OPEN_QUESTIONS.md` — all of which
said `v1.1.4` — landed with this directory.

## The include closure, measured — and `<mutex>` is not in it

Those same documents said the closure was "ten headers reaching `<mutex>` / `<shared_mutex>`
through `ffx_types.h`". That is true of **1.1.x**, where `ffx_types.h` includes them behind
`#ifndef FFX_MUTEX`. At `fsr3-v3.0.4` **`ffx_types.h` includes `<stdint.h>` and nothing else**,
and no file below pulls in the STL or `<windows.h>`:

```
ffx_fsr3.h
├── ffx_interface.h
│   ├── ffx_assert.h  → ffx_types.h, ffx_util.h (→ ffx_types.h)
│   ├── ffx_types.h   → <stdint.h>
│   └── ffx_error.h   → ffx_types.h
├── ffx_fsr3upscaler.h → ffx_interface.h, ../gpu/fsr3upscaler/ffx_fsr3upscaler_resources.h (defines only)
├── ffx_frameinterpolation.h → ffx_interface.h
└── ffx_opticalflow.h → ffx_interface.h
```

Ten files, no `ffx_message.h` (that file exists at 1.1.x, not here). The Overlay is built with
`_HAS_EXCEPTIONS=0` and no C++ exceptions on hook paths; a closure with no STL in it has nothing
to audit for `throw`, which is the property the Streamline closure had to be measured for.

## `FFX_API` is `__declspec(dllexport)` unconditionally

`ffx_types.h` defines `FFX_API` as `__declspec(dllexport)` on every non-GCC compiler — there is
no import switch at this tag (the `FFX_BUILD_AS_DLL` switch arrives in 1.1.x). That is the same
hazard `ffx_api.h`'s `FFX_API_ENTRY` carries and it has the same answer: a declaration exports
nothing unless something *defines* it. `FrameLedger.Overlay` includes `ffx_fsr3.h` for types and
`decltype` only and defines none of the names; `tools/hookinventory-check.ps1` Pass C reads the
built DLL's export table (forbidden pattern `^ffx[A-Z]`) to keep that true, and the
`ffx_fsr3_x64.dll` fixture in `tools/vendor-stubs` — which *does* define four of them through the
vendor's own macro — is the parser's second positive control.

## Coexistence with `../fidelityfx/` in one translation unit

Both trees are included by `dllmain.cpp` and by `hook-harness`. Measured at the first build: the
type names are disjoint (`FfxApiResource` / `FfxApiDimensions2D` there, `FfxResource` /
`FfxDimensions2D` here) **except one** — both trees define a struct named
`FfxFrameGenerationConfig` (2.3.0's `ffx_framegeneration.h`, 3.0.4's `ffx_interface.h`) — and the
one macro both define, `FFX_RESOURCE_NAME_SIZE`, is `64` in each, a legal identical redefinition.
So the consumers include this tree **inside a namespace** (`namespace fsr3host { #include
<FidelityFX/host/ffx_fsr3.h> }`): the headers stay verbatim, the entry points keep their C
linkage (a C-linkage function declared in two namespace scopes is one function), and every field
the Overlay reads is still the vendor's own declaration, spelled `fsr3host::…`.

## Unmodified, and checked rather than claimed

`git hash-object` on each vendored file equals the blob sha the GitHub tree API reports for the
same path at the tag. Upstream ships **LF**; `.gitattributes` (`* text=auto eol=crlf`) stores LF
in the index and materialises CRLF in the working tree, so `git ls-files -s` agrees with the table
below and a checked-out file differs from upstream in line endings only — the same shape as the
`../fidelityfx/` tree:

| File | Blob sha |
|---|---|
| `LICENSE.txt` | `3c5c80518fc83a2532f8445f925d46677eca7782` |
| `sdk/include/FidelityFX/host/ffx_fsr3.h` | `62bf127fcbf0625d057b2f6ff0d72cdee9716f74` |
| `sdk/include/FidelityFX/host/ffx_interface.h` | `3a7277afae65bd651d1ae8ea4a356bc122df052c` |
| `sdk/include/FidelityFX/host/ffx_fsr3upscaler.h` | `83bee5d9cdf84ed32dd974cc447599d30d62be82` |
| `sdk/include/FidelityFX/host/ffx_frameinterpolation.h` | `4a98ad05eee506fed7ed17e79dfece37a6aeca77` |
| `sdk/include/FidelityFX/host/ffx_opticalflow.h` | `f18a6ddabb9c89cb8eede03c00d92869ca1a3764` |
| `sdk/include/FidelityFX/host/ffx_assert.h` | `11718eca730e0eae6a7b9f6db5070a649bab31ef` |
| `sdk/include/FidelityFX/host/ffx_types.h` | `64494df9eff3f14d29f97e9add9d85e83e8a4c07` |
| `sdk/include/FidelityFX/host/ffx_error.h` | `d1d466fec4ba3964ce9121850a51cdc42d0aa685` |
| `sdk/include/FidelityFX/host/ffx_util.h` | `123cd1969f6307601be8d94e902fe6e5a2b0d77c` |
| `sdk/include/FidelityFX/gpu/fsr3upscaler/ffx_fsr3upscaler_resources.h` | `d98cfcc0602c7dd59ece0960fd02dbdaef0bb897` |

## What is here, and what is not

Kept in upstream's `sdk/include/FidelityFX/…` layout because the headers include each other by
`<FidelityFX/host/…>` path; the CMake target `fl_fsr3_host_headers` adds `sdk/include` as a
`SYSTEM` include directory and links nothing.

| | |
|---|---|
| **Here** | the ten-file closure of `ffx_fsr3.h` above, and `LICENSE.txt` |
| **Not here** | everything else under `sdk/` — the backends (`ffx_dx12.h`, `ffx_vk.h`), every `.cpp`, every shader, every other effect's host header, the samples and `Kits/`. Nothing is vendored ahead of a consumer (`docs/18_GPU_VENDOR_APIS.md`) |

## How it is consumed

**Types and `decltype` only, never linked.** The Overlay reads exactly one field of the argument
the title passes to one export — `FfxFsr3DispatchUpscaleDescription::renderSize` on
`ffx_fsr3_x64.dll!ffxFsr3ContextDispatchUpscale` — for the upscaler's identity (`FL_UPSCALER_FSR3`)
and render extent. Never the context, never a resource, never the command list (CLAUDE.md
rule 4). `ffxFsr3DispatchFrameGeneration` and `ffxFsr3SkipPresent` are probe-only names
(`SpeaksFsr3Host` in `fl_hook_inventory.h`): checked for existence, never resolved, never called.
