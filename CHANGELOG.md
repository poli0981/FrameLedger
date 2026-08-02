# Changelog

All notable changes to FrameLedger are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html) — `MAJOR`
bumps for a database schema or IPC protocol break (`docs/11_UPDATER.md`).

`release.yml` reads the section for the tag being released and uses it as the
GitHub release body, so a missing section means an empty release note.

## [Unreleased]

### Added
- Design documents (`CLAUDE.md`, `docs/01`–`20`, `legal/`) and the repository
  skeleton: solution, project stubs, CMake presets, `build.ps1` quality gate,
  CI workflows, issue and PR templates, seed detection rules.
- `docs/20_OPEN_QUESTIONS.md` — audit findings that need an empirical answer
  from the P0 spike or a design decision, grouped by what they block.
- `tools/license-check.ps1` and `tools/rules-validate.ps1`, both proven to fail
  on a planted violation rather than only to pass on a clean tree.
- **P0 spike, first results.** `fl-probe-hookprofile` and `hook-harness`, wired
  as four ctests so every answer below is re-checked on each build rather than
  being a one-off measurement. Both run headless — WARP and
  `CreateSwapChainForComposition` mean no GPU and no window station, so they
  pass on a hosted CI runner as well as the dev machine.
  - **H1** `/guard:cf` is compatible with MinHook trampolines. Verified with CFG
    genuinely enforcing (guard tables, 114 guarded call sites, mitigation query),
    since a green probe on a non-enforcing process would have proved nothing.
    Measured with strict mode off — recorded as residual risk.
  - **H3** `-D_HAS_EXCEPTIONS=0` works with `<atomic>`, and `std::atomic_ref` is
    lock-free at both widths. Note the define converts a would-be throw into
    `__fastfail`, i.e. an uncatchable kill of the host process.
  - **H4** Vtable indices proved by behaviour, not asserted: slot 8 `Present`,
    13 `ResizeBuffers`, 22 `Present1`.
  - **H2/H5** partly answered; see `docs/20_OPEN_QUESTIONS.md`.
- MinHook (BSD-2-Clause), fetched by CMake and pinned to the commit behind
  `v1.3.4`. Licence texts for MinHook, NVAPI, MPL-2.0 and Apache-2.0 now ship in
  `legal/licenses/`.
- **`fl-probe-guard`** (ctest `fl_guard_apis`) — measures the Windows APIs the
  anti-cheat guard is built on, unelevated, which is the default Agent under
  ADR-9. It is not the guard and takes no injection rights. Fills
  `docs/spike-notes.md` §1 and **closes §S7**.
  - **§S1 is sharper than documented:** `EnumProcessModulesEx` against a
    `CREATE_SUSPENDED` target does not return an empty list, it *fails* with
    `ERROR_PARTIAL_COPY`. An error cannot be mistaken for a clean scan the way
    an empty success can, so the guard rule is "any failure means REFUSE".
  - **`LIST_MODULES_ALL` is mandatory:** on a live 32-bit target the default
    filter returned 7 of 15 modules *as a success*.
  - Driver-scan assertions check path **content**, not count — "266 distinct
    strings" is what the historical two-byte offset bug also produced. A canary
    re-parses the same buffer with that skew every build and must be rejected.
  - Records what it could **not** measure: a service query returning
    `ACCESS_DENIED` is not producible unelevated against stock services.
- Signer matching for the unknown-but-suspicious heuristic uses the certificate
  subject's **`O=`** field, now a schema `const`. Measured: every WHQL-signed
  binary — including the NVIDIA display driver — carries
  `CN='Microsoft Windows Hardware Compatibility Publisher'`, so a `CN` match
  would make the whole driver stack read as untrusted.
- **`hook-harness --probe-unhook`** (ctest `fl_unhook_preserves_foreign`) —
  **closes §H7**. A later hooker saves *our detour* as its original, so an
  unconditional vtable restore deletes their hook silently. Compare-and-restore
  now, with both halves asserted: we decline when the slot changed and we do
  restore when it did not. Simulated rather than depending on RTSS, so it is
  deterministic and runs on CI.
- **`hook-harness --probe-cost`** — the last open bullet under `spike-notes` §3.
  A vtable detour costs **8.4 ns/present** against NFR-1's 1,000 ns budget
  (20,000 presents × 5 interleaved runs, medians). This bounds the *mechanism*,
  not the product; the Overlay's real cost is `14_TESTING` item 2 on a real
  game. Not a ctest — a timing threshold on a shared runner fails for reasons
  unrelated to the code.
- **`tools/coverage-gate.ps1`** — `14_TESTING`'s ≥80% / ≥95% thresholds were
  called PR-failing while the cobertura reports had been produced and ignored
  since the repository was scaffolded. The gate is **self-arming**: it reports
  emptiness today and starts enforcing on the first `.cs` file in Domain or
  Application, so the number is never negotiated against code that already
  exists. Five failure modes proven red.
- **`tools/versioninfo-check.ps1`** and a real `version.rc` for the Overlay and
  the Vulkan layer. `19_SAFETY` requires every shipped native binary to identify
  itself — being visible to anti-cheat is the design principle — and the Overlay
  CMakeLists asserted "CI fails the build without it" directly above a TODO to
  add it, with no `.rc` file anywhere in the repository and nothing checking.

### Verified
- **NVAPI is MIT including `nvapi64.lib`** — the import libraries are tracked
  files in the MIT repository and its `License.txt` names them explicitly, so
  Reflex / PC latency is reachable.
- **LibreHardwareMonitor carries no MPL-2.0 Exhibit B** on any depended-upon
  file, so the L2 telemetry layer is GPL-3.0 compatible. Checked against the
  pinned 0.9.6 package, not just the repository.

### Fixed
- **A ctest that could never go red.** `fl_proxy_swapchain` ended in
  `Check(true, "observation recorded")`, so the H5 regression net was green by
  construction and would have stayed green if a forwarding proxy ever stopped
  reaching our hook. Now asserts the recorded finding; proven red by breaking
  the proxy's forward, then green again. The H4 probe had the same shape in its
  "slots restored" check, which now verifies the restore.
- **Deleting an anti-cheat family passed CI.** `rules-publish.yml` reported
  removals with `::warning::` and exited 0; it also compared only
  `modules`+`drivers`, and its `paths:` filter meant a change to the schema or
  the validator never triggered it at all. Removals now fail the job, all five
  family-bearing groups are compared, per-title lists are checked for shrinkage,
  and an unobtainable base version fails closed instead of reporting success.
- **Two of the three imperative checks §S5 was closed on did not exist.**
  `rules-validate.ps1` had only the required-family floor. Added:
  case-insensitive duplicate values, and a prefix floor that rejects both
  too-short prefixes and any prefix shadowing a system module. The family floor
  is now group-aware — it previously unioned `modules`+`drivers`, so moving
  Riot Vanguard out of `drivers` satisfied it while the machine-wide driver gate
  lost its only entry.
- **`19_SAFETY` §Blocklist seed published glob syntax the schema rejects**
  (`EasyAntiCheat*.dll`, `pb*.dll`). A maintainer copying those created entries
  matched literally, which never fire. The table now shows literal tokens with
  their group and match kind, and `rules-validate.ps1` cross-checks it against
  the data so the two cannot drift. Activision Ricochet and Valve VAC are kept
  as explicit "no data yet" rows rather than dropped.
- Pre-injection check 3 is **inert** — `blockedExecutables` and
  `blockedStoreIds` are both empty, so it matches nothing. Recorded in the data,
  beside the check, and as `20_OPEN_QUESTIONS` §S14, rather than being
  inferable only from two empty arrays.
- **Coverage reports accumulated and were never pruned** — 24 after a handful of
  builds. Any gate reading "the coverage reports" would have read mostly
  history, and taking the best rate across them means a project that once scored
  95% and now scores 10% still passes. `build.ps1` clears `TestResults` before
  each run, and the gate takes only the newest report per test project.
- **An empty assembly reported as 100% covered.** Coverlet emits `line-rate=1`
  for an assembly with no coverable lines; taken at face value that is a vacuous
  pass, and it is what the coverage gate printed on its first run. It now counts
  `<line>` elements so "fully covered" and "nothing to cover" stay distinct.
- `17_HOOK_ENGINE` called vtable swapping a "cleaner uninstall" than inline
  patching. In the multi-overlay case — the normal state of a gamer's machine —
  that is backwards (§H7).
- `14_TESTING` still required runtime hook-index verification in a form §H4
  proved unimplementable: a vtable slot carries no identity, so slot identity is
  provable only by behaviour.
- `CMakePresets.json` had no `x64-debug` test preset.
- Shared-memory layout was arithmetically impossible: the header was 88 bytes
  while the control block was mapped to `0x0040`. In code `unhookRequested`
  would have aliased `faultCount`, firing the safety stop on any hook fault.
- Frame-generation detection relied on `GetFrameStatistics().PresentCount`
  exceeding the application's own present count, which cannot happen. Replaced
  with FG feature evaluations counted at the source.
- `dispatchRaysCount` was `uint16` and saturated on every ray-traced title at
  1080p or above.
- `SetColorSpace1` was attributed to `IDXGISwapChain4`; it is on
  `IDXGISwapChain3`.
- `fl_shm.h` defined a contract expressed entirely in `std::atomic_ref` without
  including `<atomic>`.
- README, Disclaimer and EULA promised the no-injection capture mode is "always
  available" when it requires an elevated agent.

### Changed
- **Eight safety questions closed by decision or specification** (`S3`, `S7`,
  `S8`, `S9`, `S10`, `S11`, `S12`, `H8`, `M9`; `S4` two-thirds), each written
  into its owning doc and deleted from `20_OPEN_QUESTIONS.md`:
  - The guard stays in the C++ `FrameLedger.Injector` (§S13(a)) and **owns the
    chokepoint** — no clearance escapes it, because the injection primitive has
    internal linkage in the guard's own translation unit. A token that escapes
    can be ignored; a symbol that does not exist cannot be called. The four
    consequences of that choice are tracked as §S15.
  - `FrameLedger.Injector.exe` does not exist and does not ship. A user-runnable
    `LoadLibraryW` injector is a path into a game the guard does not stand in
    front of.
  - **No inbound pipe message may assert a safety fact.** `UpdateRules` lost its
    `path`; `SetHookEnabled` lost its client-supplied `consentAt`; `SetWatchlist`
    lost `hookEnabled` — the last two were found while auditing for the first.
  - The **driver scan now re-runs mid-session** alongside the module scan. A
    machine-wide anti-cheat driver can start after injection, and a module-only
    re-scan looks inside the hooked game and would never see it.
  - The Vulkan layer is **not registered at install time**; only while at least
    one Vulkan game has hooking enabled. The enable-list is specified — location,
    format, bounds, ACL, sole writer, and every-failure-is-passthrough.
  - A game enabled *before* it started matching is force-disabled on the next
    rules update or exe change, reusing `hook_blocked_reason`, **preserving
    consent**. A rules update that removes a match does not re-enable it.
  - Rules feed: one read location, replace-only-if-valid keeping the last valid
    copy, and staleness that **warns and never disables**. Signing stays open.
  - NFR-3 no longer promises the Overlay "must never crash a game" — SEH cannot
    catch stack overflow or `__fastfail`, and `-D_HAS_EXCEPTIONS=0` produces the
    latter.
  - P0 is resequenced: guard is item 0, Vulkan passthrough item 1. The accuracy
    **baseline detector is added to P0 scope** (§M9 — the "old detection" this
    comparison assumed does not exist). The FPS-impact exit criterion moves to
    the end of P1, since it silently imported P2's drain and recorder.
- Two questions nobody had recorded, added: §S14 (pre-injection check 3 is inert
  and has no "cannot determine" state) and §S16 (*which* process the guard
  scans — `Check(pid)` is singular, but anti-cheat often lives in the launcher
  or a sibling, not the presenter).
- Direct3D 9 is not a Tier-1 API in v1: the Overlay is x64-only and those
  titles are almost entirely 32-bit. They are captured at Tier 2.

[Unreleased]: https://github.com/poli0981/frameledger/commits/main
