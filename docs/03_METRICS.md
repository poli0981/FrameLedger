# 03 — Metrics

**Single source of truth** for every number FrameLedger displays. Implement in `FrameLedger.Domain.Metrics` with golden tests (`14_TESTING`).

Every metric declares which **capture tier** it requires (`01_ARCHITECTURE` §Capture tiers). A Tier-2 session simply has `N/A` where Tier-1 data is missing — never a silently degraded estimate presented as fact.

## Inputs

**Tier 1** — `FlFrameRecord` stream from the ring (`17_HOOK_ENGINE` §Ring writer), which is the authoritative field list. Every field is consumed **except the two protocol fields** `seq` and `reserved`, which are named here so "every field is consumed" does not have to be read as a promise about them: `qpc`, `frameIndex`, `presentFlags`, `syncInterval`, `renderW/H`, `outputW/H`, `api`, `upscaler`, `upscalerQuality`, `upscalerSharpness`, `fgMode`, `fgEvaluations`, `rtFlags`, `featureFlags`, `dispatchRaysVolume`, `maxTraceRecursionDepth`, `psoCreatedThisFrame`, `vramUsedMb`, `reflexLatencyUs`, `colorSpace`, `measuredMask`, `swapchainId`. Plus, from `FlWriterState`: `vramBudgetMb` for `budget_exceeded_pct`, `rtTier` and `hooksInstalledMask` for the RT tri-state, and `rtStateObjectsCreated` / `rasterPsoCreated` for `pt_confidence`.

> **Renamed in layout v3 (2026-08-05):** `vramUsedBytes` → `vramUsedMb` (MiB, matching the `vramBudgetMb` it is compared against and the `vram_mb` this document already exported), and `hdr` → `colorSpace` (a bool had no third state). `measuredMask` is 16-bit.

~~**Tier 2** — PresentMon CSV.~~ **THERE IS NO TIER-2 INPUT, 2026-08-28.** PresentMon was dropped (§S31 row P2) and no mechanism replaced it (§G), so Tier 2 produces no frames and parses nothing. Everything below that is marked tier `2` is Tier-1-only until a mechanism exists. The version-pinning and header-map reasoning this line carried is preserved in `spike-notes` §11 and `20_OPEN_QUESTIONS` §M2, where the measurements that retired it also live.

**Both** — 1 Hz `GpuSample` (`18_GPU_VENDOR_APIS`) + optional CPU sample.

## Frame times

Frame time `ft[i] = (qpc[i] - qpc[i-1]) / qpcFreq × 1000` ms, measured at **present entry** (Tier 1) — consistent, low-jitter, and unaffected by driver-side frame pacing.

Let `D` = session duration (first → last present), `F_app` = frames the *game* presented, `F_disp` = frames actually shown.

## Core definitions

| Metric | Definition | Tier |
|---|---|---|
| **Native FPS (avg)** | `count(F_app) / D` | 1 |
| **Displayed FPS (avg)** | `count(F_disp) / D` | 1 |
| **Presented FPS (avg)** | `presents / D` — **numerically Displayed FPS; the name for the one number that stands alone.** "Native" and "Displayed" are printed only *together* (rule 6); when frame generation is not measured, this is the headline, with a mandatory qualifier (§Frame Generation, rung 0's qualifier) | 1 |
| **FG factor** | `DisplayedFPS / NativeFPS`, shown `×N.N`; `—` when FG inactive; **`N/A` when FG is not measured** | 1 |
| **Avg FPS** (headline) | = Native FPS when `fg_mode` is measured (`api` rung) or `none`; **= Presented FPS, labelled, when `fg_mode` is `N/A`**. Time-based, **not** the mean of instantaneous FPS values | 1 |
| **Median FPS** | `1000 / p50(ft_app)` | 1 |
| **1% Low** | `1000 / p99(ft_app)` | 1 |
| **0.1% Low** | `1000 / p99.9(ft_app)` | 1 |
| **Min / Max FPS** | `1000 / max(ft_app)` / `1000 / min(ft_app)` | 1 |
| **Frametime σ** | population stddev of `ft_app` (ms) | 1 |
| **Stutter count** | `ft_app[i] > 2 × rollingMedian(ft_app, 19)` (centered, window in **frames**) | 1 |
| **Stutter time %** | `Σ ft_app[stutter] / Σ ft_app × 100` | 1 |
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

> **When frame generation is NOT measured, the lows are taken over presents and labelled
> `(presented)`** — added 2026-09-03 with Presented FPS. On a title with no in-process frame
> generation that is exactly the application-frame low; on one that generates frames through a
> path this writer does not hook it is smoother than the truth, which is why the label stays on
> until `fg_mode` is measured or `none`. An unlabelled low is a claim about application frames,
> and a present-only writer cannot make it.

**Sufficiency guards (FR-4.8):** 0.1% Low needs ≥ 10,000 application frames, 1% Low ≥ 1,000; otherwise `N/A`.

## Frame Generation — ground truth (Tier 1)

This is the metric the rewrite exists for. Resolution ladder, highest confidence first; the winning rung is stored in `fg_source`:

1. **`api` (authoritative).** An NGX `FrameGeneration` feature (or Streamline `DLSS_G`, or an FFX frame-interpolation context, or `xess_fg`) was **created and evaluated this frame** → FG is on, and we know *which* technology by name, from the vendor's own API call. This is a fact, not an inference.
2. ~~**`etw`** (Tier 2).~~ **RUNG REMOVED 2026-08-28.** It read *"PresentMon 2.x `FrameType` column reports generated frames directly"*; §S31 measured that column classifying every frame of a ×4 capture as an application frame, PresentMon was retired, and then dropped. **There is no Tier-2 rung and no Tier 2 to put one on.** The ladder is now rungs 0, 1, 3 and 4 — renumbering them would break every reference in this file and in `fl_shm.h`'s `fg_source`, so the gap is left visible instead.
3. **`cadence`** (last resort, ~~both tiers~~ **Tier 1 only** — Tier 2 has no frames to find a cadence in). Sustained `Displayed/Native ≥ 1.5` → `Detected (unknown)`.
4. Otherwise `fg_mode = none`, factor `—`. **Reachable since 2026-09-04 by counting**: a
   published `presents / tokens` at or below 1.05 over a uniform window (§The source, below).
   Never from a hook that merely saw nothing (rung 0), and never from the census.

> **RUNG 2 IS CONDITIONAL ON THE VENDOR, measured 2026-08-20, and this list read as
> though it were not.** `--track_frame_type` is a **beta** option in PresentMon 2.5.1
> and its own help says it *"requires application and/or driver instrumentation using
> Intel-PresentMon provider"*. So `FrameType` is a report of events an application or
> a graphics driver chose to emit through Intel's provider — **not** a classification
> of any present from first principles. **Which vendors instrument it is unmeasured —
> including Intel's own.** It is Intel's provider and Intel ships XeFG, so the obvious
> reading is that XeFG is covered; that reading is not a measurement, and this block
> exists precisely because the rung was written as though availability were settled.
> The one that decides P0 is NVIDIA's DLSS-G driver, and it is unmeasured too.
>
> **The parser must therefore report a capability loss rather than fall through to
> rung 4.** §Inputs already says so about a missing `FrameType` column, and the same
> now applies to a column that is present and classifies nothing: every row spelled
> `Application` while frame generation is on is an ABSENCE, and rung 0 turns an
> absence into `N/A`. A rung-4 `none` there would be the affirmative negative this
> whole ladder exists to prevent, reached from a new direction.
>
> §S31 carries the pre-committed decision table, including the two rows that retire
> the rung outright. `tools/frametype-oracle.ps1` **was** what produced the input
> — deleted 2026-08-27 with the tool it parsed — and
> also the reason `spike-notes` §11 now records that the console binary will not run
> unelevated on the dev box at all.
>
> ### 🔴 MEASURED 2026-08-27, AND THE RUNG IS NARROWED: NVIDIA DLSS-G IS NOT COVERED
>
> §S31 ran — three legs, three game launches, Cyberpunk 2077 at off / ×2 / ×4 — and landed
> on row **P2**. `FrameType` is present and **every row of all three legs reads
> `Application`**: 1,937 / 6,488 / 10,881 rows, no other value anywhere. Meanwhile the two
> instruments agree on the present rate to within 0.3% per leg, so PresentMon SAW the
> generated presents — at ×4 roughly three in four cannot be application frames — and
> classified all of them as application frames.
>
> **So for NVIDIA frame generation this rung yields nothing on this driver and this
> build.** It is not "unreliable" here; it produces an ABSENCE, and rung 0 turns an
> absence into `N/A`. A consumer must never reach rung 4 from it.
>
> **What is still unmeasured is WHICH vendors it does cover.** Intel's own remains
> untested despite being the obvious reading — the reading this document was already
> corrected once for asserting — and AMD is untestable on the only machine that exists
> (§R5/§R6). The honest scope of rung 2 today is therefore: **no vendor is known to
> instrument this provider, and one is now known NOT to.**
>
> One sub-question does not change the above and is recorded in §S31 rather than here:
> whether `--track_frame_type` was in effect at all. If the column ships by default the
> legs measured a default rather than an absence. Either way the rung produced no
> classification, which is why §S31 landed on P2 without waiting for it.

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

> ### Rung 0's qualifier: the runtime census (2026-09-03, owner decision)
>
> **The problem rung 0 left.** With this writer, rung 0 is where *every* title lands: no
> non-Streamline title sets `FL_MEASURED_FG` at all, and on every Streamline title measured
> `Σ fgEvaluations` is 0. So a 2D title with no upscaler and Black Myth: Wukong running
> DLSS-G through a path this writer does not hook printed the *same* line — "FG factor not
> measured" — and the reader had no way to know that the first number was a frame rate and
> the second might be twice one.
>
> **The census.** `FlWriterState.runtimeCensus` (`fl_shm.h` §FlRuntimeCensus) is taken on the
> Overlay's watchdog once a second: for each module name in `FL_RUNTIME_CENSUS`, ask the loader
> whether it is present. OR-only, so monotonic; `FL_CENSUS_RAN` says the census ran at all.
> The names are the measured ones in `vendor-exports.json`, gated by `hookinventory-check`
> Pass D. **It is not a hook and not a measurement**: a set bit says a module of that name was
> loaded, a clear bit says the loader had none of that name.
>
> **What it may say.** The line under Presented FPS, one of three:
>
> | Census | Line |
> |---|---|
> | did not run | *not measured, and the census did not run — whether presents include generated frames is unknown* |
> | ran, no FG family bit | *no known frame-generation runtime was loaded, so this number **cannot** include in-process generated frames (statically linked FSR3-FG and driver-level AFMF are outside what this can see)* |
> | ran, FG family bit(s) | ***WARNING**: a frame-generation runtime was loaded (`nvngx_dlssg.dll`) and no evaluation was observed — this number **may** include generated frames; read it as Displayed, not Native* |
>
> and, for the upscaler, the difference between *no hook ran and no runtime was loaded*
> and *a runtime is loaded and no hook installed* (the Streamline 1.5.6 case).
>
> A module that MAY generate frames counts as a frame-generation runtime: `amd_fidelityfx_dx12.dll`,
> the FSR 3.1 facade, dispatches both upscaling and frame generation behind one export set (H11), and
> Lies of P ships it alone while generating frames — so it warns rather than reassures.
>
> **What it may NOT say, and this is the whole design: the census never produces `none`.**
> `FL_FG_NONE` and `FL_UPSCALER_NONE` mean "a hook ran and saw the alternative" — the only
> negative this document lets a consumer aggregate. A census-derived `none` has two holes,
> both structural: **FSR2/FSR3 are routinely linked statically**, so an FSR3-FG title has no
> module for the census to see while its proxy swapchain still calls the real `Present`,
> and a `none` there would publish presents as native — the inflated number rule 6 forbids —
> with a confident label on it; and driver-level or out-of-process generation (AFMF,
> Lossless Scaling) is invisible to any in-process list. The census may narrow an N/A's
> reason and may raise a warning. It may not close the question.
>
> **And the other direction is just as closed.** *Streamline loaded + zero `slEvaluateFeature`
> calls* does not mean *upscaling off*: Wukong loads `sl.interposer.dll`, runs DLSS-G, and
> never calls it. So a title with upscaling switched off in its settings and a title driving
> DLSS through an unhooked path are indistinguishable to this writer, and the upscaler line
> now says so — three causes, named, rather than "our coverage is short".
> **Three of five titles now** (2026-09-03): Hell Is Us with DLSS on and frame generation ×4
> drained zero batches — `spike-notes` §9.

### Counting native vs displayed frames at Tier 1

Application-generated and FG-generated frames both go out through the swapchain
we hooked, so **our present hook sees them all** — the present count alone is
`F_disp`, not `F_app`. The separation comes from the FG feature itself:

```
F_disp  = presents observed by the hook
F_app   = Σ fgEvaluations                  (APPLICATION frames, counted at the source)
```

> ### The source is `slGetNewFrameToken` — decided 2026-09-03, HANDOFF item 3's producer
>
> Streamline hands a title **one frame token per application frame**, and the title has to ask
> for it: `slGetNewFrameToken` (`sl_core_api.h`: *"obtain unique instance"* per frame; every
> `slSetTag` / `slEvaluateFeature` takes the token). The Overlay detours that export —
> module-scoped, lazily installed, an inventory row like the other two — and `fgEvaluations`
> is the number of **new frame indices** — above every index seen before — the tokens handed out
> between two presents carried. *New indices, not distinct objects and not "different from the
> last":* measured 2026-09-03, Cyberpunk asks for a token three to four-and-a-half times per frame,
> from more than one thread, and receives a different object each time (§S31 row P4). `fg_factor = presents
> / Σ tokens` therefore needs **no premise about Ray Reconstruction batches** and works on the
> titles where `slEvaluateFeature` is never called at all (Wukong, Rune Factory, Hell Is Us),
> which the previous producer could not reach on five titles out of five.
>
> **The one premise it carried, and how it was retired.** From inside the process, *"no frames
> were generated"* and *"the DLSS-G plugin requested a token for every frame it generated"*
> are the same ratio: 1.0. So until the owner's run landed, `FgWindow` refused any ratio below
> the cadence threshold. **It landed on row P1 on 2026-09-04** — Cyberpunk 2077 off / ×3 / ×4
> at **1.00 / 2.99 / 3.99**, Hell Is Us ×4 at **4.00** — which excludes the second explanation
> on the title that would have shown it. **Rung 4's `none` is therefore reachable by
> counting:** a published factor ≤ 1.05 (`FgWindow.NoneCeiling`) is the measured statement
> that every present carried an application frame. A factor ≥ 1.5 (`ActiveThreshold`, the
> cadence rung) is active; the band between is refused as a configuration no vendor ships.
> And `tokens/batch = 1.00` on every leg with batches answered §S31's own question — a drained
> Streamline batch is an application frame on that title — with a second count rather than an
> oracle.
>
> **What `fgMode` still is.** Identity — `DLSS_G` when a `kFeatureDLSS_G` evaluation drained,
> `UNKNOWN` otherwise — from the evaluate detour, unchanged. A factor ≥ 1.5 with `UNKNOWN`
> identity is *frame generation active, technology not identified*, which on a UE5 title with
> both Streamline and FidelityFX loaded is the honest answer.

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
`FrameType` can see it at Tier 2 ~~is a P0 question~~ — **closed by decision: there is no Tier 2 to see it at** (§M1, §G).

`fg_factor` is **always** the measured ratio `F_disp / F_app` regardless of which
rung identified the mode — only the *identification* comes from the ladder.

**Display rule (product requirement, CLAUDE.md rule 6):** wherever FPS appears and FG is active, render `"{native} → {displayed} FPS (×{factor} FG)"`. Cards show Native large, Displayed + factor secondary. Charts default to Native with a Displayed toggle. Exports contain all three.

**And when FG is not measured (2026-09-03):** render the one number as **Presented FPS** —
`144 FPS` with a qualifier chip chosen by the census above, never the word "Native", never
a factor, never `—` (which means `none`, a measured negative). The chip is muted when no
frame-generation runtime was loaded and a warning when one was. `08_UI` §FPS display rule
carries the three shapes.

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

> **The denominator, decided 2026-08-20 with the PR that wrote the hooks, because
> `§Counting native vs displayed` left it open and the hooks could not land without one.**
>
> **`rt_frame_pct` is a share of PRESENTS, and it says so.** Under frame generation that
> dilutes it by the FG factor — at ×4 a title that ray-traces every application frame reports
> ~25%. **The tri-state is unaffected**, and that is the whole reason this choice is safe: the
> `Yes` gate is *≥ 5% of frames*, and 25% clears it by a factor of five. The number that is
> diluted is the reported percentage, not the verdict, and the label carries its unit.
>
> **`rays_per_pixel` is taken over the RT-ACTIVE presents, not over all of them**, and is
> therefore undiluted: the writer drains its accumulator at every present, so a frame's whole
> dispatch volume lands on one present, and generated presents contribute zero volume and are
> not in the denominator. This needs **no application-frame count** — which is fortunate,
> because `HANDOFF` item 3 could not produce one.
>
> **The falsifier, written before the run rather than after it:** on a ×4 capture,
> `rt_frame_pct` should read ≈ 25% and not ≈ 100%. If it reads ≈ 100%, generated presents ARE
> carrying recorded work, the premise above is wrong, and `rays_per_pixel` over RT-active
> presents is wrong with it. Record the number and say so rather than reinterpreting it.
>
> **The unit is RECORDED, not executed** (§H6). Both hooks sit on command-list methods, so
> they count work *recorded* between two presents — possibly on several threads, possibly
> re-executed, possibly never executed at all. Aggregates derived from them inherit that word.
> The concurrency model is relaxed atomics written by any recording thread and drained once
> per present with `exchange(0)`, the same shape `g_slSeen` already uses.
>
> **`dispatchRaysVolume` saturates at `UINT32_MAX` rather than wrapping**, because it is the
> NUMERATOR of `rays_per_pixel` and a wrapped value under-reports exactly the titles the
> path-tracing heuristic exists to recognise. A consumer seeing the ceiling must refuse to
> publish `rays_per_pixel` rather than divide by a floor. This is the mirror of
> `fgEvaluations`' 255, which saturates for the opposite reason — that one is a denominator.
>
> **And `FL_MEASURED_RT` says a hook family was live, not that all three of its fields have
> producers.** Which of `rtFlags`, `dispatchRaysVolume` and `maxTraceRecursionDepth` may be
> read comes from `hooksInstalledMask`, exactly as `FL_MEASURED_UPSCALER` and
> `FL_MEASURED_UPSCALER_PARAMS` split the upscaler's identity from its parameters. As of
> 2026-08-20 `maxTraceRecursionDepth` has no producer: `CreateStateObject` is a separate PR,
> and `pt_confidence` reads it, which is one of the reasons Path Tracing is still `N/A`.

## Per-process VRAM (Tier 1)

`vramUsedMb` from `IDXGIAdapter3::QueryVideoMemoryInfo(LOCAL)` inside the game = **this game's** usage and budget. **MiB, truncating, and it must use the same divisor as `vramBudgetMb`** — the two are compared, and mismatched rounding would put a systematic bias into `budget_exceeded_pct`. Residual: a flip within 1 MiB of the budget, 0.004% of a 24 GiB card. Stored as its own series and clearly labelled apart from the adapter-wide figure from `18_GPU_VENDOR_APIS`. Aggregates: avg, max, and `budget_exceeded_pct` (share of samples where `CurrentUsage > Budget`, i.e. the driver was likely evicting — a genuinely useful stutter explanation).

## Sensor aggregates

Per session over 1 Hz samples: `avg` (mean of non-null), `max`. Sensor timeline aligned to the frame timeline via the shared QPC epoch captured at session start. Fields with no data are `N/A`, never 0.

## Accuracy budget (shown in Help → About metrics)

| Quantity | Tier 1 | Tier 2 |
|---|---|---|
| Frame times / FPS | < 0.05% (direct QPC at the present call) | **not available** |
| Native vs Displayed / FG factor | exact counts when rung 1 resolves; **`N/A` otherwise, with the runtime census qualifying the presented figure** — the census cannot see a statically linked FSR or any out-of-process generation, so its "cannot include generated frames" is bounded by those two holes and says so | **not available** |
| Upscaler + render resolution | **exact** (vendor API arguments) | not available |
| RT active | measured per frame | not available |
| Path tracing | heuristic, confidence-scored, never asserted | not available |
| Per-process VRAM | exact | not available |
| PC latency | as reported by Reflex | not available |
| GPU temp / load / power | vendor API accuracy, ±1 s sampling | same |
| CPU temperature | sensor-inherent ±1–2 °C, needs LHM + PawnIO + elevation | same |

> **The Tier-2 column is now "not available" for everything except sensors, and that is the whole change.** It previously claimed frame times within 0.1% without injection. Nothing produces them.

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
| `frame_index`, `qpc_ms`, `frametime_ms` | `frametimes` blob + session `qpcEpoch` | ~~from CSV~~ — no Tier-2 CSV exists; a Tier-2 session exports no per-frame rows at all |
| `native_or_generated` | `frame_flags` generated bit — set where `fgEvaluations == 0`, i.e. the presents that carried **no** application-frame evaluation. Inverted from the pre-2026-08-14 reading, and accurate to one frame per the note in §Frame Generation | `FrameType` (2.x) |
| `render_w/h`, `output_w/h` | `render_res` blob — **two `uint16` pairs per frame**, not one; `ResizeBuffers` is hooked precisely because output resolution changes mid-session | `N/A` |
| `upscaler`, `upscaler_quality`, `fg_mode` | segment table, joined by frame index | `N/A` |
| `rt_flags` | `rt_flags` blob, **one byte per frame** preserving all three bits (`asBuildObserved`, `dispatchObserved`, `psoCreatedEver`) — collapsing them to a single "rt-active" bit loses the inline-RayQuery distinction this project exists to measure. The third was `rtPsoAlive` until layout v3 renamed it: creation is observed at `CreateStateObject` and destruction is COM `Release`, which is not in the hook inventory and must not be added, so the bit latches and could only ever mean "created ever" | `N/A` |
| `dispatch_rays` | `dispatch_rays` blob (`uint32[]`, the volume) | `N/A` |
| `pso_created` | `pso_created` blob (`uint16[]`, the **count**, not a flag) | `N/A` |
| `vram_mb` | `vram_proc` per-frame blob. Note the value is refreshed at 1 Hz (`17_HOOK_ENGINE` §Memory) so it is a held sample, not a per-frame measurement — the header block says so | `N/A` |
| `reflex_latency_us` | `latency_us` blob; **finalize must write it** | `N/A` |

Plus a `#`-prefixed header block: game, date, **capture tier**, api, present mode, hardware snapshot, segment list, the tri-state flags with their sources, and the note that `vram_mb` is 1 Hz-sampled. The tier belongs in the header because a Tier-2 export is missing whole columns and anyone reading it later must know why.
