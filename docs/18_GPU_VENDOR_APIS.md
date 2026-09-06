# 18 — GPU telemetry

Telemetry is layered so that **no layer's licence can hold the project hostage**, and so that a missing vendor library degrades a few fields instead of the whole feature.

| Layer | Source | Licence | Covers |
|---|---|---|---|
| **L1 — baseline** | DXGI + Windows performance counters (PDH) | none needed (OS APIs) | GPU utilisation, adapter VRAM, per-process VRAM, driver/adapter identity — **all vendors** |
| **L2 — sensors** | LibreHardwareMonitorLib | **MPL-2.0** (GPL-compatible) | Temperatures, clocks, power, fan — **AMD + Intel + NVIDIA**, plus CPU/board when elevated |
| **L3 — NVIDIA extras** | NVAPI, headers vendored | **MIT** | Throttle reasons, per-domain utilisation, **Reflex / PC latency** — things no other layer exposes |

L1 always works. L2 fills in what L1 cannot see. L3 adds NVIDIA-only depth. **No AMD or Intel vendor SDK is used or bundled** — see §Vendor SDKs we deliberately do not use.

## Abstraction

```csharp
public interface IGpuTelemetrySource : IDisposable
{
    TelemetryLayer Layer { get; }           // L1 / L2 / L3, for the composite descriptor
    GpuCapabilities Capabilities { get; }   // which fields are real on this machine
    bool TryRead(out GpuSample? sample);    // 1 Hz, called by the Agent; false = nothing yet, or disabled
}
```

> **Written 2026-09-03 as `FrameLedger.Application.Telemetry`**, with two additions to what
> this block used to show: `Layer`, so the composite can record which layer supplied a
> value without reflecting on the type, and `GpuSample.AdapterName`, so a report on a
> multi-adapter machine can say which one a sample describes. `Capabilities` is
> **value-derived and monotonic**: a bit is set the first time its field carries a value
> and is never cleared by a later null tick; a sensor that exists in the tree and never
> reports sets nothing. That is the one direction a source must never get wrong — a
> capability bit over a null field is a zero presented as a measurement.

Implementations compose rather than compete:

- `BaselineTelemetrySource` (L1) — always constructed.
- `LhmTelemetrySource` (L2) — constructed when LHM initialises.
- `NvapiTelemetrySource` (L3) — constructed only on NVIDIA hardware.
- `CompositeTelemetrySource` merges them with a fixed precedence per field (**L3 > L2 > L1**) and records which layer supplied each value.

`GpuSample`: `takenAt`, `layer`, `adapterName`, `tempCoreC`, `tempHotspotC`, `tempMemoryC`, `loadPct`, `vramAdapterMB`, `coreClockMhz`, `memClockMhz`, `powerW`, `fanRpm`, `throttleReasons`, `pcieGen/Width`. Every field nullable — `Capabilities` says what to trust, and the UI shows `N/A` rather than zero.

`sessions.telemetry_source` stores the **composite descriptor** (e.g. `l1+lhm+nvapi`), not a single name, so a user can see exactly why a field is missing.

> Per-process VRAM comes from the **Overlay** (`IDXGIAdapter3::QueryVideoMemoryInfo` inside the game), not from here. Adapter-wide VRAM is a separate series with a different label — they answer different questions and users will otherwise think one of them is broken.

## L1 — baseline (no licence, all vendors)

- **DXGI:** `IDXGIFactory6::EnumAdapterByGpuPreference` → adapter description, LUID (matches the swapchain adapter from the Overlay handshake), dedicated/shared memory sizes. `IDXGIAdapter3::QueryVideoMemoryInfo` for adapter-wide usage/budget.
- **PDH performance counters** — the same source Task Manager uses, fully documented, vendor-neutral:
  - `\GPU Engine(*)\Utilization Percentage` (sum per engine type: 3D, Compute, Copy, VideoDecode)
  - `\GPU Adapter Memory(*)\Dedicated Usage`
  - `\GPU Process Memory(*)\Dedicated Usage` (cross-check against the Overlay's figure)
  - Instance names embed the LUID — parse and filter to our adapter, don't sum blindly across GPUs.
- **Driver version:** `SetupAPI` / registry adapter properties. Feeds the hardware snapshot and the trend-chart change markers (`06_DATA_MODEL`).
- **Optional probe, P0 evaluation only:** `D3DKMTQueryAdapterInfo` with `KMTQAITYPE_ADAPTERPERFDATA` reportedly exposes temperature, power and fan for any vendor. The D3DKMT structures are **not fully documented and have changed across Windows builds**. Treat as an experiment: if it proves stable on both Win 10 22H2 and Win 11 during P0, keep it as an L1 extra behind a capability flag; if it looks fragile, drop it and let L2 handle temperatures. Never let it be load-bearing.

  > **🅓 The two-OS requirement is deferred to Win 11 only — owner decision, 2026-08-05.**
  > There is one machine and it is Windows 11 (`spike-notes.md` §Environment). Win 10 22H2
  > is a **shipped configuration** — CLAUDE.md pins `SupportedOSPlatformVersion` to
  > `10.0.19045.0` and the owner confirmed that floor **stays** — so this is not the same
  > kind of gap as AMD/Intel, where the hardware simply does not exist to be tested on. It
  > is a gap that could be closed with a VM and is being left open on purpose.
  >
  > **What makes that acceptable is the last sentence above, and it is now enforced rather
  > than hoped for:** the probe must never be load-bearing. Concretely — every field it
  > would fill (core temp, power, fan) is also reachable through **L2**, and
  > `CompositeTelemetrySource`'s precedence must treat a D3DKMT value as an *extra* behind
  > a capability flag, never as the only source. If that ever stops being true, this
  > deferral stops being valid and the Win 10 measurement becomes a release blocker.
  >
  > **Recorded because it was previously invisible.** This requirement was written in two
  > documents, satisfiable on neither machine anyone has, and — unlike §R5/§R6 — carried
  > **no written deferral at all**, which is the state P0 exit criterion 2 exists to
  > forbid.

L1 gives no temperatures on its own (unless the D3DKMT probe pans out). That is what L2 is for.

## L2 — LibreHardwareMonitorLib (MPL-2.0, all vendors)

`LibreHardwareMonitorLib` ≥ 0.9.6, consumed as an **unmodified NuGet package**. It already implements per-vendor GPU sensor access internally — which means the vendor-interop licensing problem is one LHM has solved upstream, under a licence that works for us. That is the entire reason this layer exists.

- `Computer` opened with `IsGpuEnabled` always; `IsCpuEnabled` + `IsMemoryEnabled` only when the Agent is elevated and PawnIO is available.
- Poll on a dedicated thread: `computer.Accept(updateVisitor)` then read mapped sensors. Never faster than 500 ms; default 1000 ms.
- Sensor mapping by `SensorType` + name heuristics per vendor (`GPU Core`, `GPU Hot Spot`, `GPU Memory`, `GPU Package Power`, …), kept in `SensorMap.cs` with unit tests against captured sensor-tree fixtures.
- **P0 verification items:** (a) which fields LHM actually returns per vendor on real hardware — fill the capability matrix below — **NVIDIA filled 2026-09-03, AMD/Intel deferred (§R5/§R6)**; (b) ~~whether GPU-only usage works **without** elevation and without PawnIO (expected yes, since GPU sensors go through user-mode vendor paths, but confirm — it determines whether the default unelevated Agent has temperatures at all)~~ — **confirmed 2026-09-03, §M5 row R1**: eight fields unelevated, the same eight elevated, PawnIO never opened; (c) ~~confirm LHM's sources are not marked with MPL-2.0 Exhibit B~~ — **done, 2026-08-02: clear.** No source file applies the notice; the only repository hit is the `LICENSE` template itself, and every file we depend on carries the permissive Exhibit A. Method and evidence in `docs/spike-notes.md` §0. Re-check on every version bump, and check *source headers*, never the LICENSE file — MPL-2.0's own text contains Exhibit B as a template, so grepping the licence finds it in every MPL project ever published.

CPU and motherboard sensors remain LHM-only, elevated-only, PawnIO-dependent, and optional.

## L3 — NVAPI (MIT, NVIDIA only)

**Integration: vendored headers + import library, linked normally. ✅ Vendored 2026-08-05** at `src/native/third_party/nvapi/` — nine headers (`nvapi.h`'s include closure plus `nvapi_interface.h`), `License.txt`, and `amd64/nvapi64.lib`; **x64 only**, `x86/nvapi.lib` deliberately not taken. Consumed through the `fl_nvapi` INTERFACE target. NVIDIA publishes the NVAPI SDK — headers, interface definitions, and `nvapi64.lib` — under the **MIT licence** (verified 2026-08-02 and re-verified on the vendored copy: `amd64/nvapi64.lib` is a tracked file in the MIT repo and `License.txt` names the import libraries as the subject of the grant, so the binary is covered and not only the headers), so the SDK material can live in our GPL-3.0 repository with only the usual MIT attribution obligation. The runtime implementation lives in `nvapi64.dll`, which ships with the installed NVIDIA driver.

> **This section claimed the vendoring in the present tense while it had not happened**, and CLAUDE.md's pinned stack said the same — `src/native/third_party/` held `vulkan-headers` and nothing else. `legal/THIRD_PARTY_NOTICES.md` had already caught the identical claim about *itself* and gained a bidirectional licence gate for it; nothing propagated here. That gate is what turned the vendoring into a two-step with a free canary: adding the material while the notice still read "Not yet vendored" **failed the build**, in the present-but-marked-absent direction that was structurally invisible before the check was made bidirectional.

> This supersedes the earlier design's dynamic-loading-by-ordinal approach. That was a workaround for a licensing constraint that no longer exists; ordinal resolution is fragile across driver versions and should not be reintroduced. Vendored headers are simpler, type-safe, and maintainable.

Still resolve **entry points defensively at runtime**: `nvapi64.dll` may be absent (no NVIDIA GPU) or older than our headers. `NvAPI_Initialize` failing is a normal condition that must disable L3 cleanly, not throw.

| Purpose | Function |
|---|---|
| Init / enumerate | `NvAPI_Initialize`, `NvAPI_EnumPhysicalGPUs`, `NvAPI_GPU_GetFullName` |
| Temperatures | `NvAPI_GPU_GetThermalSettings` (+ extended thermal query for hotspot/memory where available) |
| Utilisation | `NvAPI_GPU_GetDynamicPstatesInfoEx` (GPU / FB / VID / BUS domains) |
| Clocks | `NvAPI_GPU_GetAllClockFrequencies` |
| Memory | `NvAPI_GPU_GetMemoryInfoEx` — **not `NvAPI_GPU_GetMemoryInfo`**, which this table named until 2026-08-05. The vendored headers mark it `__nvapi_deprecated_function` ("deprecated in release 520"), so under `/W4 /WX` a call to it fails the native build |
| **Throttle reasons** | `NvAPI_GPU_GetPerfDecreaseInfo` — not available from L1 or L2 |
| Driver version | `NvAPI_SYS_GetDriverAndBranchVersion` |
| **NGX per-process state** | `NvAPI_NGX_GetNGXOverrideState` (R570+) — the driver's feedback word per DLSS feature for a PID, read OUT OF PROCESS by `fl-probe-nvapi --ngx-state` and, since 2026-09-06, by the capture host beside each module snapshot. Measured: the SR word's `CREATED \| EVALUATE` is set without an NVIDIA-app override; the FG word follows DLSS-G once created (`CREATED \| EVALUATE` at ×4, corrected the same day — the first reading was blind because it was early), the multiplier bits stay clear; `scalingRatio` is 0 even under an override. Identity only, SR and FG (`03_METRICS` §Upscaling) |
| **Reflex / PC latency** | `NvAPI_D3D_GetLatency` → per-frame sim/render/present/driver/GPU timestamps; hooking `NvAPI_D3D_SetSleepMode` / `SetLatencyMarker` in the Overlay tells us the game enabled Reflex (`17_HOOK_ENGINE`) |

Reflex latency is the single strongest reason L3 exists — it is genuinely unavailable anywhere else.

⚠ Any function not present in the public headers is an **optional extra**: feature-flagged, guarded, never load-bearing. If a call misbehaves twice, disable L3's affected field for the session rather than retrying in a loop.

## Vendor SDKs we deliberately do not use

Recorded so nobody re-adds them later without redoing the analysis.

**Intel Graphics Control Library (IGCL)** — *rejected, licence incompatible.* Distributed under the Intel Software License Agreement, not an open-source licence. Three clauses conflict with GPL-3.0 if the headers were vendored into this repository:

1. Use and redistribution are permitted **"solely for use on Intel platforms"** — a field-of-use restriction. GPL-3.0 §10 forbids imposing further restrictions on downstream recipients, and §7's list of permitted additional terms does not cover this.
2. A **broad indemnification** obligation covering any claims arising from use. GPL-3.0 §7(f) permits indemnity terms only in a narrow form tied to contractual assumptions of liability by the conveyor.
3. An **additional termination trigger** on breach, plus a no-reverse-engineering clause on binary components — both extend beyond GPL's own termination and study rights.

Writing our own IGCL declarations to avoid vendoring the headers is **not an approved workaround**: struct layouts must match byte-for-byte, so an "independent" implementation is indistinguishable from a copy, and the legal position is unsettled. Intel GPU telemetry comes from L1 + L2 instead.

**AMD ADLX / ADL** — *not used.* Not evaluated in depth because L2 already covers AMD sensors under MPL-2.0, so there is no need to take on another vendor licence. If someone later proposes adding it, it must pass the checklist below first, and the proposal must explain what it delivers that L2 does not.

**NVIDIA NGX / Streamline, AMD FidelityFX, Intel XeSS runtimes** — not loaded and not redistributed by us at all. We observe the calls the *game* makes to the copies it ships (`17_HOOK_ENGINE`). No vendor code is distributed by FrameLedger.

**AMD FidelityFX SDK headers** — *checklist run 2026-09-04, CLEAR; **vendored the same day at
tag `v2.3.0`**, five headers, types only (`src/native/third_party/fidelityfx/README.md`).*
Read off the upstream repository through the GitHub API, not from memory:

- **Tag `v1.1.4`** (the last 1.x): the root `LICENSE.txt` is the MIT grant verbatim, and every
  header of interest carries the same grant inline — the `ffx-api/include/ffx_api/` set and the
  FSR 3.0 host API under `sdk/include/FidelityFX/host/`. None of step 2's needles appear. **This
  paragraph said "vendor from this tag", and its own reversal condition fired the same day** —
  see the third bullet. ~~The host API is still only here: `main` dropped `ffx_fsr3.h`, so the FSR
  3.0 route (Cyberpunk's `ffx_fsr3_x64.dll`, named exports) vendors from `v1.1.4` when it is built,
  and its closure is **ten** files, not the nine first listed — `ffx_interface.h` includes
  `ffx_message.h` — with `<mutex>`/`<shared_mutex>` reached through `ffx_types.h`.~~ **Wrong tag,
  corrected 2026-09-05 when the route was built.** Cyberpunk's `ffx_fsr3_x64.dll` exports twelve
  `ffxFsr3*` names (`vendor-exports.json`), which is exactly tag **`fsr3-v3.0.4`**'s declaration
  list; `v1.1.4` declares fifteen and appends `upscaleSize`, `flags` and `frameID` to
  `FfxFsr3DispatchUpscaleDescription` *after* `renderSize` — a detour typed against it would read
  the wrong bytes off the module the title ships. At `fsr3-v3.0.4` the root `LICENSE.txt` is MIT
  verbatim, every header carries the grant inline, and **`ffx_types.h` includes `<stdint.h>` and
  nothing else** — the `<mutex>` block exists only from 1.1.x behind `#ifndef FFX_MUTEX`. The
  closure is ten files with **no `ffx_message.h`** (nine host headers plus
  `gpu/fsr3upscaler/ffx_fsr3upscaler_resources.h`). Vendored at
  `src/native/third_party/fidelityfx-fsr3/` in its own directory because the licence *shape*
  differs from `v2.3.0`'s exception list; `license-check.ps1` §2e asserts it per file.
- **`main` == tag `v2.3.0`** (`60f4ea81`, "AMD FSR SDK 2.3.0", 2026-06-24) is a different shape:
  its `docs/license.md` is a **binary-only, no-reverse-engineering** default licence that
  "applies to all files except as noted below", followed by an MIT exception list of 845 paths.
  **Read off the tree API: the repository at that commit holds exactly 845 blobs, and every one
  of them is on the list** — `Kits/FidelityFX/signedbin/*.dll` included — so the default licence
  governs no file that is actually in the tree, and the headers carry the grant inline as well.
  MIT by exception, for the whole tree. `license-check.ps1` §2d asserts it per vendored file
  rather than trusting this sentence: every path under `fidelityfx/Kits/` must be on the list
  inside the vendored `license.md`, every `.h` must carry the grant, and no binary or source may
  be present.
- **Why `v2.3.0` and not `v1.1.4`, decided by the owner 2026-09-04.** The reversal condition —
  *"unless a title ships an API version that needs 2.x"* — is met on this machine: Hell Is Us
  ships `amd_fidelityfx_upscaler_dx12.dll` 4.0.3 and `amd_fidelityfx_framegeneration_dx12.dll`
  4.0.0, Expedition 33 and Dying Light: The Beast ship upscaler 4.0.2 with frame generation 3.1.5
  (all SDK 2.x effect DLLs), and the SDK's own sample dispatches
  `ffxDispatchDescFrameGenerationPrepareV2` (`0x0002000c`), which `v1.1.4` does not declare.
  Every value and layout the Overlay reads is identical in both tags (`UPSCALE 0x00010001`,
  `FRAMEGENERATION 0x00020003`, `PREPARE 0x00020004`, `ffxApiHeader`, `FfxApiResource`,
  `ffxDispatchDescUpscale`, the `Prepare` prefix through `renderSize`), so the 1.1.x monolith
  decodes with the 2.3.0 constants. Vendored: `Kits/FidelityFX/api/include/{ffx_api.h,
  ffx_api_types.h}`, `upscalers/include/ffx_upscale.h`, `framegeneration/include/{ffx_framegeneration.h,
  ffx_framegeneration_api_types.h}`, in upstream's layout because they include each other by
  relative path. Excluded, each with a reason in the README: `signedbin/`, `ffx_api_loader.h`
  (a `GetProcAddress` helper), the two `dx12/` backend headers (no consumer yet), the `.hpp`
  helpers and every source file.
- Types-only, never linked, with a consumer in the same commit — the Streamline rules
  (`src/native/third_party/streamline/README.md`). **`hookinventory-check` Pass C already
  forbade `amd_fidelityfx*` / `ffx_*` imports** — the sentence here that said the list "must
  gain" them was written before checking, and the self-test at line 242 had covered them since
  2026-08-14 — and now reads the built Overlay's **export table** too, because `ffx_api.h`
  declares its entry points `__declspec(dllexport)` with no import switch and a definition
  inside the Overlay would announce a vendor API it does not implement.

**Intel XeSS SDK** — *checklist run 2026-09-04, REJECTED; the same class as NGX.* The repository
<https://github.com/intel/xess> ships `LICENSE.txt` = **Intel Simplified Software License
(October 2022)**: the grant is for the software *"provided in binary form only"*, it forbids
*"reverse engineering, decompilation, or disassembly … nor any modification"*, and it carries
its own **termination** clause — two of step 2's needles. The headers under `inc/` (`xess/xess.h`,
`xess/xess_d3d12.h`, `xess_fg/xefg_swapchain.h`, `xess_fg/xefg_swapchain_d3d12.h`, `xell/*.h`)
carry no MIT text; each opens *"Intel copyrighted materials … you may not use, modify, copy,
publish, distribute … without Intel's prior written permission."* Step 3 therefore binds:
**do not vendor, and do not re-declare the API.** What remains in policy for Intel is the
census: module presence by name, nothing more. **A signature-free call counter — a
register-preserving thunk on a named export that increments and jumps — was considered and
refused the same day (delegated by the owner, decided 2026-09-04).** It declares no type and
reads no argument, but installing it inline-patches Intel's code inside the game process, and
the same licence forbids *"any modification"*; the census is the answer NGX got and Intel gets
the same one. Reversal condition: Intel publishing the headers under a licence that clears the
checklist, at which point this paragraph is re-run, not argued around.

### Checklist before adding any vendor SDK

1. Is it MIT / BSD / Apache-2.0 / MPL-2.0? → proceed, add the licence copy, done.
2. Otherwise, search the licence text for: **"solely for"**, **"indemnify"**, **"terminate"**, **"reverse engineer"**, **"Intel platforms"/"AMD products"**-style field-of-use wording.
3. Any hit ⇒ **do not vendor it into this repository**, and do not work around it by re-declaring the API. Find an alternative layer, or document the gap honestly.
4. Record the decision in this section either way — including rejections, with reasons.

## Runtime policy

- Poll at 1 Hz on a dedicated Agent thread. Never from the game process, never from the Overlay.
- Any layer that throws or hangs twice is disabled for the session and its fields report `N/A`, rather than being retried in a loop.
- **Read-only, always.** These libraries can also set clocks, fan curves and power limits. FrameLedger never calls a setter — not in v1, not in the backlog. A measurement tool that can modify hardware state is a different product with a different risk profile.

## Capability matrix (fill during P0 on real hardware)

**Restructured to vendor × layer on 2026-08-05**, before anything was filled in, because
the note below said in this document's own words that the old single-axis table *"cannot
currently express"* the AMD/Intel deferral and that adding the axis is *"a prerequisite of
filling it, not a tidy-up afterwards"*. Filling the NVIDIA half into the old table would
have overwritten cells that were simultaneously making an unverified claim about hardware
nobody here has.

**Legend, and the distinction the restructure exists to preserve:**

| Mark | Meaning |
|---|---|
| ✓ / ✗ | **Measured** on that vendor, available / not available |
| `arch` | Not available **by architecture** — no measurement needed or possible |
| `?` | **Not yet measured, and measurable here.** A to-do |
| `untested` | **Cannot be measured on this machine.** Not a to-do — a deferral (§R5/§R6) |

`?` and `untested` are what the old table could not tell apart. The UI consults this before
advertising a capability, and "we have not got to it" and "we cannot, here" must not render
as the same thing.

### L1 — DXGI + PDH (vendor-neutral by construction)

| Field | NVIDIA | AMD | Intel |
|---|---|---|---|
| GPU utilisation | ? | untested | untested |
| Adapter VRAM | ? | untested | untested |
| Per-process VRAM | ? (also from Overlay) | untested | untested |
| Driver version | ? | untested | untested |
| Core temp | ? (D3DKMT probe, Win 11 only — see below) | untested | untested |
| Power | ? (D3DKMT probe) | untested | untested |
| Fan | ? (D3DKMT probe) | untested | untested |
| Hotspot / memory temp, Clocks, Throttle, Reflex, CPU temp | `arch` | `arch` | `arch` |

L1 is vendor-neutral **by construction** — DXGI and PDH expose the same counters whatever
the adapter is — so the AMD/Intel `untested` cells are expected to match NVIDIA and are
still not claims. **One cell genuinely cannot be filled here even on NVIDIA:** multi-adapter
LUID instance filtering, because this machine has exactly one adapter and a `KF` CPU with no
iGPU. It must be written and can only be reasoned about.

### L2 — LibreHardwareMonitor

| Field | NVIDIA | AMD | Intel |
|---|---|---|---|
| GPU utilisation, Adapter VRAM | ✓ (`GPU Core` load; `GPU Memory Used`, MB) | untested | untested |
| Driver version | `arch` — LHM publishes no such sensor; L1 owns it | `arch` | `arch` |
| Core temp, memory temp | ✓ (`GPU Core`; `GPU Memory Junction`) | untested | untested |
| Hotspot temp | **✗ on this box** — no `GPU Hot Spot` sensor at all (RTX 5080, 616.56, LHM 0.9.6); driver, generation or library unmeasured | untested | untested |
| Clocks, Power, Fan | ✓ (`GPU Core` / `GPU Memory` MHz; `GPU Package` W; `GPU Fan 1..3` rpm) | untested | untested |
| Per-process VRAM | `arch` | `arch` | `arch` |
| Throttle reasons, Reflex / PC latency | `arch` | `arch` | `arch` |
| CPU temp | ✓ elevated + PawnIO | ✓ elevated + PawnIO | ✓ elevated + PawnIO |
| **GPU sensors unelevated, no PawnIO (§M5)** | **✓ — row R1, 2026-09-03.** Unelevated and elevated return the identical eight fields; PawnIO never opened (GPU group only) | untested | untested |

§M5 is the row that decides whether the **default, unelevated** Agent has temperatures at
all. **Measured 2026-09-03 — yes, on NVIDIA.** The instrument is
`FrameLedger.CaptureHost probe-lhm`, the raw tree and the elevated control are in
`spike-notes` §10, and the decision table it was read against was pre-committed in
`20_OPEN_QUESTIONS` §M5 before the run. The NVIDIA column above is that one machine's
answer; the `untested` cells are exactly as untested as before.

### L3 — NVAPI (NVIDIA only, `arch` elsewhere)

| Field | NVIDIA | AMD | Intel |
|---|---|---|---|
| **Initialises at all** | ✓ **measured 2026-08-05** | `arch` | `arch` |
| **Driver version** | ✓ **measured** — `610.88`, branch `r610_85` | `arch` | `arch` |
| **GPU enumeration + name** | ✓ **measured** — 1 GPU, "NVIDIA GeForce RTX 5080" | `arch` | `arch` |
| **Degrades cleanly when absent** | ✓ **measured** — see below | `arch` | `arch` |
| Core / hotspot / memory temp | ? | `arch` | `arch` |
| Per-domain utilisation | ? | `arch` | `arch` |
| Clocks, Power | ? | `arch` | `arch` |
| Throttle reasons | ? | `arch` | `arch` |
| Reflex / PC latency | ? | `arch` | `arch` |

The four measured rows come from `ctest fl_nvapi_probe` (`src/native/tools/fl-probe-nvapi`),
which exists because vendoring added 2.4 MB of headers and an import library that **nothing
else compiles against yet** — an unconsumed vendored dependency is one whose header closure
could be short by a file with every gate still green.

**"Degrades cleanly when absent" — what is observed, and what is inferred.** `nvapi64.lib`
is a *static* library of stubs that reach `nvapi64.dll` through `nvapi_QueryInterface` at
first call, so linking it does **not** make the DLL a load-time dependency: a machine with
no NVIDIA driver loads the binary and `NvAPI_Initialize` returns an error instead. The probe
prints **`BRANCH: AVAILABLE`** or **`BRANCH: DEGRADED`** and exits 0 either way, and ctest
requires one of those two strings — so a probe gutted to `return 0` fails, which exit code
alone could never catch.

> **Stated precisely because the obvious sentence would be an overclaim.** *Observed on CI:*
> the probe builds, links and passes on a hosted runner in ~0.01 s, against ~1.4 s on the dev
> box. *Inferred, not observed:* that the runner took the DEGRADED branch — hosted Windows
> runners have no NVIDIA driver and the timing matches an immediate `NvAPI_Initialize`
> failure, but **ctest prints a passing test's output nowhere**, so the CI log shows that a
> branch was reached and not which one. The load-time-dependency claim is what the run does
> prove: a load-time dependency on an absent `nvapi64.dll` would have failed to start at all.

> ⚠ **`NvAPI_GPU_GetMemoryInfo` was named in §L3's function table below and must not be
> used.** The vendored headers carry
> `__nvapi_deprecated_function("...deprecated in release 520. Instead, use
> NvAPI_GPU_GetMemoryInfoEx.")` on it, so under `/W4 /WX` a call fails the native build.
> Corrected in the table. Recorded because it is the class `17_HOOK_ENGINE` calls the
> highest false-confidence risk in the spike — a name that resolves to nothing degrades to
> "unknown", which reads as "working, no data" — found by vendoring the material and
> compiling against it rather than by reading a vendor's web documentation.

> **🅓 The AMD and Intel results are deferred as `untested` — owner decision,
> 2026-08-05** (`20_OPEN_QUESTIONS` §R5/§R6). Not deferred for cost: the dev machine
> has one adapter, an RTX 5080, and a `KF` CPU with no iGPU (`spike-notes.md`
> §Environment), so there is no measurement to take. **AFMF is unreachable for the
> same reason** — it is an AMD driver-side feature.
>
> The rule that travels with the deferral: an unmeasured cell is **`untested`**, never
> `?` and never a checkmark inferred from a vendor's documentation. `?` reads as "we
> have not got to it yet"; `untested` reads as "we cannot, here" — and this table is
> what the UI consults before advertising a capability to a user.
>
> ~~**This table cannot currently express that.** Its columns are the three layers, with
> no vendor axis, so "measured on NVIDIA, untested on AMD/Intel" has nowhere to go —
> and every `?` below is therefore ambiguous between the two meanings. **Adding the
> vendor axis is a prerequisite of filling it**, not a tidy-up afterwards, because a
> single-axis table forces whoever fills the NVIDIA half to overwrite cells that are
> also making a claim about hardware they never had. P0 item 8 owns this.~~
>
> > **Done 2026-08-05, before anything was filled in**, which was the point of calling it
> > a prerequisite. Three tables, one per layer, with a vendor axis and a four-symbol
> > legend that separates `?` (a to-do, measurable here) from `untested` (a deferral,
> > not measurable here) from `arch` (not available by architecture, so neither).
>
> The NVIDIA half is **not** deferred and ~~none of it exists in code yet — no PDH, no
> DXGI telemetry, no LibreHardwareMonitor, no NVAPI~~ — **L2 exists as of 2026-09-03**
> (`LhmTelemetrySource`, `SensorMap`, the port in `FrameLedger.Application.Telemetry`);
> L1 and L3 are still unwritten. §M5 in particular (do LHM GPU
> sensors work unelevated, without PawnIO?) decided whether the default unelevated
> Agent has temperatures at all — **it does, on NVIDIA; row R1 above.**
