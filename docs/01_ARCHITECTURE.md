# 01 — Architecture

## Component model

```
┌──────────────────────────┐   named pipe    ┌──────────────────────────────┐
│ FrameLedger.exe (UI)     │  FrameLedger.v2 │ FrameLedger.Agent.exe        │
│ standard user            │◄───────────────►│ standard user (elev. optional)│
│ WPF · Wpf.Ui · ScottPlot │                 │ ├ ProcessWatcher              │
└──────────────────────────┘                 │ ├ AntiCheatGuard* ◄─ 19_SAFETY│
                                             │ ├ Injector (launch / attach)  │
              SQLite (WAL)  ◄────────────────┤ ├ ShmReader (10 Hz drain)     │
                                             │ ├ VendorTelemetry (NVAPI/…)   │
                                             │ ├ SessionRecorder             │
                                             │ └ Tier2 EtwSource (fallback)  │
                                             └───────────▲──────────────────┘
                                          shared memory  │  Local\FrameLedger.Ring.<pid>
                                        (lock-free SPSC) │
┌────────────────────────────────────────────────────────┴──────────────────┐
│ GAME PROCESS                                                              │
│  FrameLedger.Overlay.dll (C++20, injected)   or   VK_LAYER_FRAMELEDGER_*    │
│   ├ present hooks: DXGI (D3D11/12) / OpenGL  (Vulkan: implicit layer)     │
│   ├ upscaler hooks: NGX · Streamline · FFX · XeSS                         │
│   ├ RT hooks: CreateStateObject · DispatchRays · BuildRaytracingAS        │
│   ├ PSO hooks: pipeline creation (stutter attribution)                    │
│   ├ per-process VRAM: IDXGIAdapter3::QueryVideoMemoryInfo                 │
│   └ ring writer (allocation-free, lock-free, SEH-guarded)                 │
└───────────────────────────────────────────────────────────────────────────┘
```

> `*` **`AntiCheatGuard` is a facade, not an implementation.** The guard is
> native (`20_OPEN_QUESTIONS` §S13(a)); the box above is the P/Invoke wrapper
> in `Infrastructure` that the Agent calls. Nothing managed parses rules or
> matches a blocklist — two matchers that can disagree is a fail-open by
> construction, and a test asserts there is only one (§S15 item 1).

## Why this shape

- **Hooking in-process is the only way to get the facts we care about.** Render resolution vs output resolution, upscaler identity and quality preset, whether frame generation is actually running, whether rays are actually being traced — none of these are observable from outside the process. The previous ETW-only design could only guess from file/module presence, which measured badly against real games.
- **The DLL does as little as possible.** It records; it does not analyze, log, allocate, or block. All interpretation happens in the Agent. This keeps the game-side risk surface tiny and the overhead near zero.
- **Shared memory, not pipes, on the hot path.** A present hook must not make a syscall. It writes 64 bytes into a ring and returns.
- **The Agent no longer needs elevation for its primary path.** Injecting into a same-integrity process, and reading GPU telemetry through vendor user-mode APIs, both work unprivileged. Elevation is *optional* and unlocks: CPU/board temperatures (LHM + PawnIO), attaching to games that themselves run elevated, and the Tier-2 ETW source. A significant simplification versus the previous design — with one honest consequence: **the fallback tier is the part that needs elevation**, so an unelevated Agent whose Tier-1 attempt fails degrades to Tier 3, not Tier 2 (`04_CAPTURE` §Frame source abstraction).

## Capture tiers

| Tier | Mechanism | Gets you | When used |
|---|---|---|---|
| **1** | Injected hooks (default when the user enabled it and the guard passed) | Everything: exact render/output res, upscaler + quality, FG ground truth, RT/PT evidence, per-process VRAM, PSO stutter attribution, Reflex latency, present flags | Offline/single-player titles the user opted in |
| **2** | ETW via PresentMon console (no injection) | Frame times, present mode, displayed vs presented counts, coarse FG inference | Guard refused, hook failed, user chose not to hook, or cross-checking Tier 1 |
| **3** | Nothing | Session duration + sensors only | Capture disabled |

Tier is recorded per session (`sessions.capture_tier`) and shown in the UI, because metric availability differs and comparing across tiers must be explicit.

## Injection flow

```
watcher sees tracked exe launch
  → is hooking enabled for this game?           no → Tier 2
  → AntiCheatGuard.Check(pid)                   fail → refuse + explain + Tier 2 (19_SAFETY)
  → Injector.Attach(pid, dllPath)               fail → log + Tier 2
      OpenProcess(CREATE_THREAD|VM_OPERATION|VM_WRITE|VM_READ|QUERY_LIMITED)
      VirtualAllocEx → WriteProcessMemory(dllPath) → CreateRemoteThread(LoadLibraryW)
  → DLL init thread: install hooks, create ring, publish handshake block
  → Agent maps Local\FrameLedger.Ring.<pid>, validates layout version + build id
  → first frame record → session starts
```

**Launch mode (preferred when FrameLedger starts the game):** `CreateProcess(CREATE_SUSPENDED)` → guard → inject → `ResumeThread`. This catches swapchain creation and early upscaler init, which attach mode can miss. Attach mode remains for games launched from Steam/GOG directly.

Guard re-runs every 30 s for the life of the session; anti-cheat appearing late ⇒ clean unhook (`19_SAFETY` §During a session).

## Lifecycle

- Agent idles at ~0% CPU: 1 Hz process poll. No capture machinery exists until a tracked game launches.
- Session ends when the process tree exits, or on safety unhook, or on user stop.
- The DLL is **not unloaded** from a live process after unhooking (threads may still be inside trampolines). It disables its hooks, stops writing, and goes dormant until the process exits. Documented, deliberate.
- UI exit does not stop the Agent when background capture is enabled; otherwise UI sends `Shutdown`.
- Single instance via named mutexes; the ring name is per-PID so multiple games can be captured simultaneously (rare but supported).

## Failure domains

| Failure | Behavior |
|---|---|
| Anti-cheat detected pre-injection | Refuse, explain which signal fired, offer Tier 2 |
| Anti-cheat detected mid-session | Immediate clean unhook, `exit_status = unhooked_safety`, prominent notice |
| Injection API fails (access denied, protected process) | Log, no retry loop, offer Tier 2 + "run Agent elevated" hint |
| Hook faults 3× | DLL self-disables, sets fault flag in handshake block, Agent finalizes session as `degraded` |
| Game crashes ≤ 60 s after injection, twice | Hooking auto-disabled for that game, Tier 2 takes over |
| Ring overflow (Agent stalled) | Overwrite-oldest; dropped count published in the ring header → session flagged with a data-quality warning |
| Layout/version mismatch DLL ↔ Agent | Agent refuses to attach, tells user to restart the game after update |
| Vendor API unavailable (no NVAPI etc.) | GPU telemetry degrades to `N/A`; frame metrics unaffected |
| DB locked/corrupt | Integrity check + backup-and-recreate in Tools |

## Data directory

`%LOCALAPPDATA%\FrameLedger\`

```
ledger.db                    SQLite (WAL)
rules\detection-rules.json   engine/platform/feature + anticheat blocklist
logs\ui-*.log, agent-*.log, overlay-<pid>-*.log   (native log flushed at session end, never mid-frame)
crashdumps\*.dmp
breadcrumbs\<pid>.json       pre-injection breadcrumb (19_SAFETY §Crash safety)
tmp\<sessionGuid>.partial    crash-safety flush of raw buffers, every 60 s (04_CAPTURE §Ring draining)
covers\*.jpg
```

## Key design decisions (ADR)

- **ADR-1 (superseded):** ~~Passive ETW only; injection out of scope.~~ Replaced by ADR-7.
- **ADR-2:** WPF over WinUI 3 — Velopack maturity, ecosystem stability, portfolio reuse.
- **ADR-3 (revised):** ETW/PresentMon is now the **Tier-2 fallback**, not the primary source. The `IFrameSource` abstraction that made this cheap to change is retained and vindicated.
- **ADR-4:** Raw frame series stored as compressed blobs with retention; aggregates kept forever.
- **ADR-5 (revised):** In-game overlay is now *possible* (we are already in the process) and is planned as an opt-in feature after the data path is stable — see `15_ROADMAP` P5.
- **ADR-6:** UI toolkit = WPF UI (lepoco `Wpf.Ui` ≥ 4.3.0, MIT) — see `16_WPFUI_SYNTAX.md`.
- **ADR-7 (2026-07, core change):** **Capture moves to in-process graphics-API hooking**, C++20 DLL + MinHook, with a Vulkan implicit layer for Vulkan. Rationale: passive detection of DLSS/upscaling/frame-generation/RT state showed large errors against real games; those states are only knowable at the API boundary. Trade-offs accepted: anti-cheat exposure (mitigated by the hard guard in `19_SAFETY`, offline titles only), crash risk in the host process (mitigated by SEH guards + fault policy + auto-disable), a second toolchain in the build, and per-API maintenance as vendors change SDKs. Scope limited to titles the user explicitly opts in.
- **ADR-8:** **No evasion, ever** (`19_SAFETY` §What we will never build). This is an architectural constraint, not a preference — it defines what "correct" means for this codebase.
- **ADR-9:** Elevation demoted from required to optional; the Agent runs as a standard user by default.
