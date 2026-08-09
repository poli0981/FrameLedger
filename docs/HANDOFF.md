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
> 1. **`./build.ps1 check` cannot go fully green here.** `fl_d3d12_acquisition` and
>    `fl_guard`'s D3D12 case fail because WARP's D3D12 path is broken on this Windows
>    Insider build — measured, persistent across a reboot, and detailed in §Traps. CI runs
>    the same suite on WARP and passes. Use `./build.ps1 managed` for the managed half and
>    read the native failures against that list before treating one as a finding.
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

### 2b. The params half — **START HERE**, and the route is already decided

`FL_MEASURED_UPSCALER_PARAMS` has no producer, which is what P0 exit criterion 1
actually needs. Designed 2026-08-09 by a panel plus three refuters; **the obvious route
was killed on fatal grounds and the reasoning is the part that lives in no other file.**

**DO NOT hook `slGetFeatureFunction` and MinHook the returned `slDLSSSetOptions`.**
It is the route `17_HOOK_ENGINE` reads as natural, and it fails five ways:

1. It needs an "indirect" inventory class, which becomes an **unconditional escape hatch
   from `hookinventory-check` Pass A** — its oracle would be "a `PFun_*` exists in a
   vendored header", and `sl_core_api.h:63` declares `PFun_slSetTagForFrame` for a symbol
   **zero measured modules export**. Any name the headers declare could be laundered past
   the gate.
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

P0 item 8 telemetry: `BaselineTelemetrySource` (L1), `LhmTelemetrySource` + the M5
question (do GPU sensors work unelevated without PawnIO — it decides whether the
default Agent has temperatures at all), `NvapiTelemetrySource` (L3 — the material is
vendored and `fl_nvapi_probe` proves it works), and the PresentMon binary for
`spike-notes` §11, the cheapest unfilled section in that file.

Also loose: `LibreHardwareMonitorLib` is referenced by `FrameLedger.Infrastructure`
and used by **zero lines**, so it ships into the Agent's output — an MPL-2.0 §3.1
redistribution obligation incurred for no capability. Either L2 gets written or the
reference comes out.

---

## Owner-only — no PR can close these

1. **§S23-2 — branch protection.** `Rules / validate` is not a required status check on
   `main`, so the gate that makes the anti-cheat blocklist un-removable is advisory.
   **Second-order problem:** `rules-publish.yml` filters on four paths for both `push`
   and `pull_request`, so requiring the context as-is blocks every PR touching none of
   them, forever. Land the skip-shim (drop `paths:`, short-circuit inside the job) first;
   the setting is the owner's.
2. **Real-title verification runs** for the upscaler/FG/RT work, against each game's own
   settings menu.
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

  **What that does and does not mean for the project.** CI (`windows-latest`, not an Insider build,
  no GPU) runs the same suite on WARP and passes, so `main` is not broken and no code change is
  indicated. What it does mean is that **the native suite has a hard dependency on WARP D3D12 that a
  dev box can lose on its own**, and that a red `fl_d3d12_acquisition` is not evidence about the
  code until the two adapters have been probed separately. That probe is ~40 lines of P/Invoke and
  settles it in one run; do that before reading the harness's output as a finding.
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
