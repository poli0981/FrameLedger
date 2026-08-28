# 14 — Testing

Managed: xUnit + FluentAssertions + NSubstitute. Native: Catch2. Coverage goal ≥ 80% on `Domain` + `Application`; **Domain metric calculators ≥ 95% or the PR fails**.

## Safety-guard tests (highest priority — these protect users, not code quality)

The anti-cheat guard is the one component where a bug can cost someone an account. It gets the most rigorous treatment in the codebase:

**These are Catch2 tests, not xUnit.** `20_OPEN_QUESTIONS` §S13(a) settled the guard into the C++ `FrameLedger.Injector`, so the fail-closed matrix below lives in the native suite and the guard needs seams — injectable enumerator function pointers — before any of these failures can be forced at all. Landing Catch2 is a prerequisite of the guard, not a later nicety (§S15).

- Blocklist matching: exact, case-insensitive, prefix rules; every family in `19_SAFETY` §Blocklist seed has a fixture. Two families (Activision Ricochet, Valve VAC) have **no data yet**, so their fixtures must assert that absence explicitly rather than quietly passing on an empty rule set.
- **Fail-closed proofs:** malformed `detection-rules.json`, missing `anticheat` block, unreadable target process, `EnumProcessModulesEx` failure, partial module list → **all must refuse injection**, never allow. Each is a named test.
- **Measured error paths that must be named tests** (`spike-notes.md` §1): `ERROR_PARTIAL_COPY (299)` from a suspended target, `ERROR_ACCESS_DENIED (5)` from a protected one, a driver-list parse whose paths are not native paths, and a service query returning `ACCESS_DENIED` — the last of which **cannot be produced on real services by a standard user**, so it exists only as a fake. Every one means REFUSE.
- Mid-session detection: simulated late-loading anti-cheat module → `unhookRequested` set, hooks disabled within one frame, session finalized `unhooked_safety`. Cover the **driver** scan too, not only modules: a machine-wide driver can start after injection.
- **Absence-of-override test:** no code path may reach the injection primitive without a passing guard result. The mechanism previously described here — a `sealed` token type "only the guard can produce" — does not work and was disproved by compiling (`20_OPEN_QUESTIONS` §S8: C# accessibility flows inward, `CS0122`). The replacement is structural: the guard **owns the chokepoint** and calls the primitive itself, the primitive has internal linkage in the guard's own translation unit so no other TU has a symbol to call, and a build-time check asserts that nothing else references it. A token that escapes can be ignored; a symbol that does not exist cannot.
- Static pre-scan: a game directory containing `EasyAntiCheat/` renders `hook_enabled` un-settable; API-level attempt to set it is rejected — including via `SetWatchlist`, which no longer carries hooking state at all (`07_IPC` §The pipe is not a trust boundary). **The toggle half needs persistence and is not built yet**; the scan itself is ctest `fl_prescan`, whose matrix is mostly fail-closed: a listing that FAILED, one that was TRUNCATED, a directory that could not be established, unusable rules, and a missing evidence source must each be *undetermined*, never clean. Two cases exist only to stop the rest passing vacuously — a genuinely clean directory must be **allowed**, and a name matching the wrong group (a directory named like a `files` entry) must **not** fire. One case removes a family from the rules and asserts the hit disappears, which is what proves there is a single matcher rather than a second one that happens to agree.

## Native unit tests (Catch2)

- **Ring buffer:** SPSC correctness under a hammering writer thread; seqlock torn-read detection; wrap and overwrite-oldest semantics; power-of-two capacity assumptions. Include the **one-full-lap case**: a reader stalled exactly `capacity` records must not validate a different frame as unchanged (this is what the never-reset `seq` counter defends, `07_IPC` §Protocol rules).
- **Drop accounting:** the reader-side computation (`writeIndex - readIndex > capacity`) reports the right count and resumes at the right index. The writer has no drop counter to test — by design.
- **Torn records produce gap markers,** not silently shortened streams; a golden test asserts the gap does not appear as a stutter.
- **Record and header layout:** `static_assert(sizeof(...) == 64)` and `offsetof` assertions for `FlFrameRecord` **and all three header structs** (`FlShmHandshake`, `FlWriterState`, `FlControlBlock`) — the Agent reads across the process boundary in every one of them. Both halves exist as of 2026-08-05: the native side is `layout_test.cpp` plus the `static_assert`s (ctest `fl_shm_layout`), and the runtime offset dump from `tools/fl-layout-dump` is consumed by `ShmLayoutMirrorTests`, which walks the field list in **both directions** and additionally asserts blittability — `Marshal.SizeOf` and every offset stay correct for a mirror that uses `[MarshalAs(ByValTStr)] string`, while `Unsafe.SizeOf` drops from 64 to 40 and the struct can no longer be read from a mapped view.
- **Fault policy:** injected SEH faults in a fake hook body → counter increments, original still called, self-disable at 3.
- **Hook index verification, by behaviour.** A vtable slot carries no identity, so "check that slot 8 *is* `Present`" is not a question the runtime can answer — the earlier wording here required something unimplementable. What is answerable: patch the slot, call the method, see whether the detour ran (`17_HOOK_ENGINE` §Getting vtable addresses; proved for slots 8, 13 and 22 in `spike-notes.md` §H4). Runs headless on CI via WARP and `CreateSwapChainForComposition`. A deliberate mismatch aborts installation instead of patching.
- **Every probe must be shown to fail.** A probe that ends in an unconditional assertion is green by construction and its ctest is decorative — `fl_proxy_swapchain` shipped in exactly that state. Before a probe counts as a regression net, break the thing it watches and watch it go red.

## Managed unit tests

### Golden metric tests (write first)
- Hand-computed fixtures: time-based avg (not mean-of-FPS), median, p99/p99.9 with **linear interpolation** (include cases where interpolation and nearest-rank differ), min/max, σ, stutter rule, sufficiency guards at 999/1,000 and 9,999/10,000 frames.
- FG ladder: records with API-sourced FG → `fg_source = api`; present-count delta only → `presentdelta`; ETW `FrameType` → `etw`; cadence ≥ 1.5 → `cadence`; ambiguous → `none`. Factor always = Displayed/Native regardless of rung.
- Upscaling: exact ratio from render/output pairs; **segment splitting** when resolution changes mid-stream; `settings_changed_midsession` flag; dominant-segment selection.
- RT: AS-builds-only stream (inline RayQuery case) → `rt = Yes` — the specific regression the old design got wrong; dispatch-only → `Yes`; RT-capable device with neither → `No`; D3D11 → `N/A`. PT confidence scoring never yields `Yes`.
- Tri-state precedence: `manual > measured > inherited > N/A`.
- Tier gating: a Tier-2 record stream must produce `N/A` for upscaler/RT/VRAM/latency — **never a fabricated value**.

### Parsers & infrastructure
- Shm reader against a synthetic mapping: version mismatch → refuse; torn records skipped; dropped counter propagated.
- ~~PresentMon CSV (Tier 2): header-map building with shuffled/unknown/missing columns, malformed lines, invariant decimal. Fixtures recorded from real runs.~~ **Removed 2026-08-27 with the tool.** Whatever Tier 2 ends up parsing needs its own row, written against the format it actually reads — and the lesson from the one parser that did get written is worth carrying: its CSV's first column was named `Application` while the value it counted was also `Application`, so resolving by NAME rather than position was load-bearing (`spike-notes` §11).
- Steam ACF (KeyValues), GOG `.info`, Epic `.item`, itch receipt — real-world fixtures.
- `RuleEvaluator`: fixture directory trees per engine, including the "Unity markers present but UE structure too" ordering case.
- Blob codecs round-trip for every series in `frame_blobs` (NaN forbidden — assert), including the two-pair `render_res` encoding and the three-bit `rt_flags` byte. SQLite migrations apply cleanly from an empty file to the current schema, and re-applying is a no-op. (There is no v1→v2 upgrade to test — `06_DATA_MODEL` §Migrations.)
- IPC pipe: framing, split reads, oversize rejection, unknown fields/types ignored, protocol mismatch.

## Integration tests (CI-runnable, no game, no anti-cheat surface)

`hook-harness` is what makes this architecture testable without touching a real title:

- Harness presents 120 s at a controlled cadence → Overlay injected → ring drained → session written → assert every aggregate against expected values.
- Harness simulates: resolution change mid-run (segments), stub upscaler exports (`upscaler` detection), RT PSO + dispatch (RT flags), AS builds without dispatch (inline RayQuery path), PSO compile spikes (stutter attribution), and a fault-triggering hook path (self-disable).
- `.partial` recovery: kill the Agent mid-capture, restart, assert `interrupted` session recovered.
- Guard integration: harness loads a **dummy DLL named like an anti-cheat module** → injection refused pre-launch; loaded late → safety unhook. (A renamed harmless DLL, not real anti-cheat software.)

## Hook overhead measurement (NFR-1, per release)

1. `hook-harness` in a fixed workload, present-rate uncapped: 3 runs hooked vs 3 unhooked → mean present-call cost delta must be ≤ 1 µs (measured with QPC around the call site in an instrumented harness build).
2. Real game, fixed 10-minute scene, capture ON vs OFF, 3 runs each: **game's own Avg FPS delta ≤ 0.5%**, Agent CPU ≤ 1% of a core, Agent RSS ≤ 150 MB, Overlay resident ≤ 8 MB.
3. Results recorded in the release notes ("measured overhead this release: …").

## Tier cross-validation (accuracy regression net)

Run the same game session with Tier 1 and Tier 2 simultaneously where possible (ETW does not conflict with hooking) and compare frametime distributions: Avg/1% Low must agree within **1%**. Divergence beyond that means one of the two paths is wrong and blocks the release. This is also the check that would have caught the original detection-accuracy problem.

## Manual test matrix (per release)

| Axis | Values |
|---|---|
| OS | Win 10 22H2 VM · Win 11 dev machine |
| GPU vendor | NVIDIA (primary) · AMD or Intel if available, else document as untested |
| API | D3D11 · D3D12 · Vulkan (layer path) · OpenGL · **a 32-bit title, asserting it is correctly refused and routed to Tier 2** |
| Upscaler | DLSS SR · DLSS-G · DLSS-RR · FSR2/3 · XeSS · none |
| RT | DXR 1.0 dispatch title · inline RayQuery title · non-RT title |
| Mode | launch mode · attach mode · mid-session settings change |
| Safety | game with anti-cheat → toggle disabled · simulated late AC load → unhook · double-crash → auto-disable |
| Tier | forced Tier 2 · Tier 1 → Tier 2 degradation notice |
| Update | Velopack delta; update deferred while a game is hooked (FR-12) |

## Release smoke

Clean-VM install → Legal Gate → Agent setup (unelevated) → add a game → consent dialog → hooked 2-min session → charts render → CSV opens in Excel → Vulkan layer registered/unregistered cleanly → uninstall removes layer registration, task, and asks about data.
