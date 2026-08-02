# CLAUDE.md — AI build instructions for FrameLedger

You are implementing **FrameLedger**, a Windows tool that measures game performance by **hooking graphics APIs inside the game process**, records sessions (frame times, upscaler/FG/RT ground truth, hardware telemetry), and visualizes them over time.

Read this file first, then the reading order at the bottom. **`docs/19_SAFETY_AND_ANTICHEAT.md` is not optional** — it constrains everything the hook does.

## Architecture in one paragraph

A C++ DLL (`FrameLedger.Overlay.dll`) is injected into games the user explicitly enabled. It hooks the presentation path (DXGI/D3D9/OpenGL) and the upscaler/RT APIs, writes fixed-size frame records into a lock-free shared-memory ring, and never blocks. A C# Agent drains that ring at ~10 Hz, enriches with GPU telemetry from vendor APIs, and writes sessions to SQLite. A WPF UI reads SQLite. Vulkan uses an implicit **layer** instead of hooking. If injection is refused or fails, a Tier-2 ETW/PresentMon source provides degraded metrics.

## Non-negotiable rules

1. **Injection is opt-in, per game, never automatic.** The user must add the game *and* enable hooking for it. There is no "hook everything" mode, no global auto-inject.
2. **The anti-cheat guard is a hard gate, not a warning.** Before injecting, scan the target for known anti-cheat/anti-tamper modules (`19_SAFETY`). If any is found → **refuse**, log, tell the user why. There is no override switch, no "I understand, continue anyway" button. Do not add one.
3. **Never implement evasion.** No manual mapping, no PE header erasure, no thread hiding, no signature obfuscation, no unlinking from the PEB module list. Injection uses documented `LoadLibraryW`. The DLL keeps its real name, real exports, and version info. A performance tool must be *visible* to anti-cheat, not hidden from it. Any PR that makes FrameLedger harder to detect is rejected on principle.
4. **Never read or write game memory outside our own hooks.** We read arguments passed to APIs we hooked and COM/handle objects we legitimately own. No pattern scanning for game internals, no reading player/entity state, no writing to game memory. Ever.
5. **The hook must never crash the game.** Every hook body: SEH-guarded, allocation-free, lock-free, no logging, no exceptions crossing the boundary. On repeated faults, self-disable (`17_HOOK_ENGINE` §Fault policy).
6. **FPS display rule (product requirement):** wherever FPS is shown and Frame Generation is active, show *Native FPS*, *Displayed FPS*, and the FG factor together (`62 → 118 FPS (×1.9 FG)`). Never a single inflated number. See `docs/03_METRICS.md`.
7. **RT / PT / RR are tri-state** (`Yes` / `No` / `N/A`) with manual override. They are now *measured*, not guessed — but where measurement is genuinely impossible (inline RayQuery without DXIL scan, path-tracing classification), the honest answer is still `N/A`. Never fabricate `Yes`.
8. **No telemetry, no analytics, no silent network calls.** Permitted: GitHub release check, detection-rules fetch, opt-in store metadata. Nothing else.
9. **No obfuscation of our own binaries.** GPLv3 project; ship self-contained + ReadyToRun, publish checksums.

## Pinned stack

| Concern | Choice |
|---|---|
| Managed runtime | .NET 10 (LTS), C# 14, `net10.0-windows`, x64 only |
| Native | **C++20, MSVC v143+, `/MT` static CRT, `/GS`, `/guard:cf`, no RTTI, no C++ exceptions in hook paths** |
| Hooking | **MinHook** (BSD-2-Clause) for inline hooks; direct vtable-entry swap for COM interfaces |
| Vulkan | **Implicit Vulkan layer** (`VK_LAYER_frameledger_overlay`), not hooking — `17_HOOK_ENGINE` §Vulkan |
| UI | WPF + WPF UI (`WPF-UI` pinned = 4.3.0) + CommunityToolkit.Mvvm — `docs/16_WPFUI_SYNTAX.md` |
| App composition | .NET Generic Host + DI in `FrameLedger.App` |
| Charts | ScottPlot 5 |
| GPU telemetry | Layered: DXGI + PDH counters (all vendors) → LibreHardwareMonitorLib (MPL-2.0, all vendors) → NVAPI (**MIT, headers vendored**, NVIDIA extras + Reflex). **No AMD/Intel vendor SDK** — `docs/18_GPU_VENDOR_APIS.md` |
| CPU/board sensors | LibreHardwareMonitorLib ≥ 0.9.6 (PawnIO) — **optional**, elevated only |
| Tier-2 fallback | Intel PresentMon console binary (ETW) |
| Storage | SQLite via Microsoft.Data.Sqlite + Dapper (no EF) |
| Shared memory IPC | `CreateFileMapping` + lock-free SPSC ring — `docs/07_IPC.md` |
| Logging | Serilog (managed); ring-buffer + deferred flush (native) |
| Packaging | Velopack, GitHub Releases, unsigned + SHA256SUMS |
| Win32 interop (C#) | CsWin32 source generator |
| i18n | `.resx`: `en` (default), `vi`, `ja` |
| Tray | H.NotifyIcon.Wpf |

## Solution layout

```
FrameLedger.sln
src/
  native/
    FrameLedger.Overlay/       # C++20 DLL injected into the game (hooks + ring writer + optional overlay)
    FrameLedger.Injector/      # C++20 static lib + tiny exe: launch/attach injection, AC guard probe
    FrameLedger.VkLayer/       # C++20 Vulkan implicit layer DLL + manifest JSON
    FrameLedger.Shm/           # header-only: ring buffer + record layout, shared by native & C# (mirrored)
  FrameLedger.Domain/          # entities, metric calculators — zero dependencies
  FrameLedger.Application/     # use cases, ports
  FrameLedger.Infrastructure/  # SQLite, shm reader, vendor APIs, injector interop, ETW fallback, parsers
  FrameLedger.Shared/          # IPC contracts (System.Text.Json source-gen) + ShmRecord struct mirror
  FrameLedger.Agent/           # capture orchestrator: watcher, injector control, shm drain, recorder
  FrameLedger.App/             # WPF UI
tests/
  FrameLedger.Domain.Tests/  FrameLedger.Application.Tests/  FrameLedger.Infrastructure.Tests/
  native/FrameLedger.Overlay.Tests/   # Catch2: ring buffer, record encode, fault policy
tools/                         # fixture recorder, rules validator, shm inspector, hook harness
docs/  legal/
```

Dependency direction unchanged: `App/Agent → Application → Domain`; `Infrastructure` implements `Application` ports; Domain references nothing. **The native layer is reachable only through `Infrastructure`** — no P/Invoke anywhere else.

## Coding conventions

**C# —** `Nullable enable`, `TreatWarningsAsErrors`, `AnalysisLevel latest-all`, file-scoped namespaces, Roslynator + Meziantou + VS Threading analyzers, `dotnet format` clean. Async suffixed `Async`, `ConfigureAwait(false)` off the UI. All user-visible strings from `.resx`. Timestamps UTC (unix-ms in SQLite); QPC ticks only inside the capture pipeline.

**C++ —** `clang-format` (LLVM base, 4-space, 120 col) enforced in CI. No STL containers that allocate in hook paths. No `std::mutex` in hook paths. `-D_HAS_EXCEPTIONS=0` in the Overlay target. Every hook entry point wrapped per the `FL_HOOK_GUARD` macro (`17_HOOK_ENGINE`). Static analysis: `/analyze` + clang-tidy (`bugprone-*`, `cert-*`, `concurrency-*`).

**Struct mirroring —** `FlFrameRecord` exists twice (C++ header, C# `[StructLayout(LayoutKind.Sequential)]`). A test asserts `sizeof` and every field offset on both sides; a version constant in the shm header must match or the Agent refuses to attach.

**Dev mode —** `FL_MOCK=1` runs the whole app with a synthetic frame source and no injection at all. Keep it working; it is how the UI is developed. `tools/hook-harness` is a dummy D3D11/D3D12/Vulkan app used to exercise hooks without a real game.

## Definition of done (per PR)

- Builds warning-free (C# and C++); tests green; `dotnet format` + `clang-format` clean.
- Hook-path changes state measured overhead in the PR body (`14_TESTING` §Hook overhead).
- Any new hook is listed in `17_HOOK_ENGINE` §Hook inventory and justified against rule 4.
- New user-visible strings exist in `en`, `vi`, `ja`.
- Any deviation from a doc updates that doc in the same PR.

## Reading order

1. `docs/19_SAFETY_AND_ANTICHEAT.md` — the guard, the refusal list, what we will not build
2. `docs/01_ARCHITECTURE.md` — processes, injection flow, lifecycle
3. `docs/02_SPEC.md` — FR/NFR ids used everywhere
4. `docs/03_METRICS.md` — metric math, source of truth
5. `docs/17_HOOK_ENGINE.md` — the C++ hook layer
6. `docs/04_CAPTURE.md` — capture orchestration (Agent side)
7. `docs/05_DETECTION.md` — runtime API interception → engine/upscaler/FG/RT facts
8. `docs/18_GPU_VENDOR_APIS.md` — layered GPU telemetry (DXGI/PDH · LHM · NVAPI) + vendor-SDK licence rules
9. `docs/06_DATA_MODEL.md` · `docs/07_IPC.md` · `docs/08_UI.md` · `docs/16_WPFUI_SYNTAX.md`
10. Remaining (`09`–`15`).
