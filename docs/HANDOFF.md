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

---

## The queue, in dependency order

Each entry names the acceptance criterion and **what makes it fail on unmodified
`main`** — because a criterion already true on `main` is decoration, and this project
has shipped three of those.

### 1. Consent store + the first production driver of the guard loop

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

### 2. Upscaler hooks + a harness that speaks the vendors' symbol names

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
