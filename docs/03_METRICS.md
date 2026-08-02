# 03 — Metrics

**Single source of truth** for every number FrameLedger displays. Implement in `FrameLedger.Domain.Metrics` with golden tests (`14_TESTING`).

Every metric declares which **capture tier** it requires (`01_ARCHITECTURE` §Capture tiers). A Tier-2 session simply has `N/A` where Tier-1 data is missing — never a silently degraded estimate presented as fact.

## Inputs

**Tier 1** — `FlFrameRecord` stream from the ring (`17_HOOK_ENGINE`): `qpc`, `presentFlags`, `syncInterval`, `renderW/H`, `outputW/H`, `upscaler`, `upscalerQuality`, `fgMode`, `rtFlags`, `dispatchRaysCount`, `psoCreatedThisFrame`, `vramUsedBytes`, `reflexLatencyUs`, `hdr`.

**Tier 2** — PresentMon CSV: `MsBetweenPresents`, `MsBetweenDisplayChange`, `FrameType`, `PresentMode`, `Runtime`, `Dropped`.

**Both** — 1 Hz `GpuSample` (`18_GPU_VENDOR_APIS`) + optional CPU sample.

## Frame times

Frame time `ft[i] = (qpc[i] - qpc[i-1]) / qpcFreq × 1000` ms, measured at **present entry** (Tier 1) — consistent, low-jitter, and unaffected by driver-side frame pacing.

Let `D` = session duration (first → last present), `F_app` = frames the *game* presented, `F_disp` = frames actually shown.

## Core definitions

| Metric | Definition | Tier |
|---|---|---|
| **Native FPS (avg)** | `count(F_app) / D` | 1, 2 |
| **Displayed FPS (avg)** | `count(F_disp) / D` | 1, 2 |
| **FG factor** | `DisplayedFPS / NativeFPS`, shown `×N.N`; `—` when FG inactive | 1, 2 |
| **Avg FPS** (headline) | = Native FPS. Time-based, **not** the mean of instantaneous FPS values | 1, 2 |
| **Median FPS** | `1000 / p50(ft_app)` | 1, 2 |
| **1% Low** | `1000 / p99(ft_app)` | 1, 2 |
| **0.1% Low** | `1000 / p99.9(ft_app)` | 1, 2 |
| **Min / Max FPS** | `1000 / max(ft_app)` / `1000 / min(ft_app)` | 1, 2 |
| **Frametime σ** | population stddev of `ft_app` (ms) | 1, 2 |
| **Stutter count** | `ft_app[i] > 2 × rollingMedian(ft_app, 19)` (centered) | 1, 2 |
| **Stutter time %** | `Σ ft_app[stutter] / Σ ft_app × 100` | 1, 2 |
| **PC latency** | mean/p95 of `reflexLatencyUs` when Reflex reports it | **1 only** |
| **PSO stutter %** | share of stutter frames with `psoCreatedThisFrame > 0` | **1 only** |

**Percentile method:** sort ascending, linear interpolation between closest ranks (NumPy `linear` / Excel `PERCENTILE`). Document it, test it — tools differ and users will compare numbers.

**Lows use application frames only.** Generated frames smooth display cadence but do not represent simulation stalls; mixing them hides real stutter. A secondary "Displayed 1% Low" is stored for the Displayed chart series but never replaces the headline.

**Sufficiency guards (FR-3.5):** 0.1% Low needs ≥ 10,000 application frames, 1% Low ≥ 1,000; otherwise `N/A`.

## Frame Generation — ground truth (Tier 1)

This is the metric the rewrite exists for. Resolution ladder, highest confidence first; the winning rung is stored in `fg_source`:

1. **`api` (authoritative).** An NGX `FrameGeneration` feature (or Streamline `DLSS_G`, or an FFX frame-interpolation context, or `xess_fg`) was **created and evaluated this frame** → FG is on, and we know *which* technology by name, from the vendor's own API call. This is a fact, not an inference.
2. **`presentdelta`.** Compare our hooked present count against `IDXGISwapChain::GetFrameStatistics().PresentCount` over a window: the driver-side excess = generated frames. This catches **driver-level FG the application never sees** (AMD AFMF, driver-injected paths) — the case that hooking alone cannot see. Requires flip-model presentation to be reliable; when `GetFrameStatistics` is unavailable, this rung is skipped.
3. **`etw`** (Tier 2). PresentMon `FrameType` column reports generated frames.
4. **`cadence`** (last resort, both tiers). Sustained `Displayed/Native ≥ 1.5` → `Detected (unknown)`.
5. Otherwise `fg_mode = none`, factor `—`.

`fg_factor` is **always** computed as `DisplayedFPS / NativeFPS` over the session regardless of which rung identified the mode — the ratio is measured, only the *identification* comes from the ladder.

**Display rule (product requirement, CLAUDE.md rule 6):** wherever FPS appears and FG is active, render `"{native} → {displayed} FPS (×{factor} FG)"`. Cards show Native large, Displayed + factor secondary. Charts default to Native with a Displayed toggle. Exports contain all three.

## Upscaling — measured, not guessed (Tier 1)

From the upscaler hooks we get, per frame:

- `upscaler`: the technology **actually executing** (`dlss`, `dlss_rr`, `fsr2`, `fsr3`, `fsr4`, `xess`, `nis`, `none`) — from the API that was called, not from a DLL sitting on disk.
- `renderW × renderH` vs `outputW × outputH` → **exact upscale ratio**: `sqrt((outW×outH)/(renW×renH))`, reported both as a ratio (`1.50×`) and a percentage (`67% render scale`).
- `upscalerQuality`: the vendor's own quality enum (DLSS Performance/Balanced/Quality/DLAA, XeSS quality setting, FSR preset), mapped to a display name per vendor in `Domain.UpscalerNames`.
- Resolution changes mid-session (user changed settings, or dynamic resolution scaling) produce **segments**: the session stores a segment list `(startFrame, renderW/H, outputW/H, upscaler, quality)`. Aggregates report the dominant segment plus a "settings changed during session" flag — averaging across a settings change is the classic way benchmark numbers become meaningless.

Tier 2 has none of this: `upscaler = unknown`, ratio `N/A`.

## RT / PT / RR — evidence-based tri-state

Tri-state `Yes | No | N/A` per session with `source` (`measured | manual | inherited`).

| Flag | Tier-1 evidence | Result |
|---|---|---|
| **Ray Tracing** | `BuildRaytracingAccelerationStructure` called, **or** `DispatchRays` called, in ≥ 5% of frames | `Yes` |
| | RT-capable device (`D3D12_FEATURE_D3D12_OPTIONS5` tier ≥ 1.0) present, no AS builds and no dispatches for the whole session | `No` |
| | No RT-capable API in use (D3D11/D3D9/OpenGL), or evidence inconclusive | `N/A` |
| **Ray Reconstruction** | NGX `RayReconstruction` feature created **and evaluated** (or Streamline `DLSS_RR`) | `Yes` / `No` if DLSS is active without it |
| **Path Tracing** | heuristic only — see below | usually `N/A` |

**Honest limits, documented in the UI tooltip:**

- Hooking `BuildRaytracingAccelerationStructure` is what makes **inline ray tracing (DXR 1.1 `RayQuery`)** detectable at all — those shaders never call `DispatchRays`, so dispatch counting alone would report `No` for a game that is very much ray tracing. AS-build activity catches both paths. This is why both hooks exist.
- **Path tracing has no API-level signature.** The heuristic combines: rays dispatched per output pixel ≥ ~1.0, `MaxTraceRecursionDepth` from the RT PSO config, number of distinct RT state objects, and the ratio of RT to raster work. It produces a **confidence score**, and only ≥ 0.8 offers a *suggestion* in the UI ("looks like path tracing — confirm?"). It never sets `Yes` on its own. Manual override remains the authoritative path, per game, inherited by future sessions.
- A game can also enable RT for a subset of effects only; `Yes` means "rays were traced", not "everything is ray traced". The UI says so.

Derived extras (Tier 1): `rays_per_pixel` (mean dispatch volume ÷ output pixels), `rt_frame_pct` (share of frames with RT activity), `rt_pso_count`.

## Per-process VRAM (Tier 1)

`vramUsedBytes` from `IDXGIAdapter3::QueryVideoMemoryInfo(LOCAL)` inside the game = **this game's** usage and budget. Stored as its own series and clearly labelled apart from the adapter-wide figure from `18_GPU_VENDOR_APIS`. Aggregates: avg, max, and `budget_exceeded_pct` (share of samples where `CurrentUsage > Budget`, i.e. the driver was likely evicting — a genuinely useful stutter explanation).

## Sensor aggregates

Per session over 1 Hz samples: `avg` (mean of non-null), `max`. Sensor timeline aligned to the frame timeline via the shared QPC epoch captured at session start. Fields with no data are `N/A`, never 0.

## Accuracy budget (shown in Help → About metrics)

| Quantity | Tier 1 | Tier 2 |
|---|---|---|
| Frame times / FPS | < 0.05% (direct QPC at the present call) | < 0.1% (ETW) |
| Native vs Displayed / FG factor | exact counts when rung 1–2 resolved | inferred |
| Upscaler + render resolution | **exact** (vendor API arguments) | not available |
| RT active | measured per frame | not available |
| Path tracing | heuristic, confidence-scored, never asserted | not available |
| Per-process VRAM | exact | not available |
| PC latency | as reported by Reflex | not available |
| GPU temp / load / power | vendor API accuracy, ±1 s sampling | same |
| CPU temperature | sensor-inherent ±1–2 °C, needs LHM + PawnIO + elevation | same |

## Export schema (per-frame CSV, FR-8.1)

`frame_index,qpc_ms,frametime_ms,native_or_generated,render_w,render_h,output_w,output_h,upscaler,upscaler_quality,fg_mode,rt_flags,dispatch_rays,pso_created,vram_mb,reflex_latency_us`

plus a `#`-prefixed header block: game, date, **capture tier**, api, present mode, hardware snapshot, segment list, and the tri-state flags with their sources. The tier belongs in the header because a Tier-2 export is missing whole columns and anyone reading it later must know why.
