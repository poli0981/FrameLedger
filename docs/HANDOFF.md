# HANDOFF — read this first, then stop reading it

The one file a new session opens to pick up P0. It carries **sequencing, decisions
and traps**. It deliberately carries **no status**.

> ## The rule this file lives under
>
> **This file must never restate what another document already states.** Four
> documents disagreeing about the gate's composition is what §S23-4 and §S23-5 were
> raised for, and `rules-validate` now fails a fifth copy in the shipped data. A
> handoff that summarises status becomes the next stale copy within a day — that has
> already happened three times here (#45's note, then #46–#52; `15_ROADMAP`'s status
> block; `spike-notes` §8 correcting a stale claim and going stale in the same file).
>
> So: **status lives where it lives.** Read it there.
>
> | Question | File |
> |---|---|
> | What is unresolved and what does it block? | `docs/20_OPEN_QUESTIONS.md` — §S24 is the index; the ✅/◐/❓/🔴/🅓 markers are load-bearing |
> | What was *measured*, on what machine? | `docs/spike-notes.md` |
> | Which P0 item is where? | `docs/15_ROADMAP.md` |
> | What landed? | `CHANGELOG.md` |
> | What does the software promise users? | `README.md`, `legal/DISCLAIMER.md` — both carry accuracy blocks |
>
> **And verify a status claim against the code before planning on it.** Four times now
> those files have asserted in the present tense something that had been closed, or
> was never true. The 2026-08-05 session found the ledger stale across seven PRs and
> two documents contradicting themselves *inside one section*.

---

## Where P0 actually stands

Two exit criteria remain (`spike-notes.md` §Exit criteria). Do not re-derive them
from memory:

1. **A throwaway build records a real session** from a real offline game reporting
   *correct* upscaler, quality preset, render→output resolution, FG factor and RT
   state, verified against the game's own settings menu.
2. **Every S-series item resolved, or explicitly deferred with a written rationale.**

Criterion 2's count is in §S24's summary and is kept current there. Criterion 1 needs
the feature hooks below.

> **What changed on 2026-08-06, in one sentence, because it changes the shape of
> criterion 1 rather than its content:** there is now a path from a consent record to a
> drained session, driven by a real binary, so "a throwaway build records a real
> session" needs feature hooks **and nothing else**. The queue's item 1 is struck
> below for that reason.

---

## The queue, in dependency order

Each entry names the acceptance criterion and **what makes it fail on unmodified
`main`** — because a criterion already true on `main` is decoration, and this project
has shipped three of those.

> ### Before you start: two things about this machine, neither of them the code
>
> 1. ~~**`./build.ps1 check` cannot go fully green here.** `fl_d3d12_acquisition` and
>    `fl_guard`'s D3D12 case fail because WARP's D3D12 path is broken on this Windows
>    Insider build — measured, persistent across a reboot, and detailed in §Traps.~~
>    **NO LONGER TRUE, measured 2026-08-20: `./build.ps1 check` is fully green here, 20/20
>    native tests.** `hook-harness --probe-d3d12` succeeds and `D3D12CreateDevice` on the WARP
>    adapter returns `S_OK`. **Nothing in this repository fixed it** — the machine moved from
>    Insider build **26300/29639** to **29648**, and `d3d10warp.dll` / `D3D12Core.dll` are both
>    `10.0.29648.1000`, so the Insider-build regression §Traps describes was fixed upstream.
>    Recorded with both build numbers because "it works now" without them is the same
>    unfalsifiable claim as the untested remedy §Traps already records. **Check it on your own
>    build before planning around either state.**
> 2. **The managed suite is green and was stabilised the hard way.** Ten consecutive full
>    runs at the time of writing. Five assertions in `ShmDrainIntegrationTests` /
>    `CaptureHostEndToEndTests` had budgets sized on the harness's measured rate or read a
>    state once; #61 and #62 fixed them, and §Traps records the shape so the next one is
>    recognised rather than re-derived.

### ~~1. Consent store + the first production driver of the guard loop~~ — LANDED 2026-08-06

**Do not start here.** The whole of this entry is built: `IGameConsentStore` with a
file-backed adapter, `FrameLedger.CaptureHost` driving `HookedCaptureGate` →
`FlGuardedInject` → `ShmRingReader.TryAttach` → a 10 Hz drain with
`GuardSupervisor.ScanOnceAsync` and `PublishGuardResult`, §S29(c) closed by deleting
`ShouldUnhookAsync`, §S29(e) closed with a held process handle, the throwaway consumer,
and all three test gaps. **The head of the queue is item 2.**

What it does *not* move: items 4, 6 and 7 still need feature hooks. The writer still
records `measuredMask = FL_MEASURED_OUTPUT_RES | FL_MEASURED_PRESENT_ARGS` and nothing
else, and the consumer reports `N/A` for upscaler, FG and RT because that is what the
data says. What it moves is the *path*: there is now a route from a consent record to a
drained session, so exit criterion 1's throwaway build needs hooks and nothing else.

Status lives where it lives — `CHANGELOG.md` and §S24 — and this entry is struck rather
than deleted because **the sequencing claim is what this file is allowed to carry, and
the sequencing changed.** Leaving it queued would tell the next session to build what
already exists, which is the failure this file's own rule exists to prevent, one level
up: it went stale by *not* being touched.

Two things from it are still open and are the owner's:

- **Six decisions surfaced by building it**, listed in §S24 — who may clear
  `hook_blocked_reason`; whether the operator acknowledgement's wording is reviewed like
  a `Safety_*` string; whether FR-2.4's kill switch is an input to `HookedCaptureGate`;
  whether FR-11 gates consent; whether §S18 blocker 3 is re-ratified now that a third
  project imports the guard; and the disclosure/wording version, which was **decided and
  implemented** because it cannot be retrofitted — reversing it is still the owner's.
- **`tools/package-closure-check.ps1` reads `ProjectReference` only.** It is what keeps
  §S27 closed, and it does not follow `<Import>` or `Directory.Build.props`, so a
  `.targets` file that stages a foreign binary into a publish root's output is outside
  what it sees. Stated here because the next person to widen the packaging story needs
  to know the shape of the hole rather than discovering it.

<details>
<summary>The original entry, kept for the reasoning it carries</summary>

The layer under everything else, and the answer to §S27.

- `IGameConsentStore` (Application port) + a file-backed implementation. This gives
  `HookedCaptureGate`'s three inputs — `hook_enabled`, `hook_consent_at`,
  `hook_blocked_reason` — a **real source** for the first time. §S27 rejects
  *synthesising* them; a record written by an explicit user action is not synthesis.
- A capture host driving `HookedCaptureGate` → `FlGuardedInject` →
  `ShmRingReader.TryAttach` → ~10 Hz drain + `GuardSupervisor.ScanOnceAsync` +
  `PublishGuardResult`. **A separate project, not `FrameLedger.Agent`**: `12_BUILD`
  publishes only `FrameLedger.App` and `FrameLedger.Agent`, so a project neither
  references is outside the package *by construction* rather than by a flag. Stage the
  Overlay and guard beside it for §S22, as `FrameLedger.DrainFixtures.targets` already
  does for the integration test.
- **This is the first production driver of `guardTicks`** — the *sending* half of the
  30 s re-scan that `19_SAFETY` calls the most important runtime behaviour and that
  `README` already promises. Both the write path (`ShmRingReader.PublishGuardResult`)
  and the read path (the Overlay watchdog) exist and are tested; only the loop is
  missing. **A missing loop reads as a missing subsystem — say "loop".**
- **Close `HookedCaptureGate.ShouldUnhookAsync`** (§S29(c)). It is a *second*
  in-session re-scan path that publishes no tick and does not latch — the two
  properties `GuardSupervisor` exists to guarantee — it is unit-tested so it reads as
  sanctioned, and it sits on the object this loop will already be holding. Route it
  through `GuardSupervisor` or delete it.
- **Session-end detection** (§S29(e)): `ShmRingReader` holds the section open, so an
  exited game leaves `writeIndex` frozen and `status` `READY` — byte-identical to a
  loading screen.
- **A throwaway consumer**, minimum form only: `measuredMask` → rule 7 tri-state,
  `fg_factor = F_disp / F_app`, and **segmentation** (`03_METRICS` §Upscaling requires
  a mid-session settings change to split the session; the record was designed for the
  Agent to segment by `swapchainId`). Not P2's SQLite.
- Three cheap test gaps in reach: `SetPaused` has **no test at all** though both halves
  exist and the harness is staged; the pause round trip is never exercised; the drop
  path is asserted *absent* (`TotalDropped == 0`) rather than driven against the real
  native writer.

*Fails on main because:* there is no consent record, no host, and no path from
`HookedCaptureGate` to a drain. Assert `ConsentMissing` with `FlGuardedInject` **never
reached**, and `guardTicks` advancing from a non-test binary.

</details>

### ~~2. Upscaler hooks + a harness that speaks the vendors' symbol names~~ — IDENTITY LANDED 2026-08-09

**Do not start here for identity.** `sl.interposer.dll!slEvaluateFeature` is hooked,
module-scoped, installed lazily by the watchdog, and proven firing inside an injected
target. The stub DLLs, the decoy, `tools/hookinventory-check.ps1` and the Streamline
MIT vendoring all landed with it. Status is in `CHANGELOG.md` and §S24.

**What is still open from this item is the params half, and it is now item 2b below —
designed, refuted, and not built.** `FL_MEASURED_UPSCALER_PARAMS` has no producer, so
quality and render → output resolution are absent, which is exactly what P0 exit
criterion 1 needs. The route this repo documented for years is **licence-blocked**:
`NVSDK_NGX_Parameter_SetUI` needs NGX declarations, and the NGX/DLSS SDK is the
proprietary RTX SDKs Licence, so `18_GPU_VENDOR_APIS` §Checklist step 3 forbids both
vendoring it and re-declaring it. `17_HOOK_ENGINE` §The NGX parameter surface carries
that reasoning; **§2b carries which in-policy route to take and, more importantly, which
one to refuse.**

**Two corrections to what this entry used to say**, kept because the entry was the
trap it was warning about: it named `NVSDK_NGX_EvaluateFeature`, which **no measured
module exports** — the real symbol is `NVSDK_NGX_D3D12_EvaluateFeature` — and it said
four modules where the measured answer is **seven** (`nvngx_dlss`, `nvngx_dlssd`,
`nvngx_dlssg`, `nvngx_deepdvc`, `sl.common`, `_nvngx`, `nvngx`). The argument was
untouched by both errors; the data was wrong in the bullet whose whole subject is that
a wrong symbol name degrades silently.

<details>
<summary>The original entry, kept for the reasoning it carries</summary>

Item 4's primary hook class. **Streamline first** (owner decision); NGX-direct is its
own PR with its own rule-4 justification.

- Stub DLLs exporting the **exact** names from `docs/vendor-exports.json` (measured
  data, not vendor SDK) + a harness mode that calls them with known values.
  `17_HOOK_ENGINE` calls a wrong symbol name degrading silently to `unknown` the
  highest false-confidence risk in the spike, and nothing tests it.
- **Module-scoped** resolution. `NVSDK_NGX_EvaluateFeature` is exported by **four**
  modules on the dev machine (`sl.common.dll`, `nvngx.dll`, `_nvngx.dll`,
  `nvngx_dlssg.dll`); a name-resolved hook without scoping double-counts FG
  evaluations, straight into `fg_factor`.
- **The vehicle for "the first time their module appears" already exists**: the
  watchdog thread runs once a second. No `LoadLibrary` hook is needed for P0 — which
  keeps §S6 genuinely separable. The Overlay has **no** module-resolution machinery
  today (`GetProcAddress`/`GetModuleHandle` appear nowhere), so that helper is part of
  the real cost.
- `tools/hookinventory-check.ps1`: every symbol the Overlay resolves by name must
  appear in `vendor-exports.json`. **No drift exists today** — a refuter resolved every
  inventory symbol against all 34 measured modules — so write it as prevention and do
  not claim it fixed anything.
- Set `FL_MEASURED_UPSCALER` and `FL_MEASURED_UPSCALER_PARAMS` **separately**; that
  split exists because an NGX-direct title yields identity and nothing else.

</details>

### ~~2b. The params half~~ — LANDED 2026-08-15, and one deferral in it was overturned

**Do not start here.** `FL_MEASURED_UPSCALER_PARAMS` has a producer. Status is in
`CHANGELOG.md` and §S24; what belongs here is the **sequencing that changed** and the
**decisions that live in no other file**:

- **`slSetTag` was deferred by the entry below and is IN.** The deferral reasoned that a
  globally-tagging title "yields nothing from this PR — not a regression, not a fabrication".
  Measured, that risk is the common case rather than the edge: `sl_core_api.h:258` documents
  the local tags in `inputs` as merely *allowed*, and **only 4 of the 10** Streamline titles
  installed here ship `sl.dlss.dll` at all. An inputs-only producer could have shipped with a
  hit rate of zero, and nothing would have found out until a real-title run.
- **Both tag paths are read, and they are alternatives rather than layers.** Global
  (`slSetTag`) and local (`slEvaluateFeature`'s `inputs`) do not interact — the vendor says so
  in as many words — so a title using one yields nothing from the other. Local wins when
  present: it is scoped to the evaluation, so it cannot be older than the frame.
- **`sl_dlss.h` is vendored** (ten headers now), so `upscalerQuality` carries the vendor's own
  `DLSSMode`. **`upscalerSharpness` is `0xFF` permanently** — `DLSSOptions::sharpness` carries
  `SR_DEPRECATED_SHARPENING` in the header itself. That is the true value, not a placeholder.
- **The installer was restructured first, and had to be.** The old expansion ignored
  `FL_HOOK_INVENTORY`'s `family` column and bound the first resolving row to
  `Hook_SlEvaluateFeature`. Correct with one row; with two it would have detoured `slSetTag`
  with a body that reads argument 1 as a feature id.

**What it does NOT deliver, so nobody plans on it:** a title that tags **whole resources**
yields `renderW/H = 0`, the in-band unknown; a title that sets its preset through
`slDLSSSetOptions` and never chains `sl::DLSSOptions` yields `upscalerQuality = 0xFF`. Both
are honest absences.

**The hit rate is no longer unmeasured, and it SPLIT** — Cyberpunk 2077, 2026-08-15,
`spike-notes.md` §8. The extent half works and is *exact*: `renderW/H = 1485×835` against the
title's own `DLSS = Balanced` at 2560×1440, which is a number a writer that hardcoded a
plausible resolution could not have produced. The `sl::DLSSOptions` half has a **hit rate of
zero on that title** — it never chains the struct, so `upscalerQuality = 0xFF` there is a true
property of the title rather than a bug to go hunting for. One title, one configuration;
Alan Wake 2 is still unmeasured. **What the same run did find is a defect — §S30, and it
belongs to item 3.**

<details>
<summary>The original entry, kept for the reasoning it carries</summary>

### 2b. The params half — ~~START HERE~~, and the route is already decided

*(Archived. The live entry is above; this header's "START HERE" is struck so the file
does not contain two of them — that ambiguity is the exact class this file exists to
prevent, and leaving it verbatim would have been the letter of "kept unchanged" against
its purpose.)*

`FL_MEASURED_UPSCALER_PARAMS` has no producer, which is what P0 exit criterion 1
actually needs. Designed 2026-08-09 by a panel plus three refuters; **the obvious route
was killed on fatal grounds and the reasoning is the part that lives in no other file.**

**DO NOT hook `slGetFeatureFunction` and MinHook the returned `slDLSSSetOptions`.**
It is the route `17_HOOK_ENGINE` reads as natural, and it fails five ways:

1. It needs an "indirect" inventory class, which becomes an **unconditional escape hatch
   from `hookinventory-check` Pass A** — its oracle would be "a `PFun_*` exists in a
   vendored header", and `sl_core_api.h:63` declares `PFun_slSetTagForFrame` for a symbol
   ~~zero measured modules export~~. Any name the headers declare could be laundered past
   the gate. *(2026-09-04: `slSetTagForFrame` IS now exported — by `sl.interposer.dll` 2.8.0,
   which Dying Light: The Beast ships — and it entered the inventory the ordinary way, as a row
   Pass A checks against the measured oracle. The argument stands: the point was never that
   this one symbol was fictional, it was that a header-declared oracle cannot tell the two
   apart. The oracle itself needed a fix to see it: `tools/vendor-exports.ps1` took the FIRST
   copy of each module name, and with 2.7.1 and 2.8.0 both installed the row's verdict
   depended on directory order. It unions across copies now and lists every version seen.)*
2. A second MinHook install site **reopens the install-after-stop window**
   `dllmain.cpp:643-646` exists to close.
3. MinHook v1.3.4 has **no function-length oracle for a runtime-returned address**, so a
   short thunk gets patched into its neighbour — a crash inside vendor code where
   `FL_HOOK_GUARD` cannot reach.
4. `GetModuleHandleExW` with a reference is `LdrAddRefDll`, i.e. **the loader lock, on the
   game's thread, inside NVIDIA's own call.**
5. It structurally **misses the game's first `slDLSSSetOptions` call**, which for a
   benchmark configured before launch is often the only one.

**DO extend the hook we already own.** `Hook_SlEvaluateFeature` is handed
`const sl::BaseStructure** inputs, uint32_t numInputs` and today ignores both;
`sl_core_api.h:251` documents `inputs` as carrying *"viewport, tags, constants etc"*. A
bounded walk (cap 32 elements, 8 `next` links, match `s_structType` **and**
`structVersion >= kStructVersion1`) reaches `sl::ResourceTag`
(`kBufferTypeScalingInputColor` extent → `renderW/H`) and `sl::DLSSOptions`
(`mode` → `upscalerQuality`). **Zero new inventory rows, zero new MinHook patch sites, no
module-lifetime story, no gate changes.** The design sweep compiled and ran the walk in
the shipped configuration: tagged extents yield 1280×720, a chained `DLSSOptions` yields
`mode=3`, and the whole-resource case yields the honest all-zero.

**Three fields, three honest answers — and one of them is permanent:**

| Field | Answer |
|---|---|
| `renderW/H` | produced from local tags; **0** when the title tags whole resources, which is the in-band unknown `fl_shm.h` already defines |
| `upscalerQuality` | from `sl::DLSSMode`, mapped so `eOff`, out-of-range and conflict all become **`0xFF`** and the byte is **never 0** — which retires `fl_shm.h:328`'s "DLSS Performance published as a measurement" worry *at the writer* |
| `upscalerSharpness` | **cannot be measured in policy, ever, on this route.** `DLSSOptions::sharpness` is `[[deprecated("Sharpness is not supported")]]`, and `DLSSOptimalSettings::optimalSharpness` is Streamline's *recommendation*, not what the title applied. Ship `0xFF` permanently — that is the true value, not a placeholder |

**The sample must not latch.** Pack it in one `std::atomic<uint64_t>` consumed with
`exchange(0)` in `RecordPresent` beside the existing `g_slSeen.exchange(0)`, so a params
sample cannot outlive its frame. Replace the unfalsifiable "the writer must write all
four fields" invariant with one a fixture can kill: **bit 8 set implies
`upscalerQuality != 0` and `upscalerSharpness == 0xFF`**, asserted natively and in
`MeasuredFacts.IsHonest`.

**Two things to state in the PR body rather than discover:**

- **The hit rate is unmeasured and it is the largest unknown.** Whether shipping titles
  chain these structures into `inputs` was not measured — the `ResourceTag` half has a
  vendor-documented mechanism, the `DLSSOptions` half does not and is likely rarer. The
  design is built so the unmeasured case is free: nothing chained ⇒ bit 8 clear ⇒ the
  record is byte-identical to today's and the consumer says N/A. **Alan Wake 2 is the
  title that settles it** (§Owner-only item 2).
- **No gate verifies this walk.** `hookinventory-check`, `license-check` and
  `struct-mirror` all stay still — which is the route's advantage and simultaneously
  means the fixtures are the only thing standing between this producer and a silent
  wrong answer. Weight the harness modes accordingly.

Vendor `sl_dlss.h` **with its consumer in the same commit** (`18_GPU_VENDOR_APIS`
records that an unconsumed vendored dependency can have an incomplete header closure with
every gate green), and correct `THIRD_PARTY_NOTICES`' nine→ten headers and the README's
"excluded deliberately" line in that same commit.

Deferred with reasons: `slSetTag` global tags (a globally-tagging title yields nothing
from this PR — not a regression, not a fabrication), and the segmenter tuple
`03_METRICS:133` wants, so a mid-session settings change currently degrades to N/A rather
than cutting a segment.

</details>

### ~~3. Frame generation~~ — VALIDATED 2026-09-04 (§S31 row P1), and the item is done

**Do not start here.** The run landed on P1 — 1.00 / 2.99 / 3.99 on Cyberpunk's own
off / ×3 / ×4, 4.00 on Hell Is Us — after run 1 landed on P4 and #105 keyed the count on a
NEW frame index. `fg_factor` is published, `none` is reachable by counting, and
`tokens/batch = 1.00` answered §S31's own question on the way. Status is in `CHANGELOG.md`
and §S24; what belongs here is that **P0 exit criterion 1's FG factor is no longer the
blocker** — the remaining gap in that criterion is the quality preset, a title property on
Cyberpunk and an unmeasured one on Alan Wake 2 (Owner-only item 2).

<details>
<summary>The entry as it stood while the run was owed</summary>

**Do not start here.** `sl.interposer.dll!slGetNewFrameToken` is the application-frame
producer: an inventory row, a detour that counts distinct tokens between presents, and
`fg_factor = presents / tokens` with no batch premise. Chosen **with no oracle behind it** and
said so; the pre-committed table is `20_OPEN_QUESTIONS` §S31 and the owner-only run is
Cyberpunk 2077 off / ×2 / ×4 plus Hell Is Us ×4. **A ratio near 1 is not published until row
P1 lands** — the sequencing consequence, and the reason `none` is the PR after that run, not
this one. The entry below is kept for the route reasoning it carries.

</details>

<details>
<summary>The entry as it stood while the producer was unchosen</summary>

**Do not start here, and do not build the counter again.** `fgEvaluations`, `fgMode`,
`FL_MEASURED_FG` and `FL_MEASURED_FG_COUNTS` all have producers; the drain, the consumer
arithmetic, the refusals and the fixtures are in and gated. What is missing is not code.

> **`slEvaluateFeature(kFeatureDLSS_G)` IS NEVER CALLED.** Measured 2026-08-15, Cyberpunk
> 2077 (SL 2.7.1), five 40 s captures at four frame-generation settings: **0 evaluations
> across ~14,000 Streamline batches**, while frame generation was demonstrably active. The
> owner ruling of 2026-08-14 — count evaluations directly — is correct arithmetic on a route
> the vendor does not use. The counter is right and has nothing to count.
>
> **The zero is corroborated, not assumed.** `UNDECODED` is 0 in the same runs, and that
> bucket is now proven capable of reading non-zero (injected
> `--hold-presenting-upscaled-unknown`, canaried red), so a vendored `kFeatureDLSS_G`
> constant not matching the runtime id is excluded. What is NOT measured is where frame
> generation IS driven from: an absence at one export of one module locates nothing, and the
> NGX tier — `NVSDK_NGX_D3D12_EvaluateFeature`, **seven** exporting modules — is hooked
> nowhere.
>
> **What DOES work, and it is a proxy.** `presents / batch` reads **1.000 / 2.000 / 4.000**
> against the title's own off / ×2 / ×4, three independent runs at ×4. So generated presents
> reach our vtable (§H5's central fear does not occur) and the count tracks the multiplier.
> **But a "batch" is a present that drained a Streamline evaluation, not an application
> frame** — they coincide here only because Ray Reconstruction is evaluated once per
> application frame on this title, which no independent oracle has confirmed. Shipping
> `presents/batch` as `fg_factor` would be shipping that unverified premise as a measurement.
>
> **THE PREMISE IS NOW FOUR TITLES WIDE, NOT ONE — 2026-08-20, `spike-notes` §8.** Everything
> above rests on Cyberpunk. Five captures across four titles say something stronger and
> different: **`kFeatureDLSS_G` is zero on every one**, and two of the four never call
> `slEvaluateFeature` *at all* — Black Myth: Wukong, with `sl.interposer.dll` and
> `sl.dlss_g.dll` both loaded and DLSS-G demonstrably running, and Rune Factory: Guardians of
> Azuma. So the finding is not "DLSS-G avoids that export". On half the titles measured,
> **nothing** goes through it, and `upscaler` correctly reads `Unknown` — *a hook ran and could
> not identify what it saw*, which is the first time that distinction has mattered outside a
> fixture.
>
> **Two consequences for the routes below.** Any producer reached *via* `slEvaluateFeature`
> inherits that coverage hole, so it cannot be the whole answer. And `presents / batch` is
> unreadable on those two titles — where a **second proxy**, `presents ÷ RT-active presents`,
> read **5,764 / 1,441 = 4.0000 exactly** against a ×4 setting. It carries the *same* unverified
> premise (work recorded once per application frame), so it is a proxy and not a producer; what
> it adds is coverage, on a disjoint set of titles. On the one run where both were readable they
> agreed. **Whichever producer is chosen has to work on a title that speaks no Streamline
> features at all.**
>
> **So the open question is no longer "build a counter". It is: what is the in-policy
> producer for DLSS-G on Streamline 2.x?** Candidate routes, none costed and none chosen:
> hooking the interposer's swapchain proxy; `slGetFeatureFunction` + `slDLSSGGetState`
> (§2b refused a near neighbour on five specific grounds — read them before reaching for it);
> vendoring `sl_dlss_g.h` with its consumer; NGX-direct `nvngx_dlssg`; or shipping
> `presents/batch` with the premise stated and an oracle attached. **This is the decision to
> take before any more code.**
>
> **STATE AT THE END OF 2026-08-16, so the next session does not re-derive it.** Five
> real-title captures across off / x2 / x4 establish that `presents / batch` tracks the
> configured multiplier exactly (1.000 / 2.000 / 4.000) and that the presents frame
> generation adds ARE visible to our hook. What they do NOT establish is that a drained
> Streamline batch IS an application frame: every instrument tried so far either divides by
> the same known constant we do, or is another in-process present hook.
> **Three oracles were tried and all three fell** — `fl-baseline-probe` (retired by its own
> pre-committed falsifier: it reports two mutually exclusive FG implementations both
> "loaded"), the x2 overlay reading (one agreement counted twice, by algebra), and the x4
> overlay reading (kills the fixed-divisor rival, cannot separate an independent count from
> a correct derivation). ~~**The next measurement is PresentMon 2.x `FrameType`**, which
> classifies each present from ETW and divides by nothing.~~
>
> **THE ROUTE IS CHOSEN AND THE MEASUREMENT IS NOW OWNER-ONLY, 2026-08-20.** The decision
> below is taken: **measure before hooking.** `§S31` carries the decision table, written
> before the run and with two of its six rows retiring PresentMon outright;
> `tools/frametype-oracle.ps1` produces the input as two dimensionless ratios. What belongs
> here is the sequencing consequence — **the run needs the owner, for two reasons neither of
> which is the code:**
>
> 1. **The console binary will not start a trace session unelevated on this machine.** Exit 6;
>    the account is in neither Administrators nor Performance Log Users, and the running
>    `PresentMonSharedService` does not help because the console starts its own session.
> 2. **`--track_frame_type` is a BETA option that needs the VENDOR to instrument Intel's
>    provider**, by its own help text. So "classifies each present from ETW and divides by
>    nothing" is true of the mechanism and is **not** a promise that the mechanism is available
>    on a DLSS-G title. It may be as unavailable as `fl-baseline-probe` turned out to be, and
>    §S31 says so in the row that would retire it.
>
> **Do not build an FG hook before that run.** Every candidate route below costs more than the
> measurement does, and two of §S31's rows change which of them is even worth costing.
>
> ~~**And a prerequisite with code attached, before `presents / batch` is published anywhere:**
> `FgWindow`'s uniformity guard keys on `fgEvaluations`, which is zero on this route, so it
> passes vacuously and cannot see a window that mixed frame-generation states. Measured: an
> alt-tab mid-capture produced 1.84 instead of 2.00 — an 8% error with no diagnostic. A
> published `presents / batch` needs its own per-bucket guard, and the report should be able
> to say when the window was not uniform.~~ — **BUILT.** `FgWindow.BatchRefusal` is the
> per-bucket `presents / batch` check, `SessionReport` prints its verdict on the line under the
> ratio, and the drain tick samples foreground ownership so the report can name an alt-tab as
> the cause. **The sequencing consequence, which is what belongs here:** the proxy is no longer
> the unguarded number in the report, so a route decision below can be taken on measurement
> rather than under time pressure to stop publishing something unsafe. Status is in
> `CHANGELOG.md`; the reasoning is in `03_METRICS` §Frame Generation and `04_CAPTURE`
> §Ring draining.

> **One cheap measurement would sharpen it and has not been run**: the game's own frame
> counter beside a capture, pre-committed in §S30. **The other one HAS been run and is
> retired** — `fl-baseline-probe` at ×4 / ×2 / off reported all seven capabilities `loaded`,
> including two mutually exclusive frame-generation implementations, so its own written
> falsifier fired in one run. This bullet claimed both were unrun while the block above it
> said three oracles had fallen; a file whose whole subject is staleness contradicted itself
> inside one entry, which is recorded rather than quietly repaired.
>
> ~~**And the owner's list for the §S31 run**~~ — **RUN 2026-08-27.** A runbook script drove it
> — three legs, three game launches — and it landed on row **P2**: `FrameType` present
> and **every row of all three legs `Application`**, while the two instruments agreed on the
> present rate to within 0.3% — so PresentMon saw the generated presents and classified none
> of them as generated.
>
> **PRESENTMON IS RETIRED as the application-frame oracle for NVIDIA frame generation.** That
> is the fourth oracle to fall, after `fl-baseline-probe` and two readings of Steam's overlay.
>
> **AND THIS ITEM IS BACK WHERE P1/P2 SAID IT WOULD GO: the hook routes.** What was retired is
> the *instrument*, not the question. "Is a drained Streamline batch an application frame?" is
> exactly as open as it was, `presents / batch` still reads 2.00 and 3.99 against a title's own
> ×2 and ×4 on the same unverified premise, and it is still not publishable as `fg_factor`.
>
> **So the decision this entry has been waiting on is now unblocked and unmade.** The candidate
> routes above are unchanged and none is costed: the interposer's swapchain proxy;
> `slGetFeatureFunction` + `slDLSSGGetState`; vendoring `sl_dlss_g.h` with its consumer;
> NGX-direct `nvngx_dlssg`; or shipping `presents/batch` with the premise stated. **The
> measure-before-hooking rule has been discharged — the measurement was taken and it did not
> choose a route for us.** Whoever picks this up is choosing without an oracle, and should say
> so in the PR rather than implying one.
>
> Numbers in `spike-notes` §11. Two loose ends recorded there and not here: our own `off` leg
> drained ZERO batches (Ray Reconstruction was off, so there is nothing to divide), and whether
> `--track_frame_type` was in effect at all is unmeasured — it changes the reason, not the
> action.

</details>


> **§H5 case 3 is MEASURED as of 2026-08-15, and the answer is half of what the entry below
> feared.** `fl-probe-interposer` now calls `slInit` (the licence blocker died with #64's MIT
> vendoring) and reports: with the interposer engaged, **the swapchain the title holds is not
> an instance of the class whose shared vtable we patch** — its vtable is inside
> `sl.interposer.dll`. Reproduced on Alan Wake 2 (SL 2.7.0) and Cyberpunk 2077 (SL 2.7.1).
>
> **It does not follow that we miss the present, and the difference is the whole metric.**
> `--probe-proxy` already showed a *forwarding* proxy is caught one layer down, because it
> calls `real_->Present(...)` — an ordinary virtual dispatch. **Different class ≠ missed
> present.** What is still unmeasured is whether *this* proxy forwards, and whether DLSS-G's
> **generated** presents reach the same vtable. That needs presents driven through the proxy
> with our hook installed, which is a fixture nobody has built.
>
> **Two things that change the shape of this item:**
>
> - **`slEvaluateFeature`'s signature is not stable across Streamline generations.** The
>   Witcher 3 ships `sl.interposer.dll` **1.5.6**, which exports the same NAME with a
>   different argument list. `ResolveScoped` now refuses a module that does not speak SL2
>   (#71), so this item inherits that guard — do not weaken it to reach an older title.
> - **`g_slSeen` is a bitmask, and FG needs a COUNT.** A bit collapses two evaluations
>   between two presents into one. Whatever replaces it must not break 2b's consume block,
>   which reads the same word — that is why 2b landed first.

- **§S30 is yours, and it is the first defect a real-title run has produced.** Cyberpunk 2077:
  every one of **2,461** params-carrying records decoded the upscaler as `UNKNOWN` while the
  title was demonstrably running DLSS. The mechanism is this item's exact drain — 10,169
  presents carried only 2,461 `g_slSeen.exchange(0)` batches, and the batches that reached a
  present held `DLSS_G` / `DLSS_RR` and not `kFeatureDLSS`. **Do not fix it by making the
  decode prefer DLSS.** Which ids actually arrive was never printed, so that fix converts a
  wrong answer into a *confident* wrong answer. Print them first.
- **`presents / batches = 4.13` was measured, and it is a PROXY rather than this item's
  number.** Two runs gave 4.13 and 4.12 against the title's own `DLSS_MultiFrameGeneration =
  x4`. Encouraging, and *not* `presents / fgEvaluations` — nothing counts `kFeatureDLSS_G`
  evaluations yet, which is the whole of this item. Quoting 4.13 as an FG factor would be
  reporting the oracle back to itself.
- **The arithmetic needs deciding before the hook, not after.** `03_METRICS` defines
  `F_app = presents − Σ fgEvaluations`, i.e. `fgEvaluations` counts GENERATED frames — but
  `slEvaluateFeature(kFeatureDLSS_G)` fires once per APPLICATION frame and produces N−1 of
  them, and N lives in `sl::DLSSGOptions`, set out of band through `slDLSSGSetOptions`: the
  same refused route as 2b's. **Counting evaluations directly** gives
  `F_app = Σ evaluations`, `F_disp = presents`, `fg_factor = presents / evaluations` with no
  multiplier and no new header — at the cost of a `03_METRICS` change in the same PR.
  Owner-decided 2026-08-14: **count evaluations directly.**
- **The other two vendors, measured.** `libxess_fg.dll` proxies the swapchain
  (`xefgSwapChainD3D12InitFromSwapChain`, `…GetSwapChainPtr`, `…SetEnabled`). `ffx_fsr3_x64.dll`
  exports `ffxFsr3SkipPresent`. **But the newer `amd_fidelityfx_framegeneration_dx12.dll`
  (3.1.5, in three installed titles) exports only the five generic `ffx*` entry points** —
  identity comes from the arguments, not the symbol, so telling FG from upscaling means
  decoding a vendor struct we have no headers for. Defer with a written rationale rather than
  guessing.
- Oracle: the game's own settings menu and frame counter (owner decision). **Cyberpunk 2077 is
  the sharpest**: `DLSS_MultiFrameGeneration = x4` in its settings file, so the expected
  answer is `fg_factor ≈ 4.0` — a number a structurally-1.0 bug cannot fake.

<details>
<summary>The original entry, kept for the reasoning it carries</summary>

### 3. Frame generation, and the present it may not own

- `fgEvaluations` / `fgMode` / `FL_MEASURED_FG` + `FL_MEASURED_FG_COUNTS`.
- **The swapchain question must be answered here, not deferred past it.** Measured from
  `vendor-exports.json` and recorded nowhere else until 2026-08-05: **all three FG
  vendors take over the present.** `libxess_fg.dll` exports
  `xefgSwapChainD3D12InitFromSwapChain` / `GetSwapChainPtr`; `ffx_fsr3_x64.dll` exports
  `ffxFsr3SkipPresent`. §H5 case 3 is live for DLSS-G, XeFG **and** FSR3-FG. If it
  bites, `fg_factor` is **structurally 1.0**, which looks exactly like "FG is off".
  Minimum: notice a swapchain we did not see created and report *unknown*.
- Oracle: the game's own settings menu and frame counter (owner decision). The exit
  criterion already says "verified against the game's own settings menu", so this does
  not import P2's ETW source.

</details>

### ~~4. Ray tracing~~ — BUILT 2026-08-20, and the trap it hit is the one to carry forward

**Do not start here.** `FL_MEASURED_RT` has a producer, `rtFlags` carries real evidence, and the
tri-state's `Yes` and `No` are both reachable for the first time. Status is in `CHANGELOG.md` and
`spike-notes` §6. What belongs here is **the trap**, because it generalises past ray tracing:

> **A COMMAND LIST'S vtable IS NOT THE ONE YOU READ OFF A FRESH ONE.** The first `Reset()`
> replaces it with a **per-object** vtable in which the vendor driver has taken methods over.
> Measured on an RTX 5080: `DispatchRays` moves from `D3D12Core.dll` into `nvwgf2umx.dll`, while
> `BuildRaytracingAccelerationStructure` stays put. Every game resets its lists every frame, so
> the addresses in an unreset list's vtable are ones no title ever calls for the moved methods.
>
> **The first version of the hook did exactly that.** It installed, published
> `FL_HOOK_RT_DISPATCH`, and never fired — `withDispatch = 0` beside `hooks = RT_DISPATCH |
> RT_AS_BUILD`, a mask bit with nothing behind it. And because the OTHER hook worked, it read as
> a bug in the dispatch detour rather than in the acquisition they share.
>
> **The rule: put a throwaway object through the same lifecycle the game's objects go through,
> or it is not a sample of them.** §H5 says the same thing about swapchains one layer up. The
> fix was one call; finding it took an injected fixture, because no probe that reads a fresh
> object can see it.
>
> **And every vendor-specific result here is ONE DRIVER'S.** Nothing says an AMD or Intel UMD
> splits the two methods the same way, or leaves either in `D3D12Core`. `--probe-dxr` Q5 prints
> the module on each side of the Reset so the next machine answers for itself.

<details>
<summary>The original entry, kept for the reasoning it carries</summary>

### 4. Ray tracing — ~~**START HERE**~~, and item 3 just took away its denominator

*(Archived 2026-08-27. The live entry is above; this header's "START HERE" is struck so the
file does not contain two of them — the same correction item 2b's header carries, and the
exact class this file exists to prevent. **Its question is also answered.** The denominator was
chosen on 2026-08-20, in the PR that wrote the hooks: `rt_frame_pct` is a share of PRESENTS with
the ×FG dilution written down as a known, quantified limit — at ×4 it reads ~25%, against a `Yes`
gate of ≥ 5%, so the verdict is unaffected and only the reported percentage is diluted — while
`rays_per_pixel` is taken over RT-ACTIVE presents, is therefore undiluted, and needs no
application-frame count at all. `03_METRICS` §RT/PT/RR carries the decision and its falsifier,
which did not fire on two independent titles. Kept rather than deleted for the reasoning it
carries about why the choice mattered.)*

> **The dependency you inherited, before anything else.** The plan for this item routed
> `03_METRICS`' RT thresholds — `≥ 5% of frames`, `rays_per_pixel`, `rt_frame_pct` — through
> **application frames**, using `Σ fgEvaluations` as the denominator, because dividing by
> PRESENTS dilutes every one of them by the frame-generation factor: at ×4 a title that
> path-traces every application frame reports `rt_frame_pct = 25%`, and `rays_per_pixel`
> lands at a quarter of the truth, below the heuristic's own 1.0 threshold. That decision was
> taken when `fgEvaluations` was expected to have a producer. **It does not, on the one route
> measured** (item 3 above). So this item starts with a choice nobody has made:
>
> - use `presents / batch`-derived application frames, inheriting item 3's unverified premise;
> - divide by presents and write the dilution into `03_METRICS` as a known, quantified limit;
> - or accumulate RT evidence to an application-frame boundary **in the writer**, which costs
>   more in the hook and needs the same boundary signal item 3 could not find.
>
> Whichever is chosen, say so in `03_METRICS` in the same PR. Do not leave the thresholds
> reading as though the denominator were settled.

### 4. Ray tracing — §S29(f) is ruled, and the cheapest conjunct has landed

> **The RayQuery contradiction is settled, 2026-08-14 (owner).** AS-build activity proves
> **ray tracing is happening** ⇒ `RT = Yes`; *classifying the technique as RayQuery* is what
> needs a DXIL scan and stays `N/A`. CLAUDE.md rule 7's `N/A` applies to the **classification**,
> not to the yes/no question — so `03_METRICS:170` and `README.md:34` both stand. **Amend rule
> 7 in the PR that writes the hook**, and build both hooks: a `DispatchRays`-only writer sees
> nothing on a RayQuery-only title and its silence is indistinguishable from a real negative.
>
> **`rtTier` already has a producer** (#67) — `ResolveApi` queries
> `D3D12_FEATURE_D3D12_OPTIONS5` on the device DXGI hands it. Two of the `No` branch's three
> conjuncts are therefore live; **what is still missing is `FL_MEASURED_RT`**, so RT is `N/A`
> on every session and both `Yes` and `No` remain unreachable. The gap moved; it did not close.
>
> **And a trap that has now appeared twice in two different vendors' enums.**
> `D3D12_RAYTRACING_TIER_NOT_SUPPORTED` is **0**, against `rtTier`'s "not queried";
> `sl::DLSSMode::eOff` is **0**, against `upscalerQuality`'s "not measured". Both would have
> published an affirmative negative by simply copying the vendor value. **Assume the next
> vendor enum has the same shape**, and resolve it at the writer, where the two states are
> still distinguishable.

### 4. Ray tracing, including the path dispatch counting misses

- `CreateStateObject`, `DispatchRays`, `BuildRaytracingAccelerationStructure`;
  `dispatchRaysVolume`, `maxTraceRecursionDepth`, `rtFlags`, `FL_MEASURED_RT`, and
  `FlWriterState.rtTier` + `hooksInstalledMask`.
- Harness DXR mode **including a RayQuery-only variant** — that is what makes item 6's
  actual claim provable: AS-build catches a title `DispatchRays` misses.
  **Check first:** whether WARP on the CI runners supports DXR. If not, gate the ctest
  on device support and say so rather than reporting a skip as coverage.
- **§S29(f) must be settled before the hook is written.** CLAUDE.md rule 7 and
  `03_METRICS` §RT disagree about whether inline RayQuery is measurable, and the answer
  decides whether a RayQuery-only title reports `Yes` or `N/A`.

</details>

### 5. Item 4's answer, measured on real titles

Real-title runs; `spike-notes` §8's empty per-title table, §6 and §9's empty bullets.
The README sentence is **not a percentage** — the recorded decision is that the baseline
cannot answer four of the five questions at all.

**Two things nobody has costed** (found by the completeness critic, 2026-08-05):
`StaticGameDetector` has **no runnable vehicle** — every construction is in tests — so
item 4's mandated comparison cannot be re-run on the *baseline* side either; and the
only candidate title exercising all five required values is a **Streamline** title,
i.e. the one class where the hook premise is recorded INCONCLUSIVE.

### 6. The S-series ledger, audited against the criterion it serves

Written rationales for whatever is still ❓/⏳/◐ in §S24, and unify the glyphs — one
disposition currently wears three (`✅ deferred` / `🅓 deferred` / `🔴 deferred`) in a
table whose only purpose is being auditable by counting them.

### 7. **START HERE** — the three things left once the FG blocker fell (2026-09-04, owner's list)

P0 exit criterion 1 reads **4 of 5** as of 2026-09-04: upscaler identity, render → output,
FG factor and RT state are measured against the titles' own menus. What follows is the
owner's list for the next session, in the order the dependencies run, each with what makes
it fail on unmodified `main` — because a criterion already true on `main` is decoration.

#### 7a. The quality preset — the last value criterion 1 lacks

*Fails on `main` because:* every real title measured reads `upscalerQuality = 0xFF`.
Cyberpunk 2077 never chains `sl::DLSSOptions`, so 0xFF there is the title's truth; **Alan Wake
2 (SL 2.7.0) is the candidate that may chain it and is unmeasured** (Owner-only item 2).

- **First, the cheap measurement:** one Alan Wake 2 capture at a known preset, read
  `quality=` off the `render …  quality=…  upscaler=…` line. If it reads the preset, criterion 1
  is 5 of 5 on that title and this item is a spike-notes entry. If it reads 0xFF too, the
  Streamline options route yields nothing on either measured title, and the decision below is
  live. *(Runbook title 12 is Alan Wake 2 as of 2026-09-04; Epic may update the executable on
  launch, so launch first and grant after.)*
- **The input to that decision is on the report now** (2026-09-04): `render -> output` prints the
  two measured sizes, `03_METRICS`' ratio and the render-scale percentage — Cyberpunk reads
  `1485x835 -> 2560x1440 = 1.72x (58% render scale)` — plus a **SETTINGS MOVED** flag when the
  window held more than one tuple. It prints **no preset name**; that is the decision below,
  still the owner's. Every FSR title measured on 2026-09-04 also reads `0xFF` for the quality
  byte, and on that route it is the true value rather than a gap: the ffx-api dispatch carries
  no quality mode at all, so the derived label is the only route to a preset name for AMD too.
- **The decision, if 0xFF everywhere:** `renderW/H ÷ outputW/H` is *exact* (Cyberpunk: 1485×835
  at 2560×1440 = 0.58, which IS DLSS Balanced, and the menu agreed), and DLSS's preset ratios
  are documented constants (DLAA 1.0, Quality 0.667, Balanced 0.58, Performance 0.5, Ultra
  Performance 0.333). A label **derived** from the measured ratio — printed as *"render scale
  58% ≈ Balanced"*, never written into `upscalerQuality`, never aggregated as the vendor enum —
  is an inference over a measurement, and `05_DETECTION`'s rule against static hints setting
  runtime facts does not forbid it: the input is a runtime measurement. **Whether a derived
  label may appear beside a measured field is the owner's call**; do not build it before that
  call, and if built, the byte stays 0xFF and the report says *derived*.
- **Do not** reach for `slDLSSSetOptions` via `slGetFeatureFunction`: §2b refused it on five
  grounds that have not changed, and NGX-direct `NVSDK_NGX_Parameter_GetUI` is licence-blocked.
- **Alan Wake 2 did not run on 2026-09-04** (the owner tried every workaround; the title is
  broken on this machine), so the measurement moved to **Dying Light: The Beast at DLSS**, the
  first Streamline title measured whose batches carry `kFeatureDLSS` — identity `Dlss` on 4,575
  of 4,575 batches. It read `UpscalerParams = 0` on three captures, because DL:TB ships
  **Streamline 2.8.0, which deprecates `slSetTag` for `slSetTagForFrame`**, and only the
  deprecated export had a row. The `slSetTagForFrame` row exists as of this entry (#115) —
  **and the rerun on it still read `UpscalerParams = 0` with both rows live**, so the title
  tags with a zero extent or not on this route at all. The next build reads the size the
  tagged `sl::Resource` declares when the extent is zero (`TagSize`, every route); one more
  DL:TB DLSS capture on it is ~~owed~~ **ran 2026-09-05 00:53 (#116 build): `N/A` stands** —
  params 0 of 3523 with both rows live and the fallback in. The route yields nothing on this
  title; the honest answer is `N/A`, and 7a on DL:TB is closed on that. What would reopen it
  is a tag-route census (calls per export, zero-extent seen, Resource-size seen), which needs
  a layout bump and is not worth one for a preset byte. The derived-label decision above is
  unchanged and applies to the titles that do print the line.
- **Separately, and more important than the preset:** every DL:TB DLSS run (five, the last at
  00:53 on the #116 build) had DLSS Frame Generation ON per the owner, and every one read
  `presents = tokens`, `frame generation: none`. That is `20_OPEN_QUESTIONS` §H5 case 3's
  candidate title. **Owner decision 2026-09-05: this is its own session and its own PR.** Read
  §H5 first; the discriminator (the owner's counter beside our `FPS:` line) is pre-committed
  there and is the first thing that session asks for. Nothing in FG counting changes before
  that reading. **The reading came the same day and the first PR landed on it** — the owner:
  the title runs at DLSS ×4, the frames we record are the native ones (§H5, verbatim). The
  capture host now prints each census-named module's file version and withholds `none` on
  that shape (`03_METRICS` §Frame Generation; the key is `sl.dlss_g.dll`, not
  `nvngx_dlssg.dll`). **What is next is the owner's session pre-committed in §H5** — DL:TB with
  DLSS FG off and ×4, Cyberpunk MFG ×4 as the control, PresentMon elevated beside each, the
  counter's NUMBER written down — and the row it lands on decides whether the gate stays, a
  second present sink is costed (§H5 lists the candidates; the route is the owner's), or the
  gate is withdrawn. Nothing in the hook changes before that row.

#### 7b. Titles whose upscaler or frame generation is compiled into the executable

*Fails on `main` because:* the census has no module to see, the inventory has no export to
resolve, and the report prints *"cannot include in-process generated frames"* — which on a
statically linked FSR3-FG title is wrong in the dangerous direction. The sentence names the
hole; it does not close it.

- **Measure before costing.** No installed title has this shape: every FSR title on this
  machine ships the `ffx_*` / `amd_fidelityfx_*` DLLs (`vendor-exports.json`, 34 modules), and
  the six-title run found none. The catalogue that links FSR2 statically is mostly
  2022–2023 UE4/Unity titles that took the source SDK. **Find one first.** A rationale written
  about a title that does not exist is the kind of deferral §S24 counts against nobody.
- **What is and is not possible, so the costing is honest.** Nothing by name: no module, no
  export. What survives static linking is behaviour on interfaces we already hook or could:
  FSR3's frame-generation *proxy swapchain* is created through `IDXGIFactory*::CreateSwapChain*`
  on the shared `dxgi.dll` class vtable — visible **only at creation**, i.e. only in launch mode
  (§S1, deferred) or on a title that recreates its swapchain on a settings change; and per-frame
  work on the D3D12 queue (`ExecuteCommandLists` on the FG's async queue) is a cadence signal
  with no vendor name on it, which is rung 3 at best and needs an application-frame count this
  title cannot give. **The honest ceiling for a statically linked title in attach mode is
  Presented FPS with the census's qualifier** — which is what ships today, with the hole named.
- **The only cheap improvement is wording**, and it is already in: the qualifier says
  *statically linked FSR3-FG … outside what this can see*. If a title is found, the first PR is
  a `spike-notes` row and a `20_OPEN_QUESTIONS` §Scope entry with the measured shape, not code.
- ~~**Measured 2026-09-04: no such title is installed.**~~ **Found the same evening, by running
  rather than by scanning: Rune Factory: Guardians of Azuma.** The scan was right that every FSR
  title *ships* its DLLs — Rune Factory ships the 1.1.x monolith — and wrong to read shipping as
  loading: with FSR frame generation on, the census has **no AMD module at all**, so the plugin's
  FSR 3 is compiled in. And the ceiling is better than this entry feared: Streamline's frame
  token is requested on that UE5 title regardless, so the count read `59.99 → 119.95 (×2 FG)`,
  *technology not identified* — the trio, with the vendor missing. Identity by name stays out of
  reach; §Scope decisions carries the corrected row. A disk scan cannot find this shape; a
  capture with the census line can. **Black Myth: Wukong is the second** — its FSR upscaling
  dispatches to neither the loaded monolith nor the loader, and it offers no FSR frame generation.

#### ~~7c. AMD and Intel identity, measured rather than inferred~~ — **AMD LANDED 2026-09-04; Intel closed; the owner's three launches are owed**

**Do not start here.** The three ffx-api leaves are hooked, the FidelityFX headers are vendored
at tag **`v2.3.0`** (not the `v1.1.4` step 1 below names — its own reversal condition fired:
installed titles ship SDK 2.x effect DLLs whose `PrepareV2` descriptor 1.1.4 does not declare),
the K = 1 / K = 4 pair runs on both vendor shapes, and the acceptance table is pre-committed in
`20_OPEN_QUESTIONS` §H11 (rows A1–A3, R1–R5). Status is in `CHANGELOG.md` and §H11. **Three
corrections to the entry below, each of which changed what got built:**

- **Scope to the modules a GAME CALLS — the three leaves AND the SDK 2.x loader — not "the
  facade", and this bullet reversed itself within the day.** Step 2 says *"module-scope every row
  to the facade … the plugin modules are called by the facade"*. The morning's build hooked the
  three leaves only and refused the loader by `static_assert`, arguing that five identical export
  tables leave the loader no route to a leaf but the leaf's own export, so hooking both would
  double-count. The UE5 half held — Hell Is Us and Expedition 33 ship the two effect DLLs with
  **no loader** and their dispatches arrive at the leaves, 1×. **The loader half did not:** Dying
  Light: The Beast at FSR + FSR FG, KCD2 at FSR and Wukong at FSR produced **zero** dispatches at
  any leaf export (§H11's run table, row R6). The signed loader reaches its providers through an
  object, not an export. So the row set is *whatever module the game calls*: four rows, the
  loader's stub forwards through the leaves' direct entry to model the measurement, and the
  K = 1 control with all four hooked is what would show a loader that re-entered an export.
- **`ffxCreateContext` is not a row.** Identity comes off the UPSCALE dispatch's type — per
  frame, so it says *running* rather than *created* — and a create-context row's honest family
  is `UPSCALER_IDENTITY` alone, which collides with `slEvaluateFeature`'s under the installer's
  equality binding.
- **"Extend Pass C's forbidden-import list" was already done** — `^(sl\.|_?nvngx|libxess|ffx_|amd_fidelityfx)`
  since 2026-08-14, self-tested. Pass C gained the **export** table instead, because `ffx_api.h`
  declares its entry points `__declspec(dllexport)` unconditionally.

**Run 2026-09-04 evening, thirteen captures, §H11's table:** A1–A3 landed as pre-committed
(`Fsr3` / `FsrFg` / ×2.00 / 1.00 on Lies of P; `none` by count with FG off; `FSR (3.1 or 4 …)` /
`FsrFg` / ×2 on Hell Is Us), Expedition 33 showed the mixed-vendor shape (FSR upscale + DLSS FG ×3
→ *active, technology not identified* at ×2.97), Cyberpunk at FSR 3 split across two AMD
generations, and **Rune Factory turned out to be 7b's statically linked title** with the Streamline
token count still reading ×2. **Row R6 ran the same night on #111:** Dying Light: The Beast at
FSR + FSR FG printed `FSR (3.1 or 4 …)` / `FsrFg` / `70.49 → 140.9 (×2)` with the counts agreeing
at 1.01, KCD2 at FSR printed the identity and `none` by count at 1.00 — the loader row carries
both, once. **Wukong at FSR read silent at the monolith AND the loader with the monolith loaded**:
FSR upscaling compiled into its UE plugin (Rune Factory's shape) with the monolith loaded and
unused — ~~owed: Wukong with FSR frame generation ON~~ **the title has no FSR frame-generation
option (owner, same night)**, so Wukong is the second static title and nothing is owed on it. **The FSR 3.0 host route
(`ffx_fsr3_x64.dll`, step 3 below) is deferred to its own PR** — ten host headers from `v1.1.4`;
Cyberpunk at FSR 3 is its oracle and already prints `FsrFg` from the monolith beside it.

<details>
<summary>The entry as it stood, kept for the reasoning it carries</summary>

#### 7c. AMD and Intel identity, measured rather than inferred — §H11's deferral has a next step

*Fails on `main` because:* Lies of P with FSR 3.1 frame generation demonstrably on reads
`upscaler: N/A … frame generation: N/A` plus a census WARNING, and Hell Is Us at FSR reads the
same as its off run. The three AMD 3.1 modules export the same five generic names
(`ffxCreateContext`, `ffxDispatch`, `ffxQuery`, `ffxConfigure`, `ffxDestroyContext`); XeSS
exports `xessD3D12Init` / `xessD3D12Execute` and XeFG `xefgSwapChainD3D12InitFromSwapChain`
(`vendor-exports.json`).

1. **The `18_GPU_VENDOR_APIS` §Checklist has been run on both SDKs (2026-09-04) — read the
   result before assuming the previous sentence, which said both were MIT and was wrong about
   Intel.** *AMD FidelityFX*: clear. Tag **`v1.1.4`** is MIT at the root and inline in every
   header of interest; vendor **types-only, with a consumer in the same commit**
   (`18_GPU_VENDOR_APIS` records why an unconsumed vendoring is worse than none). The files:
   `ffx-api/include/ffx_api/{ffx_api.h, ffx_api_types.h, ffx_api_loader.h, ffx_upscale.h,
   ffx_framegeneration.h, dx12/ffx_api_dx12.h, vk/ffx_api_vk.h}` for the 3.1 facade the
   installed titles call, and `sdk/include/FidelityFX/host/{ffx_fsr3.h, ffx_fsr3upscaler.h,
   ffx_frameinterpolation.h, ffx_opticalflow.h, ffx_types.h, ffx_error.h, ffx_interface.h,
   ffx_util.h, ffx_assert.h}` for the FSR 3.0 host API. Do not take `main`: it is SDK 2.x under
   a binary-only `docs/license.md` that excepts these headers as MIT only by an 845-line list.
   Extend `hookinventory-check` Pass C's forbidden-import list with `amd_fidelityfx_*.dll` /
   `ffx_*.dll` in the same PR — the Streamline lesson, one vendor over. *Intel XeSS SDK*:
   **rejected.** Intel Simplified Software License (binary-only grant, no reverse engineering,
   termination), and the headers themselves forbid copying without Intel's permission, so
   step 3 binds — no vendoring and **no re-declaration** of `xessD3D12Execute` or
   `xefgSwapChain*`. **Intel stays at the census — decided 2026-09-04, delegated by the owner
   and taken.** The one route that looked left in policy, a signature-free call counter on a
   named export, was weighed and refused: the thunk declares nothing, but installing it still
   inline-patches Intel's code inside the game process, and the same licence forbids *"any
   modification"*. FrameLedger has never patched a restricted-licence vendor binary — the NGX
   answer was the census, not a cleverer hook — and Intel gets the same answer. It would also
   have been the only hook entry outside `FL_HOOK_GUARD` and a second language in the
   Overlay's build, for `Xess` identity alone with no render extents, on a vendor with no
   test hardware here. `20_OPEN_QUESTIONS` §Scope decisions carries the row and the reversal
   condition (Intel relicensing the headers). **So 7c is AMD identity, full stop.**
2. **AMD 3.1 (`amd_fidelityfx_dx12.dll`, the facade the game calls):** identity is in
   `ffxCreateContextDescHeader.type` — `FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE` vs
   `…_FRAMEGENERATION` — read from the argument of a hooked `ffxCreateContext`, which is the
   same class of read as `slEvaluateFeature`'s `inputs` (rule 4). `ffxDispatch` with a
   frame-generation dispatch desc fires **once per application frame** and is this vendor's
   `slGetNewFrameToken`; an upscale dispatch desc carries `renderSize` / `upscaleSize`, which
   is `renderW/H` exact. **Module-scope every row to the facade**: three modules export the
   same five names, and the plugin modules are called by the facade, not by the game — an
   unscoped hook would count each dispatch twice, straight into `fg_factor`.
3. **AMD 3.0 (`ffx_fsr3_x64.dll`, Cyberpunk's copy):** named exports, no struct decode needed —
   `ffxFsr3ContextDispatchUpscale` for identity and extent, `ffxFsr3DispatchFrameGeneration`
   once per application frame.
4. **Intel — closed at the census (step 1).** No hook on any `libxess*.dll` export. The
   report line for an Intel title is what the writer already produces: upscaler
   `NOT_REPORTED`, and if `libxess_fg.dll` is in the census, Presented FPS with the *"MAY
   include generated frames"* qualifier rule 6's amendment requires. `renderW/H` stays
   `NOT_REPORTED` for XeSS *by licence, not by ignorance* — the report's wording should say
   so, which is a string change, not a hook. Note that census presence is not identity:
   Cyberpunk loads `libxess.dll` at DLSS, so the census must never be read as *"XeSS on"*.
5. **Acceptance, against the titles' own menus:** Lies of P at FSR + FG prints
   `upscaler: Fsr3`, `frame generation: FsrFg`, and the trio at ≈ ×2 with the same
   `presents / dispatches` arithmetic; Hell Is Us at FSR prints identity where today it prints
   the off run's line; Cyberpunk at XeSS prints upscaler `NOT_REPORTED` and the census
   sentence with `libxess.dll` named, and nothing that reads as identity. **A wrong preset name degrades
   silently** — the histogram and `tokens/batch`-style second count are what make a mistake
   visible, so build the per-vendor second count before trusting the first.

**Traps carried from the Streamline work that apply verbatim:** the vendor's zero is not our
zero (resolve at the writer); a vendor keeps a NAME and changes a SIGNATURE (`SpeaksExpectedAbi`
needs an AMD and an Intel arm); the K = 1 / K = 4 harness pair must exist before the first
real-title run, or a writer that counts presents instead of dispatches is green everywhere.

</details>

### Separable

`fl-probe-signer` → §S19(b) → drop `-SkipIntegration`. **Not** a prerequisite of the
feature hooks — §S29(a) said it was and was wrong; see below. What it buys is the
*managed* drain being in the merge gate.

> **The price went up on 2026-08-06 and is worth stating before someone budgets it.**
> `Category=Integration` was three cases; it is **nine** — the drain loop, the pause
> round trip, the drop path against the real writer, and the capture host's four
> end-to-end cases including the only assertion anywhere that `guardTicks` advances
> from a non-test binary. All nine run on a dev box and none in the merge gate.
>
> **And §S19(b) alone does not buy them back.** `build.ps1 -SkipIntegration` applies
> `--filter 'Category!=Integration'`, which excludes the class *before* the guard is
> ever asked — two independent mechanisms produce one absence, and `ci.yml` has to
> drop the switch as well. Anyone costing this as "fix the signer half" is costing
> half of it.

### ~~§M5 — **START HERE**~~ — MEASURED 2026-09-03, row R1, and the head moves

**Do not start here.** `LhmTelemetrySource` exists, `probe-lhm` ran unelevated and elevated,
and both landed on row R1 of the table pre-committed in §M5: eight GPU fields with no
elevation and no PawnIO. Status is in §M5, `spike-notes` §10 and `CHANGELOG.md`. What
belongs here is the **sequencing consequence**: the three user-facing sentences stand, so
nothing in `README`, `legal/EULA.md` or the consent disclosure is waiting on telemetry any
more, and the `LibreHardwareMonitorLib` loose end below is closed by having a consumer.

~~**The live head is now the FPS-display rule, decided by the owner on 2026-09-03 and
recorded in no other file until its PR lands:**~~ **LANDED the same day** — `03_METRICS`
§Presented FPS and §Rung 0's qualifier, `fl_shm.h` §FlRuntimeCensus, `17_HOOK_ENGINE`
§Watchdog observations, CLAUDE.md rule 6's amendment. Status is in `CHANGELOG.md`. ~~**What is
still open from it is the owner's:** the two real-title captures that would show the two
qualifiers on real games (Cyberpunk 2077 → WARNING naming `nvngx_dlssg.dll`; any D3D11
title with no upscaler → "cannot include"), one launch each. The fixtures prove both
directions natively; the titles would prove the names.~~ **RUN THE SAME DAY, six titles,
`spike-notes` §9.** Both shapes seen; the names are the loader's; one refusal
(`TargetAmbiguous`, NW.js) became a scope row; Hell Is Us became the third title where DLSS is
on and `slEvaluateFeature` sees nothing. The decision text is kept below
because the reasoning lives in no other file. When frame generation is *not measured*
(`FL_MEASURED_FG` clear — every non-Streamline title today, and every Streamline title
on the measured route), the headline is **Presented FPS** (`presents / D`, the name for the
one number that stands alone; "Native" and "Displayed" appear only together), with a
mandatory qualifier chosen by a **runtime-module census** run on the Overlay's watchdog
and published in `FlWriterState`. **The census never produces `FL_FG_NONE` or
`FL_UPSCALER_NONE`** — statically linked FSR has no module to see, and a census-`none` on
such a title would print the inflated number rule 6 forbids. It refines the *reason* for
N/A and warns loudly when a frame-generation runtime is loaded and nothing was observed.
The two cases that motivated it print identically today: a 2D title with no upscaler, and
a Streamline title with upscaling switched off in its settings. **`SL loaded + zero calls ≠
off`** (Black Myth: Wukong) is why the second may not become `none` either.

<details>
<summary>The entry as it stood while it was the live head, kept for the reasoning</summary>

**Do GPU sensors work unelevated, without PawnIO?** It used to decide how ADR-9 read to
users. It now decides whether a sentence **already in `README`, `legal/EULA.md` and the
operator consent disclosure** is true.

The two-rung ladder says a Tier-2 session records *"session duration and whatever hardware
telemetry this machine can provide"*. That hedge is there because **nobody has measured
this**. If LHM needs elevation for GPU sensors, the DEFAULT unelevated Agent's Tier-2
session is **duration only** — and the honest wording is narrower than what those three
documents now carry. The hedge is deliberate and it is also a placeholder: it buys
correctness at the cost of telling a user something useful.

`18_GPU_VENDOR_APIS` §P0 item (b) already specifies the test and the dev box can run it. It
needs `LhmTelemetrySource`, which does not exist — so the first slice of P0 item 8 is now
the cheapest way to make three user-facing documents say something definite.

**This heading sits under §Separable on purpose, and it is still the live one.** §M5 is not a
prerequisite of any feature hook — nothing in the queue above waits on it. It is here because
it is the smallest piece of work that makes an already-shipped claim definite, which is a
better place to restart than a decision nobody has taken.

**Two other things are open and neither is this.** Item 6 — written rationales for every
❓/⏳/◐ still in §S24, and unifying the glyphs — is P0 **exit criterion 2** itself and is pure
editorial; take it if you want P0 closer to done rather than more correct. And `fl-probe-signer`
→ §S19(b) above buys the managed drain a place in the merge gate. Both are real; neither has a
claim in a legal document depending on it.

**Why this and not the frame-generation route.** Item 3's producer decision is unblocked and
unmade, and whoever takes it chooses **with no oracle behind the choice** (§S31 row P2, and
`14_TESTING`'s cross-validation gate struck the same day). That is a decision to take
deliberately, not a task to pick up. §M5 is a measurement with a specified method, a machine
that can run it, and a claim already shipped that depends on the answer.

Rest of P0 item 8 telemetry: `BaselineTelemetrySource` (L1), `NvapiTelemetrySource` (L3 — the
material is vendored and `fl_nvapi_probe` proves it works). ~~and the PresentMon binary for
`spike-notes` §11~~ — that section is filled and PresentMon is gone.

~~Also loose: `LibreHardwareMonitorLib` is referenced by `FrameLedger.Infrastructure`
and used by **zero lines**, so it ships into the Agent's output — an MPL-2.0 §3.1
redistribution obligation incurred for no capability. Either L2 gets written or the
reference comes out.~~ — **L2 was written, 2026-09-03.** The obligation now buys eight fields.

</details>

---

## Owner-only — no PR can close these

1. **§S23-2 — branch protection.** `Rules / validate` is not a required status check on
   `main`, so the gate that makes the anti-cheat blocklist un-removable is advisory.
   **Second-order problem:** `rules-publish.yml` filters on four paths for both `push`
   and `pull_request`, so requiring the context as-is blocks every PR touching none of
   them, forever. Land the skip-shim (drop `paths:`, short-circuit inside the job) first;
   the setting is the owner's.
2. **Real-title verification runs** for the upscaler/FG/RT work, against each game's own
   settings menu. ~~**Live as of 2026-09-03:** the `slGetNewFrameToken` producer's table —
   Cyberpunk 2077 off / ×2 / ×4 and Hell Is Us DLSS + FG ×4, one launch each, reading
   `presents/tokens` off the `FG counts:` line against §S31's rows P1–P4.~~ **Run twice,
   2026-09-03 (P4) and 2026-09-04 (P1); done.** What is still owed here is the **quality
   preset** on a title that chains `sl::DLSSOptions` — Alan Wake 2 is the candidate — and
   the DLSS-G identity on a title where `kFeatureDLSS_G` is evaluated, if one exists.
3. **Which titles** go in `blockedExecutables` — a product decision with false-refusal
   consequences. The list ships empty until it is taken.

**Answered 2026-08-05, do not re-ask:** remove `gameguard` and keep `guard` (approved
over a red `Rules` gate, with the reasoning recorded in the merge commit); vendor NVAPI
headers **and** `amd64/nvapi64.lib`; Windows 11 is the **measurement scope** only —
`SupportedOSPlatformVersion` stays at Win 10 22H2 and the D3DKMT two-OS requirement is
deferred with a written rationale.

---

## Two corrections carried forward, because both changed what got built

- **§S29(a) was wrong and had already re-ordered the work.** It claimed the assertion
  catching "a mask bit set with no hook behind it" lives only in
  `ShmDrainIntegrationTests`, which CI skips. It does not: `guard_test.cpp`'s *"the
  injected Overlay records real presents into the ring"* asserts it, as ctest
  `fl_guard`, **20.58 s on CI**. `fl_guard_test.exe` is a *native* host and never loads
  the `protect`-matching .NET assembly — the same §S19(b) mechanism cutting the other
  way. Corrected in place.
- **`README:14`'s RayQuery claim is not a rule-7 contradiction**, which an audit
  asserted. `03_METRICS:128` says AS-build hooking is what makes inline RayQuery
  detectable *at all*. The real contradiction is between CLAUDE.md rule 7 and
  `03_METRICS` — recorded as §S29(f).

---

## Traps that cost a cycle each, in this repo

The mechanical ones live in the toolchain notes; these are the ones that cost a *wrong
diagnosis*.

- **A CHROMIUM-BASED TITLE IS SEVERAL PROCESSES WITH ONE IMAGE PATH.** *Flower in Us* (NW.js /
  RPG Maker) returned `TargetAmbiguous`, exit 6, on 2026-09-03: three processes match the
  consented path. The presenting one is the GPU process, which owns no window — so do not "fix"
  it by picking the windowed one. **Fixed the same day by Chromium's own `--type=gpu-process`
  flag** — and that fix did not fire, because **NW.js has no GPU process**: the GPU runs
  in-process in the browser, the one process Chromium leaves untyped. Second rule: one
  untyped candidate among typed siblings is the browser and the target. **Read the tree before
  guessing the rule** — the refusal line prints it now. Whether the Overlay can load inside
  that browser process is the next thing the title measures.
- **A TITLE ASKS STREAMLINE FOR A FRAME TOKEN SEVERAL TIMES PER FRAME AND GETS A DIFFERENT
  OBJECT EACH TIME.** Cyberpunk 2077: 3 to 4.6 requests per application frame, distinct pointers,
  same index. A count keyed on the pointer reads the request rate and the `off` leg comes out
  at 0.22 (§S31 row P4). Key on the frame index, **and on a NEW index only** — frames in flight
  interleave their requests across threads, so "differs from the last" still over-counts. The stub
  and harness now reproduce both shapes, so the K = 1 control catches a pointer-keyed writer and
  a last-index-keyed one.
- **THE CENSUS IS A STARTUP PROPERTY ON UE5.** Hell Is Us published the identical census word
  at off, FSR and DLSS + FG ×4; Lies of P likewise across its three runs. The plugins load at
  init whatever the settings menu says, so the census can never separate "loaded and off" from
  "loaded and driven through an unhooked path". It was designed not to try; on 2026-09-03 that
  stopped being a design note and became a measurement.
- **STREAMLINE LOADED + ZERO `slEvaluateFeature` CALLS DOES NOT MEAN "UPSCALING OFF".** Black
  Myth: Wukong has `sl.interposer.dll` and `sl.dlss_g.dll` loaded, DLSS-G demonstrably running,
  and never calls the one export this writer hooks. **Hell Is Us made it three of five on
  2026-09-03**: DLSS on, frame generation ×4, 17,920 records carrying the identity bit, zero
  batches drained. So a title with upscaling switched off in
  its settings and a title driving DLSS through an unhooked path produce byte-identical
  records — every one `UNKNOWN` — and the consumer may not turn that into `none` for either.
  The 2026-09-03 census can say which vendor runtimes are *present*; it cannot say what the
  title did with them, and it is written so it never claims to.
- **A vendor keeps the SYMBOL NAME and changes the SIGNATURE, and no gate we have can see
  it.** The Witcher 3 ships `sl.interposer.dll` **1.5.6**: it exports `slInit`,
  `slEvaluateFeature`, `slSetTag` and `slShutdown`, so every name check passes, and it exports
  **none** of `slSetD3DDevice` / `slIsFeatureLoaded` / `slGetNewFrameToken`. `slInit`'s
  `sl::Preferences` has a different layout, so calling it with the vendored 2.x struct
  **access-violates**. `docs/vendor-exports.json` records **one copy per module NAME**, so
  `hookinventory-check` Pass A resolves against one machine's 2.7.4 and says nothing about a
  1.5.6 in a game directory. The guard is `SpeaksStreamline2` in `fl_hook_inventory.h`;
  **do not weaken it to reach an older title.**
- **A vendor enum's zero is not your zero, and this has now happened twice.**
  `D3D12_RAYTRACING_TIER_NOT_SUPPORTED` is 0 against `rtTier`'s "not queried";
  `sl::DLSSMode::eOff` is 0 against `upscalerQuality`'s "not measured". Copying the vendor
  value verbatim publishes an affirmative negative in both cases — and for `upscalerQuality`,
  0 decodes as "DLSS Performance". **Assume the next one has the same shape**; resolve it at
  the writer.
- **`main` has `strict: true` branch protection, so merging ANY pull request makes every
  other one `BEHIND`.** A batch of ready PRs cannot be merged in a loop: each needs
  `gh pr update-branch` after the previous one lands, which re-runs CI (~6–11 min each). And
  every PR that touches `CHANGELOG.md` under `[Unreleased]` — which is every PR touching
  `src/` — conflicts with the one before it. Budget the batch accordingly, or merge one at a
  time and expect to resolve the same conflict repeatedly.
- **`gh pr update-branch` can COMMIT literal conflict markers, and GitHub then reports the PR
  as `BLOCKED` rather than `DIRTY`.** Measured 2026-08-15 on #73: the update-branch merge left
  `<<<<<<<` / `=======` / `>>>>>>>` inside two source files and pushed them, so CI failed with
  `error C2059: syntax error: '<<'` and a redefinition. **`DIRTY` is the status that means
  "conflict", and this was not it** — the PR read as an ordinary red build, so a "re-run CI"
  reflex would never have reached the cause. After any `update-branch`, grep the branch for
  markers *before* reading the checks; the whole tree is one command and it is cheaper than
  one CI round. **Anchor the pattern to the line start** — this very bullet contains all three
  markers inline, so an unanchored sweep reports this file and teaches you to ignore it.
- **Never run a git-manipulating script in the background against the working tree you are
  working in.** A merge helper doing `git checkout` between branches moved the tree under an
  in-flight edit, and a documentation commit meant for one PR landed on another branch and was
  pushed there. Recovery cost a cherry-pick, a `--force-with-lease`, and a re-verification of
  the *wrong* PR to prove none of its own work had been lost. This is wrong by design rather
  than by bad luck — the tree has one HEAD, and a background job racing you for it will
  sometimes win. Use a separate worktree, or keep it in the foreground.
- **Anything that writes a file with a script writes LF, and `.gitattributes` hides it until
  it does not.** `dotnet format --verify-no-changes` is the documented victim, but a Python
  or PowerShell `write` is the usual cause. `git commit` prints *"LF will be replaced by
  CRLF"* and then normalises for you — so the COMMIT is fine and the WORKING TREE is not,
  which is the confusing half. Normalise explicitly after any scripted edit.
- **Never run `cmake`/`ctest` directly — always `./build.ps1`.** Without the MSVC
  environment it imports, configure fails, the build never runs, and ctest executes the
  **stale binary** and prints `Passed`. Cost two wrong diagnoses in one session.
- **Read the build's exit code before the test's**, every time. A canary that fails to
  compile reports "All tests passed" from the previous binary.
- **A canary that dies before reaching the gate looks exactly like a canary that
  worked.** Check *which* step failed.
- **In PowerShell, a backtick inside a double-quoted string is an escape.** A search
  needle containing `` `blockedExecutables` `` silently became something else, `.Replace`
  matched nothing, the file was unchanged — and the validator correctly passed. Print
  the mutated text before validating.
- **`./build.ps1 check` with no switches** after every PR. CI runs `-SkipIntegration`,
  so a green CI is not evidence for anything touching the managed drain.
- **A canary that does not compile is not a canary, and this repo's analyzers make that
  easy to hit.** Removing a call to prove a gate goes red can leave a constructor
  parameter unread — `CS9113` under `TreatWarningsAsErrors` — so the build fails, the
  test run prints "All tests passed" from the **previous binary**, and the canary looks
  like it worked. Measured this way on 2026-08-06 while proving the consent gate.
  Add `_ = param;` to keep it compiling, and read the build's exit code first.
- **A document can go stale by NOT being touched.** This file's queue item 1 described
  work that had just landed, in a PR that changed 69 other files — the failure was the
  edit that was never made. `CHANGELOG.md` and §S24 are gated; this file is not, so it
  is the one that needs a deliberate pass at the end of every item.
- **ONE GAME LAUNCH PER CAPTURE. A capture that ENDS kills the Overlay in that process 65 s
  later, permanently.** When the capture host exits nothing publishes `guardTicks` any more, the
  Overlay's watchdog hits `FL_GUARD_TICK_DEADLINE_MS` and calls `StopObserving` — which clears
  `g_observing` one-way and disables every hook for the life of the process. A second capture
  against the same running game returns `SupervisionLost` with zero records.

  **This is designed behaviour, not a defect** (`19_SAFETY` §During a session), and it is
  recorded here because it changes how a measurement sweep has to be planned: §S31's off / ×2 /
  ×4 comparison needs **three game launches**, not three captures in one. Measured 2026-08-20;
  the second capture of the session hit it within minutes of the first.

- **A LAUNCHER CAN UPDATE THE GAME BETWEEN THE CONSENT GRANT AND THE CAPTURE, and the refusal
  then says something false.** Alan Wake 2, 2026-08-20: consent granted at 09:12Z, the executable
  went from 62,026,752 to 62,304,768 bytes when the title was launched, and the capture refused
  with `ConsentMissing` — signal *"the per-game consent dialog has not been accepted"*, which the
  operator had done forty minutes earlier. `HookRequest.FromConsent` is right to null
  `consentedAt` and right to leave the stored record untouched; the *message* collapses three
  situations that need three different actions. `Program.WhyConsentMissingAsync` now prints
  which one it was. **Re-verify the fingerprint after launching a title, before blaming the
  gate.**

- **`D3D12CreateDevice(WARP)` can fail on a dev box while the real GPU's D3D12 works, and it takes
  two ctests down with it — `fl_d3d12_acquisition` and `fl_guard`'s D3D12 case.** The harness prints
  only `[FAIL] D3D12CreateDevice(WARP)`, which reads like a code regression.

  **Measured on this machine 2026-08-06, and the second measurement contradicted the first
  write-up.** `EnumWarpAdapter` succeeds and returns *Microsoft Basic Render Driver*;
  `D3D12CreateDevice` on it returns **`DXGI_ERROR_DRIVER_INTERNAL_ERROR` (0x887A0020)** at **every**
  valid feature level (12_2 through 11_0; 10_1 and below return `E_INVALIDARG`, which is correct
  because D3D12 has an 11_0 floor). On the **same adapter object**, `D3D11CreateDevice` succeeds at
  FL 11_0, and `D3D12CreateDevice(nullptr, …)` on the RTX 5080 returns `S_OK`.

  > **"A reboot clears it" was written here and is FALSE.** I asserted a remedy I had not tested.
  > The machine rebooted at 13:58 and the identical failure reproduced at 14:17, 19 minutes into the
  > new boot. `d3d10warp.dll`, `d3d12.dll` and `D3D12Core.dll` are all `10.0.29639.1000` and match the
  > OS — **Windows 11 Insider Preview build 26300/29639** — so nothing is mismatched or corrupt, and
  > the system log has no display errors since boot. It reads as an Insider-build regression in
  > WARP's **D3D12** path, and it is persistent.

  > **AND IT STOPPED REPRODUCING, measured 2026-08-20 — which does not retire the trap, it dates
  > it.** `hook-harness --probe-d3d12` passes and `./build.ps1 check` is fully green here, 20/20
  > native tests. **No change in this repository caused that**: the machine moved from Insider
  > build **26300/29639** to **29648**, and `d3d10warp.dll` / `D3D12Core.dll` are both
  > `10.0.29648.1000`. So the fix is upstream and the window was 2026-08-06 → some point before
  > 2026-08-20.
  >
  > Both dates are recorded because the useful fact is not "it works now" — it is that **this
  > dependency broke and healed under the machine without anyone touching the code**, twice
  > giving a native suite a colour that said nothing about `main`. Do not delete this entry when
  > it is green; check which state your build is in.

  **What that does and does not mean for the project.** CI (`windows-latest`, not an Insider build,
  no GPU) runs the same suite on WARP and passes, so `main` was never broken and no code change was
  indicated. What it does mean is that **the native suite has a hard dependency on WARP D3D12 that a
  dev box can lose on its own**, and that a red `fl_d3d12_acquisition` is not evidence about the
  code until the two adapters have been probed separately. That probe is ~40 lines of P/Invoke and
  settles it in one run; do that before reading the harness's output as a finding.
- **A NATIVE test can go red because the RUNNER did not schedule two threads, and it looks
  like a code regression.** `ring_test.cpp`'s *"a tiny ring, so the writer laps the reader
  constantly"* ends with `CHECK(dropped > 0)` — an **anti-vacuity** assertion, not a property
  of the ring. Its own comment says why: *"with 8 slots the writer MUST have lapped us, so a
  run reporting no drops means the threads never actually overlapped and the case proved
  nothing."*

  It fired on CI on 2026-08-28, on a pull request that **touched no native source and no
  test** — `ctest` reported `1 tests failed out of 23`, `build.ps1` threw `ctest failed (exit
  8)`, and the run before and the run after were both green. Eleven of the twelve CI runs
  around it passed, two of them on `main` within minutes.

  **The tell is which assertion fired.** A ring defect fails `CHECK_FALSE(mixed)` or
  `CHECK_FALSE(poisonAccepted)` — the properties. A hosted runner that did not overlap the
  threads fails only the guard at the end. Read the assertion before reading the diff: this
  is the integration-budget trap (*"no budget may be sized on the harness's measured rate"*)
  one layer down, in the **native** suite, where §Traps did not have it.

  Do **not** relax it to `>= 0`. The guard is what stops the case passing while proving
  nothing, and `ring_test.cpp`'s own header already records that two of its four properties
  have never been shown red.

- **A self-test's cases can all pass while testing nothing, and their NAMES will not tell
  you.** Found 2026-08-27 in a PowerShell gate's `-SelfTest`: removing the fail-closed guard
  from a hash comparison left **all ten cases green**. `[string]::Equals` already refuses
  empty-vs-present, so three cases named *"an empty hash fails closed"*, *"a null hash fails
  closed"* and *"an empty EXPECTATION fails closed too"* were testing `Equals`, not us.

  **The case the guard actually carried was the one nobody wrote:** `Equals('','')` is
  **`true`**. That is the §S23-1 shape — two empty build ids compared equal and
  `ShmHandshakeValidator` reported `Ok` for every process on the machine — arriving in a
  different language.

  *"A gate never observed failing is a comment"* applies to a gate's **own tests**. Plant the
  canary in the thing under test, not in the caller, and check that it fails on exactly one
  case: if it turns several red, the cases are coupled and none of them is pinning what its
  name claims.
- **A failing `REQUIRE` used to END THE WHOLE BINARY, so one red test hid every test
  after it.** Fixed 2026-08-09, and recorded because the symptom was invisible: `ctest`
  reported *"1 test failed"* when the truth was *"and 2 never ran"*.

  Catch2 compiled itself with `CATCH_CONFIG_DISABLE_EXCEPTIONS` — the top-level
  `src/native/CMakeLists.txt` strips `/EHsc` from `CMAKE_CXX_FLAGS` deliberately, and
  `tests/CMakeLists.txt` added it back **only to the test binaries**, never to the
  Catch2 library that decides the mode. Measured off the generated command line:
  `... -std:c++20 -MT -Zi` with no `/EH` flag at all. In that mode a failed `REQUIRE`
  calls `std::terminate`.

  On this box, where WARP's D3D12 path is broken, that meant `fl_guard`'s D3D12 case
  killed the run and took the end-to-end injection cases with it — **including the
  honesty assertion §S29(a) names as the merge gate's coverage**, which passes fine
  when run on its own. `target_compile_options(Catch2 PRIVATE /EHsc)` recovered
  **70 test cases and 1051 assertions**. If you see a suspiciously small assertion
  count next to a failure, this is the shape to look for.
- **No budget in the integration tests may be sized on the harness's measured rate.** Five were, and
  every one went red under load while nothing was wrong: the suite runs four test assemblies in
  parallel, each spawning a harness and injecting an Overlay that creates a WARP device, so the
  presenting thread is descheduled far longer than a rate-derived constant allows. Wait for the
  **state**, bounded by a wall clock generous enough to be about the state rather than about the
  machine.
- **When one of those fails, capture the MESSAGE before changing anything.** #62 spent two rounds
  applying the remedy for a race to what turned out to be a loop bound and its assertion disagreeing
  by one — the loop exited at exactly 10 and the assertion demanded more than 10, so timing decided
  whether the off-by-one was visible. **A defect class is a hypothesis about the next failure, never
  a diagnosis of it.** One run with the message printed settles what four runs of pattern-matching
  cannot.
- **A test that reads a writer state ONCE is racing `InitThread`.** `layoutVersion` is published at
  step 2 and `status` becomes `READY` at step 6, with a WARP device creation in between; `apiMask` is
  set later still, on the first present the hook sees. So `TryAttach` succeeding, `guardTicks`
  advancing, `status == INIT` and `apiMask == 0` are all legitimate simultaneous states. Poll for the
  state you mean, and keep the timeout failing — `INIT` past the budget is
  `WriterNeverInstalledHooks`, not slowness.
- **Files written with LF fail `dotnet format --verify-no-changes`** even though the diff
  looks identical: `.editorconfig` mandates CRLF and `.gitattributes` normalises on
  checkout, so the working tree disagrees with both. Run `dotnet format` (no switch)
  before `check`, and normalise `.csproj`/`.ps1` by hand — the formatter does not touch
  those.
- **GPG can stall.** `git commit` fails with `signing failed: Timeout`, and `git push`
  then reports success while pushing only the old HEAD. Never pass `--no-gpg-sign`; just
  retry the commit and read *its* exit code.
- **The squash trap.** After a squash-merge the branch you were on is not an ancestor of
  `main`; a PR from it reports CONFLICTING and runs **no CI at all**. Read "no checks
  reported" as a symptom of that. Always `git fetch origin` then branch from
  `origin/main`; capture old base SHAs *before* merging a stacked pair.
- **Do not pass `--delete-branch`**: `main` is checked out in the primary worktree,
  which blocks it. GitHub deletes the remote branch anyway.

---

## Keeping this file honest

It goes stale the moment it starts describing state. If you find yourself writing "X is
done" here, that belongs in `CHANGELOG.md` or §S24 — put it there and write a pointer.
The only things that belong here are: **what to do next and in what order**, **why that
order**, **decisions that live in no other file**, and **traps**.
