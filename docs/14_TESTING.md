# 14 — Testing

Managed: xUnit + FluentAssertions + NSubstitute. Native: Catch2. Coverage goal ≥ 80% on `Domain` + `Application`; **Domain metric calculators ≥ 95% or the PR fails**.

## Safety-guard tests (highest priority — these protect users, not code quality)

The anti-cheat guard is the one component where a bug can cost someone an account. It gets the most rigorous treatment in the codebase:

- Blocklist matching: exact, case-insensitive, prefix rules; every family in `19_SAFETY` §Blocklist seed has a fixture.
- **Fail-closed proofs:** malformed `detection-rules.json`, missing `anticheat` block, unreadable target process, `EnumProcessModulesEx` failure, partial module list → **all must refuse injection**, never allow. Each is a named test.
- Mid-session detection: simulated late-loading anti-cheat module → `unhookRequested` set, hooks disabled within one frame, session finalized `unhooked_safety`.
- **Absence-of-override test:** a test asserts no code path reaches `Injector.Attach` without a passing `AntiCheatGuard` result (enforced by making the guard result a required constructor argument of a `sealed` token type that only the guard can produce — a design that makes the bug unrepresentable rather than merely untested).
- Static pre-scan: a game directory containing `EasyAntiCheat/` renders `hook_enabled` un-settable; API-level attempt to set it is rejected.

## Native unit tests (Catch2)

- **Ring buffer:** SPSC correctness under a hammering writer thread; seqlock torn-read detection; wrap and overwrite-oldest semantics; `droppedRecords` accuracy; power-of-two capacity assumptions.
- **Record layout:** `static_assert` plus a runtime offset dump consumed by the managed struct-mirror test.
- **Fault policy:** injected SEH faults in a fake hook body → counter increments, original still called, self-disable at 3.
- **Hook index verification:** the dummy-object vtable probe returns the expected indices on the CI runner's D3D runtime; a deliberate mismatch aborts installation instead of patching.

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
- PresentMon CSV (Tier 2): header-map building with shuffled/unknown/missing columns, malformed lines, invariant decimal. Fixtures recorded from real runs.
- Steam ACF (KeyValues), GOG `.info`, Epic `.item`, itch receipt — real-world fixtures.
- `RuleEvaluator`: fixture directory trees per engine, including the "Unity markers present but UE structure too" ordering case.
- Blob codecs round-trip (NaN forbidden — assert). SQLite migrations 0→v2 including the **v1→v2 upgrade with existing ETW sessions** (they must survive as `capture_tier = 2`).
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
| API | D3D11 · D3D12 · D3D9 (VN/older indie) · Vulkan (layer path) · OpenGL |
| Upscaler | DLSS SR · DLSS-G · DLSS-RR · FSR2/3 · XeSS · none |
| RT | DXR 1.0 dispatch title · inline RayQuery title · non-RT title |
| Mode | launch mode · attach mode · mid-session settings change |
| Safety | game with anti-cheat → toggle disabled · simulated late AC load → unhook · double-crash → auto-disable |
| Tier | forced Tier 2 · Tier 1 → Tier 2 degradation notice |
| Update | Velopack delta; update deferred while a game is hooked (FR-12) |

## Release smoke

Clean-VM install → Legal Gate → Agent setup (unelevated) → add a game → consent dialog → hooked 2-min session → charts render → CSV opens in Excel → Vulkan layer registered/unregistered cleanly → uninstall removes layer registration, task, and asks about data.
