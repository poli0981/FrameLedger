# 02 — Specification

Requirement IDs (`FR-x`, `NFR-x`) are referenced by other docs, commits, and tests.

## Functional requirements

### FR-1 Game library
- FR-1.1 Add game by picking an `.exe`, or drag-dropping an exe/shortcut.
- FR-1.2 Auto-import from Steam, GOG Galaxy, Epic, itch.io with a review checklist. **Import never enables hooking for anything.**
- FR-1.3 Editable metadata (name, cover, publisher, version, notes); auto-detected fields badged and overridable.
- FR-1.4 Remove game (asks whether to keep or delete its sessions).
- FR-1.5 "Supports" row shows shipped-capability hints (DLSS/FSR/XeSS/FG) — visually distinct from measured per-session values (`05_DETECTION` §Capability hints).

### FR-2 Hooking consent & control (new, safety-critical — see `19_SAFETY`)
- FR-2.1 Hooking is **off by default for every game**. Enabling requires a per-game consent dialog stating what is injected, why, and the anti-cheat/ban risk.
- FR-2.2 The static anti-cheat pre-scan disables the toggle outright for titles shipping anti-cheat; the reason is shown and the control is not clickable.
- FR-2.3 The runtime guard refuses injection and refuses to continue a session when anti-cheat is detected. **No override exists anywhere in the UI, config, or CLI.**
- FR-2.4 Global "disable all hooking" kill switch in Settings; also honored by the Vulkan layer.
- FR-2.5 Repeated crashes shortly after injection auto-disable hooking for that game, with an explanation and a manual re-enable path.
- FR-2.6 Users can always see, per session, which tier produced the data.

### FR-3 Session capture
- FR-3.1 Watcher detects tracked exe launches (1 Hz) and captures automatically at the highest permitted tier.
- FR-3.2 Launch mode: start the game from FrameLedger for suspended-launch injection (catches startup-time upscaler init).
- FR-3.3 Attach mode for games launched externally; sessions flagged `late_attach`.
- FR-3.4 Track the process tree; capture the descendant that actually presents.
- FR-3.5 Per-frame records for the whole session; telemetry at 1 Hz (configurable 0.5–2 s).
- FR-3.6 Session ends on process-tree exit, safety unhook, or user stop. Sessions under the minimum length (default 30 s) are discarded.
- FR-3.7 Crash detection: nonzero exit code or matching Application Error 1000 / WER 1001 events → `exit_status = crashed`.
- FR-3.8 Post-session toast with summary + "View".
- FR-3.9 Pause/resume capture globally from tray.

### FR-4 Metrics (definitions in `03_METRICS`, which is authoritative)
- FR-4.1 Per session: Avg, Median, Min, Max FPS; 1% Low; 0.1% Low; frametime σ; stutter count and time %; frame counts; duration.
- FR-4.2 **Native vs Displayed FPS + FG factor**, always shown together (`62 → 118 FPS (×1.9 FG)`).
- FR-4.3 *(Tier 1)* Measured upscaler identity, quality preset, render resolution, output resolution, upscale ratio; mid-session changes captured as segments.
- FR-4.4 *(Tier 1)* Measured RT activity (`rt_frame_pct`, `rays_per_pixel`, RT PSO count); RR from the NGX feature; PT as a confidence-scored suggestion only.
- FR-4.5 *(Tier 1)* Per-process VRAM usage/budget and budget-exceeded percentage.
- FR-4.6 *(Tier 1)* PSO-compilation stutter attribution; *(Tier 1 + Reflex)* PC latency avg/p95.
- FR-4.7 Telemetry aggregates: CPU/GPU temp, hotspot, loads, power, throttle %, RAM.
- FR-4.8 Sufficiency guards: 0.1% Low needs ≥ 10,000 app frames, 1% Low ≥ 1,000; else `N/A`.
- FR-4.9 Any metric unavailable at the session's tier renders `N/A` — never an estimate presented as measurement.

### FR-5 Visualization
- FR-5.1 Session summary: stat cards + frametime timeline + FPS histogram + sensor overlay + segment ribbon.
- FR-5.2 Game detail: frametime, distribution, percentile curve, trend over sessions with hardware-change markers, sensors, and *(Tier 1)* a latency tab.
- FR-5.3 Charts handle ≥ 500k points via min/max decimation; zoom/pan; PNG export.
- FR-5.4 Native/Displayed toggle wherever FG data exists.
- FR-5.5 Stutter markers annotated with cause when known (`PSO compile`, `VRAM budget exceeded`).

### FR-6 History & comparison
- FR-6.1 Sessions table per game: date, duration, tier badge, Native, Displayed, FG×, lows, resolution + upscaler, RT/PT/RR chips, crash badge, tags.
- FR-6.2 Compare 2–5 sessions; **mixed-tier comparisons require explicit acknowledgement** and are marked on the chart.
- FR-6.3 Hardware snapshot per session; trend charts mark changes ("GPU driver 572.16 → 576.02").
- FR-6.4 Sessions with mid-session settings changes are excluded from trends by default, with a toggle.
- FR-6.5 Session tags and notes.

### FR-7 Detection
- FR-7.1 Static: engine, engine version, store platform, store id, publisher, game version (`05_DETECTION`).
- FR-7.2 Runtime: API, present mode, swap effect, HDR, upscaler, FG, RT — all Tier 1, all measured.
- FR-7.3 Rules ship as `detection-rules.json`, updatable independently of releases; **anticheat-block updates apply regardless of the user's rules auto-update preference.**

### FR-8 Tri-state flags
- FR-8.1 `Yes / No / N/A` for Ray Tracing, Path Tracing, Ray Reconstruction, per session, with source (`measured | manual | inherited`).
- FR-8.2 Auto values require positive evidence; PT never auto-promotes to `Yes`.
- FR-8.3 Manual override per session and per game (game default inherited by future sessions), visually distinct from measured values.

### FR-9 Export
- FR-9.1 Per-frame CSV including tier, resolutions, upscaler, RT and latency columns.
- FR-9.2 Session JSON (metadata + aggregates + segments).
- FR-9.3 Chart PNG.

### FR-10 Settings
Language (en/vi/ja), theme, start with Windows, minimize to tray, background capture, **global hooking kill switch**, min session length, telemetry interval, retention, Agent elevation (optional — explains what it unlocks), Vulkan layer registration state, update channel, online metadata opt-in, reopen legal documents.

### FR-11 Legal Gate
First run blocks until the user accepts EULA, GPLv3 notice, Disclaimer, Privacy Policy. Re-shown when a document version increments. The injection risk is stated in the Disclaimer *and* repeated at per-game consent (FR-2.1) — once is not enough for something that can cost an account.

### FR-12 Updates
Velopack against GitHub Releases; silent startup check, manual check in Help; error dialogs mapped per `11_UPDATER`. **Updates never apply while a game is hooked** — deferred to session end (the DLL on disk must not change under a running game).

### FR-13 Logging & bug reports
Rolling Serilog logs per process plus native overlay logs; in-app viewer; bug bundle zip + prefilled GitHub issue (`10_LOGGING`). Bundles include the overlay log and hook fault details, which are what make injection bugs diagnosable.

### FR-14 Tray & notifications
Tray icon states (idle / capturing T1 / capturing T2 / paused); menu; toasts for session saved and update available. **Safety events (`CaptureRefused`, `SafetyUnhook`, hook auto-disable) are never toasts** — they get a dedicated dialog or a persistent `ui:InfoBar` that cannot be dismissed by timeout (`08_UI` §Notifications policy, `07_IPC` §Client behavior).

### FR-15 In-game overlay *(v1.1, opt-in, off by default)*
Draw live FPS/frametime/upscaler info inside the game. Only for games already hooked; a separate toggle; never on by default. Deferred until the data path is stable (`15_ROADMAP` P5).

## Non-functional requirements

- **NFR-1 Game-side overhead:** ≤ 1 µs per present, ≤ 8 MB resident, measured game FPS impact ≤ 0.5% vs uninstrumented.
- **NFR-2 Agent overhead:** ≤ 1% of one core during capture, ≤ 150 MB working set, ~0% idle.
- **NFR-3 Stability:** faults **originating in our own hook bodies** are contained by `FL_HOOK_GUARD`, counted, and self-disable the Overlay after 3 occurrences; the safety-unhook path completes within one frame. The Overlay adds no allocation, lock, syscall or logging to the present path.

  > This deliberately does **not** say "must never crash a game" (`20_OPEN_QUESTIONS` §H8). SEH containment cannot reliably catch stack overflow, cannot intercept `__fastfail` — and `-D_HAS_EXCEPTIONS=0` converts a would-be STL throw into exactly that (`spike-notes.md` §H3) — and does not help when the game installs a vectored exception handler that runs first. An absolute promise the mechanism cannot support does not belong in a requirement, and must stay out of user-facing text: the Disclaimer already says injection carries risk, and that wording is the honest one.
- **NFR-4 UI performance:** cold start ≤ 2 s; opening a game with 100 sessions ≤ 500 ms; charts interactive at 60 fps post-decimation.
- **NFR-5 Storage:** raw series ≤ 3 MB per hour compressed; 50 games × 50 sessions ≤ 500 MB at default retention.
- **NFR-6 Accuracy:** per `03_METRICS` §Accuracy budget, stated per tier.
- **NFR-7 Resilience:** power loss loses at most the last 60 s of raw data; DB never corrupts (WAL + transactions); Agent restart recovers `.partial` sessions as `interrupted`.
- **NFR-8 OS support:** Windows 10 22H2+ and Windows 11, x64. No ARM64 in v1.
- **NFR-9 Accessibility:** full keyboard navigation, adequate contrast in both Fluent themes, Per-Monitor V2 DPI awareness.
- **NFR-10 Offline-first:** everything except updates and opt-in metadata works with zero connectivity.
- **NFR-11 Auditability:** the entire set of hooks is enumerable in `17_HOOK_ENGINE` §Hook inventory and matches the code; a reviewer must be able to verify in minutes that nothing reads game memory.

## Out of scope

**Permanently:** any evasion technique (`19_SAFETY`); reading/writing game memory; kernel drivers; hardware control (overclocking, fan curves, power limits — `18_GPU_VENDOR_APIS` is read-only); online/competitive titles.

**v1:** Microsoft Store/UWP/Game Pass titles (packaged-app injection constraints); macOS/Linux; cloud sync; benchmark automation; ARM64. **Tier-1 capture of 32-bit games, including most Direct3D 9 titles** — the Overlay is x64-only and an x64 DLL cannot be loaded into a 32-bit process; those titles are Tier 2 (`17_HOOK_ENGINE` §Build profile, `20_OPEN_QUESTIONS` §Scope).
