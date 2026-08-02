# 15 — Roadmap

The hook rewrite front-loads risk: almost everything uncertain is in P0/P1. That is deliberate — a hook layer that doesn't work is not a feature you discover in week four.

## P0 — Spike (4–6 days) · *gates everything*

Findings written to `docs/spike-notes.md`. Nothing in P1 starts until the exit criteria pass.

1. **Hook viability.** `hook-harness` (D3D11 + D3D12) + MinHook: dummy-device vtable probe, verify present indices at runtime, install/uninstall cleanly, measure per-present cost. Confirm `/MT` DLL loads into a real (offline, non-AC) game without incident.
2. **The accuracy question — the reason this rewrite exists.** On the dev machine (RTX 5080), verify against ≥ 3 real offline titles that hooks recover: NGX/Streamline feature identity, render vs output resolution, quality preset, and DLSS-G activity. Compare against what the *old* file/module-based detection reported. **Quantify the improvement** — this number justifies the whole trade-off and belongs in the README.
3. **Vendor SDK reality check.** Resolve actual exported symbol names for NGX, Streamline, FFX (`ffx_api` vs legacy FSR2/3), XeSS on the dev machine. The names in `17_HOOK_ENGINE` are conventions, not verified facts — correct the doc.
4. **RT detection.** Harness + a real DXR title: `DispatchRays` counting *and* `BuildRaytracingAccelerationStructure`; verify the AS-build path catches an inline-RayQuery title that dispatch counting misses.
5. **Frame Generation ground truth.** Compare rung 1 (API) against rung 2 (`GetFrameStatistics` present delta) against Tier-2 ETW `FrameType` on a DLSS-G title. Decide whether rung 2 is reliable enough to ship for driver-level FG (AFMF).
6. **Telemetry layering.** Fill the `18_GPU_VENDOR_APIS` capability matrix on real hardware:
   - L1 baseline (DXGI + PDH counters) working vendor-neutrally; decide whether the `D3DKMT` perf-data probe is stable enough on Win 10 **and** Win 11 to keep.
   - L2: which fields LibreHardwareMonitor actually returns per vendor, and **whether GPU sensors work unelevated without PawnIO** — this decides whether the default unelevated Agent has temperatures.
   - L3: NVAPI linked from vendored MIT headers; Reflex latency, throttle reasons, per-domain utilisation.
   - **Licence confirmations:** LHM free of MPL-2.0 Exhibit B; NVAPI SPDX blocks intact; `tools/license-check` in place.
7. **Vulkan layer.** Minimal implicit layer intercepting `vkQueuePresentKHR`, registered under `HKCU`, with the opt-in check that keeps it passthrough for non-enabled processes.
8. **Guard prototype.** Module + driver enumeration, blocklist matching, fail-closed behavior on every error path.

**Exit criteria:** a throwaway build records a real session from a real offline game reporting *correct* upscaler, quality preset, render→output resolution, FG factor and RT state — verified against the game's own settings menu — with measured game FPS impact ≤ 0.5%.

## P1 — Native core (1.5 weeks)
`FrameLedger.Overlay` proper: hook installation for D3D11/12/9/OGL, feature hooks, ring writer, fault policy, unhook path, native logging. `FrameLedger.Injector` with launch + attach modes. **The guard, complete and fully tested** (`14_TESTING` §Safety-guard tests) — it ships before the first real injection, not after. Struct mirror + Catch2 tests. Vulkan layer to parity.

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
