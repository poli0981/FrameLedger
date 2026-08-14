# Changelog

All notable changes to FrameLedger are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html) — `MAJOR`
bumps for a database schema or IPC protocol break (`docs/11_UPDATER.md`).

`release.yml` **will** read the section for the tag being released and use it as the
GitHub release body, so a missing section will mean an empty release note.

> **It does not exist yet** — `.github/workflows/` holds `ci.yml`, `codeql.yml` and
> `rules-publish.yml` only, and `docs/13_CI_CD.md` records that. Written in the future
> tense here from 2026-08-06 rather than describing a workflow nobody has. Note also
> that this file has no `## [x.y.z]` heading yet, so the first release needs one
> created before a tag has anything to find.

## [Unreleased]

### Added

<<<<<<< HEAD
- **The LOCAL tags too — `slEvaluateFeature`'s `inputs` walk, hardened against input a title
  should never send.** `sl_core_api.h:258` is explicit that buffer tags passed to
  `slEvaluateFeature` are *"local"* and *"do NOT interact with same tags sent in the global
  scope using slSetTag API"*. The two are alternative integration styles, not layers, so a
  locally-tagging title yields nothing from the `slSetTag` hook and vice versa. Both are now
  read; the local one wins when present, because it is scoped to the evaluation rather than to
  the viewport and cannot be older than the frame being measured.
  - **In a header, `fl_sl_inputs.h`, precisely so it can be tested without a game.** Every
    branch dereferences caller-supplied pointers, and a fault lands in `FL_HOOK_GUARD` and
    burns one of the three that self-disable the Overlay — so a malformed input that faults is
    a **bug**, not degradation. `ctest fl_sl_inputs` drives 12 shapes in microseconds: null
    array, zero count, null element mid-array, wrong buffer type, wrong struct GUID,
    `structVersion` below `kStructVersion1`, whole-resource extent, a tag reached through
    `next`, a **self-referential** `next`, a **two-node cycle**, and a `numInputs` of
    `0xFFFFFFFF`.
  - **The cycle handling is the depth cap, and that is deliberate**: a visited-set would
    allocate, which a hook path may not. A cycle is worse than a fault — no exception, no
    self-disable, just a frozen game with our DLL in it.
  - **What the input cap does not buy, stated rather than implied.** It turns
    `numInputs = 4 billion` into 32 reads. It does **not** protect against a count of 5 with a
    2-element array: nothing in the ABI carries the allocation's length, so that direction is
    unprotectable from here. Same for a struct whose GUID matches but whose allocation is
    short. Both are the vendor's contract to keep, and both are written down.

=======
>>>>>>> origin/main
- **`FL_MEASURED_UPSCALER_PARAMS` gets its first producer: render resolution, from the global
  resource tags** — `docs/HANDOFF.md` item 2b, and one of the five values P0 exit criterion 1
  names. A second inventory row detours `sl.interposer.dll!slSetTag`, reads the
  `kBufferTypeScalingInputColor` extent, and publishes `renderW/H`.
  - **`slSetTag` was scheduled for deferral and is included instead**, on measured grounds:
    `sl_core_api.h` documents the local tags in `slEvaluateFeature`'s `inputs` as merely
    *allowed* — *"they do NOT interact with same tags sent in the global scope"* — and only
    **four of the ten** Streamline titles installed here route DLSS super-resolution through
    Streamline at all. An inputs-only producer could have shipped with a hit rate of zero.
  - **The installer had to be restructured first, and it was a latent mis-bind.** The
    expansion over `FL_HOOK_INVENTORY` ignored the `family` column, stopped at the first row
    that resolved, and hooked it with `Hook_SlEvaluateFeature`. Correct with one row; with two
    it would have detoured `slSetTag` with a body that reads argument 1 as a feature id. Each
    row now carries its own detour, family bit and latch, and a row whose family has no detour
    installs **nothing** rather than borrowing a neighbour's. The install-after-stop guard is
    factored out of the single installer for the same reason.
  - **Two conditions to publish, and the second is the one that is easy to drop:** the params
    hook live, **and** an evaluation seen *this frame*. A tag is viewport state that outlives a
    frame, so publishing on the tag alone would report a render resolution for every frame
    after a title stopped upscaling — stale state dressed as a measurement.
  - `upscalerQuality` is **`0xFF`, never 0** — 0 is NGX MaxPerf, a real preset, so it would
    publish "DLSS Performance" as a measurement. `upscalerSharpness` is `0xFF` **permanently**:
    `DLSSOptions::sharpness` is deprecated as unsupported and `optimalSharpness` is
    Streamline's recommendation, not what the title applied.
  - **`claimedParams == 0` is inverted, not deleted** — that was the honest assertion while
    `renderW/H` had no source, and it is the line a reviewer should see change. It now asserts
    the **exact** tagged extent, the honesty invariant on every claiming record, and the
    reverse direction (a value set while the bit is clear).
  - Two fixture bugs the test found rather than reasoning: tagging **once** at startup landed
    before injection and left the bit set on **0 of 43** records — and `eValidUntilPresent`
    means a real title re-tags every frame anyway, so tagging once was simply wrong about the
    vendor contract. And a floor of `> size - 3` hid a real 6-record window where identity was
    live and params was not; the drain now waits for **both** families and asserts equality.
  - Proved red by canary: hardcoding a plausible 1920×1080 trips `wrongExtent` on all 40
    records, and it compiles.
  - No `#pragma warning` for C4996 on the deprecated `slSetTag`, and that is measured:
    `/std:c++20` **without** `/Zc:__cplusplus` makes MSVC report `199711L`, so the
    `#if __cplusplus >= 201402L` guard on the attribute never opens. A pragma would suppress a
    warning that is not emitted. The condition that would change it is recorded at the
    declaration.

### Fixed

- **The Overlay would hook a Streamline module of the wrong generation, and read its
  arguments through the wrong signature.** The inventory scopes `slEvaluateFeature` to
  `sl.interposer.dll` — correct, and not enough. **The Witcher 3 ships that module at
  1.5.6**, a different API generation: it exports `slInit`, `slEvaluateFeature`, `slSetTag`
  and `slShutdown` so every name check passes, plus `slGetHooks`, `slIsFeatureEnabled` and
  `slSetFeatureConstants` which Streamline 2 does not have, and **none** of
  `slSetD3DDevice` / `slIsFeatureLoaded` / `slGetNewFrameToken`.
  - **The name survived the version bump and the signature did not**, which is why no gate
    saw it. `docs/vendor-exports.json` records one copy per module *name*, so
    `hookinventory-check` Pass A resolves against one machine's 2.7.4 and says nothing about
    a 1.5.6 in a game. A detour typed with SL2's `PFun_slEvaluateFeature`, called with SL1's
    argument list, reads argument 1 as a feature id when it is not one — a **wrong upscaler
    name**, not a crash, which `17_HOOK_ENGINE` calls the highest false-confidence risk in
    the spike.
  - `ResolveScoped` now refuses a `sl.interposer.dll` that does not export the three
    SL2-only entry points. No hook is installed, `FL_HOOK_UPSCALER_IDENTITY` is never
    published, and the record says `FL_UPSCALER_NOT_REPORTED` — true, rather than a guess.
  - **Scoped to the interposer by name, and the first version got that wrong.** Those three
    are *interposer* exports; a real `sl.common.dll` is a plugin and exports none of them, so
    an `sl.*` prefix test refused modules whose generation it had no business judging. The
    existing decoy fixture caught it.
  - New fixture `stub_sl_interposer_v1.cpp` — right module name, right symbol name, SL1 ABI —
    and `ctest fl_sl_abi_guard`. It is the complement of the decoy: scoping catches a wrong
    module *name*, and cannot see a wrong *signature*. Its own ctest and its own process,
    because both fixtures are called `sl.interposer.dll` and `GetModuleHandleExW` resolves by
    name, so sharing one would have the two silently test each other.
  - Proved red by canary: neutering the check leaves exactly one assertion failing, and it
    compiles, so it is a canary rather than a build failure wearing one.
  - The SL2 stub gains the three markers, so it honestly looks like the generation it stands
    in for. Found because the guard correctly refused it otherwise.

### Added

- **`FlWriterState.rtTier` gets a producer, and the vendor enum it copies had a collision in
  it** — `docs/HANDOFF.md` queue item 4, the one conjunct that needs no hook. `ResolveApi`
  already obtained an `ID3D12Device*` on the first present of a D3D12 swapchain and released
  it three lines later without asking it anything; it now asks
  `CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5)` first. No hook, no MinHook, no vtable —
  a capability query on a device DXGI handed us for a swapchain we were called on.
  - **`D3D12_RAYTRACING_TIER_NOT_SUPPORTED` is 0, and `rtTier`'s 0 already meant NOT
    QUERIED.** Storing the enum verbatim — the obvious implementation, and the one
    `fl_shm.h`'s own field comment described — would have published "nobody looked" about
    every non-RT device: the affirmative-negative collision layout v3 exists to prevent,
    reached by copying a vendor enum rather than by a guess. New `FlRtTier` carries three
    states: `NOT_QUERIED = 0`, `UNSUPPORTED = 1`, and the D3D12 value verbatim otherwise.
  - **Measured against the Windows SDK header rather than remembered:** `NOT_SUPPORTED = 0`,
    `TIER_1_0 = 10`, `TIER_1_1 = 11`, **`TIER_1_2 = 12`** — the enum is already "tier ×10", so
    nothing multiplies it and nothing names the individual tiers. A tier newer than the SDK
    this was built against arrives intact instead of being clamped to what the build knew.
  - **Both directions are asserted, in `ctest fl_guard`.** The D3D12 case asserts the field
    holds a legal `FlRtTier` value; the D3D11 case asserts it is exactly `NOT_QUERIED`,
    because a writer that stored `UNSUPPORTED` unconditionally would pass the first on its
    own. What the D3D12 case does **not** assert is *which* tier: the fixture's device is
    WARP, and whether WARP supports DXR is the open question `HANDOFF` item 4 says to check
    rather than assume — so the value is `CAPTURE`d and the test records the answer instead
    of depending on it.
  - **It does not make RT reachable yet, and the consumer comment now says so precisely.**
    `MeasuredFacts.RayTracingOf`'s two conjuncts both have producers now, but
    `FL_MEASURED_RT` still has none, so RT is `N/A` on every session. The gap moved; it did
    not close.
- **`hookinventory-check` grows a third pass, over the one failure the other two cannot
  see** — and the document that already claimed this pass existed is corrected in the same
  commit. Passes A and B are source checks: they see what the Overlay *resolves*. Neither
  sees what it *links*. Taking the address of an `SL_API` declaration in evaluated code makes
  `sl.interposer.dll` a **load-time dependency** of `FrameLedger.Overlay.dll`, which then
  fails to load in every game that ships no Streamline — in the loader, before `DllMain`,
  with no message anywhere. **Pass C reads the binary's own dependency list** and fails on
  `^(sl\.|_?nvngx|libxess|ffx_|amd_fidelityfx)`.
  - **`src/native/third_party/streamline/README.md` had asserted this gate since
    2026-08-09.** It did not exist: the script's only mention of `dumpbin` was a comment
    about a different tool. Found by an audit that went looking for the code instead of
    trusting the sentence — the shape this project keeps hitting, and worse here because the
    failure Pass C catches has no symptom to notice.
  - **It refuses rather than passes whenever it cannot look.** A zero-length import list is a
    failure, not a clean result — every way that parse can break produces the same empty list
    as a binary with no vendor imports. The list must also contain `kernel32.dll` before any
    verdict is formed, the same discrimination rule the oracle probe already follows.
  - **It runs only under `-RequireBinaries`.** The first version read whatever binary was in
    the build tree, so `check -SkipNative` printed a skip line *and* ran the pass anyway,
    against an artefact the run did not produce. Reporting on the wrong binary is worse than
    saying nothing.
  - Proven on real PEs as well as fixtures: out of `AlanWake2.exe`'s 47 imports and
    `Cyberpunk2077.exe`'s 36, it names exactly `sl.interposer.dll` and — for Cyberpunk —
    `libxess.dll`, `libxess_fg.dll`, `ffx_fsr3_x64.dll`, `ffx_backend_dx12_x64.dll`, and
    nothing else. Self-test is 17 cases, both directions, including that the match is
    anchored so an innocent name merely *containing* a vendor prefix passes.
  - Pass B's stray-literal sweep gains `xefg[A-Z]`, which `xess[A-Z]` does not cover:
    `libxess_fg.dll`'s 31 measured exports include 28 `xefgSwapChain*` names and no `xess*`
    name at all. Widened before the FG hooks land rather than after.
- **§H5 case 3 gets an answer, and a second finding cost a crash to get** —
  `fl-probe-interposer` reported INCONCLUSIVE because it never called `slInit`, blaming a
  licence question over `sl::Preferences`. #64 vendored Streamline under MIT and removed that
  blocker; the probe now runs the sequence a real title runs — `slInit` →
  `D3D12CreateDevice` **through the interposer** → `slSetD3DDevice` — with every entry point
  resolved by `GetProcAddress`, never linked.
  - **MEASURED: the swapchain class is not ours.** With `slInit` returning `eOk`, the
    interposer hands back a swapchain whose vtable sits inside `sl.interposer.dll` while
    `dxgi.dll`'s own route yields `dxgi.dll`'s. Reproduced on Alan Wake 2 (SL 2.7.0) and
    Cyberpunk 2077 (SL 2.7.1). **It does not follow that we miss the present** — §H5's
    `--probe-proxy` result stands, a forwarding proxy is caught one layer down — and the
    probe says so rather than converting a premise into a verdict.
  - **The Witcher 3 ships Streamline 1.5.6, and it crashed the probe.** A different API
    generation: `slGetHooks`, `slIsFeatureEnabled`, `slSetFeatureConstants`, and **no**
    `slSetD3DDevice` or `slIsFeatureLoaded`. `slInit` exists in both with a different
    `sl::Preferences` layout, so the vendored 2.x struct access-violates. Now version-guarded
    on the SL2-only exports, skipping with a reason.
  - **That reaches the hook inventory.** `docs/vendor-exports.json` records one copy per
    module *name*, so its `sl.interposer.dll` is one machine's 2.7.4 and says nothing about a
    1.5.6 a title ships. Pass A would accept `slEvaluateFeature` against such a title — the
    name exists in both generations — while the **signature** differs. Today's hook reads
    only `feature` and is probably unharmed; **item 2b's `inputs`/`numInputs` walk is not**,
    and needs its own version guard before dereferencing anything.
  - **The engagement read was a race first.** Plugin load is deferred: the first run reported
    both features unloaded while Streamline's own log — flushed after ours — showed six
    plugins verifying. Now polled for the state, bounded by a wall clock.
  - `ctest fl_vtable_identity_control` (Part 1, the control) is unchanged and still runs on
    CI; the probe imports `d3d11.dll` and `KERNEL32.dll` only, so linking Streamline could
    not have broken it.

- **The upscaler identity hook, and a fixture that can prove a wrong symbol name wrong** —
  `docs/HANDOFF.md` queue item 2. `FrameLedger.Overlay` gains **module-scoped symbol
  resolution** (it had none: `GetProcAddress`/`GetModuleHandle` appeared nowhere in the
  target) and one MinHook detour on `sl.interposer.dll!slEvaluateFeature`, installed
  lazily by the existing 1 Hz watchdog so a title that loads Streamline at device
  creation is still caught without a `LoadLibrary` hook — which keeps §S6 separable.
  - **`FlWriterState.hooksInstalledMask` gets its first producer anywhere in the tree.**
    It had none — not even `FL_HOOK_PRESENT`, which the present hook was always entitled
    to — and `MeasuredFacts.RayTracingOf` already said so in its own comment while
    reaching `N/A` on every session because of it.
  - **Never `FL_UPSCALER_NONE`.** A Streamline-only writer cannot see FFX, XeSS or
    NGX-direct, and `NONE` is the only one of the three states `fl_shm.h` allows to be
    aggregated as a negative. An unrecognised feature id reports `UNKNOWN` — "a hook ran
    and could not identify what it saw" — which is a state this codebase could not
    previously produce. A ctest drives a target evaluating `0xF00D` to prove it.
  - **Ray Reconstruction's OBSERVED bit is gated on having seen an SL evaluation *this
    frame*, not on the hook being installed.** Found by an adversarial refuter over the
    design: the consumer returns `Tri.No` when OBSERVED is set on every record and no
    record carries the fact bit, so a writer setting OBSERVED whenever the hook was live
    would publish **"Ray Reconstruction: No" on an NGX-direct DLSS-RR title**, which
    never calls `slEvaluateFeature` at all. The honest-looking choice was the wrong one.
  - **`FL_MEASURED_UPSCALER_PARAMS` is deliberately unset, and the reason is a licence.**
    `17_HOOK_ENGINE` recommended hooking `NVSDK_NGX_Parameter_SetUI` for quality and
    render size; that needs NGX declarations, and the NGX/DLSS SDK is the proprietary
    **NVIDIA RTX SDKs License** — verified upstream, hitting three of
    `18_GPU_VENDOR_APIS` §Checklist step 2's four needles — so step 3 forbids vendoring
    it **and** re-declaring it. The document recommended a path its own rule forbids and
    nothing had noticed, because no code had gone near it. Corrected in place.
- **NVIDIA Streamline MIT headers vendored** at `src/native/third_party/streamline/` —
  the nine-file include closure of `sl.h`, one atomic commit with the licence copy, the
  notices row and both of `license-check.ps1`'s arrays. It buys the vendor's own
  `PFun_slEvaluateFeature`: five parameters, every one integer-class, so **nothing
  travels in XMM** — the exact residual hazard nobody could rule out while the signature
  was a guess, and a guess wrong by one argument corrupts the stack *inside the original
  function*, where `FL_HOOK_GUARD`'s `__try` cannot reach. `sl_nvperf.h` is excluded (the
  same `license.txt` carries a second, proprietary NSight Perf block naming it) and all
  of `external/` is excluded (upstream's `external/ngx-sdk` is the RTX licence, so a
  recursive copy does the forbidden thing by default). `license-check` asserts all three,
  proven red in four directions. This also answers the question `spike-notes` §5 recorded
  as open: §H5 case 3 was *"blocked on a licence decision, not on hardware"*, and the
  decision is taken.
- **`tools/hookinventory-check.ps1`** — every vendor symbol the Overlay resolves by name
  must exist, **in the module it takes it from**, in `docs/vendor-exports.json`. Wired
  into `build.ps1` in both halves. **Prevention: it fixed nothing**, and its own docstring
  says so, because a gate whose write-up implies it caught something cannot be audited
  later. Five red cases proven; the load-bearing one is an **oracle-discrimination
  canary that runs before any verdict**, since every failure mode of such a lookup
  produces the same answer as "absent".

- **The params half is designed and not built**, and the design is in `docs/HANDOFF.md` §2b
  rather than here, because it is a sequencing decision and not a change. Recorded because the
  panel **rejected the route the documents make obvious**: hooking `slGetFeatureFunction` and
  MinHook-patching the returned `slDLSSSetOptions` would need an "indirect" inventory class that
  is an unconditional escape hatch from `hookinventory-check` (its oracle would be "a `PFun_*`
  exists in a vendored header", and `sl_core_api.h:63` declares one for a symbol **zero measured
  modules export**); it would reopen the install-after-stop window; MinHook has no function-length
  oracle for a runtime-returned address, so a short thunk gets patched into its neighbour inside
  vendor code where `FL_HOOK_GUARD` cannot reach; `GetModuleHandleExW` in a detour takes the
  loader lock on the game's thread; and it structurally misses the game's FIRST
  `slDLSSSetOptions` call, which for a benchmark configured before launch is often the only one.
  The chosen route extends the hook the Overlay already owns — a bounded walk of
  `slEvaluateFeature`'s `inputs` chain, which it already receives and ignores.

### Fixed

- **A failed `REQUIRE` terminated the whole native test binary, hiding every test after
  it.** Catch2 compiled itself with `CATCH_CONFIG_DISABLE_EXCEPTIONS`: the top-level
  CMakeLists strips `/EHsc` from `CMAKE_CXX_FLAGS` deliberately, and `tests/CMakeLists.txt`
  re-added it **only to the test binaries**, never to the Catch2 library that decides the
  mode — a comment whose premise was right and whose effect was not. On a box where WARP's
  D3D12 path is broken this silently removed the end-to-end injection cases from `ctest`,
  **including the honesty assertion §S29(a) names as the merge gate's coverage**, which
  passes when run alone. `ctest` reported "1 test failed" where the truth was "and 2 never
  ran". Recovered **70 test cases and 1051 assertions**.
- **`MeasuredFacts.IsHonest` compared `measuredMask` against a hardcoded constant**, which
  was correct for a present-only writer and became wrong the moment a feature hook landed:
  an honest record claiming `FL_MEASURED_UPSCALER` counted as a violation. Widening the
  constant would have made the check a statement about one build rather than about
  honesty, so entitlement is now **derived from `hooksInstalledMask`** — a writer may
  claim a measurement only where it installed a hook capable of taking it — plus the
  reverse direction that was missing, a *value* set while its mask bit is clear.
- **`docs/12_BUILD.md` said the NVAPI SDK "is not vendored yet"** and that
  `src/native/third_party/` held only `CMakeLists.txt` and `vulkan-headers`. Both false
  since #55. That sentence was itself a correction of the opposite error, so this file has
  now been wrong in both directions about one fact — `legal/` is gated bidirectionally and
  caught its own version, `docs/` is not.
- **`docs/HANDOFF.md` item 2 named `NVSDK_NGX_EvaluateFeature`, which no measured module
  exports**, and said four modules where `NVSDK_NGX_D3D12_EvaluateFeature` has **seven**.
  The argument survived both errors; the data was wrong in the bullet whose subject is
  that a wrong symbol name degrades silently.

- **The consent store, and the first production driver of the guard loop** — `docs/HANDOFF.md`
  queue item 1. `HookedCaptureGate`'s three inputs (`hook_enabled`, `hook_consent_at`,
  `hook_blocked_reason`) have a real source for the first time, and
  `FlControlBlock.guardTicks` is now advanced by a **non-test binary**. Both ends of that
  field existed and were tested since #46/#50; only the loop was missing, and a missing loop
  reads as a missing subsystem.
  - **`FrameLedger.CaptureHost` is a separate project that nothing publishes**, and
    `tools/package-closure-check.ps1` is what keeps that true rather than remembered. §S27 was
    closed on the strength of there being no injecting entry point on any shipped binary; the
    host **is** one, so what keeps §S27 closed is its absence from the package. The gate walks
    the transitive `ProjectReference` closure of the two roots `12_BUILD` publishes and names
    the edge that reached anything else. **Proven:** a reference from `FrameLedger.Agent` leaves
    the build green and turns the gate red with `FrameLedger.Agent → FrameLedger.CaptureHost`.
    Both halves are wired — the self-test (5 cases, 4 RED) **and** the live pass, because a gate
    wired self-test-only never reads the repository, which is the defect it exists to prevent.
  - **`HookRequest` can no longer be synthesised, and that is what actually closed the hole.**
    It was a `record` with `required`/`init` members, so
    `new HookRequest { HookEnabled = true, ConsentedAt = DateTimeOffset.UtcNow }` passed every
    check in the gate and reached `GuardedInjectAsync` — **verbatim the expression §S27 named and
    rejected**. A store, a record and a provenance flag do not close that; they add an honest path
    beside it. It is now get-only with a private constructor and one factory, so the dishonest
    path is a compile error rather than a discouraged idiom. Found by a safety refuter over the
    design, before any of it was built.
  - **`GameConsentRecord.Stored` is `internal`**, because `FrameLedger.Domain` is inside both
    publish closures: a public minting factory there would be a blessed, *shipped* API for
    producing consent nobody gave, and the closure gate cannot see it — it walks project
    references, and Domain legitimately belongs to both. The `InternalsVisibleTo` list is the
    reviewable artifact; a test asserts the public factory does not exist.
  - **An acknowledgement cannot clear a block.** `RecordOperatorAcknowledgementAsync` takes an
    `OperatorAcknowledgement`, which carries neither `BlockedReason` nor `PreScanUnverified`, so
    the store's merge is their only source. Taking a whole record — the first shape — meant
    `consent grant` on a title the pre-scan had blocked would write `BlockedReason = null` and
    the gate's `PreviouslyBlocked` branch would stop firing: the "I understand, continue anyway"
    button CLAUDE.md rule 2 forbids, arrived at by omission.
  - **The pre-scan's third state gets its own field.** `05_DETECTION` makes "could not verify"
    neither a hit nor a pass, while `hook_blocked_reason` is two-state by definition. It is
    refused in the loop, never mapped to `PreviouslyBlocked` and never cleared — routing it
    through the gate would force one of the two collapses that document forbids.
  - **A consent record carries a disclosure provenance and a wording version**, neither of which
    `06_DATA_MODEL` has. A bare timestamp cannot say whether anything was disclosed, and
    `ConsentProvenance.NotRecorded = 0` means a record nobody filled in refuses. **FR-2.1's value
    is deliberately not declared**: no `.resx` exists anywhere in this tree, `09_I18N` reviews
    `Safety_*` keys as legal text, and a declared-but-producerless value is the "reads as
    sanctioned" shape §S29(c) was raised for. The version is carried from the *first* record
    because it cannot be added later — retrofitting one means treating unversioned consent as
    either current or stale, and both are wrong about some record.
  - **The command surface has no `--pid`, `--payload`, `--force`, `--yes` or `--diag`**, and a
    test pins it. §S27's gap was a user-named pid on a binary with no consent record; resolving
    the target from the same normalised path the record is keyed on makes "consent for A,
    injection at B" inexpressible rather than discouraged. Two processes of one image refuse as
    ambiguous rather than picking one.
  - **`consent grant` refuses redirected stdin**, so no script can acknowledge on a human's
    behalf, and the disclosure states in its first line that it is *not* FR-2.1 consent.

- **Session end has a signal (§S29(e) closed).** A held process handle, opened before the
  injection and never re-resolved — pids recycle and the ring is named after one.
  - **It was first closed on a handle that was never held, and the correction is the
    interesting part.** The first version used `Process.GetProcessById` and read
    `HasExited`. **Measured** on .NET 10.0.10 by a probe over the live object:
    `_haveProcessHandle == false` and `_processHandle == null` both after construction and
    after reading `HasExited` — `GetProcessById` opens nothing and `HasExited` opens a
    transient handle and releases it in its own `finally`. Three source comments, a ledger
    entry and a changelog line all asserted the pid was pinned; nothing checked it, and it
    was not. `Infrastructure.Io.HeldProcessHandle` now opens `SYNCHRONIZE |
    PROCESS_QUERY_LIMITED_INFORMATION` and keeps the `SafeProcessHandle`, a pid that
    cannot be opened is a refusal (`TargetCannotBePinned`) rather than a session, and the
    test asserts the *property* — it answers about a fully exited process at a pid
    `Process.GetProcessById` will not resolve, which nothing holding a handle-less object
    can do. The ninth entry for "measure Windows APIs, don't trust them", and the first
    where the API misled by doing **less** than its name implies.
  `SessionEndClassifier` takes no elapsed-time parameter, so **a frozen `writeIndex` can never
  end a session**, which is the whole of the defect: `ShmRingReader` holds the section open, so
  an exited game leaves `status` `READY` and `writeIndex` frozen, byte-for-byte a loading screen.
  §S26 made it strictly worse by dropping `DXGI_PRESENT_TEST`, removing the accidental heartbeat
  an occluded title used to emit. It also separates the two stops the mapping cannot: `StopObserving`
  stores `FL_STATUS_UNHOOKED` for the safety stop **and** for supervision loss, so only the side
  that caused one knows which — and `legal/DISCLAIMER.md` §2 discloses them differently.

- **A throwaway consumer**: `measuredMask` → rule 7 tri-state, segmentation, and the one number a
  present-only writer may publish. In the unshipped host, deliberately not in
  `FrameLedger.Domain.Metrics.*` — P2 owns the real calculators and `coverage-gate` carries a
  separate 95% floor for that namespace.
  - **Segmentation is stream-first, settings-second**, and reversing it manufactures a segment per
    present. Two axes exist and neither document mentions the other: `03_METRICS` §Upscaling
    splits on a settings change, `fl_shm.h` says the Agent "segments by [`swapchainId`] and reports
    the dominant stream". One vtable patch sees every swapchain in the process, so a title with a
    UI or video swapchain interleaves two streams in one ring.
  - **`frameIndex` is process-global, so it cannot detect a gap within a stream.** The first design
    excluded any interval whose index did not advance by one — which would have excluded *every*
    interval in any multi-swapchain title and reported no duration at all, including for Displayed
    FPS. `dllmain.cpp` assigns `g_frameIndex++` four lines before `swapchainId`; the overflow
    harness interleaves 17, so the dominant stream's indices step by ~17. Found by a feasibility
    refuter over the design.
  - Everything unmeasurable is `null` or `N/A` with **no fallback**: `fg_factor` is never `1.0`,
    `fgMode` is never `"none"`, the upscale ratio is not computed at all (`renderW/H` are 0), and
    the retired `FlUpscaler` value 2 is never decoded as `dlss_rr` — `03_METRICS` §Upscaling
    listed it as an upscaler value until this PR, which removes it and records why. The
    renderer is asserted too: rule 6 is a rule about
    *showing* a single inflated number, and a test matches the rendered text against `×N`.

- **`--present-interval-ms` on `hook-harness`**, because one test could not be written without it.
  The Agent's drop accounting fires when the writer laps the reader — 8192 records — and at the
  default ~120/s that is **68 seconds** of a reader deliberately not draining, so the branch
  `04_CAPTURE` calls "the Agent stalled for over ~16 s" had never run against the real Overlay in
  either language. Measured: uncapped, the harness does **171,636 presents in 3 s**, so the ring
  laps in well under a second.

### Changed

- **`HookedCaptureGate.ShouldUnhookAsync` is deleted (§S29(c) closed).** It was a second in-session
  re-scan that published no tick and did not latch — the two properties `GuardSupervisor` exists to
  guarantee — and it was the *more discoverable* of the two, because a drain loop already holds the
  gate. Its polarity was inverted from the survivor's, too: `true` meant STOP where
  `ScanOnceAsync`'s `true` means MAY CONTINUE. Deleted rather than routed: it had **zero production
  callers**, its body was strictly weaker than `ScanOnceAsync`, and routing would have given the
  gate per-session state on a class whose contract is that it "adds no judgement of its own".
  - **The two Facts that covered it are replaced by one that is stronger.** They asserted the
    boolean and never the tick or the latch, so they certified the API as sanctioned while saying
    nothing about what was wrong with it. `TheGateExposesNoSecondInSessionRescanPath` pins the
    gate's public instance surface to exactly `{StartAsync}` and is red on unmodified `main`.

- **`ShmDrainIntegrationTests`' honesty helper is split.** `AssertRecordsAreHonest` mixed
  fixture-independent invariants with assertions about attach *timing* and a single swapchain, and
  a fixture that deliberately stalls the reader satisfies neither — reusing it produced a red test
  whose failure message was about frame indices, indistinguishable from a real regression.

### Fixed

- **`ShmRingReader.SetPaused` had no test at all.** Both halves of `pauseRequested` existed —
  `MayObserve()` has read the flag since #46 — and the managed writer had never been driven in
  either direction. Now both: a merge-gated round trip proving the byte lands at
  `ControlOffset + 0` and that `unhookRequested` four bytes away is untouched, plus an integration
  case proving the Overlay acts on it, **ticking throughout**, because the defect #46 fixed only
  appeared on frames where `guardTicks` had changed. A pause is invisible in the record stream —
  `MayObserve()` returns false before `frameIndex` is assigned — which the consumer must not read
  as one enormous frame time.

- **`ShmHandshakeValidator` never compared `handshake.Pid`**, so a ring left behind by a finished
  session, or one under a recycled pid, validated `Ok` on build id, layout and capacity alone.
  Not an ABI change: `ShmAttachRefusal` is managed-only with no native mirror.

- `NativeAntiCheatGuard`'s §S21 rationale was attached to `NativeCheckRules` by two stacked
  `<summary>` blocks while `NativeRulesFilePath`, which it describes, had none.

### A fifth rate-sized budget, and a correction to what I said about WARP

`TheGuardInjectsTheOverlayAndTheReaderDrainsRealFrames` — pre-existing, from #51 — drained for a
fixed twelve iterations and then asserted a floor sized on "the harness presents at ~120/s". It waits
for the record count now, bounded by a wall clock, and gained an assertion that the loop supervised
more than once so the shortened path cannot pass vacuously. **This is explicitly not presented as the
fix for the one failure observed**: it failed once in twelve runs with its message uncaptured, and
after #62 — two rounds of the wrong remedy — the rule is that a defect class is a hypothesis about
the next failure, not a diagnosis of it. The budget was removed because that is defensible on its own
terms. Ten consecutive full runs clean.

**And the WARP note in `HANDOFF` §Traps said "a reboot clears it", which is false.** That sentence was
written on plausibility and never tested. The machine rebooted at 13:58 and the identical failure
reproduced at 14:17. Measured properly this time: `D3D12CreateDevice` on the WARP adapter returns
`DXGI_ERROR_DRIVER_INTERNAL_ERROR` at **every** valid feature level, while `D3D11CreateDevice` on the
**same adapter object** succeeds at FL 11_0 and D3D12 on the RTX 5080 succeeds; `d3d10warp.dll`,
`d3d12.dll` and `D3D12Core.dll` all match the OS build (Windows 11 Insider 26300/29639), so nothing is
corrupt. It reads as an Insider-build regression in WARP's D3D12 path and it is persistent. CI is not
an Insider build and passes the same suite, so `main` is unaffected — but the native suite has a hard
dependency on WARP D3D12 that a dev box can lose on its own, which is now written down.

### A fourth failure that was not a race at all — an off-by-one wearing a flake's costume

`APausedSessionStopsRecordingAndResumesWhereItLeftOff` failed roughly one run in five, which is
exactly what the three fixed above looked like. It was **`EstablishRecordingAsync`'s loop bound and
its assertion disagreeing by one**: the loop ran while `seen < 10` and so exited at *exactly* 10,
and the assertion demanded `> 10`. It passed only when a single drain happened to bring in eleven or
more at once — so the timing decided whether the off-by-one was visible, and two rounds of budget
tuning made it rarer without touching it. One constant now serves both.

**The lesson is the diagnosis, not the fix.** Having just repaired three genuine races, the fourth
failure in the same file was assumed to be a fourth race, and it was treated with the remedy for the
previous three. What settled it was reading the failure message instead of the pattern: *"Expected
seen to be greater than 10 … but found 10"* names the defect exactly and took one run to obtain.

Swept alongside it, since the class was the thing under review: the pause test now waits for the
writer to **settle** — two reads 100 ms apart with nothing between them — rather than assuming a
fixed 250 ms covers a present already in flight, and it keeps ticking throughout, because the pause
path is only reachable on a frame where `guardTicks` changed (the defect #46 fixed, and the reason
this test exists). The resume-drain budget goes 1 s → 3 s for the contention reason.

Ten consecutive full runs clean afterwards.

### Three racy assertions that #60 merged, caught by the post-merge run

`docs/HANDOFF.md` says to run `./build.ps1 check` with no switches **after** every PR. This is what
that found, and all three are the same shape: an assertion that reads a state once, at a moment when
the state is legitimately still in transit.

- **`Status == Ready` read immediately after the first guard tick.** `InitThread` publishes
  `layoutVersion` at step 2 and sets `READY` at step 6 — after `InstallPresentHooks` creates a
  throwaway WARP device, tens of milliseconds. `TryAttach` succeeds as soon as `layoutVersion` lands
  and the host publishes its first tick immediately **by design**, because the Overlay's 65 s
  supervision clock starts at mapping publish. A tick and `INIT` are therefore a legitimate
  simultaneous state. Polled now, with `INIT` past the budget still failing — that is
  `WriterNeverInstalledHooks` and must not read as a passing session.
- **`ApiMask` asserted as soon as `Status` became `Ready`.** "Hooked and recording" is two events:
  `apiMask` is set inside `FindOrAdd`, on the first present the hook actually *sees*, so `READY` with
  `apiMask == 0` is another legitimate window. Failed once in five full-suite runs and never once in
  six isolated ones — the signature of a window widened by contention.
- **QPC ascending asserted across a deliberate lap, and it is unassertable in EITHER direction.**
  Measured both ways: under the full suite the drop test's first drained record came back ~148 ms
  *later* than the second; run alone the same batch was perfectly ascending. `Drain` resumes at
  `writeIndex - capacity`, the oldest survivor, and whether the writer has overwritten that slot when
  the copy arrives is a race decided in microseconds — the seqlock catches a tear *during* a copy, not
  a slot cleanly overwritten *before* it. Ascending qpc is a property of a reader that **kept up**, so
  it now lives only with the other attach-timing assertions. What matters downstream is enforced where
  it belongs: `MeasuredFacts` skips non-positive deltas, and `04_CAPTURE` requires a non-zero drop
  count to be surfaced as a session warning.

Six consecutive full runs clean afterwards. **Two native cases stay red on this machine for an
unrelated reason**, recorded in `HANDOFF` §Traps: `D3D12CreateDevice(WARP)` returns
`DXGI_ERROR_DRIVER_INTERNAL_ERROR` (0x887A0020) while the same call on the real adapter succeeds. CI
runs the same suite on WARP and passes, and nothing in #60 touches that path.

### What the adversarial review of this diff caught before it landed

An adversarial pass over the working tree raised 23 candidates; 17 survived a second agent told
to refute them. Beyond the process handle above, the ones that were real defects rather than
documentation drift:

- **"Could not read the executable" passed the fingerprint check instead of refusing.**
  `observed ?? record.Fingerprint` made `FromConsent`'s mismatch comparison compare the record
  against **itself**, so an unreadable binary decided as "this is the consented one" — the one
  polarity everything else here is built to avoid. Now `ExecutableUnreadable`.
- **`Enum.TryParse` accepts numeric strings**, so `"provenance": "1"` in the consent file yielded
  `UnshippedHostOperator` and `"42"` an undeclared value cast to the enum — an end-run around the
  two-member count a test pins. The comment claimed the opposite in as many words. Now
  `Enum.IsDefined` plus an ordinal name comparison, with the numeric cases tested.
- **An unreadable consent file silently cleared a persisted guard block.** "There is no file" and
  "I could not read the file" both produced an empty store, and every write is a
  read-modify-write over that — so the merge that carries `BlockedReason` forward carried nulls,
  and the write republished a file containing only the new entry, dropping every other game's
  record. The two outcomes are now distinct and an unreadable store refuses every write.
- **A guard exception mid-session discarded every record already drained.** Not advancing the tick
  is correct and required; losing the session's data with the stack was neither. Now
  `SupervisionFaulted`, with the final drain still running — and the *first* scan still throws,
  because there is no session to preserve yet.
- **`StreamSegmenter` cut a spurious 0×0 segment** when a stream's first records had no measured
  size: `w`/`h` were both the "no baseline yet" sentinel and a real resolution, so the split fired
  on the first measured record — cutting a segment on the writer's silence, which the surrounding
  comment forbids.
- **`TargetResolver` narrowed to a false single match.** Its comment claimed skipping an unreadable
  process avoided that; skipping is what caused it, because `matches.Count == 1` could not tell
  "one candidate" from "one readable candidate and others invisible". Skips are counted now.
- **`ConsentWriteOutcome.StaleFingerprint` had no producer** — a declared-but-producerless value,
  the shape this same PR invokes two files away to justify `ConsentProvenance` having no FR-2.1
  member. It is produced now, and only where it matters: a re-grant against a different binary
  cannot inherit an existing block, while an ordinary re-consent after a patch still works.
- **Three of the new tests could not fail for the property they named.**
  `ATerminalAttachRefusalIsNotRetriedIntoATimeout` asserted only the final reason, which is
  identical whether the refusal returns immediately or is retried to the budget — it counts
  attempts now, and gained the green half. `TheFirstTickIsPublishedBeforeAnyDrain` recorded
  publishes and drains in two independent lists, so no ordering was observable — one ordered event
  list now. And the end-to-end refusal case called `File.Delete` on a path whose parent directory
  only exists once some test has written a record, so on a clean build output it threw and its
  verdict depended on which sibling ran first — in the very test whose comment says a test whose
  verdict depends on what ran before it is one this repository does not accept.

- **`docs/HANDOFF.md` — one file to pick the work up from, and it carries no status.**
  Sequencing, the decisions that live in no other file, and the traps that cost a wrong
  diagnosis rather than a build cycle. Status stays in `20_OPEN_QUESTIONS` §S24,
  `spike-notes`, `15_ROADMAP` and this file, and the handoff points at them.
  - **The rule is written into the file because the alternative is what keeps
    happening.** A handoff that summarises status becomes the next stale copy within a
    day — #45's note did, `15_ROADMAP`'s status block did, and `spike-notes` §8 went
    stale *in the same file* that was correcting an earlier stale claim. It also would
    have been a fifth statement of the gate's composition, which `rules-validate` now
    refuses in the shipped data.

- **Shared-memory layout v3: the zero value of every enum in the record is now "nobody
  said", not a fact.** `FL_SHM_LAYOUT_VERSION` 2 → 3, spent deliberately in the last window
  where it costs nothing — nothing has shipped and no user has a session, and after the
  first release the same edit is a SemVer MAJOR that makes the Agent refuse to attach and
  tell the user to restart the game.
  - **The generator of the defect, not four instances of it.** `FlFrameRecord rec{}`
    zero-initialises, so whatever 0 means is what a writer publishes when it **forgets**. In
    v2, 0 meant `FL_UPSCALER_NONE`, `FL_FG_NONE`, and an `rtFlags` with no evidence bits —
    three measured negatives about a title nobody examined, which `03_METRICS` turns into
    `upscaler none` and `fg_factor 1.0`, the single inflated number rule 6 forbids.
    `measuredMask` made that safe **by convention**; v3 makes it safe **by construction**,
    and the mask becomes corroboration rather than the sole defence. `rtFlags`' polarity is
    flipped so every bit means *observed*.
  - **Four answers items 4/6/7 owe had no home.** DLSS super-resolution **and** Ray
    Reconstruction concurrently — RR was a mutually exclusive `upscaler` *value* while
    `03_METRICS` makes it an independent tri-state axis — plus `upscalerSharpness`, and, in
    `FlWriterState` because they are session facts rather than per-frame, the device
    **RT tier** without which rule 7's definite `No` had no producer at all, with
    `rtStateObjectsCreated` and `rasterPsoCreated`.
  - **Paid for by two narrowings that are corrections, not sacrifices.** `vramUsedBytes`
    (u64 bytes → u32 MiB) carried 64 bits of byte precision that every consumer divided
    away, to feed a comparison against `vramBudgetMb` — **already MiB** — that was
    unit-mismatched at the point of use. `fgEvaluations` (u32 → u8): ×4 multi-frame
    generation is 3, so saturating at 255 would mean 256× frame generation.
  - **`seq` @56 and `swapchainId` @60 did not move**, so `fl_ring.h`'s two pins hold
    verbatim, the seqlock's payload spans are unchanged, and the ring needed no edit.
  - **Three mask bits split producers that do not arrive together** — the same defect three
    times. `FL_MEASURED_UPSCALER_PARAMS`, because an NGX-direct title exports only the
    parameter-object *factories* so a writer knowing *which* upscaler ran knows nothing
    about quality (publishing `0` = "DLSS Performance" as measurement);
    `FL_MEASURED_PRESENT_ARGS`, because `wglSwapBuffers` and `vkQueuePresentKHR` have no
    such arguments and `syncInterval = 0` is a *real* DXGI value with no in-band sentinel;
    and `FL_MEASURED_FG_COUNTS`, because identity and per-present counts are two hook rows
    and a writer with only the first would publish `fg_factor 1.0` having counted nothing.
  - `FL_RT_PSO_ALIVE` → `FL_RT_PSO_CREATED_EVER`: creation is observed at
    `CreateStateObject` and destruction is COM `Release`, which is not in the inventory and
    must not be added — so the bit latches and could only ever mean "created ever".
  - **`03_METRICS`' RT `No` is now three conjuncts**, and the second is the one that is easy
    to drop: `hooksInstalledMask` must contain the **AS-build** hook. A writer with only
    `DispatchRays` sees nothing on an inline-RayQuery title, and its silence would otherwise
    be indistinguishable from a real negative.
  - **`pt_confidence` loses its fourth input rather than substituting one.** "The ratio of
    RT to raster work" has no cheap denominator — counting raster work means a per-draw hook
    — and §H6 records that a command-list count measures *recorded* rather than *executed*
    work anyway. The score may only ever *suggest*, so a weaker score is not a fabrication.
  - **The design came from a four-way panel, three judges and three refuters**, and the most
    useful thing the refuters found was not in any proposal: see the `FL_MEASURED_OUTPUT_RES`
    fix under Fixed. They also killed a claim this entry would otherwise have made — that
    reserved bytes let a future field skip the version bump. They do not: `recordSize` and
    `layoutVersion` are compared before a reader looks at anything, so an old reader refuses
    a new writer and never reaches an unknown field. What the reserve buys is that existing
    offsets do not move.
  - **Hook-path cost:** `RecordPresent` gains one comparison of two `uint16`s already in
    registers, and `Publish`'s two memcpy spans are `offsetof`-derived and byte-identical.
    **Not carried forward: the 8.4 ns figure.** That is `--probe-cost`'s empty-detour floor,
    which the tool says of itself; no instrument in the tree measures `RecordPresent`.

- **NVAPI is vendored, and the capability matrix gets the axis it could not be filled
  without.** `src/native/third_party/nvapi/` now holds nine headers (`nvapi.h`'s include
  closure plus `nvapi_interface.h`), `License.txt` and `amd64/nvapi64.lib`, from
  `github.com/NVIDIA/nvapi` @ `cd6918f6` (MIT). **x64 only** — `x86/nvapi.lib` is 438 KB of
  material we could never link and would still have to disclose. `NvApiDriverSettings.{c,h}`
  and the HLSL-extension headers are left behind for the same reason.
  - **Why that repository and not the SDK installer** is the whole licence argument:
    `amd64/nvapi64.lib` is a *tracked file in the MIT repo* and `License.txt` names the
    import libraries as the subject of the grant, so the **binary** is covered. The same
    file from the SDK installer arrives under NVIDIA's own agreement and could not be
    vendored into a GPL-3.0 tree.
  - **The canary came for free, and it ran in the order that makes it evidence.**
    `license-check.ps1` was made bidirectional after it was found unable to see
    *claimed-but-absent* material. Vendoring while `THIRD_PARTY_NOTICES.md` still said
    "Not yet vendored" failed the build — the *present-but-marked-absent* direction, which
    was structurally invisible before. The notice was flipped afterwards, not before.
  - **`ctest fl_nvapi_probe` exists because an unconsumed vendored dependency is
    unverified.** Nothing else compiles against `fl_nvapi` yet, so a short include closure
    or a wrong-architecture `.lib` would sit there with every gate green. The probe's
    *compile* is half the test.
    - **It is green on both kinds of machine, and exiting 0 is not what makes it green.**
      `nvapi64.lib` is a **static** stub library reaching `nvapi64.dll` through
      `nvapi_QueryInterface` at first call, so it is not a load-time dependency: a CI runner
      with no NVIDIA driver loads the binary and `NvAPI_Initialize` returns an error, which
      is exactly the degradation §L3 requires. Both branches exit 0 — so ctest additionally
      requires the string `BRANCH: (AVAILABLE|DEGRADED)`. **Canary: a probe gutted to
      `int main(){return 0;}` compiles, exits 0, and is now RED** at
      *"Required regular expression not found"*, with the native build and the other 15
      ctests still green. The alternation has to stay: pinning one branch would turn the
      other kind of machine red for being itself.
    - **What that still does not give you, said rather than implied:** ctest prints a
      passing test's output nowhere, so a CI log shows a branch was reached and **not
      which**. On a hosted runner it is inferable — no NVIDIA driver exists there, and the
      probe returns in ~0.01 s against ~1.4 s here — but inferable is not observed, and
      `18_GPU_VENDOR_APIS` §L3 now separates the two rather than claiming the stronger
      thing. What the run *does* prove is the load-time claim: a load-time dependency on an
      absent `nvapi64.dll` would not have started at all.
    - It refuses a **zero** GPU count rather than letting the name loop run no iterations,
      because every assertion inside a loop that never runs is vacuous.
  - **Measured on this machine:** driver `610.88` (branch `r610_85`), 1 physical GPU,
    "NVIDIA GeForce RTX 5080".
  - **A doc error the vendoring found and reading vendor documentation would not have:**
    §L3's function table named `NvAPI_GPU_GetMemoryInfo`. The headers mark it
    `__nvapi_deprecated_function` ("deprecated in release 520 — use
    `NvAPI_GPU_GetMemoryInfoEx`"), so under `/W4 /WX` a call to it **fails the native
    build**. Corrected. This is the class `17_HOOK_ENGINE:128` calls the highest
    false-confidence risk in the spike, sitting in a document instead of in code.
  - **The capability matrix is now vendor × layer**, restructured *before* anything was
    filled in — `18_GPU_VENDOR_APIS:137` said in its own words that the single-axis table
    "cannot express" the AMD/Intel deferral and that the axis is "a prerequisite of filling
    it, not a tidy-up afterwards". The legend separates `?` (a to-do, measurable here) from
    `untested` (a deferral, not measurable here) from `arch` (not available by
    architecture), which is precisely what the old table could not distinguish and what the
    UI consults before advertising a capability.
  - **The D3DKMT probe's two-OS requirement is deferred to Win 11, with a rationale.** One
    machine, and it is Win 11; Win 10 22H2 **stays a supported floor** and is explicitly
    unmeasured. Unlike the AMD/Intel gap this one *could* be closed with a VM and is being
    left open deliberately — so the deferral is conditional on the probe staying
    non-load-bearing, which the doc now states as the condition rather than as advice.

> **These five entries were written retrospectively.** PRs #40–#44 changed 8 files,
> every one under `src/native/`, and touched no documentation at all — so for a day the
> repository's own ledger described an Overlay with no hooks. CLAUDE.md's "any deviation
> from a doc updates that doc in the same PR" did not catch it, because none of the five
> deviated from a doc: they implemented specifications that were already written, and
> the staleness landed in the *status* claims of other files. `legal/DISCLAIMER.md` §Accuracy
> audit records the same drift from the user-facing side.
>
> > **And then it happened again, immediately, seven more times.** #45 — the PR that wrote
> > the five entries above and complained about the gap — was followed by #46 through #52
> > with **no changelog entry for any of them**, including the C# struct mirror, the ring
> > reader, the handshake validator, the closed write-read loop and check 3's call site.
> > The entries below were written retrospectively too, on 2026-08-05, from the diffs.
> >
> > **A retrospective note is not a mechanism, which is the actual finding.** #45 recorded
> > the drift in prose and nothing about the repository changed, so the same failure ran a
> > second time at greater length. `ci.yml` now carries a `changelog` job that fails a pull
> > request touching `src/` without touching this file. Prose asking people to remember is
> > what was already tried.
> >
> > **One of the seven is worse than missing.** `bd6d367` (#48) carries, verbatim, the
> > commit body of `bff0f6a` (#47) — a squash whose `--body-file` came from the wrong
> > branch. The subject line describes the occlusion-probe fix; every paragraph beneath it
> > describes the struct mirror. Its entry below was reconstructed from the diff, because
> > the commit message is evidence about a different PR.

- **Check 3 gets a call site, and the half that cannot have one** (#52). `19_SAFETY` has
  listed the per-title blocklist as check 3 from the beginning and it had **no call site**:
  `MatchesBlockedExecutable` was implemented, tested, and asked by nobody, so "check 3
  passed" read as "this title is not a known online title" while nothing had looked.
  `CheckBlockedExecutable` now runs inside `EvaluateImpl`, between the module scan and the
  pre-scan — one `OpenProcess` and a string compare, ahead of the only check that touches
  the filesystem.
  - **It needed a new seam.** `Sources::ImageDirectory` deliberately resolves the *install
    root* — Unreal puts the exe three levels below it, measured on Lies of P — and the file
    name is exactly what it discards. `ImageFileName` is a different fact, so it is a
    different seam and a new row in the fail-closed matrix.
  - **Unresolvable identity refuses** (`kProcessUnreadable`): `kFailed`, `kIncomplete`, an
    empty name and a null seam all take one path. The narrow conversion uses
    `WC_ERR_INVALID_CHARS` with **no** default character, so a name that cannot be
    represented exactly fails rather than becoming a string containing `?` — §S21's ANSI
    defect was exactly a silent lossy conversion.
  - **The store-id half cannot be called**, for three independent reasons: nothing produces
    a `store_id` (the platform metadata extractors were never built), `FlGuardEvaluate`
    takes a pid and nothing else *by design* (§S3 forbids a caller asserting a safety
    fact), and "unknown refuses" applied to it would refuse every title on every machine.
    It stays implemented, tested and uncalled. **Do not "fix" the second by widening the ABI.**
  - **The list ships empty**, so nothing is refused today; what changed is that populating
    it would now do something. The acceptance criterion asserts `kBlockedExecutable`
    *specifically*, because "it refuses" is indistinguishable from the four refusals the
    guard already makes.

- **The write-read loop closes, against our own harness and nothing else** (#51). Real
  guard, real injection, real Overlay, real reader: `ShmDrainIntegrationTests` seeds the
  rules with the product's own `RulesSeeder`, starts `hook-harness --real --hold-presenting`,
  injects through `NativeAntiCheatGuard`, and drains records while driving
  `GuardSupervisor.ScanOnceAsync` and `PublishGuardResult`. A second case proves the safety
  stop: >10 records flowing, then `unhookRequested`, then `writeIndex` frozen 700 ms later.
  - **CI found a hard-gate defect the dev box structurally could not.** Running the test,
    the guard refused our own harness — `SuspiciousUnsigned unknown
    System.Security.Cryptography.ProtectedData.dll`. §S16 puts the target's **ancestors**
    in the scan set and the .NET test host is the harness's parent, which is the
    launch-mode arrangement. A .NET host that loads that assembly poisons its own scan set.
    **A gate that cannot pass**, and §S19(b)'s "plausible and unmeasured" is superseded.
  - **So the drain tests are `Category=Integration` and CI runs `-SkipIntegration`.** The
    skip is loud, names §S19(b) and says how to run them; a developer running
    `./build.ps1 check` with no arguments still gets everything. Proven both directions —
    76 Infrastructure tests by default, 73 with the switch.
  - **What that costs, stated rather than left to be discovered:** the only end-to-end proof
    of the capture path does not run in the merge gate. See §Known issues.

- **The Agent can read the ring, and the pointer trap that would have hidden it** (#50).
  `ShmRingReader` mirrors `fl_ring.h`'s `RingReader::Drain` rather than re-deriving it —
  where the two disagree the header is right and the C# is the bug.
  - **Map at offset 0, and this is measured** (.NET 10, Win 11 26300): a view created at a
    non-zero offset is mapped from the 64 KiB allocation-granularity boundary *below* it,
    and `AcquirePointer` returns **that** base. So the obvious way to reach the control
    block — map region 3 at `0x80`, write `*(uint*)(p + 12)` — writes
    `FlShmHandshake.pid` instead of `FlControlBlock.guardTicks`. It does not fault, and
    `Read<T>` does not bounds-check it either, because the handle legitimately spans from
    byte 0. The supervision counter would have landed silently on the field a reader
    validates first. `Bind` now asserts `PointerOffset == 0` rather than assuming it.
  - **Seed the read index from the writer.** `fl_ring.h` starts its reader at 0 because it
    is created alongside its writer; an Agent attaching to a ring already in flight is a
    different situation. Canary: seeded at 0 against a ring at `writeIndex` 1,000,000 the
    first drain reports **999,992 drops** — and `04_CAPTURE` defines any non-zero drop count
    as "the Agent stalled for over ~16 s". It also re-ingests up to `capacity` stale records
    from a finished session as current frames. Records already published at attach are
    reported separately as `RecordsBeforeAttach`, which is not a stall.
  - **Bound the capacity against the mapping.** The validator accepted any power of two —
    every value up to 2³¹ — and returned `Ok` having never related that claim to the section
    it describes. The reader indexes by raw pointer arithmetic (the seqlock needs ordering
    `Read<T>` does not provide) and so gives up that API's bounds check.
    `CapacityExceedsMapping` is taken from `ByteLength`, never from `DefaultCapacity`.
  - **Publish order is a safety property and it is the reverse of the natural one** — flag
    first, tick second, because a fresh tick *resets* the Overlay's supervision clock.
    **The suite does not verify that order**, and says so: it asserts both values land,
    which a publisher writing them the wrong way round also satisfies. What holds the
    property is `PublishGuardResult` being the only writer of either field.

- **The Agent gets a build id of its own, so refuse-to-attach can run** (#49). `07_IPC`
  makes a `buildId` mismatch a hard refuse-to-attach and `04_CAPTURE` says the Agent
  compares it "against its own" — and the Agent had no own value. Two documents specified a
  check that could not run in either direction (§S23-1).
  - `FlGuardBuildId` is observation-only and **refuses rather than truncating**: a shortened
    id is not a partial answer, it is a *different* id, and returning one would produce a
    permanent mismatch caused by this function rather than by any real version skew.
  - **Why the guard carries it and not the Overlay:** reaching `FlGetBuildId` means
    `LoadLibraryW` on `FrameLedger.Overlay.dll`, which starts its init thread and creates a
    ring under the *Agent's* own pid. The payload is not something its own host may load.
  - `ShmHandshakeValidator` is the comparison, as a pure function, so every refusal is
    drivable without a live target. `layoutVersion == 0` is **`Incomplete`, not a mismatch**
    (it is published last behind a release fence, so zero means "retry", not "restart the
    game"); the version is checked **before** the fields it vouches for; and the default is
    `NotEvaluated`, never `Ok`.
  - **The fail-open, measured.** The naive implementation compares two strings, and with
    neither side carrying an id `string.Equals("", "")` is **true** — so the gate reported
    `Ok` for every process on the machine. **The test for it was nearly missed:** the first
    draft asserted the two halves as separate cases and never put both in one call, which
    is the only arrangement that reaches the defect.

- **The C# mirror exists, and the gate that guards it stops being a `Test-Path`** (#47).
  CLAUDE.md calls the struct mirror the mechanism protecting the shared-memory ABI; nine
  files described it in the present tense and none of it existed (§R10).
  - `ShmLayout.cs` is driven by `tools/fl-layout-dump`'s JSON, never a transcribed table —
    a hand-written offset table is a *second* statement of the layout, and two statements of
    one fact drift, which would be the defect the mirror exists to catch, reintroduced
    inside the catcher.
  - **Blittability is asserted, and that is not decoration.** Measured: swapping the
    `fixed byte BuildId[32]` for the `[MarshalAs(ByValTStr)] string` idiom this repository
    already uses correctly elsewhere keeps `Marshal.SizeOf` at 64 with **every offset
    assertion still passing**, while `Unsafe.SizeOf` collapses to **40**. That mirror looks
    correct by every obvious check and cannot be read out of a memory-mapped view.
  - **Both directions**, and the reverse walk caught something on its first run:
    `fl-layout-dump` was not emitting the `reserved` tails, so the C# declared members the
    dump could not confirm. Fixed in the dump rather than excluded from the check.
  - **The gate was the bigger finding.** `build.ps1`'s struct-mirror step was
    `Test-Path ShmLayout.cs`, then printed "covered by dotnet test" — it never looked for
    the test. An *empty* `ShmLayout.cs` would have turned an honest loud skip into a silent
    green, and deleting the test afterwards would have kept it green forever. It now reads
    the run's `.trx` and requires the named class to have **executed**. `dotnet test` makes
    a regression red; this makes *deleting the regression test* red.
  - Three canaries proven red, and **the harness was broken twice before it proved
    anything** — a renamed class tripped a file-name analyzer and the run died at *build*;
    a `sed` rewrite gave the file LF endings and the run died at *`dotnet format`*. Both
    printed a failure, and a failure upstream of the gate proves nothing about the gate.

- **`api` is resolved per swapchain, and `GetDevice` does not return what the docs imply**
  (#44). `api` was hardcoded to `FL_API_D3D11` on every record — a guess written into a
  field `03_METRICS` consumes and `06_DATA_MODEL` persists. One hook on the shared
  `dxgi.dll` class vtable catches D3D11 and D3D12 alike, so the present call cannot tell
  them apart; the Overlay now asks the swapchain which device created it, once per
  swapchain, and caches it.
  - **Measured, and the obvious implementation was wrong.**
    `CreateSwapChainForComposition` takes a **command queue** for D3D12, so the first
    version queried the returned device for `ID3D12CommandQueue` — and every record from
    a real D3D12 target came back `FL_API_UNKNOWN`. DXGI resolves the queue to its owning
    device before storing it, so `ID3D12Device` is what answers. The queue query is kept
    anyway: it costs one failed QI per swapchain, and a DXGI that *did* hand back the
    queue would otherwise regress to `UNKNOWN` silently.
  - `apiMask` now records what was **seen presenting**, not what the process loaded — a
    title can link `d3d12.dll` and present through D3D11.
  - Both directions, which is what makes either assertion mean anything: a new
    `--hold-presenting-d3d12` harness mode builds device → command queue → swapchain, and
    the D3D11 case gained the mirror assertion, so a resolver that always answered D3D12
    fails there.
  - **OpenGL is not attempted.** `wglSwapBuffers` is a flat export and the hook is small,
    but `hook-harness` has no OpenGL mode, and shipping an unexercised hook into a game
    process is not something this project does. The harness mode is the prerequisite.

- **The safety stop and supervision loss, on the present path** (#43). `19_SAFETY` calls
  the mid-session stop the single most important runtime behaviour in the capture layer,
  and `legal/DISCLAIMER.md` promises both to the user. Until now neither existed —
  `FlRequestUnhook` set a status field while the hooks kept running.
  - `unhookRequested` → hooks out, status `UNHOOKED`. `guardTicks` stalled past
    `FL_GUARD_TICK_DEADLINE_MS` → the same. The clock starts when the **mapping is
    published**, not at first present, because `07_IPC` is explicit that "never advanced"
    and "stopped advancing" are the same state.
  - **Stopping is one-way.** Resuming ticks does not resume recording; a capture side
    that can un-stop itself is one whose stop is advisory.
  - **Two of three canaries red, and the third is recorded rather than hidden.** Removing
    `g_observing = false` and leaving only `MH_DisableHook` kept the suite **green** — so
    "the flag is necessary" is *not* a property this suite proves. The flag is kept
    deliberately (it closes the window for a thread already inside the hook body, and it
    is the only thing that holds if `MH_DisableHook` fails) and the comment now says so.
  - **Not covered, and stated rather than left to look covered:** the 65-second expiry in
    its real configuration. The canary proves the comparison fires, not that 65000 is the
    number on the shipped path.

- **The present hook, and a harness that presents while we inject** (#42). MinHook on the
  shared `dxgi.dll` class vtable, read off a throwaway WARP composition swapchain that is
  released immediately — slots 8 `Present`, 13 `ResizeBuffers`, 22 `Present1`, proved by
  behaviour in ctest `fl_vtable_indices`, never hardcoded.
  - **`--hold-presenting` had to exist first.** `--hold` presents 240 frames and *then*
    sleeps; those are over in milliseconds while `fl_guard_test` injects ~800 ms later, so
    an Overlay injected into `--hold` observes exactly **zero**. Every "N presents → N
    records" assertion written against it would have been vacuous — the same shape as the
    `DXGI_PRESENT_TEST` defect this harness was already fixed for once. The handshake test
    now asserts `writeIndex == 0` against `--hold` deliberately, so the trap has a test on
    it instead of a comment.
  - **Honesty, which is why #36 spent two bytes.** A present-only writer sets
    `measuredMask = FL_MEASURED_OUTPUT_RES` and `rtFlags = FL_RT_NOT_MEASURED` and claims
    nothing else. Leaving the mask at 0 with the zero-defaults would assert "no upscaler,
    no frame generation, no ray tracing" as measured fact ~118 times a second — producing
    `fg_factor 1.0` (rule 6) and a definite RT `No` (rule 7) about a title nobody looked at.
  - `FL_HOOK_GUARD` wraps **only our code, never the call to the original** — otherwise a
    game's own fault inside the trampoline is counted as ours. Three faults →
    `MH_DisableHook(MH_ALL_HOOKS)` and `status = self_disabled`.
  - `status` reaches `READY` only when hooks are actually installed; a failed
    `MH_Initialize` leaves it `INIT`, because `READY` would claim a capture side that does
    not exist and the Agent's degradation path is what should run.
  - Four canaries, each proven red: claiming everything measured, asserting a definite RT
    `No`, a `frameIndex` that stops advancing, and a swapchain never identified.

- **The DLL gets inside, maps its ring, and publishes a handshake** (#41). The first real
  code in `FrameLedger.Overlay`, which until then was a 30-line scaffold exporting one
  function. `DllMain` does **only** `DisableThreadLibraryCalls` and `CreateThread`;
  everything else runs on the init thread, outside the loader lock (§H2).
  - The mapping carries a DACL granting only the current user's SID.
    `BuildUserOnlySecurity` returns false rather than falling back to a default DACL — a
    mapping the machine can write is not a degraded version of this one, because the
    Agent's control block is in it and `unhookRequested` is the safety stop.
    `ERROR_ALREADY_EXISTS` also refuses.
  - `layoutVersion` is published **last**, behind a release fence, because it is the field
    a reader validates first: a reader that saw the version while `capacity` was still
    zero would compute a ring of no slots and read garbage.
  - **`status` is `INIT`, not `READY`, and that is the point** — nothing was hooked in that
    slice, so no record could ever arrive.
  - Three canaries, each proven red: claiming `READY` with no hooks, leaving `capacity`
    unpublished, and corrupting `buildId`.

- **The SPSC ring, and an honest account of which half the suite proves** (#40).
  `fl_ring.h` implements `07_IPC` §Protocol rules and nothing else; where the two
  disagree the document wins and the header is the bug. Drop accounting lives on the
  **reader**, the only side that knows what it consumed.
  - **Two of four canaries came back GREEN, and that is the finding.** *"The payload write
    steps over `seq`"* and *"the reader re-reads `seq`"* both survive, because the damage
    is observable only inside a 64-byte memcpy and neither a 1024-slot nor an 8-slot
    concurrency case lands in it across 200,000 records. **Those two properties are
    therefore unverified**, and the file header says so rather than letting nine
    assertions imply coverage.
  - **One test was wrong in a way that looked like the code was wrong**: "the payload write
    never touches `seq`" asserted on slot 1 because the record's `frameIndex` was 1 — but
    the slot is chosen by the publish counter, which starts at 0. It failed against a
    correct writer.
  - Claims `FL_MEASURED_HDR` (bit 7) in the last free window. `hdr` had no "not measured"
    state; #36 fixed that class for five other fields and missed this one. The byte is
    already written every frame, so it costs no layout change now — and after the C#
    mirror exists the identical edit is user-visible and a SemVer MAJOR.

- **`fl-probe-interposer` — the vtable premise, proven, and the Streamline
  question narrowed to a licence decision** (`20_OPEN_QUESTIONS` §H5 case 3,
  `spike-notes.md` §5, previously an empty template). It runs in **our own
  process**: no game, no injection, no guard, and it needs **no vendor headers**,
  only `GetProcAddress` and DXGI types from the Windows SDK.
  - **ctest `fl_vtable_identity_control`** asserts both directions of the property
    the whole hook design rests on — two independently created composition
    swapchains share one vtable, and a different interface does not. A comparison
    never shown to detect a *difference* carries no information when it reports
    "same", so the negative control is not optional.
  - **The interposer half is INCONCLUSIVE, and that is the result.** Loaded from
    Cyberpunk 2077 and Black Myth: Wukong, `sl.interposer.dll` forwards to
    `dxgi.dll` and leaves the factory *and* swapchain vtables untouched until
    `slInit()` has run. The probe enumerates its own modules, finds no `sl.*`
    plugin mapped, and exits 2 rather than rendering a verdict.
  - **Its first version got this wrong and the fix is the interesting part.** It
    printed *"the vtable is THE SAME — a hook DOES catch Streamline presents"* for
    both titles, which would have closed §H5 case 3 on a measurement of
    passthrough. The tell was already in its own output: an interposing
    Streamline cannot leave the **factory** vtable unwrapped, because wrapping the
    factory is how it reaches the swapchain. "Could not look" must not read as
    "looked and it was clean" — the guard's tri-state discipline, applied to a
    probe.
  - **The blocker is now named:** reaching the wrapped path needs `slInit`'s
    `sl::Preferences`, i.e. vendor ABI — the question `THIRD_PARTY_NOTICES.md`
    answers for Intel IGCL and nobody has asked for NVIDIA. That is a licence
    decision, not absent hardware. And the exposure is narrower than §H5 implied:
    NGX-direct titles never wrap the swapchain.

### Fixed
- **The present-only writer claimed its one measurement unconditionally, including on
  records that had none** (§S29(g)). `RecordPresent` set `FL_MEASURED_OUTPUT_RES` on every
  record; two paths reach it with no output size — `FindOrAdd` returning `nullptr` once its
  fixed 16 slots are taken, and `GetDesc` failing in `FindOrAdd` or after a resize. The
  record therefore said **"output resolution MEASURED: 0 × 0"**, and `03_METRICS` computes
  the upscale ratio as `sqrt((outW*outH)/(renW*renH))` from exactly those two fields.
  - This is the defect #36 spent two bytes to fix, surviving *inside* the fix: the mask
    distinguishes looked-from-did-not-look for six fields and for the seventh it was a
    constant. Found by a design panel refuting a proposed layout — in the shipped writer,
    not in the proposal.
  - **Proving the second direction needed a new fixture.** Nothing in `hook-harness` could
    reach the overflow branch: `--plus-ui` makes *one* extra swapchain, which is a second
    stream and not an overflow. `--hold-presenting-overflow` round-robins 17 chains for the
    whole hold. The existing end-to-end test asserts the mask is exactly `OUTPUT_RES` on a
    normal target; the new one asserts it is exactly 0 on an overflowed one, so a writer
    that always claimed — or never claimed — fails one of the two.
  - **The fixture was wrong first, and the test's own vacuity guard is what said so.** Its
    first version filled the table at startup and held on the 17th chain — but the Overlay
    injects ~800 ms later and only sees presents made after it hooks, so it observed an
    empty table and gave the "overflowed" chain slot 1. `overflowed > 30` reported **0**
    rather than letting a loop full of `CHECK`s pass by never executing. The test also pins
    the overflowed stream's ~1/17 *share*, because an absolute floor alone is satisfied by a
    harness presenting on a single chain.

- **`ctest fl_vtable_indices` proved a fact about `dxgi.dll`, not about the Overlay**
  (§S29(b)). `hook-harness` declared `kPresentIndex = 8` / `kResizeBuffersIndex = 13` /
  `kPresent1Index = 22` as its own constants, textually duplicated from the inline literals
  in `dllmain.cpp` with nothing binding them. **Change the Overlay's 8 to a 9 and the test
  still passed** — it exercised the harness's copy. The only test coupling the two is the
  drain integration class, which CI skips for §S19(b), so in the merge gate the coupling
  was absent entirely.
  - Closed by `FrameLedger.Overlay/include/fl_dxgi_vtable.h` and an `fl_dxgi_vtable`
    INTERFACE target, so the harness reads the shipped constants without linking the DLL.
  - **Canary:** setting `kPresentIndex = 9` leaves the native build **green** and turns
    `fl_vtable_indices` red — along with three neighbouring harness tests, which depend on
    hooking working at all. Restored: 16/16.
  - **What it does not do**, said rather than implied: the indices are still not *trusted*.
    The header is where the assumption is written once; `--probe-vtable` calling each slot
    on a real swapchain is what makes it a measurement. It is also not licence to hardcode
    a vtable *pointer* — the Overlay still reads the vtable off a throwaway WARP
    composition swapchain and releases it. What is ABI-constant is the slot index, not the
    address.
  - P0 item 2's ✅ rests partly on "vtable indices proved by behaviour". Until now that
    proof did not reach the shipped values.

- **Four gates that could not fail, or could not discriminate** — §S19(a), §S19(d)'s
  residual, §S23-5, and §S29(d). Each is closed by a mechanism rather than by a
  correction, because three of the four were *already* corrections that had gone stale.
  - **`gameguard` could never fire, and it is now impossible to add another that
    cannot.** The heuristic match is a case-insensitive substring and `guard` is a
    substring of `gameguard`, so the shorter token always won first. Removed, and
    `rules-validate` now fails when **any** `nameFragment` contains another.
    - **Deleting a fragment is normally a detection removal in a hard gate, and this
      is the one case where it removes nothing** — subsumption means *by construction*
      that every name `gameguard` could match, `guard` matches. Checked separately and
      not previously recorded: nProtect GameGuard also has its **own named module
      family** (`GameGuard`, `npgg`, `GameMon`), so the fragment was redundant twice.
      The standing objection still applies in full to `protect` (§S19(b)).
    - The hazard was always in the future: with both present, removing `guard` leaves
      a list that still *appears* to cover nProtect. "Cosmetic" was the wrong word.
    - It trips `rules-publish`'s removal check. **That is the gate working** — it
      exists to make a blocklist removal reviewable, and this is one.
  - **The schema canary proved the schema was not inert, and nothing more.** It was
    `{"schemaVersion":"not-a-number"}`, which any schema still pinning `schemaVersion`
    rejects — so deleting `minItems` from `nameFragments` left it passing. A second
    canary now carries `nameFragments: []` and must be rejected, **derived from the
    shipped document** rather than hand-written: a hand-written canary is a second
    statement of the schema's shape and drifts from it, which is the defect the
    validator exists to catch. Mutating the real document makes the constraint under
    test the only difference between the passing and failing cases.
    - **§S19(d)'s stated consequence was overstated and is corrected in place.** The
      floor would *not* have silently disappeared: `gen-ac-floor.ps1` hard-errors on an
      empty list and runs as a CMake custom command, so the native build fails. What
      was unguarded is the *schema* half.
  - **The shipped `detection-rules.json` carried a fourth statement of the gate's
    composition**, omitting the services tier — the only one ever measured firing on
    real anti-cheat — in the one copy that reaches users. Closed the way §S23-4 closed
    the same class: by **removing** the restatement, not correcting it, with
    `rules-validate` failing if any `$comment` enumerates checks again.
    - **That rule's first version could not fire, which is the finding.** It was scoped
      to `anticheat.$comment`; the text lives in the **top-level** `$comment`. A check
      pointed at the wrong object — this project's signature defect, committed inside
      the fix for it. It now walks every `$comment` in the document **and fails if it
      finds none**, because a walk reporting clean having looked nowhere is the same
      defect one layer up.
    - **Its canary reported green twice before the cause was found.** The first time,
      a backtick inside a double-quoted PowerShell needle silently mangled the search
      string, so the mutation never applied and the validator was correctly passing on
      unmodified data. That is the sixth time on this project that the verification
      harness was the broken thing, and the sixth time it reported success.
  - **`vklayer-blastradius.ps1` case 3 is an assertion instead of a printout.** It
    tested whether the Vulkan loader compares `enable_environment`'s **value** or
    merely its existence — the difference between a stray `set
    FRAMELEDGER_ENABLE_VK_LAYER=0` doing nothing and it mapping FrameLedger into every
    Vulkan process on the machine — and printed in both branches, never touching
    `$errors`. Being an observation was correct *while the answer was unknown*; it was
    measured on 2026-08-02 and recorded as settled, and the step kept printing in green
    either way. **When a measurement becomes a recorded fact, the step that produced it
    has to become the thing that defends it.** Still only runs by hand: the script
    writes `HKCU` and is excluded from `build.ps1` and CI by design.

- **Occlusion probes were reaching the ring as frames** (#48). `DXGI_PRESENT_TEST` runs the
  presentation test and **submits nothing**. The writer recorded them like any other present,
  so a minimised or fully occluded game — which issues them continuously — produced records
  `03_METRICS` would have turned into a frame rate it was not rendering, and into frame-time
  intervals bounding no frame.
  - **Responsibility for filtering was assigned to nobody.** `07_IPC` did not say, and
    `03_METRICS` lists `presentFlags` among the consumed fields while being silent on this
    value. The harness's own history is why that matters: every present in `hook-harness` was
    once a probe, which made "N presents → N records" satisfiable **only** by a writer that
    counts non-frames.
  - **Decided: the writer drops them**, so the ring means one thing. The filter sits *after*
    the safety checks, so a probe-only process still evaluates the stop rather than going
    unsupervised because it stopped drawing.
  - **Measured, both directions.** `hook-harness --hold-presenting 12` *without* `--real` — a
    live, hooked, supervised target presenting nothing but probes — puts **142 records** in
    the ring against the pre-fix writer and **0** after. The test asserts `status == READY`
    and `faultCount == 0` alongside the count, so "empty because we unhooked" and "empty
    because we faulted" cannot pass for the right answer.
  - **Its commit message is the wrong PR's**, verbatim — see the note at the top of this
    section. This entry was reconstructed from the diff and from §S26.

- **The safety stop could not fire in a game that had stopped presenting** (#46). Every
  runtime safety decision in the Overlay lived behind `MayObserve()`, whose only caller is
  `RecordPresent` — so a game that had hung, been alt-tabbed, or was sitting in a menu never
  read `unhookRequested` and never evaluated the `guardTicks` deadline. The hooks stayed
  patched in for the life of the process.
  - `fl_shm.h` says over `FL_GUARD_TICK_DEADLINE_MS`, in capitals, that this must **not** be
    driven by the present hook — "the clock would stop when presents stop, which is the exact
    scenario this exists for". A normative comment prescribing the opposite of the code
    beneath it, which is §S21's `MoveFileEx`/`ReplaceFileW` shape again.
  - **Measured:** `unhookRequested = 1` against a live injected `hook-harness --hold` left
    `status` at `READY` through 10 s of polling. **The exposure stated precisely rather than
    inflated:** a process that is not presenting is also not *recording*, so nothing false
    was written; what failed is the clean unhook `19_SAFETY` requires and `DISCLAIMER` §2
    promises.
  - **`pauseRequested` was unreachable on any frame where `guardTicks` changed.** The
    tick-freshness check sat between the safety stop and the pause check and returned true as
    soon as the tick differed. Measured: **12 leaked records across 12 guard ticks**, exactly
    one per tick. At the real 30 s cadence each leaked record carries a `qpc` ~30 s after its
    predecessor — a **fabricated 30-second frame interval** in the series `03_METRICS`
    computes 1% and 0.1% lows from. Latent only because nothing writes `pauseRequested` yet.
  - **The fix is a watchdog thread, and why a thread is acceptable *here* is written down.**
    §S2 rejected a worker thread for the Vulkan layer for three reasons, **all of which are
    properties of the layer**: the loader owns its mapping, the re-scan allocates ~1.15 MB
    transiently, and it would probe the SCM from inside a game — the behavioural signature of
    anti-analysis code (rule 3). None applies to the Overlay. This thread enumerates nothing,
    probes nothing and allocates nothing: two `uint32` reads from our own mapping, then
    sleep. **A watchdog that starts scanning is a different object under rule 3.**
  - Moving the deadline off the present path fixes the pause leak *by construction* — there
    is no early return left to jump over the check. `StopObserving` became a
    compare-exchange so the **first** reason wins; which reason was recorded would otherwise
    have depended on thread scheduling.
  - **Fixed without a test, and saying so:** `NoteFault` discarded `MH_DisableHook`'s return
    value and stored `SELF_DISABLED` unconditionally while setting no `g_observing`. It now
    routes through `StopObserving` — but **the three-fault path still has no test at all**,
    and the blocker is the vehicle. Both rejected approaches are recorded in
    `src/native/tests/CMakeLists.txt` so they are not rediscovered.

- **The `trustedSigners` gate was polarity-inverted, and its own comment claimed
  the capability the code structurally could not have.** Added one commit earlier
  (`cea744e`), which is what makes it worth recording rather than quietly fixing.
  `rules-publish.yml` fed `heuristic.trustedSigners` into `Get-Tokens`, whose only
  consumer is `$removed = old − new`. A token that appears only in the *new* file
  can never be in old-minus-new, so an **addition** — the direction that suppresses
  refusals, and the one the comment named — always passed. Meanwhile *removals*
  did fire, and removing a trusted signer makes the guard **stricter**. The gate
  blocked the safe direction and waved through the dangerous one, under a comment
  reading "§S19(d) already records that rules-publish cannot see such an addition.
  It can now."
  - `trustedSigners` now has its own `$new − $old` comparison, because it is the
    one ALLOW-widening list and shares no polarity with the five groups or with
    `nameFragments`. Those stay on the removal check, which is right for them.
  - Proven by **extracting the shipped step from the YAML and running it**, rather
    than re-implementing it — a second copy of a check is a second checker that
    can disagree. Five cases against the real seed, before and after: adding a
    signer went `PASS → FAIL`, removing one went `FAIL → PASS`, and the three
    pre-existing cases (unchanged, a removed blocklist value, a removed
    `nameFragment`) were unaffected in both directions.
- **`legal/THIRD_PARTY_NOTICES.md` asserted two bundled components this
  repository does not contain**, in the one document the EULA incorporates by
  reference. NVIDIA NVAPI said *"**Yes** — headers and import library vendored.
  **Verified 2026-08-02**"*; `src/native/third_party/` holds `CMakeLists.txt` and
  `vulkan-headers` and nothing else, and the only `nvapi` path in the tree is the
  licence copy. Intel PresentMon said *"Bundled as a pinned native binary; SHA-256
  verified at build"*; `assets/` does not exist. `docs/12_BUILD.md` repeated both.
  - **Not a licence violation — over-disclosure**, which is its own defect in a
    document a user relies on, and the same shape as the privacy policy disclosing
    a weekly network request that did not exist. The NVAPI *licence* verification
    was real and is kept; only the *bundling* claim was false.
  - **The licence gate could not have caught either.** `license-check.ps1` keyed
    its check on the directory a component *would* occupy, so it fires on
    vendored-without-a-licence and never on claimed-vendored-but-absent — a gate
    whose verdict is decided before it looks, inside `legal/`, which §S23-6
    already records as audited by nothing.
  - It now cross-checks each component's table row against the filesystem
    **bidirectionally**: claimed-but-absent fails, and present-but-still-marked
    "Not yet" fails too, because a one-way check goes quiet the day someone
    vendors a component and forgets the notice. A renamed row **fails rather than
    skips**. Four canaries, each proven red, plus the clean tree proven green.
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

- **The blocklist had no Anti-Cheat Expert family at all**, and a kernel-level
  anti-cheat was present on the dev machine with every check returning `Allow`
  (`spike-notes.md` §13). `ACE-BASE.sys` / `ACE-ADVT.sys` under `System32\drivers`,
  the service `AntiCheatExpert Protection`, and a driver the game ships inside its
  own install tree — none of them matched anything.
  - Anti-Cheat Expert added to `drivers`, `services` and `files`; the unmeasured
    sibling names are marked as unmeasured rather than presented as evidence.
  - **Easy Anti-Cheat gained a `drivers` row for a sharper reason**:
    `EasyAntiCheat_EOSSys` was measured **Running as a kernel driver** during a
    live EAC session and matched nothing in that group. The refusal came from the
    service check instead — and something else firing is exactly what makes such
    a gap invisible.
- **The static pre-scan could not reach a driver the game ships two levels down**
  (`kMaxPreScanDepth` 2 → 3). Adding the blocklist row above changed nothing from
  the install root, which is where check 4 actually runs; it only fired when the
  scan was started from a subdirectory. Measured with a control tree at increasing
  depth, and the cost measured before the value moved because the entry cap is a
  refusal, not a truncation: worst case across 67 installed titles is 506 entries
  at the old reach and 729 at the new one, against a 4096 budget.
  - Re-run over all 67 titles afterwards: 65 `Allow`, exactly the two anti-cheat
    titles refused, **0** `PreScanFailed`.

- **Four gates were repaired, each proven red afterwards.** All four passed on a
  clean tree and would have passed on the input they exist to catch.
  - `chokepoint-check.ps1` exempted the chokepoint from **all ten** forbidden
    patterns, including the six evasion primitives, above a comment saying "only
    for the primitive itself". Appending `ZwSetInformationThread` to
    `fl_guard.cpp` — the likeliest place for it — passed. Split into
    chokepoint-only and forbidden-everywhere, and a **managed pass** added: the
    check only ever scanned `src/native`, so the same Win32 calls reached from
    C# were invisible.
  - `versioninfo-check.ps1` short-circuited on an **empty** `OriginalFilename`,
    so a binary carrying none — the least identifiable state there is — passed
    the gate whose purpose is that our binaries name themselves. Proven with a
    fixture DLL valid in every other field.
  - `coverage-gate.ps1` maximised the rate and the line count **independently
    across reports**, so it printed a pair no run produced. It also counted each
    line twice (`.//line` matches class-level and method-level elements) and
    keyed on `filename`, which two of the three reports spell differently. Now a
    line-level union keyed on the class name: `FrameLedger.Domain` was reported
    as **89.1% over 422 lines** and is actually **93.8% over 211**.
  - `rules-publish.yml` compared family **names** only, so gutting a family's
    values one token at a time passed CI — and shrank the §S21 compiled-in floor,
    which is generated from that file. Now compares values.
- **`build.ps1` declares the struct-mirror gate and skips it loudly.** Nine files
  describe that gate in the present tense, including `fl_shm.h`, which is
  normative. It does not exist. A named skip is honest; silence reads as coverage.
  > **Superseded by #47** — the mirror, the test and a `.trx`-backed gate all exist.
  > Left in place because it is the record of a past PR, with the pointer added
  > because as written it reads as a live status claim.
- Documentation corrected where it claimed capabilities that do not exist: the
  Overlay's `LoadLibrary` hook (§S6 and `19_SAFETY` both said "already installs" —
  the Overlay installs nothing), `GuardSupervisor` "publishes `guardTicks`"
  (nothing maps the shared memory; it has no production caller), `FL_MOCK`
  (specified in four places, implemented nowhere), `12_BUILD`'s native-copy bullet
  (wrong in four ways, including naming a binary the same document says does not
  exist), and `build.ps1`'s own "nine-gate" self-description.
  > **Two of those are now half-true, and the halves matter.** #50 made
  > `ShmRingReader.PublishGuardResult` map the shared memory and write `guardTicks`,
  > so "nothing maps the shared memory" is false and only the *production caller* is
  > still missing — a missing loop, not a missing subsystem. #40–#44 gave the Overlay
  > three real `MH_CreateHook` calls, so "the Overlay installs nothing" is false too;
  > the `LoadLibrary` hook specifically is still unwritten. `FL_MOCK` and the
  > `12_BUILD` bullet are unchanged.
- **`legal/` carries accuracy notes** where it promises behaviour the software
  does not have: the 30-second in-session re-scan and stop, the two-crash
  auto-disable, and a **weekly outbound safety-list request** in the privacy
  policy. Over-disclosure in a document the user relies on is a defect in the
  same way an omission is.

- **The shm layout's unmade decisions are made, while they are still free.** Five
  of them, all in the last window before a C# mirror exists — after that the same
  changes cost a `FL_SHM_LAYOUT_VERSION` bump, which `fl_shm.h` defines as
  user-visible: the Agent refuses to attach and tells the user to restart the game.
  - **`measuredMask`** (was `_pad0` @39) distinguishes "we looked and there was
    none" from "we did not look". The zero-defaults are affirmative negatives, so
    a present-only writer with no feature hooks would have asserted "no upscaler,
    no FG, no ray tracing" as measured fact 118 times a second — producing
    `fg_factor 1.0`, the single inflated number CLAUDE.md rule 6 forbids, and a
    definite RT `No`, which rule 7 forbids. `FL_RT_NOT_MEASURED` is the same fix
    inside `rtFlags`.
  - **`swapchainId`** (was `_pad1` @60). One hook sees every swapchain in the
    process — patching a vtable slot patches the shared `dxgi.dll` class vtable,
    measured identical across five configurations — so a title with a separate UI
    swapchain inflates `F_disp` and nothing could tell the streams apart.
  - **`adapterLuid` is published at first present, not at init**, and `0` now
    means "not yet known". The handshake is documented write-once at init, which
    is two steps before the graphics modules are even resolved; our throwaway
    dummy device's adapter is not the game's.
  - **`buildId` has a producer**: `FL_BUILD_ID`, from `git describe`. It had none
    — three references in the tree, all declaration or dump — while `07_IPC` makes
    a mismatch a hard refuse-to-attach. A check whose input nobody writes compares
    `""` with `""` forever. `fl_shm_layout` now fails if it is missing, empty, or
    too long for the field.
  - **The supervision deadline is 65 s** (`FL_GUARD_TICK_DEADLINE_MS`). `07_IPC`
    §Supervision loss depended on a number it never stated. 65 s is two missed
    30 s scans: ~35 s would end a session on one late tick, and a tick is late
    whenever the machine is busy. The cost — the worst-case unsupervised window
    doubles — is now in `legal/DISCLAIMER.md` in those words rather than left to
    imply 30.

### Known issues
- **A real VAC title is allowed by the guard today** (`spike-notes.md` §13).
  Counter-Strike 2 measures `Allow`: VAC is neither a machine-wide driver nor a
  service, it is modules inside the game process — and that process denies module
  enumeration (`EnumProcessModulesEx` → `ERROR_ACCESS_DENIED`, even though
  `OpenProcess` succeeds). The route `19_SAFETY` reserves for VAC is
  `blockedStoreIds` — check 3's **store-id half**, which cannot be called (§S14). #52
  wired check 3's *executable* half; the conclusion for VAC is unchanged, and the
  reason is now narrower than "check 3 is unwired". A renamed exe would defeat the
  executable half anyway, which is why `19_SAFETY` reserves the store-id route.
- **While any Easy Anti-Cheat title is running, the guard refuses every target on
  the machine** — measured against a freshly spawned, completely unrelated
  process. Checks 2 and 2b do not depend on the target, so this is the intended
  fail-closed posture for a live anti-cheat driver, but the user-facing text has
  to explain it or it reads as a bug: the signal names a game the user may not be
  playing.
- **The module and driver tiers have still never been observed to fire.** The one
  title here whose modules are readable carries none, EAC-protected processes deny
  enumeration outright, and the new driver rows were added from installed-state
  evidence while the drivers themselves were Stopped. Data-complete, behaviourally
  untested.
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
    > **It has now been shown, and by CI rather than by argument** (#51). Running
    > the drain integration test: `the guard refused our own harness:
    > SuspiciousUnsigned unknown System.Security.Cryptography.ProtectedData.dll`.
    > The mechanism is the one the paragraph above ruled out — §S16 puts the
    > target's **ancestors** in the scan set, and a .NET test host is the
    > harness's parent, which is the launch-mode arrangement. A .NET host that
    > loads that assembly poisons its own scan set and the injection it is
    > attempting is refused: **a gate that cannot pass.** Attach mode is
    > unaffected. The consequence is live in the merge gate — the drain tests are
    > `Category=Integration` and CI runs `-SkipIntegration`, so **the only
    > end-to-end proof of the capture path never runs on the machine that gates
    > merges.**
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
- ~~**The honesty contract protecting the not-yet-measured fields is not in the merge
  gate.**~~ **Wrong, and corrected 2026-08-05 — see §S29(a).** The assertion is in the
  **native** suite and CI runs it: `guard_test.cpp`'s *"the injected Overlay records
  real presents into the ring"* injects into `hook-harness` and requires
  `measuredMask == FL_MEASURED_OUTPUT_RES` on every drained record. It is ctest
  `fl_guard`, 20.58 s on CI. `fl_guard_test.exe` is a native host, so it never loads
  the `protect`-matching assembly a .NET host does — which is precisely why it runs
  and `ShmDrainIntegrationTests` does not.
  - **What is genuinely ungated, stated narrowly this time:** the *managed* drain —
    `ShmRingReader`, the handshake validator against a live writer, and the
    `PublishGuardResult` round trip. Fixing §S19(b) buys that. It is **not** a
    prerequisite of the feature hooks, and the earlier wording was used to re-order
    the work before it was checked.
- **`ctest fl_vtable_indices` does not pin the Overlay's vtable indices.**
  `hook-harness` declares `kPresentIndex = 8` / `kResizeBuffersIndex = 13` /
  `kPresent1Index = 22` as its own literals, textually duplicated from the inline
  values in `dllmain.cpp` with no shared header. Change the Overlay's 8 to a 9 and
  the ctest still passes: it proves a fact about `dxgi.dll`, not a fact about
  `FrameLedger.Overlay`. The only test coupling the two is the integration class CI
  skips, so in the merge gate the coupling is absent entirely.
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
