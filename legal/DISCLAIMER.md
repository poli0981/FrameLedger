# FrameLedger — Disclaimer

**Version:** 2.0-draft · **Effective:** {{RELEASE_DATE}}

> ⚠ Draft for review. Not legal advice. Review before first public release — this version covers code injection and should be read carefully.

> ⚠ **Accuracy audit — re-checked 2026-08-14, a FOURTH time, after PR #64. SIX statements below describe behaviour the software does not yet fully have**, and they are flagged here rather than quietly reworded because a document the user accepts must not over-promise.
>
> **The fourth drift is the first one that ran the other way, and that is the finding.** Every previous re-count found this document claiming *more* than the software did. #64 landed the upscaler identity hook and this block went on saying "there is no upscaler hook anywhere in the software" — an under-claim, in the one file whose failure mode everybody was watching for in the opposite direction. It went unnoticed for five days for exactly that reason: a reviewer checking whether `legal/` over-promises reads a sentence like that and moves on. **Re-reading this block means checking both directions.**
>
> **The re-count was overdue and this block's own rule is what says so.** The last line of this block requires whoever changes what `FrameLedger.Overlay` does to re-read it. #48 changed exactly that — the Overlay now drops `DXGI_PRESENT_TEST` presents without recording them — and this block was not re-read. That is the second time the rule has been broken since it was written, and both breaches were invisible from inside the PR that caused them.
>
> **Two additions this round, both about what the software claims to *measure*** — the class the project has already had to retract twice:
>
> **Two bullets changed substance and neither could be struck.** The capture side that *receives* a stop now exists and now runs whether or not the game is presenting; the side that would *send* one still does not. That is the pattern to watch for when re-counting: half a mechanism reads as a whole one, and the half that is missing is usually the one nobody was looking at.
>
> - ~~§1's *"what it observes: … presentation, upscaling, ray tracing, pipeline creation"* and §4's *"upscaling, frame-generation and ray-tracing state are read from the parameters the game passes to those APIs"* — **only the presentation path is intercepted.** There is no upscaler hook, no frame-generation hook, no ray-tracing hook and no pipeline hook anywhere in the software.~~ **FALSE SINCE #64, corrected 2026-08-14 — the fourth drift, and this one is the block's own subject.** An **upscaler hook exists**: one detour on `sl.interposer.dll!slEvaluateFeature`, module-scoped, reading one argument. It measures **which upscaler is running** (DLSS, NIS, or `unknown` — never "none") and, for Streamline titles, **whether Ray Reconstruction is active**, which is a `Yes`/`No` the software now publishes. What still does not exist: quality preset, render resolution, frame generation, ray tracing, pipeline. The capture side states all of that in its own data on every frame rather than defaulting to "none", so nothing false is *recorded*; what over-promised was this document, in the direction of under-claiming rather than over-claiming — which is the safer direction and is still wrong. Both sentences are qualified where they sit.
> - §2's *"and every 30 seconds afterwards … it stops at the next scan"* — the pre-injection guard is real and refuses. The **receiving half is implemented**: an injected D3D title removes its hooks within one frame of the stop flag being set. The **sending half is still missing, and the missing part is narrower than this block used to say.** Both the write path (`ShmRingReader.PublishGuardResult`, which maps the shared memory and advances the field) and the read path exist in shipped code and are tested; what does not exist is a production caller that runs the loop between them. A missing loop, not a missing subsystem — and the user-facing consequence is identical: **no re-scan runs and no stop is ever sent.**
> - §2's *"waits **65 seconds** before concluding that contact is lost"* — **the deadline now has a reader, and since 2026-08-05 it runs whether or not the game is presenting.** For one day it was evaluated only on a present, so a title that had hung or been alt-tabbed never reached it — the exact case the mechanism was specified for. A watchdog inside the injected component now checks once a second regardless. Two qualifications remain: it covers **injected Direct3D titles only**, because the Vulkan layer intercepts nothing yet, and **the 65-second value itself is not covered by a test** — the stop is proven to fire, but a suite that runs on every build cannot wait 65 seconds, so what is verified is the mechanism, not the number.
> - §3's *"automatically stops injecting into a game that crashes shortly after injection twice"* — the only trace of this anywhere is a `hook_crash_count` column in a schema for which no `.sql` file exists. There is no design for it.
> - §1's *"Only FrameLedger's own component is ever loaded"* — **narrower than it sounds, and the qualification is now in §1 itself.** The check is a directory test: the payload must resolve into FrameLedger's own install directory. It does not compare a filename, a version or any content, deliberately — a name check would be defeated by any DLL that borrowed the name. A published self-contained build puts several hundred files in that directory.
>
> **None of this may ship as-is.** Either the behaviour exists at first release or these sentences change. Tracked in `docs/20_OPEN_QUESTIONS.md`.
>
> **This block is maintained by hand and nothing verifies it.** Every other document here is bound to the code by something — `rules-validate` cross-checks the blocklist, `static_assert`s bind `fl_shm.h` to `07_IPC`, `versioninfo-check` and `chokepoint-check` bind claims to binaries. `legal/` is bound by nothing, and it has now gone stale **three times**: first within hours of being written, when the 65-second sentence was added directly beneath a header that said "Three"; then again when five PRs (#40–#44) changed the Overlay from a stub into a hooking, shared-memory-mapping, control-block-reading component and touched **no documentation at all**, leaving the second bullet above asserting four things about that binary that were each false. and a third time when #46–#52 landed — seven PRs, including one (#48) that changed what the Overlay does *inside a game* — with no edit here at all. None of the three drifts was visible from inside the PR that caused it. **Whoever edits any promise in this file must re-count — and whoever changes what `FrameLedger.Overlay` does must re-read this block, because nothing will remind them.**
>
> **That sentence has now failed twice, so it is being replaced by something that is not a sentence.** `ci.yml` fails a pull request that touches `src/` without touching `CHANGELOG.md`. That is a weaker gate than the ones binding every other document — it forces a *ledger* entry, not a re-read of *this* file — and it is written down as weaker rather than described as a fix. A gate over the claims in `legal/` would have to assert things about behaviour rather than about files, and nobody has designed one (`docs/20_OPEN_QUESTIONS.md` §S23-6).

## 1. How FrameLedger measures (read this first)

To measure what a game is *actually* doing — its real render resolution, which upscaler is running at which quality preset, whether frame generation is active, whether rays are being traced — FrameLedger loads a component (`FrameLedger.Overlay.dll`) **inside the game process** and observes calls the game makes to graphics APIs. Vulkan titles use a standard Vulkan layer instead. This is the same class of technique used by widely-used tools such as frame-rate overlays, screen-recording software, and post-processing injectors.

None of this information is obtainable from outside the process, which is why the software works this way.

**FrameLedger only loads a library from its own installation directory.** The injection path refuses anything else, and there is no setting that changes this.

Be precise about what that does and does not promise, because the shorter version — *"only FrameLedger's own component is ever loaded"* — claims more than the check performs:

- It establishes **where the file came from**, not what is in it. There is no filename, version or content comparison, and that is deliberate: a check on the name `FrameLedger.Overlay.dll` would be satisfied by any library that borrowed the name.
- A FrameLedger installation contains **several hundred files**, because the application ships self-contained with the .NET runtime beside it. Any library in that directory satisfies the check.
- FrameLedger is distributed **unsigned**, so nothing attests to that directory's contents. Anyone able to write there could alter what gets loaded — and could equally replace the component that performs this check.

Install it somewhere only you can write to, and verify the published SHA-256 checksums.

**What it observes:** arguments the game passes to graphics APIs we intercept (presentation, upscaling, ray tracing, pipeline creation) and video-memory usage reported by the graphics runtime.

> **Today that list is two items long: presentation and upscaling** — and the upscaling half is *identity only*. The software can say which upscaler is executing, and whether Ray Reconstruction is active alongside it; it cannot yet say at what quality preset or at what render resolution. No ray-tracing or pipeline hook exists yet, and video-memory usage is not read. The list describes what the software is designed to observe, and the boundary — API arguments and nothing else — holds for all of it. See the accuracy audit at the top.

**What it never does:** read or write the game's memory outside those API arguments; read save files, input, chat, or network traffic; modify game behavior; hide itself from any security software; or install a kernel driver of its own.

## 2. Anti-cheat systems — the main risk to you

**Loading code into a game process can be detected by anti-cheat and anti-tamper systems, and may result in a warning, a block, or a permanent ban of your account.** This risk falls on you, not on the developer.

FrameLedger is designed to reduce that risk substantially:

- Injection is **off by default** and must be enabled by you **per game**.
- Before injecting, and every 30 seconds afterwards, FrameLedger scans for known anti-cheat and anti-tamper components. **If it finds one, it refuses to inject; if a session is already running, it stops at the next scan.** There is no setting to override this. Because the scan runs every 30 seconds rather than continuously, anti-cheat that loads mid-session may be present for **up to 30 seconds** before FrameLedger detects it and stops.

  **And there is a second, longer window you should know about.** The part of FrameLedger running inside the game also stops on its own if it loses contact with the part doing the scanning — because a scanner that has stopped cannot protect you. It waits **65 seconds** before concluding that contact is lost, so that one delayed scan on a busy machine does not end your session. In the worst case those windows combine: if the scanner stops at the moment anti-cheat appears, the component inside the game may keep running for up to **65 seconds** afterwards. That number is 65 and not 30, and this document says so rather than leaving the 30 above to imply it.

- **What "stops" means differs by graphics API, and the difference is worth
  stating plainly.** For Direct3D and OpenGL titles FrameLedger injects a
  library and genuinely removes its hooks. Vulkan titles use a Khronos *layer*
  instead, and a layer cannot remove itself from the loader's chain while the
  game is running — attempting to leave crashes the application. There,
  "stops" means FrameLedger stops observing and passes every call through
  untouched. It records nothing further, but its library remains loaded until
  the game exits.
- FrameLedger contains **no evasion techniques of any kind** — it does not hide, rename, obfuscate, or disguise itself. It is intended to be plainly identifiable to any security software that looks.

  For completeness about what it *does* run inside the game: besides the intercepted calls themselves, the component starts **one background thread** that sleeps for a second at a time and checks whether it has been told to stop. It reads only FrameLedger's own shared memory. It does not scan the game, enumerate what the system has loaded, or query Windows services — deliberately, because software that does those things from inside a game process looks like the thing this tool is trying not to be mistaken for.
- FrameLedger injects into a game only after you enable that game individually. For every other game — and for any game where the safety checks refuse — it records the session's start, end and duration together with whatever hardware sensor readings your machine provides, and reports every other measurement as not available. **There is no measurement mode that works without injecting**; an earlier version of this document described one, and it was never built. FrameLedger never estimates a value it could not measure, and it always shows which of the two modes produced a session. Neither mode requires administrator rights.

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

> **Frame timing is measured today, and so are two of the settings.** Which upscaler is executing, and whether Ray Reconstruction is running with it, are read from the arguments the game passes — for titles that go through Streamline. **The rest are not measured at all yet**: quality preset, render resolution, frame generation and ray tracing have no hook behind them. Where a value has not been measured the software marks it as unmeasured rather than reporting a zero, so it will read `N/A` and never a confident wrong answer — including for a title whose upscaler runs by a route this hook cannot see, which reports `unknown` rather than "none". See the accuracy audit at the top.

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
