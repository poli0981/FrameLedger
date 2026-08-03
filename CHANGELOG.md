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
- **ctest `fl_rules_budget`** — asserts the thing nothing asserted: **that the
  rules file we actually ship parses in the guard.** Every case in
  `guard_test.cpp` parses an inline fixture, so `rules/detection-rules.json` had
  never been through `ParseRules` in a test. Its boundary cases are **generated
  from the constants in `fl_ac_rules.h`** rather than hand-copied, so the schema,
  the parser and the test cannot drift apart a second time.
  - It also counts jsmn tokens against a stated budget and **prints the headroom
    on every run**, because the hazard is a capacity nobody looks at until it is
    already breached. Measured today: 9,128 bytes, **475 of 8,192 tokens**, of
    which **275 (58%) are `$comment`/`engines`/`platforms`/`capabilities` the
    guard never reads** — jsmn tokenises the whole file before it locates
    `anticheat`, so growing detection data spends the safety gate's budget. The
    budget is half the capacity so that crossing it fails while there is still
    room to act. Stated plainly: with ~8× headroom this assertion will not fire
    for a long time, and the seed parse is what earns the test its place.
- **`tools/coverage-gate.ps1`** — `14_TESTING`'s ≥80% / ≥95% thresholds were
  called PR-failing while the cobertura reports had been produced and ignored
  since the repository was scaffolded. The gate is **self-arming**: it reports
  emptiness today and starts enforcing on the first `.cs` file in Domain or
  Application, so the number is never negotiated against code that already
  exists. Five failure modes proven red.
- **The anti-cheat guard** (`FrameLedger.Injector`) — P0 item 0, the component
  `19_SAFETY` calls the one where a bug can cost someone an account. Native, per
  §S13(a).
  - **The guard owns the chokepoint.** There is no `Check()` that hands a
    verdict to a caller who might ignore it: the injection primitive has
    internal linkage inside `fl_guard.cpp`, so no other translation unit has a
    symbol to call. `tools/chokepoint-check.ps1` enforces it, and also fails on
    the Win32 calls that *constitute* injection — someone writing a second
    injector elsewhere would never touch the first name.
  - **Every evidence source is a seam**, so each failure `14_TESTING` requires
    is forced rather than hoped for: enumeration failure, a partial module list,
    an unreadable process, an empty scan set, a denied service query, and five
    ways for the rules file itself to be unusable. 24 cases, 132 assertions.
  - **Every collector returns a tri-state, never a bare list.** `kOk` /
    `kFailed` / `kIncomplete` exist because an empty list is the exact ambiguity
    that produced this project's worst defect — it reads as "nothing found" when
    it may mean "could not look".
  - **§S16 implemented**: the scan set is the injection target, its descendants,
    and its ancestors up to but excluding the first platform launcher.
  - The rules parser re-checks the required-family floor **and group**. CI
    already enforces that, but rules ship as updatable data, so CI is not in the
    loop at injection time.
  - **The injection primitive**, in the order CLAUDE.md rule 2 requires: guard
    and full matrix first, then the primitive. `VirtualAllocEx` +
    `WriteProcessMemory` + `CreateRemoteThread` on documented `LoadLibraryW` —
    the most ordinary technique there is, which is the point. Minimal handle
    rights, `PAGE_READWRITE` (we write a *path*, never code), a bounded wait,
    the remote page always freed, and WOW64 targets refused because an x64 DLL
    cannot load there and the `LoadLibraryW` address would be meaningless.
  - Success is **verified by observation**: the target is re-enumerated and the
    module looked up by name. `GetExitCodeThread` would give `LoadLibraryW`'s
    `HMODULE` truncated to 32 bits, so a handle with a zero low word reads as
    failure — and a nonzero one reads as success with nothing having checked
    that our DLL is actually there.
  - **The evidence seam is compiled out of everything that ships.**
    `GuardedInject`/`Evaluate` take no `Sources`; the injectable versions exist
    only under `FL_GUARD_TESTABLE`, which only the test target defines, and the
    guard sources are compiled *into* the test rather than linked from the
    static lib. `FrameLedger.Injector.lib` therefore contains zero
    `WithSources` symbols — verified with `dumpbin`. `chokepoint-check` fails
    the build if any other CMakeLists defines the macro.
  - End-to-end tests inject into `hook-harness --hold` — our own dummy D3D11
    app, no game and no anti-cheat surface — and assert both directions: a
    passing verdict really loads the DLL, and a **refused** verdict leaves the
    target untouched.
- **`GuardSupervisor` — the Agent half of §S2 part three.** Publishes
  `guardTicks`, and the load-bearing property is what it counts: **completed
  guard evaluations, never seconds**. The field was specified as "Agent bumps
  every second"; a timer attests the Agent *process* is alive while the guard
  loop can be dead — a swallowed exception, a blocked service query, a stall on
  one unreadable process in the §S16 scan set — and the capture side would keep
  observing *because* the thing supervising it had stopped. Seven tests force
  each of those and assert the tick does not move. A refusal latches.
- **§S2's second half — the Vulkan layer's blocklist self-scan.** The layer
  scans its OWN process at init and goes fully passthrough on any hit, using the
  **same matcher and the same rules file as the injection guard**
  (`fl_ac_rules.h`, compiled into both). A layer with its own blocklist would be
  a second matcher that can disagree with the first.
  - Every uncertainty resolves to inert: rules unreadable, malformed or
    incomplete, enumeration failed, a truncated list, a module that could not be
    named, or an actual hit. Opposite *polarity* from the injection guard —
    where an unknown means refuse to inject — same principle: leave the host
    alone.
  - `fl-probe-vklayer` (ctest `fl_vklayer_selfscan`) asserts **both**
    directions: a clean process is not forced inert, and one carrying a planted
    module is. The planted module is our own DLL under a blocklisted name, per
    `14_TESTING`; no real anti-cheat software is shipped, downloaded or run.
  - The probe installs the repository seed rules when none exist and removes
    them afterwards. Without that it skipped on any machine that had not run the
    product — and a ctest that always skips is a gate that cannot fail. There is
    deliberately no way to point the layer at a different rules file (§S3), so
    installing them is the only honest option.
- **The managed guard facade — §S15 item 1, and the first real managed code.**
  `FrameLedger.Guard.dll` exposes a C ABI; `Infrastructure`'s
  `NativeAntiCheatGuard` is a thin P/Invoke facade over it, and
  `Application`'s `IAntiCheatGuard` exposes only the two questions the guard
  answers — no rules, no blocklist, no evidence. **Nothing managed matches an
  anti-cheat blocklist**, because two matchers that can disagree is a fail-open
  by construction. Two tests keep that true: one asserts no managed type
  carries a blocklist token and the port accepts no evidence; the other asserts
  `AntiCheatRefusalReason` has not drifted from `fl::guard::Reason`, by reading
  every name back through the ABI.
  - `HookedCaptureGate` checks the one thing the native guard structurally
    cannot see — **per-game consent** (CLAUDE.md rule 1) — and refuses an
    un-enabled, unconsented or previously-blocked game *without the guard being
    called at all*.
  - **The guard DLL is loaded by absolute path** and never by search: a planted
    `FrameLedger.Guard.dll` would replace the entire gate. CA5393 rejects
    `ApplicationDirectory` and no "safe" search path fits a DLL of our own, so
    a `DllImportResolver` pins the load to one file beside the assembly.
  - The **coverage gate armed itself** exactly as designed the moment Domain
    and Application gained source: 100% over 38 and 50 lines against the 80%
    floor, with no threshold negotiated after the fact.
- **Catch2 and jsmn**, both pinned by commit (§S15 items 2 and 3). jsmn was
  chosen for what it does *not* do: no allocation, no exceptions, failure as a
  return code. `/EHsc` is on the test binary only — a throw crossing the guard
  would be an unstructured exit from the one function that must always reach a
  verdict.
- **Khronos Vulkan headers vendored** (`Apache-2.0 OR MIT`) at
  `src/native/third_party/vulkan-headers`, copied from SDK 1.4.357.0 rather than
  fetched: CI must not need a ~1 GB SDK install, and these are the exact headers
  matching the loader the blast-radius test runs against. Only the C closure a
  Windows layer needs; the C++ bindings are excluded because they allocate and
  throw, which CLAUDE.md forbids in the layer.
- **The Vulkan layer is real**: loader ABI (`vkNegotiateLoaderLayerInterfaceVersion`,
  instance/device chain walking), the enable-list from
  `17_HOOK_ENGINE` §The enable-list, exports by name via a `.def` file, and the
  manifest JSON that `12_BUILD` had listed as a build output since the start but
  which existed nowhere. **It still intercepts nothing** — §S2's in-layer
  blocklist scan has to land before `vkQueuePresentKHR` is hooked.
- **`tools/vklayer-blastradius.ps1`** — answers `spike-notes` §2 and closes the
  first half of §S2. `enable_environment` verified against loader 1.4.357: with
  the variable unset the loader locates the manifest and **never maps the DLL**,
  and it compares the variable's *value*, so a stray `=0` does not enable us.
  Vulkan Tier 1 is therefore **launch-mode-only**. The script is the only place
  the layer is registered and unregisters in a `finally`.
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
- **The schema accepted rules files the guard refuses to parse** (`20_OPEN_QUESTIONS`
  §S17). Eight bounds, and the schema was looser in every one. An over-cap entry
  is not a dropped entry: `ParseRules` returns `kMalformed` and `19_SAFETY` turns
  that into **REFUSE for every title on the machine**. Rules ship as updatable
  data pushed to every client and `rules-validate.ps1` validated against the
  *loose* schema, so a CI-green rules edit could have taken the product out in
  the field. Proven both directions on one input: a 17-value family passes the
  old schema and fails the calibrated one.
  - **The worst was a shape mismatch, not a size one.** `blockedExecutables` and
    `blockedStoreIds` are arrays of objects in the schema and were read as bare
    strings. The first entry anyone added would either overflow `kMaxValueLen`
    with its JSON text and refuse the whole file, or fit and sit there
    unmatchable. Only the two empty arrays kept that theoretical. The parser now
    reads the objects, keeps `family` and `reason` — `19_SAFETY` requires the UI
    to name the check that fired, and an exe name explains nothing on its own —
    and composes `store` + `id` into the joined `"steam:730"` form.
  - The thresholds are **read out of `fl_ac_rules.h` by regex** rather than
    restated, and an unreadable header or a renamed constant **fails rather than
    skips**: an unread threshold is a check that passes without looking.
- **A `static_assert` that could not fire on the change it existed to catch.**
  `fl_guard_abi.cpp` pinned `kRulesIncomplete == 16` to protect
  `FlGuardReasonCount() == 17` — but `kRulesIncomplete` was the **last**
  enumerator, so appending a `Reason` left it at 16, the assert passed, the
  exported count stayed stale, and the managed mirror iterated 0–16 and never
  compared the new value. Now derived from a `Reason::kCount` sentinel; verified
  by appending a reason and watching `GuardMirrorTests` report 18 against 17.
- **`ReasonName`'s exhaustiveness was claimed in a comment and enforced by
  nothing.** Omitting `default:` does not make MSVC object — C4061/C4062 are off
  by default even at `/W4`, measured by appending an enumerator with no case and
  watching `/W4 /WX` build clean. Replaced by ctest `fl_guard`'s "every Reason
  has a distinct name", proven red the same way.
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
- **Two comments in `layer.cpp` documented designs that were measured to be
  wrong** — the more dangerous kind of stale, because a reader designs a gate
  around them. One claimed the Vulkan loader "checks that the variable EXISTS
  rather than comparing its value", the opposite of `spike-notes` §2. The other
  said returning non-`VK_SUCCESS` from negotiation is "a documented, supported
  way to be absent, and what we use when the process was not opted in" — the
  exact design that access-violates every Vulkan application on the machine —
  sitting directly above the text correcting it.
- **The 30 s guard re-scan was scoped to injection**, so it was not specified to
  run for Vulkan at all: `19_SAFETY` said "during a hooked session" / "after
  injection", and `04_CAPTURE`'s state machine reaches `Capturing` only through
  `Injecting`, which a layered session never enters. Rescoped to every Tier-1
  session, with the Vulkan path spelled out.
- **`07_IPC` said the Overlay "keeps writing (harmless)"** when the Agent's
  heartbeat stops — describing an *unsupervised hooked process* as harmless,
  when the 30 s re-scan is exactly what has stopped in that state. Supervision
  loss now means stop observing, for both hosts.
- **`DISCLAIMER` and `README` promised FrameLedger "unhooks"** on detection.
  True for Direct3D/OpenGL; false for Vulkan, where a layer cannot leave a
  running game's loader chain — attempting to leave crashes the application.
  Both now say what actually happens, in legally reviewed text.
- **`fl-probe-vklayer` step 3 was a bare `printf`** with no assertion, in the
  file this project cites as its assert-both-directions exemplar. It now checks
  that the self-scan does not latch, and polls for the unload rather than
  sleeping a fixed interval.
- **A test harness that kept a stale copy of the thing under test.**
  `fl-probe-vklayer` loaded the layer from a copy placed beside it by a CMake
  `POST_BUILD` command — which runs only when the *probe* relinks, so editing
  only the layer left the old DLL in place. A red-green canary left the broken
  layer behind and every later run kept failing against it. The same mechanism
  could just as easily have kept a *working* copy and reported a broken layer as
  passing. The probe now loads the layer's real build output.
- `FrameLedger.App` could not compile once `FrameLedger.Application` existed:
  a sibling namespace beats a `using`, so the bare name `Application` resolved
  to the namespace rather than the WPF type. The `App` class now says
  `System.Windows.Application` in full.
- **A Vulkan layer that declined to load would have crashed the host.** Found by
  the blast-radius test, in code written the same day. Returning
  `VK_ERROR_INITIALIZATION_FAILED` from `vkNegotiateLoaderLayerInterfaceVersion`
  — the obvious way to say "this process did not opt in, skip me" — does not
  make loader 1.4.357 skip the layer; it access-violates the application. Every
  Vulkan program on the machine outside our enable-list would have crashed,
  which is a far larger blast radius than the one §S2 exists to reduce. The
  layer now always accepts negotiation and always forwards; the enable-list
  decides what we *intercept*, never whether we *load*.
- **Two false positives in the blast-radius test itself**, both of which would
  have reported a working gate as broken: the loader prints the manifest path
  during discovery (which contains `FrameLedger.VkLayer`), and `vulkaninfo`
  lists the layer as *available* by name. Neither means the DLL was mapped. The
  test now matches only the loader's `Insert instance layer` / `Inserted device
  layer` lines.
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
