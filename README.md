# FrameLedger

**A local-first game performance ledger for Windows.** FrameLedger records frame times, FPS metrics, hardware telemetry, and — crucially — the settings your game is *actually rendering with*, every time you play. Then it turns your history into charts, so you can see exactly how a patch, a driver update, or a settings change affected performance.

> No telemetry. No accounts. All data stays on your machine.

<!-- accuracy-block:begin -->
> ⚠ **What FrameLedger actually measures today — 2026-09-06.** The software is pre-alpha: the
> measurement path exists as an injected Direct3D 11/12 component and an **unshipped** capture
> host that drives the guard loop and prints a report; there is no Agent loop, no storage, no
> charts, no library import, no UI and no installer yet.
>
> - **Frame times and output resolution:** measured, from the present hook, for injected D3D11/12
>   titles. The Vulkan layer and OpenGL/D3D9 intercept nothing yet.
> - **Which upscaler is running:** measured from the API the game calls — DLSS (with Ray
>   Reconstruction Yes/No) through NVIDIA Streamline; FSR 2/3.x/4 through AMD's shipped DLLs; DLSS on
>   titles that bypass Streamline is reported from the NVIDIA driver's own per-process record,
>   labelled *driver-reported*. Intel XeSS is **not** read (its SDK licence forbids it) and reads
>   `N/A` by policy; an upscaler compiled into the game executable reads `N/A`.
> - **Quality preset:** `N/A` everywhere. No route this software may use exposes it.
> - **Render → output resolution:** measured where the vendor's own call carries the size (AMD
>   dispatches, some Streamline titles); `N/A` where it does not (NVIDIA-direct titles, most
>   Direct3D 12 Streamline titles).
> - **Frame generation:** the Displayed rate is counted from presents — including, on one title,
>   presents DXGI counted that the hook could not — and the Native rate from the vendor's own
>   per-frame calls where the title makes them. Identity: DLSS-G and FSR-FG named from the calls
>   the game makes; anything else (XeSS-FG, a generator compiled into the game) is reported *by
>   elimination* and never named. Where no per-frame call exists the Native rate is `N/A`.
> - **Ray tracing:** Yes/No measured from DXR dispatches and acceleration-structure builds on
>   Direct3D 12; the technique and path tracing are `N/A`.
> - **Not measured at all:** video memory, shader-compilation stutter, PC latency (Reflex), HDR.
> - **Safety:** every pre-injection check runs before injection, including the signed-by-a-known-
>   vendor half of the suspicious-module rule (since 2026-09-06), and the unshipped capture host
>   re-runs them every 30 s and stops the capture on refusal. No shipped component drives that loop
>   yet. There is no override anywhere.
>
> Where a value is not measured it reads `N/A`; the software never substitutes an estimate.
<!-- accuracy-block:end -->

> The rows below that describe **safety behaviour** are qualified where they sit, rather than
> here, because that is where a reader checking their own risk will look. What this README
> describes beyond the block above is the finished product; the block is what exists.

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
| **Hard refusal** | Known anti-cheat/anti-tamper components are detected before injection and every 30 s during a session. Detected ⇒ FrameLedger refuses, or stops capturing at the next scan — so up to 30 s can pass before it reacts to anti-cheat that loads mid-session. For Direct3D/OpenGL that means hooks are removed; for Vulkan it means the layer goes passthrough, because a layer cannot leave the loader chain of a running game. **There is no override — not in settings, not in a config file, not on the command line**<br><br>⚠ **Half of this is built.** The *pre-injection* refusal is real and runs every documented check. The **in-session re-scan does not run at all**: the component inside the game removes its hooks within one frame of being told to, and within 65 s if it stops hearing from the scanner — but nothing yet drives the scanner during a session, so nothing ever tells it. `legal/DISCLAIMER.md` §2 states this in full. The Vulkan layer additionally intercepts nothing yet, so there is no Vulkan capture to stop. |
| **No evasion, ever** | FrameLedger does not hide, rename, obfuscate, or disguise itself. It keeps its real name, real exports, and version info. It is meant to be plainly visible to any security software that looks. This is an architectural rule, not a setting |
| **Read-only** | It never reads or writes game memory, never modifies game behavior, never touches saves or input, and never changes GPU clocks, fans, or power limits |
| **Always a way out** | FrameLedger refuses rather than pushes through. When the guard finds anti-cheat, or the hook fails, or you have not enabled a game, it **stops** — and records the session anyway: start, end, duration, whatever hardware sensors your machine provides, and the reason there is nothing else. Every measured value reads `N/A`; it never estimates one.<br><br>**The way out is that it does not inject, not that it measures another way.** An earlier version of this row promised a no-injection measurement mode. It was designed around Intel PresentMon, that was dropped on 2026-08-27 after measurement, and no replacement was chosen — so **frame times require hooking**. Said in this row rather than a footnote because the row is in the SAFETY table, and a reader deciding whether to enable a game needs the real alternative, not a comfortable one |

**FrameLedger is for offline and single-player games.** If you enable it for anything with an online or competitive component, that is your call and your responsibility. The developer cannot reverse a ban. Please read [`legal/DISCLAIMER.md`](legal/DISCLAIMER.md) and [`docs/19_SAFETY_AND_ANTICHEAT.md`](docs/19_SAFETY_AND_ANTICHEAT.md) before enabling it for anything.

### Capture tiers

| Tier | How | What you get |
|---|---|---|
| **1** | Injected hooks (opt-in, per game) | Everything above |
| **2** | None — nothing is injected | **Session duration, whatever hardware telemetry your machine can provide, and why there is nothing else** (which check refused, or that the hook failed, was not enabled, or the game is 32-bit). On NVIDIA that telemetry is measured to be GPU temperature, load, power, clocks, VRAM in use and fan speed, with no elevation; AMD and Intel are untested. Everything else reads `N/A` |

The tier is recorded on every session and shown in the UI. Metrics unavailable at a session's tier read `N/A` — FrameLedger never substitutes an estimate for a measurement.

> **This ladder used to have three rungs and a middle one that measured frame times without injecting.** It was to be built on Intel PresentMon; that was dropped on 2026-08-27 after measurement, and no replacement was chosen. **So frame times, FPS and the lows are now Tier-1-only.** Said plainly because the previous README promised them without injection, and a reader who remembers that would otherwise assume it still holds.

## Architecture

| Component | Runs as | Role |
|---|---|---|
| `FrameLedger.exe` | Standard user | Fluent desktop UI (WPF + [WPF UI](https://github.com/lepoco/wpfui)), charts, library, settings |
| `FrameLedger.Agent.exe` | Standard user (elevation optional) | Injection control, safety guard, data collection, GPU telemetry, storage |
| `FrameLedger.Overlay.dll` | Inside the game | C++20 hooks + lock-free shared-memory writer. Records only; never analyzes, allocates, or blocks |
| `FrameLedger.VkLayer.dll` | Inside the game | Vulkan implicit layer (Vulkan titles use this instead of injection) |

Elevation is **optional — for everything.** Hooked capture is the normal path and runs as a standard user, and there is no longer any tier that needs more than that. Elevation unlocks exactly two extras: CPU temperature sensors, and attaching to games that themselves run elevated. ~~and the Tier-2 ETW fallback. If you expect to rely on Tier 2, run the Agent elevated.~~ — there is no ETW fallback to rely on.

## Requirements

- Windows 10 (22H2) or Windows 11, 64-bit
- A 64-bit DirectX 11/12, Vulkan, or OpenGL game for full (Tier-1) capture. **32-bit games — including most DirectX 9 titles — cannot be measured at all**: the component FrameLedger loads is x64 and an x64 DLL cannot enter a 32-bit process. Such a title records duration and hardware telemetry and nothing else. *(This line previously said "supported at Tier 2 only", which meant frame times without injection. That tier no longer exists.)*
- Optional: [PawnIO](https://pawnio.eu/) for CPU temperature (GPU telemetry works without it, through your graphics driver's own libraries)

## Install

1. Download the latest `FrameLedger-win-Setup.exe` from [Releases](https://github.com/poli0981/frameledger/releases).
2. SmartScreen may warn — releases are not code-signed (free, open-source project). Verify the SHA-256 checksum published with each release, then **More info → Run anyway**.
3. Follow the first-run Legal Gate and Agent setup. Nothing is injected until you enable it for a specific game.

## Privacy

Everything lives locally in `%LOCALAPPDATA%\FrameLedger`. The only network calls FrameLedger will ever make: update checks against GitHub Releases, detection-rules updates from this repository, and *optional, opt-in* store metadata lookups. Bug reports are always built locally, shown to you, and submitted by you. Full policy: [`legal/PRIVACY_POLICY.md`](legal/PRIVACY_POLICY.md).

**Today it makes none of them.** The rules feed in particular does not exist — the blocklist ships with the build and updates only when you install a new one (`docs/20_OPEN_QUESTIONS.md` §S20). Listing a fetch that has no code is over-disclosure, and this document was already corrected once for describing a weekly outbound request the software never made.

## License

GPL-3.0-only. See `LICENSE`. Third-party components: [`legal/THIRD_PARTY_NOTICES.md`](legal/THIRD_PARTY_NOTICES.md).

GPU telemetry is layered so the project never depends on a proprietary vendor licence: a vendor-neutral DXGI/performance-counter baseline, LibreHardwareMonitor (MPL-2.0) for sensors on all vendors, and NVIDIA's NVAPI SDK (MIT) for NVIDIA-only extras such as Reflex latency. No Intel or AMD GPU *telemetry* SDK is bundled — see `docs/18_GPU_VENDOR_APIS.md` for why. The one AMD component in the tree is five MIT headers from the FidelityFX SDK, used for types only so the upscaler hook can read an FSR title's own dispatch descriptor; nothing AMD-built is linked or redistributed.

## Documentation

Developer/AI-facing docs in [`docs/`](docs/). Start with `CLAUDE.md`, then `docs/19_SAFETY_AND_ANTICHEAT.md` (which constrains everything else), then `docs/01_ARCHITECTURE.md`.

## Reporting a safety gap

If you find a game with anti-cheat that FrameLedger fails to detect, please open an issue — **that is a safety bug and is treated with the same priority as a security report.**

**How fast a fix can reach you, stated accurately.** Blocklist entries are data rather than code, so a fix is a one-line change here. But the software has **no rules-update path yet**: it installs the blocklist that shipped with your build and never fetches another (`docs/20_OPEN_QUESTIONS.md` §S20, feed half). Until that exists, a blocklist fix reaches you **only when you install a new release**. This paragraph previously said the opposite, and the sentence it said it in was a response-time promise attached to a security-priority commitment.

---

**Status:** pre-alpha, under active development. Roadmap: [`docs/15_ROADMAP.md`](docs/15_ROADMAP.md).
