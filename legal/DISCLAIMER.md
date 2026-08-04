# FrameLedger — Disclaimer

**Version:** 2.0-draft · **Effective:** {{RELEASE_DATE}}

> ⚠ Draft for review. Not legal advice. Review before first public release — this version covers code injection and should be read carefully.

> ⚠ **Accuracy audit, 2026-08-04. Three statements below describe behaviour the software does not yet have**, and they are flagged here rather than quietly reworded because a document the user accepts must not over-promise:
>
> - §2's *"and every 30 seconds afterwards … it stops at the next scan"* — the pre-injection guard is real and refuses; the **in-session re-scan and stop are not implemented**. `GuardSupervisor` has no production caller and nothing writes the shared-memory field the capture side would read.
> - §3's *"automatically stops injecting into a game that crashes shortly after injection twice"* — the only trace of this anywhere is a `hook_crash_count` column in a schema for which no `.sql` file exists. There is no design for it.
> - §1's claim that FrameLedger loads `FrameLedger.Overlay.dll` **is now true as written** (§S22): the injection path refuses any library outside FrameLedger's own directory. It was not true before that change, which is why it is listed here.
>
> **None of this may ship as-is.** Either the behaviour exists at first release or these sentences change. Tracked in `docs/20_OPEN_QUESTIONS.md`.

## 1. How FrameLedger measures (read this first)

To measure what a game is *actually* doing — its real render resolution, which upscaler is running at which quality preset, whether frame generation is active, whether rays are being traced — FrameLedger loads a component (`FrameLedger.Overlay.dll`) **inside the game process** and observes calls the game makes to graphics APIs. Vulkan titles use a standard Vulkan layer instead. This is the same class of technique used by widely-used tools such as frame-rate overlays, screen-recording software, and post-processing injectors.

None of this information is obtainable from outside the process, which is why the software works this way.

**Only FrameLedger's own component is ever loaded.** The injection path refuses any library that does not come from FrameLedger's own installation directory, and there is no setting that changes this. Be aware of the limit of that promise: it establishes where the file came from, not what is in it, and FrameLedger is distributed unsigned — so anyone able to write to your FrameLedger installation could alter what gets loaded. Install it somewhere only you can write to, and verify the published SHA-256 checksums.

**What it observes:** arguments the game passes to graphics APIs we intercept (presentation, upscaling, ray tracing, pipeline creation) and video-memory usage reported by the graphics runtime.

**What it never does:** read or write the game's memory outside those API arguments; read save files, input, chat, or network traffic; modify game behavior; hide itself from any security software; or install a kernel driver of its own.

## 2. Anti-cheat systems — the main risk to you

**Loading code into a game process can be detected by anti-cheat and anti-tamper systems, and may result in a warning, a block, or a permanent ban of your account.** This risk falls on you, not on the developer.

FrameLedger is designed to reduce that risk substantially:

- Injection is **off by default** and must be enabled by you **per game**.
- Before injecting, and every 30 seconds afterwards, FrameLedger scans for known anti-cheat and anti-tamper components. **If it finds one, it refuses to inject; if a session is already running, it stops at the next scan.** There is no setting to override this. Because the scan runs every 30 seconds rather than continuously, anti-cheat that loads mid-session may be present for **up to 30 seconds** before FrameLedger detects it and stops.

- **What "stops" means differs by graphics API, and the difference is worth
  stating plainly.** For Direct3D and OpenGL titles FrameLedger injects a
  library and genuinely removes its hooks. Vulkan titles use a Khronos *layer*
  instead, and a layer cannot remove itself from the loader's chain while the
  game is running — attempting to leave crashes the application. There,
  "stops" means FrameLedger stops observing and passes every call through
  untouched. It records nothing further, but its library remains loaded until
  the game exits.
- FrameLedger contains **no evasion techniques of any kind** — it does not hide, rename, obfuscate, or disguise itself. It is intended to be plainly identifiable to any security software that looks.
- A no-injection measurement mode is the default for anything the software is unsure about. It relies on Windows event tracing, which requires the FrameLedger agent to run with administrator rights; without that, measurement falls back to session duration and hardware sensors only. FrameLedger tells you which mode produced each session.

**However, these protections cannot be complete:**

1. The list of known anti-cheat systems cannot be exhaustive. New systems appear, and a game can add one in an update.
2. Some anti-tamper technology (for example Denuvo) can react badly to injection even in single-player games — usually a crash, but potentially lost progress or play time.
3. Heuristic detection can flag well-behaved software.
4. **The developer cannot restore a banned account, recover lost progress, or intervene with any game publisher on your behalf.**

**FrameLedger is intended for offline and single-player play. If you enable injection for a game with any online or competitive component, you do so entirely at your own risk and are solely responsible for the consequences, including compliance with that game's terms of service.**

## 3. Stability

Software running inside another process can, in principle, destabilize it. FrameLedger guards every intercepted call, disables itself automatically after repeated internal faults, and automatically stops injecting into a game that crashes shortly after injection twice. Despite this, the developer is not responsible for crashes, lost progress, corrupted saves, or any other loss arising from use of the software. **Save your game before benchmarking.**

## 4. Measurement accuracy

Frame timing is derived from high-resolution timestamps taken at the moment the game presents each frame; upscaling, frame-generation and ray-tracing state are read from the parameters the game passes to those APIs. This is substantially more accurate than inferring settings from files on disk, but **no measurement is guaranteed to be exact**:

- Frame timing: typically better than 0.05% error; hardware sensors carry their own accuracy limits (±1–2 °C is normal).
- 1% Low and 0.1% Low are statistical and are hidden when the sample size is insufficient.
- Path tracing has no reliable technical signature; FrameLedger reports it as a confidence-scored suggestion only and never asserts it.
- When measurement is unavailable (no-injection mode, unsupported API, missing vendor support), fields read `N/A`. FrameLedger does not substitute estimates for measurements.

Do not use FrameLedger's output as the sole basis for purchasing, warranty, overclocking, or safety decisions.

## 5. Hardware and drivers

FrameLedger reads GPU telemetry through vendor libraries that ship with your graphics driver. It **only reads** — it never changes clocks, fan curves, power limits, or any other hardware setting. Optional CPU-temperature support relies on the separate third-party **PawnIO** driver, which you install at your own discretion under its own license; some anti-cheat systems object to third-party kernel drivers regardless of what they are used for.

## 6. Third-party names

Windows and DirectX are trademarks of Microsoft Corporation. NVIDIA, DLSS, Reflex, NVAPI, AMD, FSR, Intel, XeSS, Vulkan, Steam, GOG, Epic Games, itch.io, Unity, Unreal Engine, and all other product names are trademarks of their respective owners. FrameLedger is an independent project and is **not affiliated with, endorsed by, or sponsored by** any of them, including any game publisher or anti-cheat vendor.

## 7. Content creators

If you publish benchmarks or videos using FrameLedger data, you are responsible for how you present the numbers. The software deliberately shows native frame rate alongside frame-generation-boosted output, and labels which measurement tier produced each session; keeping both visible in published material is strongly encouraged.

## 8. No professional advice

Nothing in the software or its documentation constitutes professional, legal, or engineering advice.
