# FrameLedger — Disclaimer

**Version:** 2.0-draft · **Effective:** {{RELEASE_DATE}}

> ⚠ Draft for review. Not legal advice. Review before first public release — this version covers code injection and should be read carefully.

## 1. How FrameLedger measures (read this first)

To measure what a game is *actually* doing — its real render resolution, which upscaler is running at which quality preset, whether frame generation is active, whether rays are being traced — FrameLedger loads a component (`FrameLedger.Overlay.dll`) **inside the game process** and observes calls the game makes to graphics APIs. Vulkan titles use a standard Vulkan layer instead. This is the same class of technique used by widely-used tools such as frame-rate overlays, screen-recording software, and post-processing injectors.

None of this information is obtainable from outside the process, which is why the software works this way.

**What it observes:** arguments the game passes to graphics APIs we intercept (presentation, upscaling, ray tracing, pipeline creation) and video-memory usage reported by the graphics runtime.

**What it never does:** read or write the game's memory outside those API arguments; read save files, input, chat, or network traffic; modify game behavior; hide itself from any security software; or install a kernel driver of its own.

## 2. Anti-cheat systems — the main risk to you

**Loading code into a game process can be detected by anti-cheat and anti-tamper systems, and may result in a warning, a block, or a permanent ban of your account.** This risk falls on you, not on the developer.

FrameLedger is designed to reduce that risk substantially:

- Injection is **off by default** and must be enabled by you **per game**.
- Before injecting, and every 30 seconds afterwards, FrameLedger scans for known anti-cheat and anti-tamper components. **If it finds one, it refuses to inject, or stops immediately if the session is already running.** There is no setting to override this.
- FrameLedger contains **no evasion techniques of any kind** — it does not hide, rename, obfuscate, or disguise itself. It is intended to be plainly identifiable to any security software that looks.
- A no-injection measurement mode is always available and is the default for anything the software is unsure about.

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
