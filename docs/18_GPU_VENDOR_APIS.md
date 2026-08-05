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
    GpuCapabilities Capabilities { get; }   // which fields are real on this machine
    bool TryRead(out GpuSample sample);     // 1 Hz, called by the Agent
}
```

Implementations compose rather than compete:

- `BaselineTelemetrySource` (L1) — always constructed.
- `LhmTelemetrySource` (L2) — constructed when LHM initialises.
- `NvapiTelemetrySource` (L3) — constructed only on NVIDIA hardware.
- `CompositeTelemetrySource` merges them with a fixed precedence per field (**L3 > L2 > L1**) and records which layer supplied each value.

`GpuSample`: `tempCoreC`, `tempHotspotC`, `tempMemoryC`, `loadPct`, `vramAdapterMB`, `coreClockMhz`, `memClockMhz`, `powerW`, `fanRpm`, `throttleReasons`, `pcieGen/Width`. Every field nullable — `Capabilities` says what to trust, and the UI shows `N/A` rather than zero.

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

L1 gives no temperatures on its own (unless the D3DKMT probe pans out). That is what L2 is for.

## L2 — LibreHardwareMonitorLib (MPL-2.0, all vendors)

`LibreHardwareMonitorLib` ≥ 0.9.6, consumed as an **unmodified NuGet package**. It already implements per-vendor GPU sensor access internally — which means the vendor-interop licensing problem is one LHM has solved upstream, under a licence that works for us. That is the entire reason this layer exists.

- `Computer` opened with `IsGpuEnabled` always; `IsCpuEnabled` + `IsMemoryEnabled` only when the Agent is elevated and PawnIO is available.
- Poll on a dedicated thread: `computer.Accept(updateVisitor)` then read mapped sensors. Never faster than 500 ms; default 1000 ms.
- Sensor mapping by `SensorType` + name heuristics per vendor (`GPU Core`, `GPU Hot Spot`, `GPU Memory`, `GPU Package Power`, …), kept in `SensorMap.cs` with unit tests against captured sensor-tree fixtures.
- **P0 verification items:** (a) which fields LHM actually returns per vendor on real hardware — fill the capability matrix below; (b) whether GPU-only usage works **without** elevation and without PawnIO (expected yes, since GPU sensors go through user-mode vendor paths, but confirm — it determines whether the default unelevated Agent has temperatures at all); (c) ~~confirm LHM's sources are not marked with MPL-2.0 Exhibit B~~ — **done, 2026-08-02: clear.** No source file applies the notice; the only repository hit is the `LICENSE` template itself, and every file we depend on carries the permissive Exhibit A. Method and evidence in `docs/spike-notes.md` §0. Re-check on every version bump, and check *source headers*, never the LICENSE file — MPL-2.0's own text contains Exhibit B as a template, so grepping the licence finds it in every MPL project ever published.

CPU and motherboard sensors remain LHM-only, elevated-only, PawnIO-dependent, and optional.

## L3 — NVAPI (MIT, NVIDIA only)

**Integration: vendored headers + import library, linked normally.** NVIDIA publishes the NVAPI SDK — headers, interface definitions, and `nvapi64.lib` — under the **MIT licence** (verified 2026-08-02: `amd64/nvapi64.lib` is a tracked file in the MIT repo and `License.txt` names the import libraries as the subject of the grant, so the binary is covered and not only the headers), so the SDK material can live in our GPL-3.0 repository with only the usual MIT attribution obligation. The runtime implementation lives in `nvapi64.dll`, which ships with the installed NVIDIA driver.

> This supersedes the earlier design's dynamic-loading-by-ordinal approach. That was a workaround for a licensing constraint that no longer exists; ordinal resolution is fragile across driver versions and should not be reintroduced. Vendored headers are simpler, type-safe, and maintainable.

Still resolve **entry points defensively at runtime**: `nvapi64.dll` may be absent (no NVIDIA GPU) or older than our headers. `NvAPI_Initialize` failing is a normal condition that must disable L3 cleanly, not throw.

| Purpose | Function |
|---|---|
| Init / enumerate | `NvAPI_Initialize`, `NvAPI_EnumPhysicalGPUs`, `NvAPI_GPU_GetFullName` |
| Temperatures | `NvAPI_GPU_GetThermalSettings` (+ extended thermal query for hotspot/memory where available) |
| Utilisation | `NvAPI_GPU_GetDynamicPstatesInfoEx` (GPU / FB / VID / BUS domains) |
| Clocks | `NvAPI_GPU_GetAllClockFrequencies` |
| Memory | `NvAPI_GPU_GetMemoryInfo` |
| **Throttle reasons** | `NvAPI_GPU_GetPerfDecreaseInfo` — not available from L1 or L2 |
| Driver version | `NvAPI_SYS_GetDriverAndBranchVersion` |
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

| Field | L1 (DXGI/PDH) | L2 (LHM) | L3 (NVAPI) |
|---|---|---|---|
| GPU utilisation | ✓ | ? | ? (per-domain) |
| Adapter VRAM | ✓ | ? | ? |
| Per-process VRAM | ✓ (also from Overlay) | ✗ | ✗ |
| Driver version | ✓ | ? | ? |
| Core temp | ? (D3DKMT probe) | ? | ? |
| Hotspot / memory temp | ✗ | ? | ? |
| Clocks | ✗ | ? | ? |
| Power | ? (D3DKMT probe) | ? | ? |
| Fan | ? (D3DKMT probe) | ? | ? |
| Throttle reasons | ✗ | ✗ | ? |
| Reflex / PC latency | ✗ | ✗ | ? |
| CPU temp | ✗ | ✓ (elevated + PawnIO) | ✗ |

Replace `?` with measured results per vendor. The matrix drives what the UI advertises as available, and the AMD/Intel columns of L2 are the ones that decide whether the Intel gap is actually noticeable to users.

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
> **This table cannot currently express that.** Its columns are the three layers, with
> no vendor axis, so "measured on NVIDIA, untested on AMD/Intel" has nowhere to go —
> and every `?` below is therefore ambiguous between the two meanings. **Adding the
> vendor axis is a prerequisite of filling it**, not a tidy-up afterwards, because a
> single-axis table forces whoever fills the NVIDIA half to overwrite cells that are
> also making a claim about hardware they never had. P0 item 8 owns this.
>
> The NVIDIA half is **not** deferred and none of it exists in code yet — no PDH, no
> DXGI telemetry, no LibreHardwareMonitor, no NVAPI. §M5 in particular (do LHM GPU
> sensors work unelevated, without PawnIO?) decides whether the default unelevated
> Agent has temperatures at all, and therefore how ADR-9 reads to users.
