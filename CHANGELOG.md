# Changelog

All notable changes to FrameLedger are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html) — `MAJOR`
bumps for a database schema or IPC protocol break (`docs/11_UPDATER.md`).

`release.yml` reads the section for the tag being released and uses it as the
GitHub release body, so a missing section means an empty release note.

## [Unreleased]

### Fixed
- **The anti-cheat guard gated the target process and nothing gated the payload**
  (`20_OPEN_QUESTIONS` §S22). `FlGuardedInject` — an exported C ABI on the shipped
  `FrameLedger.Guard.dll` — asked only whether a file existed at the caller's
  `dllPath`. Measured through the shipped binary with no test seam:
  `C:\Windows\System32\winmm.dll` loaded into a live process, verdict `Allow`.
  That is the standalone injector §S9 refused to ship, re-exported with a
  published calling convention.
  - The guard now requires the payload to resolve — through symlinks, 8.3 names
    and junctions — into the directory its own code was loaded from, compared by
    file id, and refuses with a new `PayloadNotOurs` reason otherwise. A null
    seam and a seam that cannot answer refuse the same way.
  - **It proves where the bytes live, not what is in them.** Anyone who can write
    to that directory can already replace `FrameLedger.Guard.dll` itself, and the
    project ships unsigned, so the check is exactly as strong as the install
    location. It is also not atomic with the load.
  - `kInjectionFailed` still covers a payload that is simply absent: a damaged
    install and a misuse of the ABI need opposite responses.
  - **Nothing shipped `FrameLedger.Overlay.dll` anywhere**, which had to be fixed
    in the same change or the new constraint would have been a gate that could
    not pass. `dotnet publish` produced an `out/app` with the Agent, the guard and
    the rules seed and no payload at all.
  - Proven red, green and recovering: three canaries each turn `fl_guard` red, and
    the accepted direction is asserted separately — the staged Overlay passes, the
    same file staged elsewhere does not, and the test refuses to run if those two
    paths ever resolve to one directory.

- **The guard's self-exemption asked about the process, not the module that
  matched** (`20_OPEN_QUESTIONS` §S22(b)). Any FrameLedger-family host that did not
  sit beside `FrameLedger.Guard.dll` refused its own injections with
  `SuspiciousUnsigned`, naming our own DLL as the signal — measured, same binary,
  only the caller's directory differing. The Agent worked only because
  `FrameLedger.Guard.targets` happens to co-locate them.
  - The exemption now asks whether the **module** that tripped the fuzzy tier is
    ours, by file id. `ProcessIsOurOwn` is gone; `ModuleIsOurOwn` and
    `PayloadIsOurOwn` share one implementation, and a live test asserts the two
    seams are the same function so they cannot become two answers.
  - **Strictly narrower than what it replaced**: a genuinely foreign suspicious
    module inside a FrameLedger process — an AppInit DLL, an AV user-mode hook, an
    IME — used to be suppressed along with our own, and now is not.
  - It required the restructure §S19(b) predicted. The module sink latched the
    first fragment-matching module and skipped the rest; with per-module
    suppression that becomes a **fail-open reachable by load order** — our DLL
    matches first, is exempted, and a suspicious module loaded afterwards is never
    recorded. The sink now skips an exempt module and keeps looking.
  - Five canaries proven red, including the load-order case in both orders.
  - **Two of the new tests were passing for the wrong reason and were fixed**: a
    fake that returned "cannot determine" without touching its out-param made the
    return-code check untestable, and a redundant null check in the same fake made
    the guard's own null clause untestable — the latter surfaced only because a
    canary disarmed the clause and nothing went red.

### Known issues
- **The anti-cheat guard's fuzzy "unknown-but-suspicious" tier matches benign
  system DLLs, and one of its rules can never fire** (`20_OPEN_QUESTIONS` §S19).
  Re-measured unelevated on Windows 11 26300: 290 processes, 0 inaccessible,
  **three** modules match the `protect` fragment and none is anti-cheat —
  including `mskeyprotect.dll`, the Microsoft Key Protection Provider from
  `system32`. Separately, `gameguard` cannot fire: the match is a
  case-insensitive substring and `guard` is a substring of `gameguard`, so the
  shorter token always wins first.
  - **The earlier entry here overstated this and is corrected.** It said a title
    loading `mskeyprotect.dll` "is refused today, in attach mode". All three hits
    are desktop processes, and none can enter a game's scan set — the ancestor
    walk stops at `explorer.exe`/`services.exe`/`svchost.exe`. Three real titles
    were scanned with no fragment hit. The honest claim is that the fragment
    matches a benign, widely-loaded system DLL and **has not been shown to match
    inside any game's scan set**.
  - **The proposed fix would have addressed one case of three, and is deferred.**
    `mskeyprotect.dll` is **catalog**-signed, so a `WinVerifyTrust(WTD_CHOICE_FILE)`
    implementation — what "wire the signer half" has meant throughout — recovers
    no signer for it and it still refuses. `Malwarebytes.Protection.Interop.dll`
    is validly signed by a publisher absent from `trustedSigners`, which no
    implementation fixes. Doing it properly needs `CryptCATAdmin*` +
    `WTD_CHOICE_CATALOG`, and `WinVerifyTrust`'s default revocation policy
    performs CRL/OCSP network I/O from inside the hard gate — against NFR-10
    offline-first as well as CLAUDE.md rule 8. Measure with `fl-probe-signer`
    first.
  - Still not fixed by deleting a fragment: that is a detection removal in a hard
    gate, and this tier is the only coverage for families the seed has no data
    for.
  - Also recorded: `signerField` and `action` are required by the schema and read
    by no code (`action` is a `const` with one legal value, so reading it could
    change nothing).
  - **The runtime fragment floor is now in place** and this entry no longer claims
    otherwise. The compiled-in floor is generated from `rules/detection-rules.json`
    at build time, so a rules file with no `heuristic` block can no longer make the
    fuzzy tier stop existing.
  - **The unreconciled copies are not fixed by that**, and there are more than the
    three this entry counted. The generated floor is *derived* and cannot drift,
    but `guard_test.cpp`, `rules_budget_test.cpp`, the prose in `19_SAFETY`
    §Heuristic tier and a `$comment` in `detection-rules.schema.json` each still
    restate the list by hand — and the schema comment restates the **four-fragment**
    version, the exact staleness §S19(e) was raised about. No gate cross-checks any
    of them.
- **A rules edit reaches no installed machine until a release** (§S20, feed half).
  The Agent now installs `detection-rules.json` to the location the guard reads,
  so a machine that has never had one no longer refuses every title — that half is
  done and measured. What does not exist is `05_DETECTION` §Trust and staleness'
  HTTPS fetch, so **FR-7.3 is unmet**: anti-cheat entries cannot arrive on their
  own schedule. The guard also still never reads `rulesVersion` or `schemaVersion`,
  so a binary fix and a data fix have no handshake — deliberately, since teaching
  it to refuse an unknown version would be a second machine-wide refusal lever
  pulled by data.
- **`trustedSigners` is the one allow-widening field a foreign rules file still
  controls.** Deliberately not floored — flooring an allowlist has the wrong
  polarity — and inert today, because `IsTrustedSigner` has no production call
  site while §S19(b) is deferred. It becomes live the moment the signer half is
  wired, which makes gating it a prerequisite of that work.

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
- **CryEngine and Source engine rules** (`rulesVersion 2026.08.2`), landed
  deliberately *after* the fixture-coverage gate so that adding them had to
  exercise it. It fired: both rules were rejected until their fixtures existed —
  *"engines id 'cryengine' has no fixture under tests/fixtures/rules"* — which is
  the cheapest available proof that the gate is real rather than decorative.
  - Source's is the first `all` group in the corpus (`gameinfo.txt` **and**
    `bin/engine.dll`). Worth having for that alone: an evaluator that treated
    `all` like `any` would still pass every other engine fixture.
  - **Two rows in `05_DETECTION`'s table are now marked inexpressible in
    schemaVersion 2** rather than half-implemented, because a rule that exists
    and never fires reads as coverage. RPG Maker MV/MZ needs a nested signal
    group, which `maxProperties: 1` forbids, and a version from a sibling `.js`
    that `strings_regex` cannot be aimed at. RPG Maker XP/VX/VXAce's signals are
    expressible but its version is "which signal matched", which no extractor
    produces; splitting it into three rules is a product decision, not a
    mechanical fill-in.
- **The static-hint rule evaluator and its fixture corpus** — the inference half
  of `15_ROADMAP` item 3. `RuleEvaluator` (Domain, no package references) over a
  `GameFileSnapshot` the probe collects in one pass; ports and a
  `StaticGameDetector` in Application; the rules reader and bounded probe in
  Infrastructure.
  - **Every signal is three-valued.** `Unknown` is what a signal returns when the
    probe could not establish that class of fact — a PE that would not read, a
    strings pass that did not finish, a bound that stopped the walk. A group is
    `Unknown` unless the signals it *did* read already decide it, and **an engine
    rule that evaluates `Unknown` stops the ordered walk** rather than falling
    through, because otherwise a later rule gets reported as the first match when
    it was not.
  - **`pe_file_version` reads the sibling its `from` names, not the executable.**
    Unity's rule reads `UnityPlayer.dll`; answering from the game exe would have
    reported a version that is *wrong* rather than merely missing.
  - **The purity claim is dropped rather than quietly kept.** The evaluator does
    no I/O, but the snapshot is rules-dependent — the strings pass must know its
    needles before it reads — so it is not a pure function of a directory.
    `05_DETECTION` says so.
  - **`tests/fixtures/rules/**` with two canaries.** `no_engine` catches an
    evaluator that matches everything, which every positive fixture would still
    pass; `every_engine_marker` carries four engines' markers at once and asserts
    exactly one is reported, catching one that returns a set or the last match.
    Plus a corpus-not-empty fact, because a `[Theory]` yielding zero cases is a
    green suite that tested nothing. Zero-byte markers only, `.gitattributes
    -text`, and the README says plainly which signal types the corpus does *not*
    cover.
  - **Evaluation runs in xUnit, not in the validator.** Re-implementing glob, PE
    and strings matching in PowerShell would be a second evaluator — the same
    defect shape as a second blocklist matcher. `rules-validate.ps1` gains
    fixture *coverage* only: every rule id has a fixture, every fixture has a
    rule.
  - The evaluator's unit tests live in **Domain.Tests**, not with the corpus:
    `coverage-gate.ps1` takes the best rate per assembly and never merges
    reports, so Domain code exercised only from Infrastructure.Tests could not
    reach its floor however thorough the corpus got.
  - **FR-1.3 provenance decided, no migration written.** `games.field_provenance`
    (JSON, absent reads as `user`) with the rule that **detection never
    overwrites a user-supplied field** — stated in no document before, and
    without it the re-run every rules update triggers silently clobbers every
    correction the user has made. The type and the rule are implemented and
    tested; persistence is P2, because §Migrations forbids editing an applied
    script and guessing a shape before its consumers exist is how a wrong guess
    becomes permanent.
- **`fl-baseline-probe` — the measurement baseline P0 item 4 compares against**
  (`15_ROADMAP` item 3, closing §M9's half of it). `15_ROADMAP` asks for "passive
  file/**module** scanning", and the module half is the part that matters:
  *"`nvngx_dlssg.dll` is loaded in this process"* is a claim of the same kind a
  hook makes, where *"a file of that name is on disk"* is not.
  - **Reuses the guard's enumerator** (`SystemSources().EnumerateModules`,
    `LIST_MODULES_ALL`) rather than carrying a second module walk, so the
    baseline and the product see the same list and the same fail-closed
    behaviour. An `INCOMPLETE` scan is printed as such and never read as "no
    capability loaded".
  - Reads the `capabilities` group in **its own translation unit**. The guard's
    parser deliberately reads only `anticheat`, and teaching it a group the hard
    gate does not need would spend the gate's parse budget on inference data.
  - **Proven in both directions** (ctest `fl_baseline_probe`): a clean process
    reports nothing loaded, a planted module *is* detected, and the answer flips
    back after unload rather than latching. The planted module is our own
    `FrameLedger.Guard.dll` under a capability name — no vendor binary is
    shipped, downloaded or executed. Proven red twice.
  - **The finding it produced matters more than the tool.** The baseline can
    answer **none** of item 4's four runtime questions — upscaler identity,
    quality preset, render→output resolution, FG activity. A loaded
    `nvngx_dlss.dll` means the title *can* use DLSS, not that it is on. So
    ADR-7's README claim cannot honestly be a percentage; the defensible form is
    "the baseline cannot answer four of these five questions at all".
    `spike-notes.md` §8 and `15_ROADMAP` item 3 both say so, before any README
    wording exists to be corrected later.
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

### Added
- **The Agent now seeds the rules file the guard reads** (`20_OPEN_QUESTIONS`
  §S20, seed half). `rules/detection-rules.json` ships in the Agent's output and
  is installed to the product location on startup. Before this, nothing in the
  repository ever wrote that file, so on any machine that had not hand-installed
  one the guard answered `RulesUnreadable` for every title — which is what the
  first real injection hit. Measured end to end: remove the file and the guard
  says `RulesUnreadable`; run the Agent and the guard reads its rules and reaches
  check 1.
  - **Provenance, not `rulesVersion`.** The first design replaced the installed
    file when the packaged seed was strictly newer. Measured against this
    repository's own history, every commit that changed the `anticheat` block
    left `rulesVersion` untouched and the one commit that bumped it changed the
    block not at all — the rule would have delivered **none** of the changes it
    existed for. The seeder records a hash of what it installed instead.
  - **`FlGuardCheckRules`** — a new observation-only ABI export, because
    `DetectionRulesFile` never reads the `anticheat` block (§S15). Validating with
    the managed reader would have checked everything except the half the hard gate
    consumes, and could have installed a document the guard then refuses for every
    title while reporting success.
  - **A usable file we did not write is left alone**, which is safe only because
    the floor is now generated from the shipped blocklist: a rules file can add
    and cannot remove. An unusable one is replaced whoever wrote it — there is
    nothing to clobber, and nothing else in the product repairs it.
  - `ReplaceFileW` with a backup, a random temp name in the destination directory
    opened `FileShare.None`, flush-to-disk before the swap, and a reparse-point
    check on the directory chain.
  - **§S20 does not close.** The HTTPS feed does not exist, so FR-7.3's
    independent anti-cheat schedule is unmet; the guard still reads no version, so
    binary and data have no handshake; and this is what makes the Vulkan layer's
    §S2 self-scan reachable for the first time. All four recorded in the entry.

### Fixed
- **The compiled-in blocklist floor shipped too narrow, and it is now generated
  from the rules file** (§S21). As first written the floor carried exactly the
  three families the completeness check required, kept minimal because a larger
  hand-written table would be a second copy of the blocklist that drifts from the
  data. Measured against the shipped seed, that bought **4 of 22 values, 2 of 5
  groups and 0 of 5 name fragments** — so §S21 closed *"a crafted rules file makes
  the guard allow everything"* and left open *"a crafted rules file removes most
  of the blocklist"*: Denuvo, GameGuard, Xigncode3, mhyprot, FACEIT, ESEA,
  PunkBuster, EAC's directories and services, BattlEye's directories, Vanguard's
  service, and the entire fuzzy tier. The write-up read as though it bounded more
  than it did.
  - **Generating it removes the objection that kept it small.** A table derived
    from `rules/detection-rules.json` at build time cannot drift from it, so the
    floor is now the whole shipped blocklist plus the name fragments.
    `trustedSigners` is deliberately excluded — it is an ALLOW-widening list, so
    "data may only add" has the wrong polarity there.
  - **It also delivers §S19(d)'s substance** without the new `ParseResult` cause
    that entry proposed, which its own text said would make `kRulesIncomplete`'s
    signal a lie and drive `layer.cpp` to machine-wide inert passthrough. A file
    with no `heuristic` block can no longer make the tier stop existing.
  - A file family identical to a floor entry is now **deduplicated**, or an
    unmodified seed would spend `kMaxFamilies` twice; `rules-validate.ps1`
    therefore bounds the file at **half** the cap, the worst case of a drifted
    file duplicating none of the floor, and prints that worst case rather than the
    raw count. Completeness is judged on what the file **supplied**, since an
    unmodified seed now stores nothing.
  - Found by the adversarial review of §S20's design: the gap was tolerable only
    while nothing delivered a rules file to any machine, and a seeder turns it
    into a push channel.
- **§S21 prescribed the wrong replace primitive.** A comment on the reader and a
  line in §S21 both told whoever implements §S20 to use temp-file +
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`. Measured against a handle opened
  exactly as the guard opens it, that returns `ERROR_ACCESS_DENIED (5)`;
  `ReplaceFileW` with a backup file named succeeds. Delete sharing is necessary
  and nowhere near sufficient. The unification of share modes is still right — it
  is what lets `ReplaceFileW` proceed — but the named call would have failed on
  exactly the machines where the guard is busy, silently, since the writer's error
  goes nowhere.
- **The guard refused itself, so launch mode could not work** (§S18).
  `FrameLedger.Guard.dll` contains the substring `guard`, one of the heuristic's
  `nameFragments`, and the project ships unsigned so the signer half can never
  rescue it. In launch mode the Agent is the game's parent and therefore inside
  the §S16 scan set, so every launch-mode injection refused.
  - The fuzzy fragment tier is now suppressed for a scan-set process whose
    **directory is FrameLedger's own** — never for the injection target, never
    when the seam cannot answer, and never for the exact blocklist, which still
    refuses in our own processes. Six fail-closed cases, each proven red against
    a specific plausible mistake.
  - **Identity is by directory, compared with
    `GetFileInformationByHandleEx(FileIdInfo)`,** never by string: a prefix
    compare has to defend against 8.3 short names, junctions, `subst`, mapped
    drives, `\\?\` forms and a sibling folder named `FrameLedgerEvil`, and it
    folds case with C-locale rules the `ja`/`vi` builds cannot rely on. Our own
    directory comes from the module **containing the guard code**, never the
    process image — under a test host the process is `dotnet.exe`.
  - **The Agent is now the sole host of the guard DLL.** It used to be a `None`
    item in `Infrastructure.csproj`, which MSBuild flows to every referencing
    project, so the WPF UI shipped it as a side effect of wanting SQLite. That is
    why process identity could not be used. The copy moved to
    `FrameLedger.Guard.targets`, imported by the Agent and by the tests that
    P/Invoke it.
  - **The justification recorded in `19_SAFETY` was false and is replaced.** It
    said our own module set "can produce false refusals and never a true one";
    measured across 290 live processes, three carried a fragment-matching module
    and none needed write access to anything of ours. The exception rests on
    trust, not on information — an attacker who can write to our install
    directory can already replace the guard — and the unsigned-shipping residual
    is now stated.
  - Unblocking §S18 removes a blocker and delivers no capability: Vulkan Tier 1
    still needs `vkQueuePresentKHR` (P1, not started), and launch-mode injection
    still needs §S1 and §S13(c). The roadmap says so rather than reading the
    removed blocker as progress.
- **The hard gate's data source was caller-nameable, and its completeness check
  never read the values that block** (`20_OPEN_QUESTIONS` §S21). The rules path
  was built from `_dupenv_s("LOCALAPPDATA")` — an inherited variable, so whoever
  launched the process chose the file — and `IsCompleteEnoughToGate` verified
  that three family *names* existed in the right *groups* without ever reading
  their `values`. A twelve-line rules file naming Easy Anti-Cheat, BattlEye and
  Riot Vanguard with junk values therefore parsed as valid and the guard returned
  **`Allow` on a machine running Vanguard**: the override CLAUDE.md rule 2 says
  does not exist, with no admin and nothing left on disk. `05_DETECTION` asserted
  the source "cannot be redirected", which was true of the pipe (§S3) and false
  of the environment.
  - **Fixed by a floor, not by the path.** `FloorFamilies` carries the three
    required families inside the binary and `ParseRules` seeds them before
    reading a byte; nothing merges, rewrites or removes them. §S8's mechanism
    applied to data — a family data cannot remove cannot be bypassed. The path
    also moved to `SHGetKnownFolderPath`, recorded as a **narrowing rather than a
    guarantee**: it removes the per-launch vector, not every redirection.
  - **The completeness check stayed able to fail.** It now runs over the file's
    families only; over the merged set it would have been satisfied by the floor
    by construction — retiring a real refusal while fixing a different bug.
  - **A second total failure in the same six lines: the path was ANSI.** Measured
    on system ACP 1252, `C:\Users\田中\...` becomes `C:\Users\??\...` and
    `Nguyễn` becomes `Nguy?n`, so the guard refused **every title for that user,
    permanently**, naming no cause. Wide throughout now, including the Vulkan
    layer's enable-list, which had the same defect and would have made Vulkan
    Tier 1 silently never work. The trigger is the *system* code page, not the
    user's language, and an ASCII profile can never expose it.
  - **Four resolvers of "the ONE location" became one.** The guard, the layer,
    `fl-probe-vklayer` and `DetectionRulesFile` each resolved it independently —
    the last while claiming in its own comment to reach the same directory.
    `FlGuardRulesFilePath` exports the guard's answer for observation only, and
    `RulesPathAgreementTests` asserts the managed side matches. Sharing modes
    unified so §S20's atomic replace cannot be blocked by a reader.
  - Proven red four ways: an emptied floor lets the disarmed rules file allow; a
    floor value absent from the seed fails `fl_rules_budget` by name; a
    completeness check starting at index 0 admits a file with no BattlEye; and a
    renamed `kFloorFamilyCount` makes `rules-validate.ps1` fail rather than skip.
- **A failed injection reported `Allow`.** `FlGuardedInject` returned
  `reason = kAllow` with the truth in a free-text signal, above a comment saying
  *"the caller distinguishes them by reason"* — and there was no reason to
  distinguish by. A caller reading `Allowed()` got `true` for a DLL that was
  never loaded. Found on the first real injection attempt, against a 32-bit
  title.
  - `Allowed()` now means **the DLL is loaded in the target**, which is the only
    reading a caller can act on. Two new reasons say whose fault it was, because
    the responses differ: **`InjectionFailed`** (the gate passed, the injection
    did not take — may be transient) and **`TargetIsWow64`** (permanent and
    expected; the Overlay is x64-only, so the answer is Tier 2, not "something
    went wrong").
  - The injection primitive returns a `Reason` instead of a `bool`.
  - `dllPath == nullptr` also stopped reporting `RulesUnreadable`, which told
    whoever had to fix it that the rules file was unreadable about a caller that
    passed no path.
  - Tested against a guaranteed 32-bit target (`SysWOW64\cmd.exe`), which fills
    `14_TESTING`'s manual-matrix row for a 32-bit title on CI as well as here.
- **The guard refused every process on the machine.** Found on the first attempt
  at P0 item 2, against a real title: check 2b reported a service as present when
  it was merely *installed*. `EasyAntiCheat_EOS` is installed machine-wide by any
  EOS game and sits **Stopped/Manual** until its own title runs — so one such
  game, anywhere, made the guard refuse `explorer.exe` and `steam.exe` as
  readily as it refused a Unity indie game with no anti-cheat anywhere in its
  install tree.
  - `19_SAFETY`'s own words for this shape: *"a gate that refuses everything is
    not a strict gate but a broken one, and it is how a user ends up looking for
    the override CLAUDE.md rule 2 says does not exist."* The behaviour was
    deliberate and documented in a comment; what nobody had measured was what it
    does on a machine that has ever installed one EOS title.
  - **Present now means running.** `SERVICE_STOPPED` is the only state treated as
    absent; start-pending, paused and stop-pending all mean code is or was live.
    The machine-wide guarantee does not rest on this check — a **loaded driver**
    is check 2 and still refuses for all titles, modules inside the target are
    check 1, and both fire when an EAC game actually runs.
  - Tested against the **real** service control manager, because the fakes cannot
    catch this one: what changed is what `present` *means*, and a fake has always
    just echoed a list. The test enumerates live services, picks one running and
    one stopped, and asserts the implementation distinguishes them — plus a third
    case for absent. Proven red by restoring `*present = true`.
- **The anti-cheat pre-scan was looking in the wrong directory** — a hole in a
  hard gate, found by running the detector against three real installs
  (`spike-notes` §8). Both the probe and `ImageDirectoryImpl` derived "the game
  directory" by stripping the filename from the executable's path. Unreal puts
  the exe at `<root>\<Project>\Binaries\Win64\`; measured on Lies of P that
  folder holds **seven files**, none of which could ever have been an anti-cheat
  SDK, because `EasyAntiCheat/` sits at the install root three levels up. **For
  exactly the layout most likely to carry EAC, check 4 scanned a directory that
  could not contain what it was looking for and returned clean.**
  - `ResolveInstallRoot` walks up to a hardcoded platform boundary
    (`steamapps\common\<X>`, `GOG Galaxy\Games\<X>`, `Epic Games\<X>`) —
    hardcoded for the same reason `IsPlatformLauncher` is, since a data-driven
    boundary lets a rules update move where the hard gate looks.
  - **An unrecognised layout keeps the executable's own directory.** Walking up
    blindly would reach a folder of unrelated games, and refusing a title
    because a *sibling* ships anti-cheat is a false refusal with no appeal. Alan
    Wake 2 installed at `D:\another\epic\AlanWake2` is exactly that case.
  - **The entry point no longer changes the answer.** Unreal titles ship two
    executables — a shim at the install root and the shipping binary nested
    under `<Project>\Binaries\Win64\` — and a user adds one, while the guard is
    handed whichever process it is handed. Measured on Lies of P's `LOP.exe` vs
    `LOP-Win64-Shipping.exe`, the two used to disagree (undetermined vs `fsr`
    only); both now resolve to the same root and produce identical results,
    asserted in Catch2 and xUnit rather than left as an observation.
- **A depth cap that made static detection useless on every real game.** The
  probe capped its walk at depth 4; measured real depths are **6, 5 and 9**. And
  an unfinished walk marked all three file signal types uncollected, so *every*
  file-based signal became `Unknown`, the engine walk stopped at its first rule,
  and nothing was ever identified. Failing safe is right; failing safe on every
  input is just not working.
  - Fixed by separating the two questions: a file the walk **listed** is there
    however early it stopped, so a hit stays `Match` and only a **miss** becomes
    `Unknown` when the listing did not finish. `GameFileSnapshot` carries
    `FileListingComplete` instead. Caps raised to depth 16 / 200,000 entries,
    with the entry count as the real bound.
  - After both fixes, all three titles detect correctly: Unity 2022.3.32 +
    Steam; Unreal + Steam + DLSS + FSR; Epic + DLSS + DLSS-G + Ray
    Reconstruction + Streamline.
- **Two documents claimed CI evaluated rules against fixture trees; it never did,
  and the trees did not exist.** `05_DETECTION` §Static hints and `13_CI_CD`
  §rules-publish both said `tools/rules-validate` "runs rules against fixture
  trees in CI" — a gate described in normative documentation and implemented
  nowhere. The validator now does fixture *coverage* and says so; the evaluation
  is `RuleFixtureCorpusTests`, which drives the real evaluator through the real
  probe under `build.ps1 check`.
  - `05_DETECTION`'s signal-type list was also missing `path_contains` and
    `strings_regex`, both of which are in the shipped data *and* the schema — the
    schema's own `$comment` said so and nobody had made the edit.
- **Pre-injection check 4 was declared and never implemented.**
  `Reason::kAntiCheatDirectory` and `kAntiCheatFile` were declared in
  `fl_guard.h`, named in `ReasonName`, and mirrored into the managed enum —
  while **nothing produced either**. `EvaluateImpl` ran drivers → services →
  modules and stopped. `19_SAFETY` and `05_DETECTION` both described the check as
  live and `14_TESTING` specified a test for it: three artifacts agreeing on a
  behaviour no code had. `15_ROADMAP` called item 0 **✅ DONE** on that basis.
  - **It now runs INSIDE the chokepoint**, as the last of four checks in
    `EvaluateImpl`, against a directory derived from the target's own pid via
    `QueryFullProcessImageNameW` — never a caller-supplied path. Building it as
    a UI advisory was the first plan and was wrong: with no persistence layer
    its verdict would have had nowhere to go, so `hook_blocked_reason` could not
    have carried it and the check would have gated nothing.
  - **No new matching.** Entry names go through the same `MatchName` as every
    other check — directories against the `directories` group, files against
    `files`. A test removes a family from the rules and asserts the hit
    disappears, which is what proves one matcher rather than two that agree.
  - **Every uncertainty is `kPreScanFailed`**, never a hit and never a pass:
    absent or unlistable directory, either bound exceeded, an unconvertible
    name, or a reparse point — which is never followed, because a junction can
    hide an `EasyAntiCheat/` beneath it.
  - `FlStaticPreScan` exposes it for FR-2.2, **advisory only**. It reports
    through the existing `FlGuardResult` so there is one reason table and one
    mirror surface. `IAntiCheatGuard` gained a third method and
    `NoSecondMatcherTests`' count was raised to 3 **as a reviewed act** — a
    separate port would have kept that number at 2 while the new surface grew
    where the test never looks.
  - `15_ROADMAP` item 0 is corrected from ✅ to ◐. **Check 3 is still unwired**
    — its matchers have no call site, so it is not merely unpopulated, and the
    status will not read ✅ again on the strength of "most of it works".
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
- **The open-questions ledger overstated its own openness, in four places.**
  Found while planning the next phase against it — a ledger that is wrong about
  what is open makes every phase planned from it over-scope, so these are
  recorded rather than quietly fixed.
  - **§S15 is closed.** Its header said "Three of four are done; item 1 is the
    one still open" while all four bullets beneath it said DONE, item 1 included.
    A status line whose verdict was decided before anyone read the list under it
    — this file's own recurring defect, in the file that exists to record it.
  - **§S19's heading said "four defects" over a body running (a) to (e).**
  - **§S19(e) is closed, and was the stale artifact itself.** It claimed
    `19_SAFETY` lists four name fragments; the doc has listed all five since the
    same commit that recorded §S19. What survives is a missing gate, not a doc
    error: the doc/data cross-check parses only the §Blocklist seed table, so the
    fragment sentence is invisible to it and the drift can recur.
  - **Two citations pointed at text that is not there.** §S19(c) and §S19(e) both
    cited `19_SAFETY:264` for the fragment sentence; line 264 is a blocklist
    table row. Line numbers were removed from those entries rather than
    corrected, since they are what went stale.
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
