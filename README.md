# FrameLedger

**A local-first game performance ledger for Windows.** FrameLedger records frame times, FPS metrics, hardware telemetry, and — crucially — the settings your game is *actually rendering with*, every time you play. Then it turns your history into charts, so you can see exactly how a patch, a driver update, or a settings change affected performance.

> No telemetry. No accounts. All data stays on your machine.

## What makes it different

Most tools tell you your frame rate. FrameLedger tells you **what produced that frame rate**:

- **Real render resolution vs output resolution** — the actual internal resolution, not the one in the settings menu.
- **Which upscaler is running, at which quality preset** — DLSS / FSR / XeSS, read from the API the game calls, not guessed from files on disk.
- **Frame Generation, measured** — native and generated frames counted separately. Always shown as `62 → 118 FPS (×1.9 FG)`, never as one inflated number.
- **Ray tracing, actually detected** — including inline ray tracing (DXR 1.1 `RayQuery`), which frame-counting tools miss entirely.
- **Per-process VRAM** and whether the driver was exceeding its budget (a real stutter explanation).
- **Shader-compilation stutter attribution** — which frame spikes were pipeline compiles.
- **PC latency** when the game uses Reflex.

Plus the usual: Avg / Min / Max / Median FPS, 1% Low, 0.1% Low, frametime distributions, stutter metrics, CPU/GPU temperatures, session history, cross-session comparison, and Steam/GOG/Epic/itch.io library import. UI in **English / Tiếng Việt / 日本語**.

## How it works — and the trade-off, stated plainly

None of the settings data above is visible from outside a game process. To read it, FrameLedger loads a component into the game and observes the calls the game makes to graphics APIs — the same class of technique used by frame-rate overlays, recording software, and post-processing injectors. Vulkan titles use a standard Vulkan layer instead.

**That carries a real risk: anti-cheat systems can detect injected code and may ban accounts.**

FrameLedger is built to keep that risk small and to be honest about it:

| Safeguard | |
|---|---|
| **Off by default** | Injection is disabled for every game until you enable it individually, after a consent prompt |
| **Hard refusal** | Known anti-cheat/anti-tamper components are detected before injection and every 30 s during a session. Detected ⇒ FrameLedger refuses, or unhooks at the next scan — so up to 30 s can pass before it reacts to anti-cheat that loads mid-session. **There is no override — not in settings, not in a config file, not on the command line** |
| **No evasion, ever** | FrameLedger does not hide, rename, obfuscate, or disguise itself. It keeps its real name, real exports, and version info. It is meant to be plainly visible to any security software that looks. This is an architectural rule, not a setting |
| **Read-only** | It never reads or writes game memory, never modifies game behavior, never touches saves or input, and never changes GPU clocks, fans, or power limits |
| **Always a way out** | A no-injection mode (ETW-based) is the default for anything the software is unsure about. It needs the Agent running elevated, because Windows restricts the trace sessions it uses — FrameLedger tells you when that applies rather than silently recording less |

**FrameLedger is for offline and single-player games.** If you enable it for anything with an online or competitive component, that is your call and your responsibility. The developer cannot reverse a ban. Please read [`legal/DISCLAIMER.md`](legal/DISCLAIMER.md) and [`docs/19_SAFETY_AND_ANTICHEAT.md`](docs/19_SAFETY_AND_ANTICHEAT.md) before enabling it for anything.

### Capture tiers

| Tier | How | What you get |
|---|---|---|
| **1** | Injected hooks (opt-in, per game) | Everything above |
| **2** | ETW / Intel PresentMon, no injection (**needs an elevated Agent**) | Frame times, FPS, lows, present mode, coarse frame-generation inference |
| **3** | None | Session duration + hardware telemetry |

The tier is recorded on every session and shown in the UI. Metrics unavailable at a session's tier read `N/A` — FrameLedger never substitutes an estimate for a measurement.

## Architecture

| Component | Runs as | Role |
|---|---|---|
| `FrameLedger.exe` | Standard user | Fluent desktop UI (WPF + [WPF UI](https://github.com/lepoco/wpfui)), charts, library, settings |
| `FrameLedger.Agent.exe` | Standard user (elevation optional) | Injection control, safety guard, data collection, GPU telemetry, storage |
| `FrameLedger.Overlay.dll` | Inside the game | C++20 hooks + lock-free shared-memory writer. Records only; never analyzes, allocates, or blocks |
| `FrameLedger.VkLayer.dll` | Inside the game | Vulkan implicit layer (Vulkan titles use this instead of injection) |

Elevation is **optional for Tier-1 hooked capture** — that is the normal path and it runs as a standard user. It unlocks CPU temperature sensors, attaching to games that themselves run elevated, and the Tier-2 ETW fallback. If you expect to rely on Tier 2, run the Agent elevated.

## Requirements

- Windows 10 (22H2) or Windows 11, 64-bit
- A 64-bit DirectX 11/12, Vulkan, or OpenGL game for full (Tier-1) capture. 32-bit games — including most DirectX 9 titles — are supported at Tier 2 only
- Optional: [PawnIO](https://pawnio.eu/) for CPU temperature (GPU telemetry works without it, through your graphics driver's own libraries)

## Install

1. Download the latest `FrameLedger-win-Setup.exe` from [Releases](https://github.com/poli0981/frameledger/releases).
2. SmartScreen may warn — releases are not code-signed (free, open-source project). Verify the SHA-256 checksum published with each release, then **More info → Run anyway**.
3. Follow the first-run Legal Gate and Agent setup. Nothing is injected until you enable it for a specific game.

## Privacy

Everything lives locally in `%LOCALAPPDATA%\FrameLedger`. The only network calls: update checks against GitHub Releases, detection-rules updates from this repository, and *optional, opt-in* store metadata lookups. Bug reports are always built locally, shown to you, and submitted by you. Full policy: [`legal/PRIVACY_POLICY.md`](legal/PRIVACY_POLICY.md).

## License

GPL-3.0-only. See `LICENSE`. Third-party components: [`legal/THIRD_PARTY_NOTICES.md`](legal/THIRD_PARTY_NOTICES.md).

GPU telemetry is layered so the project never depends on a proprietary vendor licence: a vendor-neutral DXGI/performance-counter baseline, LibreHardwareMonitor (MPL-2.0) for sensors on all vendors, and NVIDIA's NVAPI SDK (MIT) for NVIDIA-only extras such as Reflex latency. No Intel or AMD GPU SDK is bundled — see `docs/18_GPU_VENDOR_APIS.md` for why.

## Documentation

Developer/AI-facing docs in [`docs/`](docs/). Start with `CLAUDE.md`, then `docs/19_SAFETY_AND_ANTICHEAT.md` (which constrains everything else), then `docs/01_ARCHITECTURE.md`.

## Reporting a safety gap

If you find a game with anti-cheat that FrameLedger fails to detect, please open an issue — **that is a safety bug and is treated with the same priority as a security report.** Blocklist entries ship as data and can be updated without a new release.

---

**Status:** pre-alpha, under active development. Roadmap: [`docs/15_ROADMAP.md`](docs/15_ROADMAP.md).
