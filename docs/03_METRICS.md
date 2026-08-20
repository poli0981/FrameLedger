# 03 — Metrics

**Single source of truth** for every number FrameLedger displays. Implement in `FrameLedger.Domain.Metrics` with golden tests (`14_TESTING`).

Every metric declares which **capture tier** it requires (`01_ARCHITECTURE` §Capture tiers). A Tier-2 session simply has `N/A` where Tier-1 data is missing — never a silently degraded estimate presented as fact.

## Inputs

**Tier 1** — `FlFrameRecord` stream from the ring (`17_HOOK_ENGINE` §Ring writer), which is the authoritative field list. Every field is consumed **except the two protocol fields** `seq` and `reserved`, which are named here so "every field is consumed" does not have to be read as a promise about them: `qpc`, `frameIndex`, `presentFlags`, `syncInterval`, `renderW/H`, `outputW/H`, `api`, `upscaler`, `upscalerQuality`, `upscalerSharpness`, `fgMode`, `fgEvaluations`, `rtFlags`, `featureFlags`, `dispatchRaysVolume`, `maxTraceRecursionDepth`, `psoCreatedThisFrame`, `vramUsedMb`, `reflexLatencyUs`, `colorSpace`, `measuredMask`, `swapchainId`. Plus, from `FlWriterState`: `vramBudgetMb` for `budget_exceeded_pct`, `rtTier` and `hooksInstalledMask` for the RT tri-state, and `rtStateObjectsCreated` / `rasterPsoCreated` for `pt_confidence`.

> **Renamed in layout v3 (2026-08-05):** `vramUsedBytes` → `vramUsedMb` (MiB, matching the `vramBudgetMb` it is compared against and the `vram_mb` this document already exported), and `hdr` → `colorSpace` (a bool had no third state). `measuredMask` is 16-bit.

**Tier 2** — PresentMon CSV. **Pin the version:** `FrameType` exists only in PresentMon 2.x, while `MsBetweenPresents` / `MsBetweenDisplayChange` are 1.x column names — no single binary emits both sets, so a parser written against this list as originally stated could never succeed. v1 targets the **2.x console** and its column names; the header-map parser (`14_TESTING` §Parsers) resolves columns by name and reports an explicit capability loss when `FrameType` is absent rather than silently reporting `fg_mode = none`.

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
| **Stutter count** | `ft_app[i] > 2 × rollingMedian(ft_app, 19)` (centered, window in **frames**) | 1, 2 |
| **Stutter time %** | `Σ ft_app[stutter] / Σ ft_app × 100` | 1, 2 |
| **PC latency** | mean/p95 of `reflexLatencyUs` when Reflex reports it | **1 only** |
| **PSO stutter %** | share of stutter frames with `psoCreatedThisFrame > 0` | **1 only** |

**Percentile method:** sort ascending, linear interpolation between closest ranks (NumPy `linear` / Excel `PERCENTILE`). Document it, test it — tools differ and users will compare numbers.

**Rolling-median edges.** The 19-frame window is centered and measured in
*frames*, so the first and last 9 frames have no full window. They use a
**truncated symmetric window** (the largest centered odd window that fits), not a
padded or partial-shifted one — the alternatives either invent data or silently
bias the first samples. A session shorter than 19 application frames reports
`stutter_count = N/A`; it is already below the 30 s minimum session length
(FR-3.6) and would be discarded anyway. Golden tests pin both edges.

**Data gaps.** Where the drain recorded a gap (a torn record, `07_IPC`
§Protocol rules), the interval spanning the gap is **excluded** from `ft_app`
entirely rather than counted as one long frame. Including it would fabricate a
stutter — the exact artifact these metrics exist to detect honestly. Gaps count
toward the session's data-quality warnings.

**Lows use application frames only.** Generated frames smooth display cadence but do not represent simulation stalls; mixing them hides real stutter. A secondary "Displayed 1% Low" is stored for the Displayed chart series but never replaces the headline.

**Sufficiency guards (FR-4.8):** 0.1% Low needs ≥ 10,000 application frames, 1% Low ≥ 1,000; otherwise `N/A`.

## Frame Generation — ground truth (Tier 1)

This is the metric the rewrite exists for. Resolution ladder, highest confidence first; the winning rung is stored in `fg_source`:

1. **`api` (authoritative).** An NGX `FrameGeneration` feature (or Streamline `DLSS_G`, or an FFX frame-interpolation context, or `xess_fg`) was **created and evaluated this frame** → FG is on, and we know *which* technology by name, from the vendor's own API call. This is a fact, not an inference.
2. **`etw`** (Tier 2). PresentMon 2.x `FrameType` column reports generated frames directly.
3. **`cadence`** (last resort, both tiers). Sustained `Displayed/Native ≥ 1.5` → `Detected (unknown)`.
4. Otherwise `fg_mode = none`, factor `—`.

> **Rung 0, added 2026-08-06, and it has to come before rung 4 or rung 4 is a lie.** If
> `FL_MEASURED_FG` is clear the answer is **`N/A`**, not `none`: no hook capable of answering
> was live, and `none` means *"a hook ran and there was genuinely no frame generation"* —
> the only one of the three states that may be aggregated as a negative (`fl_shm.h`,
> layout v3). Applied to today's present-only writer, rung 4 as written turns "nobody
> looked" into a measured absence about every title, which is the affirmative negative
> the whole of layout v3 exists to make impossible.
>
> Likewise **`fg_factor` is `N/A` unless `FL_MEASURED_FG_COUNTS` is set**, and never `1.0`.
> With the counts unmeasured `Σ fgEvaluations` is zero, and a consumer that treated that as
> "no frame generation" would publish `F_app == F_disp` — CLAUDE.md rule 6's forbidden
> number, reached by counting nothing. **Zero is a data gap here, never a measurement**, and
> the mask bit is what tells the two apart.
>
> The same holds one level down: a present that drained no evaluation carries
> `fgEvaluations = 0` *with the bit set*, which is a real measurement of that present. A
> consumer must not filter those records out — doing so leaves `presents == Σ` and recovers
> the forbidden 1.0 from honest data.

### Counting native vs displayed frames at Tier 1

Application-generated and FG-generated frames both go out through the swapchain
we hooked, so **our present hook sees them all** — the present count alone is
`F_disp`, not `F_app`. The separation comes from the FG feature itself:

```
F_disp  = presents observed by the hook
F_app   = Σ fgEvaluations                  (APPLICATION frames, counted at the source)
```

> **MEASURED 2026-08-15, AND IT CHANGES WHAT THIS SECTION CAN PROMISE.** On the one title
> measured — Cyberpunk 2077, SL 2.7.1 — `slEvaluateFeature(kFeatureDLSS_G)` is **never
> called**: 0 evaluations across ~14,000 Streamline batches at four frame-generation
> settings, while frame generation was demonstrably active. DLSS-G on Streamline 2.x is not
> driven through the feature-evaluation entry point, so `Σ fgEvaluations` is 0 and
> `fg_factor` is correctly `N/A` rather than wrong. `presents / batch` — presents per
> Streamline evaluation drained — reads 1.000 / 2.000 / 4.000 against that title's own
> off / ×2 / ×4, but a batch is **not** an application frame: they coincide only because Ray
> Reconstruction is evaluated once per application frame there, which no independent oracle
> has confirmed. **Do not promote that proxy to `fg_factor` without attaching the premise.**
> `docs/HANDOFF.md` item 3 carries the routes to a real producer.
>
> **The proxy is PRINTED, and it now has a guard of its own — which the factor's could not be.**
> `FgWindow.BucketFactors` splits the window into buckets and refuses a factor when one bucket
> departs from the whole; it divides by `Σ fgEvaluations`, **zero on every record on this
> route**, so every bucket matched, the check passed vacuously, and `RefusalFor` returned at the
> data-gap clause before uniformity was ever considered. Measured 2026-08-16: an alt-tab
> mid-capture produced an achieved `presents / batch` of **1.84** against a title configured for
> ×2 — wrong by 8%, with nothing in the report saying so. `FgWindow.BatchRefusal` is the
> per-bucket `presents / batch` check §S30 named as a prerequisite, and `SessionReport` prints
> its verdict on the line under the ratio so the number cannot be read without it. A guard keyed
> on a quantity that is zero on the route that runs is not a guard.
>
> **This also leaves §RT/PT/RR without a settled denominator.** `rt_frame_pct`,
> `rays_per_pixel` and the `≥ 5% of frames` gate are per-APPLICATION-frame quantities, and
> dividing them by presents dilutes each by the frame-generation factor — at ×4 a title that
> path-traces every application frame reports 25%. Whoever writes the RT hooks must choose a
> denominator and state it here.

`fgEvaluations` is recorded per present by the NGX/Streamline/FFX/XeSS FG hooks
(`17_HOOK_ENGINE` §Upscaling / frame generation). `fg_factor = F_disp / F_app`.

> **`fgEvaluations` counts EVALUATIONS, not generated frames — owner ruling, 2026-08-14 —
> and this block said the opposite until the producer was written.** The subtraction form
> above (`F_app = presents − Σ fgEvaluations`) needs the count to be of *generated* frames,
> and nothing can produce that number in policy: `slEvaluateFeature(kFeatureDLSS_G)` fires
> **once per application frame** and yields N−1 generated ones, where N lives in
> `sl::DLSSGOptions` — set out of band through `slDLSSGSetOptions`, which is the route
> `HANDOFF` §2b refused on five separate grounds. Counting the evaluations themselves needs
> no multiplier and no vendor header at all, and the two forms are not interchangeable:
> on the one real title measured they differ by a factor of four.
>
> **What it costs, stated rather than discovered.** The per-frame `native_or_generated`
> bit below is now "this present carried an application frame's evaluation" rather than
> "this present was generated", and its polarity is inverted accordingly. Because the
> drain attributes a batch to the *next* present, that classification is exact in
> aggregate and approximate per frame — a generated present and its application frame are
> adjacent, and which of them carries the bit is decided by thread timing. Aggregates
> (Native FPS, the FG factor, the lows) are unaffected; a per-frame *timeline* coloured by
> that bit is accurate to one frame.
>
> **And `fgEvaluations` saturates at 255 rather than wrapping.** A wrapped count reads
> LOW and is the denominator here, so it would inflate the factor without bound. No
> configuration evaluates frame generation 255 times between two presents, so a consumer
> seeing 255 must refuse to publish a factor rather than divide by a floor.

> **What this replaces, and why.** An earlier revision derived generated frames
> from `IDXGISwapChain::GetFrameStatistics().PresentCount` minus our hooked
> present count, and claimed that difference exposed driver-level FG such as AMD
> AFMF. It does not. `PresentCount` counts presents *the application submitted
> through that swapchain* — the same events our hook intercepts — so the
> difference is structurally zero, and a metric that is always zero reads as
> "no frame generation" rather than as a failure. The rung was not conservative;
> it was silently wrong.

**Driver-level frame generation (AMD AFMF, driver-injected interpolation) is not
detectable in v1 and must not be implied to be.** It happens after present, in
the driver or the compositor, invisible to both an in-process hook and
`GetFrameStatistics`. Where the UI cannot distinguish it, the honest answer is
`N/A`, exactly as elsewhere in this document. Whether PresentMon 2.x's
`FrameType` can see it at Tier 2 is a P0 question (`20_OPEN_QUESTIONS` §M1).

`fg_factor` is **always** the measured ratio `F_disp / F_app` regardless of which
rung identified the mode — only the *identification* comes from the ladder.

**Display rule (product requirement, CLAUDE.md rule 6):** wherever FPS appears and FG is active, render `"{native} → {displayed} FPS (×{factor} FG)"`. Cards show Native large, Displayed + factor secondary. Charts default to Native with a Displayed toggle. Exports contain all three.

## Upscaling — measured, not guessed (Tier 1)

From the upscaler hooks we get, per frame:

- `upscaler`: the technology **actually executing** (`dlss`, `fsr2`, `fsr3`, `fsr4`, `xess`, `nis`, `none`) — from the API that was called, not from a DLL sitting on disk.

  > **`dlss_rr` is NOT a value of this field, and this line listed it until 2026-08-06.** Layout
  > v3 retired it and **reserved** the slot rather than reusing it, because it made Ray
  > Reconstruction mutually exclusive with DLSS super-resolution — and the two run together.
  > RR is an independent tri-state axis (§RT/PT/RR below, and CLAUDE.md rule 7's trio), carried
  > as `FL_FEAT_RAY_RECONSTRUCTION` in `featureFlags` with its own OBSERVED bit. A consumer
  > written from this line would have decoded the reserved value 2 as `dlss_rr` and resurrected
  > the conflation the record had already removed.
- `renderW × renderH` vs `outputW × outputH` → **exact upscale ratio**: `sqrt((outW×outH)/(renW×renH))`, reported both as a ratio (`1.50×`) and a percentage (`67% render scale`).
- `upscalerQuality`: the vendor's own quality enum (DLSS Performance/Balanced/Quality/DLAA, XeSS quality setting, FSR preset), mapped to a display name per vendor in `Domain.UpscalerNames`.
- Resolution changes mid-session (user changed settings, or dynamic resolution scaling) produce **segments**: the session stores a segment list `(startFrame, renderW/H, outputW/H, upscaler, quality)`. Aggregates report the dominant segment plus a "settings changed during session" flag — averaging across a settings change is the classic way benchmark numbers become meaningless.

  > **Two segmentation axes exist, they compose in ONE order, and neither document said so until
  > 2026-08-06.** This one splits on a settings change; `fl_shm.h` §`swapchainId` says the Agent
  > "segments by this value and reports the dominant stream". **Stream first, settings second.**
  > Patching a vtable slot patches the shared `dxgi.dll` class vtable, so one hook sees every
  > swapchain in the process — a title with a separate UI or video swapchain interleaves two
  > streams in one ring, and splitting on resolution first cuts a new segment every time the two
  > alternate, i.e. one segment per present.
  >
  > **`swapchainId == 0` is "one undifferentiated stream", never a valid id**, and must not be
  > reported as the dominant stream while a real one exists — it is what the writer publishes when
  > it could not identify the swapchain.
  >
  > **A gap within a stream cannot be detected from `frameIndex`.** `dllmain.cpp` assigns
  > `g_frameIndex++` once per accepted present for the WHOLE PROCESS, four lines before it assigns
  > `swapchainId`, so within one stream of an interleaved pair consecutive records' indices differ
  > by however many streams are running. An interval rule keyed on "index advanced by exactly one"
  > excludes *every* interval in any multi-swapchain title. Gaps come from the drain's own
  > accounting (`07_IPC` §Protocol rules), which is where they are known.

Tier 2 has none of this: `upscaler = unknown`, ratio `N/A`.

## RT / PT / RR — evidence-based tri-state

Tri-state `Yes | No | N/A` per session with `source` (`measured | manual | inherited`).

| Flag | Tier-1 evidence | Result |
|---|---|---|
| **Ray Tracing** | `BuildRaytracingAccelerationStructure` called, **or** `DispatchRays` called, in ≥ 5% of frames | `Yes` |
| | **All three of:** `FlWriterState.rtTier ≥ 10` (an RT-capable device — `rtTier` is `FlRtTier`, and it has **three** states, not two: `0` *not queried*, `1` *queried and this device cannot*, and otherwise `D3D12_FEATURE_D3D12_OPTIONS5`'s own tier value, which is already ×10); `hooksInstalledMask` contains **`RtAsBuild`**; and no AS builds and no dispatches for the whole session | `No` |
| | No RT-capable API in use (D3D11/OpenGL), or evidence inconclusive | `N/A` |
| **Ray Reconstruction** | NGX `RayReconstruction` feature created **and evaluated** (or Streamline `DLSS_RR`) | `Yes` / `No` if DLSS is active without it |
| **Path Tracing** | heuristic only — see below | usually `N/A` |

**Honest limits, documented in the UI tooltip:**

- Hooking `BuildRaytracingAccelerationStructure` is what makes **inline ray tracing (DXR 1.1 `RayQuery`)** detectable at all — those shaders never call `DispatchRays`, so dispatch counting alone would report `No` for a game that is very much ray tracing. AS-build activity catches both paths. This is why both hooks exist.
- **Ray Reconstruction is decided over the presents that DRAINED a Streamline batch, not over
  every present, and the difference was the whole answer on a frame-generating title.** The
  writer sets `FL_FEAT_RAY_RECONSTRUCTION_OBSERVED` under `seen != 0` — deliberately, so an
  NGX-direct title running DLSS-RR does not collect a fabricated `No` — which at ×4 is roughly
  one present in four. A consumer that required the bit on *every* record was therefore asking
  for something that cannot hold above ×1, and reported `N/A` about a title that answered the
  question 2,523 times out of 2,523. The population is the batch-carrying presents: none of them
  ⇒ `N/A` (nothing looked, and the lazy-install prefix drops out with it), any of them carrying
  the fact bit ⇒ `Yes`, batches with none ⇒ `No`. **This is also the one RR negative that may be
  aggregated**, for the same reason `FL_UPSCALER_NONE` is: a hook ran and saw the alternative.
- **The row above says NGX and the producer is Streamline-only.** `nvngx_dlssd.dll` is a
  *static hint* (`05_DETECTION`) and never a runtime fact; the NGX runtime route is licence-
  blocked (`18_GPU_VENDOR_APIS` §Checklist step 3 forbids vendoring the RTX SDKs headers **and**
  forbids re-declaring them), so an NGX-direct title yields no batch at all and `N/A` is the
  true answer there rather than a coverage excuse.
- **Why `rtTier` has a third state, recorded because the two-state version was written down
  first and was wrong.** `D3D12_RAYTRACING_TIER_NOT_SUPPORTED` is **0**, and `rtTier`'s 0
  already meant *not queried* — so a writer that stored the vendor enum verbatim would have
  published "nobody looked" about every machine without an RT-capable GPU. `FlRtTier`
  substitutes `1` for that one value. Both still fail `≥ 10` and both still yield `N/A`, so
  the table above is unchanged; what changes is that the two are now distinguishable, which
  is what lets a future reader tell a capability gap from a coverage gap.
- **The `No` branch needs all three conjuncts, and the second is the one that is easy to drop.** The AS-build hook is what makes inline `RayQuery` visible; a writer that installed only `DispatchRays` sees nothing on a RayQuery-only title, and its silence is indistinguishable from a real negative. Requiring `RtAsBuild` to have been *installed* — not merely for RT to have been "measured" — is what stops that becoming a confident `No` about a title that ray-traces every frame. Where any conjunct fails the answer is `N/A`.
- **Path tracing has no API-level signature.** The heuristic combines three inputs: rays dispatched per output pixel ≥ ~1.0, `MaxTraceRecursionDepth` from the RT PSO config, and the number of distinct RT state objects (`FlWriterState.rtStateObjectsCreated`). It produces a **confidence score**, and only ≥ 0.8 offers a *suggestion* in the UI ("looks like path tracing — confirm?"). It never sets `Yes` on its own. Manual override remains the authoritative path, per game, inherited by future sessions.

  > **A fourth input — "the ratio of RT to raster work" — was listed here and is removed, 2026-08-05.** It has no cheap denominator: counting raster work means a per-draw hook, which is a hot-path cost this project will not pay, and `§H6` records that a command-list count measures *recorded* rather than *executed* work anyway. `rays_per_pixel` already carries the intent. Removing an input weakens a score that may only ever *suggest*; it cannot produce a fabrication, which is why it is a removal and not a blocker. `FlWriterState.rasterPsoCreated` is kept as the cheapest available proxy should anyone revisit it.
- A game can also enable RT for a subset of effects only; `Yes` means "rays were traced", not "everything is ray traced". The UI says so.

Derived extras (Tier 1): `rays_per_pixel` (mean dispatch volume ÷ output pixels), `rt_frame_pct` (share of frames with RT activity), `rt_pso_count`.

## Per-process VRAM (Tier 1)

`vramUsedMb` from `IDXGIAdapter3::QueryVideoMemoryInfo(LOCAL)` inside the game = **this game's** usage and budget. **MiB, truncating, and it must use the same divisor as `vramBudgetMb`** — the two are compared, and mismatched rounding would put a systematic bias into `budget_exceeded_pct`. Residual: a flip within 1 MiB of the budget, 0.004% of a 24 GiB card. Stored as its own series and clearly labelled apart from the adapter-wide figure from `18_GPU_VENDOR_APIS`. Aggregates: avg, max, and `budget_exceeded_pct` (share of samples where `CurrentUsage > Budget`, i.e. the driver was likely evicting — a genuinely useful stutter explanation).

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

## Export schema (per-frame CSV, FR-9.1)

`frame_index,qpc_ms,frametime_ms,native_or_generated,render_w,render_h,output_w,output_h,upscaler,upscaler_quality,fg_mode,rt_flags,dispatch_rays,pso_created,vram_mb,reflex_latency_us`

**Every column above must have a stored source.** The export is written from
`frame_blobs` after the session ends, not from the live ring, so a column is
only exportable if the finalize step (`04_CAPTURE` §Finalizing) persisted it.
The blob set in `06_DATA_MODEL` is defined to cover exactly this list — when
adding a column here, add its series there in the same change, or the exporter
will emit a column it cannot fill.

Per-column sources, and what a *Tier-2* export does instead:

| Column | Tier-1 source | Tier 2 |
|---|---|---|
| `frame_index`, `qpc_ms`, `frametime_ms` | `frametimes` blob + session `qpcEpoch` | from CSV |
| `native_or_generated` | `frame_flags` generated bit — set where `fgEvaluations == 0`, i.e. the presents that carried **no** application-frame evaluation. Inverted from the pre-2026-08-14 reading, and accurate to one frame per the note in §Frame Generation | `FrameType` (2.x) |
| `render_w/h`, `output_w/h` | `render_res` blob — **two `uint16` pairs per frame**, not one; `ResizeBuffers` is hooked precisely because output resolution changes mid-session | `N/A` |
| `upscaler`, `upscaler_quality`, `fg_mode` | segment table, joined by frame index | `N/A` |
| `rt_flags` | `rt_flags` blob, **one byte per frame** preserving all three bits (`asBuildObserved`, `dispatchObserved`, `psoCreatedEver`) — collapsing them to a single "rt-active" bit loses the inline-RayQuery distinction this project exists to measure. The third was `rtPsoAlive` until layout v3 renamed it: creation is observed at `CreateStateObject` and destruction is COM `Release`, which is not in the hook inventory and must not be added, so the bit latches and could only ever mean "created ever" | `N/A` |
| `dispatch_rays` | `dispatch_rays` blob (`uint32[]`, the volume) | `N/A` |
| `pso_created` | `pso_created` blob (`uint16[]`, the **count**, not a flag) | `N/A` |
| `vram_mb` | `vram_proc` per-frame blob. Note the value is refreshed at 1 Hz (`17_HOOK_ENGINE` §Memory) so it is a held sample, not a per-frame measurement — the header block says so | `N/A` |
| `reflex_latency_us` | `latency_us` blob; **finalize must write it** | `N/A` |

Plus a `#`-prefixed header block: game, date, **capture tier**, api, present mode, hardware snapshot, segment list, the tri-state flags with their sources, and the note that `vram_mb` is 1 Hz-sampled. The tier belongs in the header because a Tier-2 export is missing whole columns and anyone reading it later must know why.
