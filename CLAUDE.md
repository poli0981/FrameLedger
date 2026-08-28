# CLAUDE.md — AI build instructions for FrameLedger

You are implementing **FrameLedger**, a Windows tool that measures game performance by **hooking graphics APIs inside the game process**, records sessions (frame times, upscaler/FG/RT ground truth, hardware telemetry), and visualizes them over time.

Read this file first, then the reading order at the bottom. **`docs/19_SAFETY_AND_ANTICHEAT.md` is not optional** — it constrains everything the hook does.

## Architecture in one paragraph

A C++ DLL (`FrameLedger.Overlay.dll`) is injected into games the user explicitly enabled. It hooks the presentation path (DXGI for D3D11/D3D12, plus OpenGL) and the upscaler/RT APIs, writes fixed-size frame records into a lock-free shared-memory ring, and never blocks. A C# Agent drains that ring at ~10 Hz, enriches with GPU telemetry from vendor APIs, and writes sessions to SQLite. A WPF UI reads SQLite. Vulkan uses an implicit **layer** instead of hooking. If injection is refused or fails, a Tier-2 ETW/PresentMon source provides degraded metrics.

## Non-negotiable rules

1. **Injection is opt-in, per game, never automatic.** The user must add the game *and* enable hooking for it. There is no "hook everything" mode, no global auto-inject.
2. **The anti-cheat guard is a hard gate, not a warning.** Before injecting, scan the target for known anti-cheat/anti-tamper modules (`19_SAFETY`). If any is found → **refuse**, log, tell the user why. There is no override switch, no "I understand, continue anyway" button. Do not add one.
3. **Never implement evasion.** No manual mapping, no PE header erasure, no thread hiding, no signature obfuscation, no unlinking from the PEB module list. Injection uses documented `LoadLibraryW`. The DLL keeps its real name, real exports, and version info. A performance tool must be *visible* to anti-cheat, not hidden from it. Any PR that makes FrameLedger harder to detect is rejected on principle.
4. **Never read or write game memory outside our own hooks.** We read arguments passed to APIs we hooked and COM/handle objects we legitimately own. No pattern scanning for game internals, no reading player/entity state, no writing to game memory. Ever.
5. **The hook must never crash the game.** Every hook body: SEH-guarded, allocation-free, lock-free, no logging, no exceptions crossing the boundary. On repeated faults, self-disable (`17_HOOK_ENGINE` §Fault policy).
6. **FPS display rule (product requirement):** wherever FPS is shown and Frame Generation is active, show *Native FPS*, *Displayed FPS*, and the FG factor together (`62 → 118 FPS (×1.9 FG)`). Never a single inflated number. See `docs/03_METRICS.md`.
7. **RT / PT / RR are tri-state** (`Yes` / `No` / `N/A`) with manual override. They are now *measured*, not guessed — but where measurement is genuinely impossible, the honest answer is still `N/A`. Never fabricate `Yes`.

   > **Amended 2026-08-20 with the PR that wrote the hooks, per the owner ruling of 2026-08-14.** This rule named *"inline RayQuery without DXIL scan"* as one of the impossible cases, and that conflated two questions. **Whether rays are being traced is measurable** — `BuildRaytracingAccelerationStructure` is called by RayQuery titles and by `DispatchRays` titles alike, which is why both hooks exist and why the `No` branch requires the AS-build hook to have been *installed*. What needs a DXIL scan is **classifying the technique as RayQuery**, and that stays `N/A`. So the impossible cases are: naming the technique, and path-tracing classification, which has no API-level signature at all (`03_METRICS` §RT/PT/RR — a confidence score that may only ever *suggest*).
8. **No telemetry, no analytics, no silent network calls.** Permitted: GitHub release check, detection-rules fetch, opt-in store metadata. Nothing else.
9. **No obfuscation of our own binaries.** GPLv3 project; ship self-contained + ReadyToRun, publish checksums.

## Pinned stack

| Concern | Choice |
|---|---|
| Managed runtime | .NET 10 (LTS, pinned via `global.json`), C# 14, TFM `net10.0-windows10.0.22621.0` with `SupportedOSPlatformVersion=10.0.19045.0` (Win10 22H2 floor, NFR-8), x64 only — `docs/12_BUILD.md` |
| Native | **C++20, MSVC v143+, `/MT` static CRT, `/GS`, `/guard:cf`, no RTTI, no C++ exceptions in hook paths** |
| Hooking | **MinHook** (BSD-2-Clause) for inline hooks; direct vtable-entry swap for COM interfaces |
| Vulkan | **Implicit Vulkan layer** (`VK_LAYER_FRAMELEDGER_overlay`), not hooking — `17_HOOK_ENGINE` §Vulkan |
| UI | WPF + WPF UI (`WPF-UI` pinned = 4.3.0) + CommunityToolkit.Mvvm — `docs/16_WPFUI_SYNTAX.md` |
| App composition | .NET Generic Host + DI in `FrameLedger.App` |
| Charts | ScottPlot 5 |
| GPU telemetry | Layered: DXGI + PDH counters (all vendors) → LibreHardwareMonitorLib (MPL-2.0, all vendors) → **NVAPI (MIT, headers + `amd64/nvapi64.lib` vendored 2026-08-05** at `src/native/third_party/nvapi/`, consumed via the `fl_nvapi` target; NVIDIA extras + Reflex). **No AMD/Intel vendor SDK** — `docs/18_GPU_VENDOR_APIS.md`. **No telemetry source exists in code yet** — `ctest fl_nvapi_probe` is the only consumer, and it exists so the vendoring is verified rather than asserted. This row previously claimed the vendoring in the present tense while `third_party/` held only `vulkan-headers`; `legal/` had already caught the identical claim about itself and gained a bidirectional gate, which is what made the real vendoring a red-then-green |
| CPU/board sensors | LibreHardwareMonitorLib ≥ 0.9.6 (PawnIO) — **optional**, elevated only |
| Tier-2 fallback | **Undecided.** ETW-based, no injection — the mechanism is an open question (`docs/20_OPEN_QUESTIONS.md` §G). ~~Intel PresentMon console binary~~ dropped by owner decision 2026-08-27 after §S31 retired it as an oracle; nothing in this repository names a Tier-2 tool |
| Storage | SQLite via Microsoft.Data.Sqlite + Dapper (no EF) |
| Shared memory IPC | `CreateFileMapping` + lock-free SPSC ring — `docs/07_IPC.md` |
| Logging | Serilog (managed); ring-buffer + deferred flush (native) |
| Packaging | Velopack, GitHub Releases, unsigned + SHA256SUMS |
| Win32 interop (C#) | CsWin32 source generator |
| i18n | `.resx`: `en` (default), `vi`, `ja` |
| Tray | H.NotifyIcon.Wpf |

## Solution layout

```
FrameLedger.slnx               # XML solution format (SDK default since .NET 10)
global.json                    # pins the SDK band — see 12_BUILD
build.ps1                      # the quality gate; CI runs this identical script
src/
  native/
    FrameLedger.Overlay/       # C++20 DLL injected into the game (hooks + ring writer + optional overlay)
    FrameLedger.Injector/      # C++20 static lib: launch/attach injection, AC guard probe
    FrameLedger.VkLayer/       # C++20 Vulkan implicit layer DLL + manifest JSON
    FrameLedger.Shm/           # header-only: ring buffer + record layout, shared by native & C# (mirrored)
  FrameLedger.Domain/          # entities, metric calculators — zero dependencies
                               #   (calculators UNWRITTEN: Domain holds AntiCheat/,
                               #    Consent/ and Detection/ only. Nothing in Domain
                               #    computes a frame time or an FPS low. measuredMask ->
                               #    rule 7's tri-state and Displayed FPS now exist, in the
                               #    UNSHIPPED FrameLedger.CaptureHost as a throwaway that
                               #    P2's recorder replaces — deliberately not in
                               #    Domain.Metrics.*, which carries a 95% coverage floor)
  FrameLedger.Application/     # use cases, ports
  FrameLedger.Infrastructure/  # SQLite, shm reader, vendor APIs, injector interop, ETW fallback, parsers
  FrameLedger.Shared/          # IPC contracts (System.Text.Json source-gen) + ShmRecord struct mirror
  FrameLedger.Agent/           # capture orchestrator: watcher, injector control, shm drain, recorder
  FrameLedger.App/             # WPF UI
  FrameLedger.CaptureHost/     # UNSHIPPED. The first production driver of the guard loop:
                               #   HookedCaptureGate -> FlGuardedInject -> ShmRingReader ->
                               #   10 Hz drain + GuardSupervisor + PublishGuardResult, plus the
                               #   file-backed consent store and a throwaway consumer.
                               #   12_BUILD publishes App and Agent ONLY, and neither references
                               #   this — tools/package-closure-check.ps1 is what keeps that true.
                               #   §S27 is closed on exactly that basis.
tests/
  FrameLedger.Domain.Tests/  FrameLedger.Application.Tests/  FrameLedger.Infrastructure.Tests/
  FrameLedger.CaptureHost.Tests/      # incl. the Category=Integration end-to-end case
  native/FrameLedger.Overlay.Tests/   # Catch2: ring buffer, record encode, fault policy
tools/                         # changelog-check, chokepoint-check, coverage-gate, gen-ac-floor,
                               # hookinventory-check, license-check, package-closure-check,
                               # rules-validate, vendor-exports, versioninfo-check,
                               # vklayer-blastradius
                               # (PowerShell). This line used to name three, one of which
                               # — resx-audit — does not exist.
                               # native tooling lives under src/native/tools:
                               #   fl-layout-dump  -> struct offsets for the C# mirror test
                               #   hook-harness    -> dummy D3D11 + D3D12 app (Vulkan/OpenGL unwritten)
rules/detection-rules.json     # engine/platform/capability + anticheat blocklist
docs/  legal/  legal/licenses/
```

Dependency direction unchanged: `App/Agent → Application → Domain`; `Infrastructure` implements `Application` ports; Domain references nothing. **The native layer is reachable only through `Infrastructure`** — no P/Invoke anywhere else.

## Coding conventions

**C# —** `Nullable enable`, `TreatWarningsAsErrors`, `AnalysisLevel latest-all`, file-scoped namespaces, Roslynator + Meziantou + VS Threading analyzers, `dotnet format` clean. Async suffixed `Async`, `ConfigureAwait(false)` off the UI. All user-visible strings from `.resx`. Timestamps UTC (unix-ms in SQLite); QPC ticks only inside the capture pipeline.

**C++ —** `clang-format` (LLVM base, 4-space, 120 col) enforced in CI. No STL containers that allocate in hook paths. No `std::mutex` in hook paths. `-D_HAS_EXCEPTIONS=0` in the Overlay target. Every hook entry point wrapped per the `FL_HOOK_GUARD` macro (`17_HOOK_ENGINE`). Static analysis: `/analyze` + clang-tidy (`bugprone-*`, `cert-*`, `concurrency-*`).

**Struct mirroring —** `FlFrameRecord`, `FlShmHandshake`, `FlWriterState` and `FlControlBlock` each exist twice: `src/native/FrameLedger.Shm/include/fl_shm.h` (**normative**) and `src/FrameLedger.Shared/ShmLayout.cs` as `[StructLayout(LayoutKind.Sequential)]`. `ShmLayoutMirrorTests` asserts size and every field offset on both sides **against JSON emitted by `tools/fl-layout-dump`**, never a transcribed table, and walks the field list in both directions so a field added on either side alone fails.

It also asserts **blittability** (`RuntimeHelpers.IsReferenceOrContainsReferences`), because offsets cannot see it: measured 2026-08-05, swapping the `fixed byte BuildId[32]` for this repo's own `[MarshalAs(ByValTStr)] string` idiom keeps `Marshal.SizeOf` at 64 and every offset correct while `Unsafe.SizeOf` collapses to **40** — a mirror that passes the obvious checks and cannot be read out of a memory-mapped view.

`build.ps1`'s `struct-mirror` gate reads the run's `.trx` and fails when that test class did not execute, so **deleting the test is red too** — it used to `Test-Path` a source file and report on a test it never looked for.

The version handshake is wired end to end as of 2026-08-05: `FlGuardBuildId` gives the Agent a build id of its own and `ShmHandshakeValidator` performs the refuse-to-attach comparison `07_IPC` specifies (§S23-1). **Its default is `NotEvaluated`, not `Ok`** — a result nobody produced has validated nothing, the same rule `AntiCheatVerdict` follows.

**Dev mode —** `FL_MOCK=1` is **specified but not implemented** — `grep -rn FL_MOCK src tests tools build.ps1` returns nothing (recorded 2026-08-04). When it exists it runs the whole app with a synthetic frame source and no injection at all, and it is how the UI is meant to be developed; until then, do not plan work on the assumption that it is there. `tools/hook-harness` is a dummy **D3D11 + D3D12** app used to exercise hooks without a real game; Vulkan and OpenGL modes are unwritten (`docs/12_BUILD.md` §Targets is the accurate list).

## Definition of done (per PR)

- Builds warning-free (C# and C++); tests green; `dotnet format` + `clang-format` clean.
- Hook-path changes state measured overhead in the PR body (`14_TESTING` §Hook overhead).
- Any new hook is listed in `17_HOOK_ENGINE` §Hook inventory and justified against rule 4.
- New user-visible strings exist in `en`, `vi`, `ja`.
- Any deviation from a doc updates that doc in the same PR.

## Reading order

0. **`docs/HANDOFF.md` — what to do next and in what order.** It carries sequencing,
   decisions that live in no other file, and the traps that cost a cycle each. It
   deliberately carries **no status**: it points at the four files that do. Start there
   if you are picking up work; start at 1 if you are learning the system.
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

**Before writing native or capture code, read `docs/20_OPEN_QUESTIONS.md`.** It
lists the defects and gaps that survived the doc audit — things the other
documents cannot answer because they need an empirical result or a decision. The
S-series items are safety items and block the first real injection. If you are
about to implement something in the hook layer, check whether it is already
listed there as unresolved rather than implementing the version the doc describes.
