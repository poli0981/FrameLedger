# 15 — Roadmap

The hook rewrite front-loads risk: almost everything uncertain is in P0/P1. That is deliberate — a hook layer that doesn't work is not a feature you discover in week four.

> **Read `docs/20_OPEN_QUESTIONS.md` alongside this file.** It holds the audit
> findings that P0 exists to answer. The §R resequencing proposals are now folded
> into the order below rather than sitting as a proposal: the guard is item 0,
> the Vulkan passthrough test is item 1, and the two licence checks — which
> needed no hardware and could each have invalidated an entire telemetry layer —
> were run first and came back clear.

## P0 — Spike · *gates everything*

Findings written to `docs/spike-notes.md`. Nothing in P1 starts until the exit criteria pass.

> **Status as of 2026-08-03.** Items 0, 1 and 2 partly. Everything still
> open needs either a real game (2, 4, 5, 6, 7), absent hardware (8, and the
> AMD/Intel half of the capability matrix), or is P1 by construction (the
> layer's presentation hooks). The safety work that had to precede the first
> injection — the guard, its matrix, the chokepoint, the layer's gates — is in.

0. **The guard.** Module + driver enumeration, blocklist matching, fail-closed behaviour on every error path. **Moved from item 8**: items 1 and 6 below inject into real games, and CLAUDE.md rule 2 plus P1's own "it ships before the first real injection, not after" both forbid that ordering. **◐ Three of four pre-injection checks.** The guard is built (`FrameLedger.Injector`, native per §S13(a)), owns the chokepoint, and its fail-closed matrix is Catch2-driven. The injection primitive landed after it, in that order. Reached from managed code through one P/Invoke facade — never a second matcher (§S15). Evidence: `spike-notes.md` §1; §S7, §S8, §S16 closed.

   > **This line said ✅ DONE, and it was wrong in a way worth recording.**
   > `19_SAFETY` specifies four pre-injection checks. **Check 4** (the static
   > pre-scan) had no implementation at all — its two reason codes were
   > declared, named and mirrored into the managed enum while nothing produced
   > either, so three artifacts agreed on a behaviour no code had. **Check 3**
   > (per-title lists) is worse than §S14 recorded: its matchers have no call
   > site, so it is *unwired*, not merely unpopulated.
   >
   > Check 4 is now implemented and runs inside the chokepoint. **Check 3 is
   > still unwired**, so this item stays ◐ until it is, and the status will not
   > read ✅ again on the strength of "most of it works".
1. **Vulkan layer passthrough.** Minimal implicit layer registered under `HKCU`, with the opt-in checks that keep it passthrough for non-enabled processes. **Moved from item 7**: a passthrough bug loads FrameLedger into every Vulkan process on the machine, which is the highest blast radius in the spike. **◐ Gates done, interception not started.** `enable_environment` measured against loader 1.4.357, blast radius verified, in-layer blocklist self-scan built and proven both directions. `vkQueuePresentKHR` is **not** hooked — that is P1, and §S2's in-layer supervision check lands with it.
2. **Hook viability.** `hook-harness` (D3D11 + D3D12) + MinHook: dummy-device vtable probe, verify present indices at runtime, install/uninstall cleanly, measure per-present cost. Confirm `/MT` DLL loads into a real (offline, non-AC) game without incident. **✅ DONE.** Vtable indices proved by behaviour (§H4), unhook proved not to clobber a later hooker (§H7), per-present cost measured at 8.4 ns against a 1,000 ns budget — and **the `/MT` DLL is now loaded into a real title**: Lies of P, attach mode, guard passing, module verified present by re-enumeration, game still rendering afterwards (`spike-notes.md` §7).

   > **Attach mode only.** Launch mode is blocked by §S18 — the Agent is the
   > game's parent and hosts `FrameLedger.Guard.dll`, whose name trips the
   > guard's own `guard` fragment. Found by this run.
   >
   > Three refusals came before the success, and each was a real defect: a
   > machine-wide `EasyAntiCheat_EOS` service that refused every process on the
   > machine, a failed injection that reported `Allow`, and §S18. None would have
   > surfaced without a real machine and a real title.
3. **The accuracy baseline.** Build a **minimal static-hint detector** — passive file/module scanning, no injection — as the thing item 4 measures against. Added to P0 scope 2026-08-02 (`20_OPEN_QUESTIONS` §M9): the "old detection" this roadmap assumed as a baseline does not exist in this repository, and without it the comparison below cannot be made and ADR-7's founding claim is unfalsifiable. It needs no guard and no injection, so it can be built at any point before item 4. **◐ The module/file half is built** — `fl-baseline-probe`, ctest `fl_baseline_probe`, proven in both directions and reusing the guard's own measured enumerator (`spike-notes.md` §8). What it produces is the baseline *record*; the per-title rows need the same offline title item 2 does. **The engine/platform/capability rule evaluator is not built** — that is the inference half, and it answers none of item 4's runtime questions.

   > **A finding from building it, recorded before the README is drafted:** the
   > baseline can answer **none** of item 4's four runtime questions (upscaler
   > identity, quality preset, render→output resolution, FG activity). A loaded
   > `nvngx_dlss.dll` means the title *can* use DLSS, not that it is on. So
   > "quantify the improvement" below cannot honestly be a percentage; the
   > defensible claim is **"the baseline cannot answer four of these five
   > questions at all"**. `spike-notes.md` §8 carries the reasoning.
4. **The accuracy question — the reason this rewrite exists.** On the dev machine (RTX 5080), verify against ≥ 3 real offline titles that hooks recover: NGX/Streamline feature identity, render vs output resolution, quality preset, and DLSS-G activity. Compare against what the item-3 baseline reports. **Quantify the improvement** — this number justifies the whole trade-off and belongs in the README.
5. **Vendor SDK reality check.** Resolve actual exported symbol names for NGX, Streamline, FFX (`ffx_api` vs legacy FSR2/3), XeSS on the dev machine. The names in `17_HOOK_ENGINE` are conventions, not verified facts — correct the doc.
6. **RT detection.** Harness + a real DXR title: `DispatchRays` counting *and* `BuildRaytracingAccelerationStructure`; verify the AS-build path catches an inline-RayQuery title that dispatch counting misses.
7. **Frame Generation ground truth.** Compare rung 1 (FG feature evaluations per present) against Tier-2 ETW `FrameType` on a DLSS-G title. ~~rung 2 (`GetFrameStatistics` present delta)~~ was removed as structurally impossible, not merely unreliable (`03_METRICS` §Frame Generation). Driver-level FG (AFMF) is undetectable at Tier 1 in v1; whether PresentMon 2.x `FrameType` sees it at Tier 2 is `20_OPEN_QUESTIONS` §M1 — and is untestable on this dev machine, which has no AMD GPU.
8. **Telemetry layering.** Fill the `18_GPU_VENDOR_APIS` capability matrix on real hardware:
   - L1 baseline (DXGI + PDH counters) working vendor-neutrally; decide whether the `D3DKMT` perf-data probe is stable enough on Win 10 **and** Win 11 to keep.
   - L2: which fields LibreHardwareMonitor actually returns per vendor, and **whether GPU sensors work unelevated without PawnIO** — this decides whether the default unelevated Agent has temperatures.
   - L3: NVAPI linked from vendored MIT headers; Reflex latency, throttle reasons, per-domain utilisation.
   - ~~**Licence confirmations:** LHM free of MPL-2.0 Exhibit B; NVAPI SPDX blocks intact~~ — **done before P0 began, both clear** (`spike-notes.md` §0). `tools/license-check` is in place and proven to fail on a planted violation.

*(The former items 7 "Vulkan layer" and 8 "Guard prototype" are now items 1 and 0. §R1/§R2 are folded in, not pending.)*

**Exit criteria:** a throwaway build records a real session from a real offline game reporting *correct* upscaler, quality preset, render→output resolution, FG factor and RT state — verified against the game's own settings menu.

> **The FPS-impact criterion moves to the end of P1** (`20_OPEN_QUESTIONS` §R4,
> decided 2026-08-02). "Records a real session" with an Agent CPU/RSS budget
> silently imported the drain, aggregate and recorder paths that P2 delivers, so
> as written P0 could not exit without building most of P2. The measurement
> itself is unchanged (`14_TESTING` §Hook overhead ≤ 0.5%); only its gate moves,
> to the point where a real Overlay and a real drain exist to measure.
>
> The **harness-level** per-present cost (≤ 1 µs, hooked vs unhooked, uncapped)
> stays in P0 as item 2 — it needs no game, no Agent and no drain.

## P1 — Native core (1.5 weeks)
`FrameLedger.Overlay` proper: hook installation for D3D11/12/9/OGL, feature hooks, ring writer, fault policy, unhook path, native logging. `FrameLedger.Injector` with launch + attach modes. Struct mirror + Catch2 tests. Vulkan layer to parity — `vkQueuePresentKHR` plus the in-layer supervision check §S2 gates on it.

> **The guard already shipped, in P0.** This line used to read "the guard,
> complete and fully tested — it ships before the first real injection, not
> after", which was the correct ordering stated in the wrong phase: P0 items 1
> and 6 inject into real games. It moved to item 0 and is done. What P1 still
> owes the guard is the launch-mode decision (§S13(c)) and the mid-session
> re-scan wired to a real Overlay.

## P2 — Capture pipeline (1 week)
Agent: watcher, tier selection, injection orchestration, shm drain, telemetry poller (NVAPI first), session recorder, segments, `.partial` recovery, Tier-2 `EtwFrameSource` retained as fallback. SQLite v2 schema + migrations. Domain metric calculators + golden tests. **Milestone: first real hooked session persisted with measured upscaler/RT data.**

## P3 — UI (1 week)
WPF UI shell (`16_WPFUI_SYNTAX`), library, per-game hooking consent flow, Dashboard live card showing *measured* settings, session summary, ScottPlot charts, game detail tabs, Compare with tier guarding, segment ribbon, tri-state chips with override, refusal/degradation/unhook notices as first-class UI.

## P4 — Detection & product polish (1 week)
Static rules engine + `detection-rules.json` (engines, platforms, capabilities, **anticheat blocklist**) + validator + fixtures; store auto-import; capability-vs-measured UI separation; i18n en/vi/ja; Legal Gate; Velopack + updater (with the hooked-session deferral); bug bundle incl. overlay logs; tray + toasts; Settings; DB maintenance; accessibility pass.

## P5 — Ship (3–4 days)
CI/CD with the native build; CodeQL (C# + C++); Dependabot; CHANGELOG; README with the P0 accuracy comparison; overhead + tier cross-validation measurements; release smoke on clean VMs; `v0.1.0-beta.1`.

**Total ≈ 5.5–6.5 weeks full-time** — roughly 1.5–2 weeks more than the ETW design, which is the honest price of the accuracy.

## v1.1 — earned features (only after the data path is stable)

- **In-game overlay** (FR-15): we are already in the process; draw live FPS/frametime/upscaler/RT state. Opt-in per game, off by default. Borderless and fullscreen-flip both handled since we own the swapchain.
- **PSO stutter deep-dive**: attribute compile stalls to specific pipeline creations with a timeline.
- Unknown-game suggestion (Tier 2 only — never suggests hooking).
- Per-segment comparison ("before/after I changed DLSS Quality → Balanced, mid-session").

## v2 backlog

- PresentMon Service + API2 for a richer Tier 2
- **YouTube overlay export**: frametime/FPS graph as transparent PNG sequence for DaVinci Resolve benchmark videos (SkullMute workflow)
- CapFrameX-compatible CSV import/export
- Microsoft Store / Game Pass titles (packaged-app injection constraints)
- AFK segmentation via `GetLastInputInfo`
- GPU frame time via timestamp queries (needs care: injecting queries into the game's command lists is more invasive than anything in v1 — needs its own risk review)
- Beta update channel; ARM64

## Permanently out of scope

Evasion of any kind; game memory access; kernel drivers; hardware control; online/competitive titles. See `19_SAFETY_AND_ANTICHEAT.md` — these are not "later", they are never.
