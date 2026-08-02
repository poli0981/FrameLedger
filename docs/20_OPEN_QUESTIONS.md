# 20 — Open questions

Defects and gaps found by auditing docs `01`–`19` that **cannot be closed by
editing prose**. Each needs either an empirical answer from the P0 spike or a
design decision. `15_ROADMAP` names `docs/spike-notes.md` as where P0 writes the
answers; this file holds the questions.

Rules for this document:

- An item leaves this file only when it is **answered**, not when it is
  rephrased. The answer goes in the owning doc, and the entry here is deleted
  with a pointer in the commit message.
- Every S-series item is a **safety** item and blocks the first real injection.
- Nothing here is a known bug in shipped code — there is no code yet. That is
  the point: these are cheap now and expensive in P1.

---

## Scope decisions already taken (recorded, not open)

| # | Decision | Consequence |
|---|---|---|
| — | **D3D9 is not a Tier-1 API in v1.** The Overlay is x64-only; an x64 DLL cannot load into a 32-bit process, and D3D9 titles are almost entirely 32-bit | The VN / JRPG / older-indie catalogue is Tier 2. Reversing this means a second 32-bit Overlay **and** injector, doubling the native build matrix and adding a second struct-mirror surface. Revisit only with evidence that users care more about those titles than about the maintenance cost |
| — | **`ci.yml` is repo-local**, not a caller stub | The ops repo's `reusable-desktop-csharp.yml` runs `dotnet` directly with no native pre-step input, and `12_BUILD` requires CI and local to run the same script |
| — | **No `v1 → v2` migration** | Nothing shipped, so no such database exists. `0001_init.sql` creates the current schema |
| — | **Tier 2 requires an elevated Agent** | An unelevated Agent whose Tier-1 attempt fails lands on Tier 3. Stated in the README, Disclaimer and EULA |

---

## S — Safety. Blocks the first real injection.

The project's central promise is that injection is opt-in per game, the guard is
a hard gate with **no override anywhere**, and no evasion is ever implemented.
Each item below is a place where the documents themselves leak a gap.

### S1 · The guard is structurally blind in launch mode

`04_CAPTURE` §Launch mode prefers `CreateProcess(CREATE_SUSPENDED)` → guard →
inject → `ResumeThread`. But a suspended process has **loaded no modules yet**,
so `EnumProcessModulesEx` returns essentially nothing and the primary
pre-injection check (`19_SAFETY` §Pre-injection checks item 1) is a no-op in the
*preferred* path. Anti-cheat that would have been caught in attach mode sails
through in launch mode.

**Proposed:** treat the static pre-scan and the driver scan as the gates that run
while suspended; resume, then re-run the module scan and only install hooks once
it passes and the first present is observed. This costs the early-init upscaler
data launch mode exists to capture — quantify that loss in P0 before accepting
it. Decide whether the answer is "inject late" or "no launch mode at all".

### S2 · The Vulkan layer has no guard and no mid-session unhook

An implicit layer is machine-wide and loads **before** anything of ours runs.
`17_HOOK_ENGINE` §Vulkan gives it an opt-in enable-list check, but that is not
the guard: no module scan, no driver scan, no blocklist, no multiplayer
heuristic. And `19_SAFETY` §During a session — "the single most important runtime
behavior in the whole capture layer" — is Agent-driven and Overlay-targeted; with
the Agent not running there is no runtime guard inside a layered process at all.

**Proposed:** use `enable_environment` in the layer manifest so the Vulkan loader
does not map the layer unless the Agent sets the variable when launching the
game. That closes the hole properly, at the cost of making **Vulkan Tier 1
launch-mode-only**. Additionally, the layer must run the blocklist module scan on
its own process at init and go passthrough on any hit. Also specify the
enable-list file itself (S5).

### S3 · `UpdateRules { path }` is a documented override of the hard gate

`07_IPC` §Messages lets the UI hand the Agent an arbitrary filesystem path to
load detection **and anti-cheat** rules from. Anything that can send on that pipe
can therefore replace the blocklist with an empty one. That is precisely the
"config-file flag" `19_SAFETY` says does not exist.

**Fix:** remove the `path` parameter. `UpdateRules` becomes a no-payload trigger;
the Agent reads only from its own `%LOCALAPPDATA%\FrameLedger\rules\` copy.

### S4 · The enable-list, and the rules feed generally, have no trust model

Three related gaps: the Vulkan opt-in list is referenced but never specified
(location, format, ACL, who writes it); `detection-rules.json` is fetched over
HTTPS from a raw GitHub URL with no signature and no staleness bound; and no doc
says what happens when the local rules file is older than some threshold. A gate
whose data can go stale indefinitely, silently, is a gate with an expiry date
nobody sees.

**Needs:** a specified file format and ACL for the enable-list; a decision on
signing the rules file; and a staleness policy — most likely "warn in the UI past
N days, never auto-disable the blocklist".

### S5 · `detection-rules.json` is not specified to validator grade

`05_DETECTION` shows an abbreviated shape. `12_BUILD` and `13_CI_CD` both gate on
`tools/rules-validate`, and `13_CI_CD` demands extra scrutiny for the `anticheat`
block — but the block's schema (`modules`, `drivers`, `blockedExecutables`,
`blockedStoreIds`) is not defined tightly enough to write that validator.
Separately, the `19_SAFETY` §Blocklist seed table expresses signals the schema
cannot represent: prefix matches, directory presence, service names, and the
"unknown-but-suspicious" heuristic.

**Needs:** a full JSON Schema, and a seed file that the schema accepts. A
non-empty `anticheat` block is a ship requirement — an empty one is a fail-closed
test fixture, not a valid state.

### S6 · The 30 s scan window is the weakest part of the most important behavior

Now disclosed honestly in the Disclaimer and README. The open question is whether
to shrink it. The Overlay **already installs a `LoadLibrary` hook** for lazily
loaded graphics DLLs (`17_HOOK_ENGINE` §DLL entry); the same hook could raise the
control-block flag the moment a blocklisted module name loads, turning a 30 s
poll into near-immediate detection. This is the cheapest available improvement to
the behavior the product treats as most critical.

**Needs:** confirmation that a name comparison against a small fixed table is
acceptable in that hook under the no-allocation rule (it should be), and a
decision on whether it supplements or replaces the poll. Supplements — the poll
also catches modules loaded before we hooked.

### S7 · Guard handle rights and WOW64

`19_SAFETY` specifies `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ` for
the module scan. Verify this is sufficient for `EnumProcessModulesEx` on the
targets we care about. Separately, **enumerating a 32-bit process from a 64-bit
one requires `LIST_MODULES_ALL`** or the list comes back empty — and an empty
module list must never read as "clean". `14_TESTING` already requires a
partial-module-list test to fail closed; add the WOW64 case explicitly.

### S8 · The absence-of-override test design does not work as described

`14_TESTING` proposes making the bug unrepresentable via "a `sealed` token type
that only the guard can produce", required as a constructor argument. In C#
nothing stops another type in the same assembly from constructing it, and
`internal` does not prevent it either. The *intent* is right and worth keeping.

**Needs:** a mechanism that actually holds — e.g. a private nested constructor
plus a factory the guard alone owns, an analyzer/architecture test asserting no
call path reaches `Injector.Attach` without a guard result, or both. Pick one and
write it down before `Injector` exists; retrofitting is how the override path
gets born.

### S9 · `FrameLedger.Injector.exe` ships as a standalone injector

`12_BUILD` builds a "thin CLI used by the Agent and for manual testing" and
ships it in the package. As a user-runnable `LoadLibraryW` injector outside
anything the managed guard constrains, it is both a bypass and a bad look for a
project whose position is "we are plainly identifiable and we refuse where we are
not welcome".

**Needs:** decide between (a) not shipping it — dev-only, excluded from the
Velopack package, or (b) shipping it with the guard compiled in and no flag that
skips it. Also define its invocation contract and how a guard result crosses the
process boundary unforgeably.

### S10 · `--register-vklayer` registers a machine-wide layer with no consent

`12_BUILD` lists the flag, `08_UI` puts a Register button in Settings, and
`12_BUILD` §Bundled assets registers the layer at **install time**. All three
register a machine-wide component before any game is enabled and before any
consent dialog. Reconcile with `17_HOOK_ENGINE`'s "registered only while at least
one Vulkan game has hooking enabled", which is the correct rule.

### S11 · FR-2.2 and FR-2.3 have no specified interaction

FR-2.2 (static pre-scan disables the toggle) and FR-2.3 (runtime guard refuses)
are each clear, but nothing says what happens when a game **already enabled**
later trips the static scan — e.g. a patch adds anti-cheat, or updated rules
newly match it. Silently leaving `hook_enabled = 1` is the dangerous reading.

**Proposed:** re-run the static pre-scan on every rules update and on every exe
change, and force `hook_enabled = 0` with `hook_blocked_reason` set, surfaced as
a persistent notice. Requires a new column or a reuse rule for
`hook_blocked_reason`.

### S12 · Breadcrumb / "cautious mode" is a one-line orphan

`19_SAFETY` §Crash safety describes a breadcrumb file and a "cautious mode"
(hooks installed, overlay drawing disabled) for the run after an unexplained
death. No other doc mentions either, and **in v1 there is no overlay drawing to
disable** (FR-15 is v1.1), so cautious mode as written is a no-op.

**Needs:** either specify what v1 cautious mode actually does differently
(plausibly: install present hooks only, no feature hooks) or defer it to v1.1
with the overlay and say so.

---

## H — Native hook layer. Blocks P1.

### H1 · `/guard:cf` and MinHook trampolines

`17_HOOK_ENGINE` §Build profile mandates `/guard:cf` on the Overlay, and
`12_BUILD` enforces it in CMake. In a CFG-enabled host process, an indirect call
to a MinHook trampoline — memory allocated at runtime and never registered as a
valid call target — triggers `__fastfail(FAST_FAIL_GUARD_ICALL_CHECK_FAILURE)`,
i.e. it takes the game down. That directly contradicts NFR-3.

**P0 must determine:** whether MinHook's allocations need
`SetProcessValidCallTargets`, whether the trampolines are reached by direct
rather than indirect calls in practice (in which case CFG never checks them), or
whether `/guard:cf` must be dropped for this target with the rationale recorded.
Test in a host built with CFG, not just the harness default.

### H2 · `LoadLibrary` hook, the loader lock, and MinHook's thread suspension

`17_HOOK_ENGINE` §DLL entry now states the rule — enqueue, install off the init
thread — but it is unproven. MinHook suspends all threads to patch; a
`LoadLibrary` hook body runs under the loader lock; any suspended thread holding
or waiting on that lock deadlocks the game. Prove the deferred-install path under
a game that loads D3D12 lazily, which is the case the hook exists for.

### H3 · `-D_HAS_EXCEPTIONS=0` with `<atomic>`

`17_HOOK_ENGINE` and `12_BUILD` both mandate it for the Overlay target. This is
not a configuration Microsoft's STL supports; `<atomic>` and other headers can
fail to compile or behave unexpectedly. Since the ring writer is built on
`std::atomic_ref`, this is load-bearing.

**P0 must determine:** whether the combination compiles cleanly on the pinned
MSVC, or whether to drop the define and rely on `/EHsc-` plus a no-throw
discipline enforced by review.

### H4 · Vtable index verification

`17_HOOK_ENGINE` now records why "verified at runtime against the dummy object"
is not implementable — a vtable slot is a bare function pointer with no identity.
What remains open is what to do instead. The `hook-harness` test on a known
runtime is the honest answer, but note `14_TESTING` expects the probe to run **on
the CI runner**, and a headless GitHub runner has no GPU, no DXGI output and no
Vulkan ICD. Decide whether CI uses WARP, whether the test is local-only, or
whether the assertion is compile-time against the SDK headers.

### H5 · Proxy swapchains defeat the dummy-vtable assumption

Streamline, ReShade and similar wrap `IDXGISwapChain` in their own COM object.
Our dummy-object probe reads the vtable of a *real* swapchain we created, then
patches that vtable — but the game may be presenting through a proxy whose vtable
we never touched, or the proxy may sit above us and see different calls. This
bites hardest on **DLSS-G titles**, which is exactly the population this rewrite
targets, since Streamline is the common integration path.

**P0 must measure this on a real Streamline title** before P1 commits to the
vtable-swap strategy — and on a machine with RTSS and the Steam overlay active,
which is the realistic case rather than the clean-room one.

### H6 · D3D12 command-list hooks count recorded, not executed, work

`DispatchRays`, `BuildRaytracingAccelerationStructure` and pipeline creation are
recorded into command lists, possibly on many threads, possibly re-executed or
never executed. `17_HOOK_ENGINE` §Ring writer keeps per-frame counters in "a
small struct updated by the feature hooks and *read* by the present hook" with no
synchronisation specified — a data race as written, and semantically it counts
recording rather than execution.

**Needs:** a specified concurrency model (per-thread counters aggregated at
present is the obvious one) and an explicit statement that RT activity means
"recorded this frame", with the accuracy budget in `03_METRICS` adjusted to say
so.

### H7 · Vtable restore on unhook clobbers later hookers

`17_HOOK_ENGINE` §Unhooking restores the original vtable entries. If another
overlay (RTSS, Discord, Steam) hooked the same slot *after* us, restoring the
original silently removes their hook. The doc's claim that vtable swapping gives
a "cleaner uninstall" is backwards in the multi-overlay case, which is the common
case on a gamer's machine.

**Needs:** compare-and-restore-only-if-unchanged, and a documented behaviour when
the slot has changed (leave it, go dormant — we already stay loaded).

> **Testable today.** The dev machine already runs RTSS, OBS, Steam Overlay,
> Steam Fossilize, EOS Overlay and GOG Galaxy Overlay (`spike-notes.md`
> §Environment). RTSS and the Steam overlay both hook D3D presentation
> in-process, so the "someone else hooked after us" case does not need to be
> simulated — it is the default state of that machine.

### H8 · "Never crash the game" is over-promised

`FL_HOOK_GUARD` catches SEH exceptions in our code. It cannot catch stack
overflow reliably, cannot intercept `__fastfail`, and does not help when the game
installs a vectored exception handler that runs first. NFR-3 says the Overlay
"must never crash a game", which is not a claim the mechanism can support.

**Needs:** reword NFR-3 to what is actually guaranteed (faults in our code are
contained and self-disable after three), and keep the absolute promise out of
user-facing text.

### H9 · `SetColorSpace1` is on `IDXGISwapChain3`

`17_HOOK_ENGINE` §Hook inventory attributes it to `IDXGISwapChain4`. Minor, but
it is a hook index — wrong interface, wrong vtable length assumption.

### H10 · Per-process VRAM thread inside the game process

`17_HOOK_ENGINE` §Memory calls `QueryVideoMemoryInfo` "once per second from our
own thread". Spawning a thread inside a host process we do not own is a heavier
footprint than the rest of the design's posture, and DXGI calls off the render
thread need checking. Consider sampling on the present hook every N frames
instead, which needs no thread at all.

---

## M — Metrics and telemetry. Blocks P2.

| # | Question |
|---|---|
| M1 | Can PresentMon 2.x `FrameType` see **driver-level** frame generation (AMD AFMF)? `03_METRICS` now says v1 cannot detect it at Tier 1. If Tier 2 can, that is a genuine and surprising capability inversion worth surfacing in the UI |
| M2 | Does the pinned **PresentMon console binary** still exist as a bundleable artifact, run unelevated, and emit the 2.x column set over stdout? `15_ROADMAP` parks the Service + API2 in v2, so there is no planned fallback if the console is gone |
| M3 | **NVAPI licensing and availability.** Is the exact vendored artifact — headers **and** `nvapi64.lib` — MIT, with SPDX blocks intact? The import library may not be covered by the same grant as the headers. If this fails, Reflex/PC latency has no alternative source and FR-4.6, the latency tab and `sessions.latency_*` become permanently `N/A`. Do **not** fall back to ordinal resolution |
| M4 | **LHM MPL-2.0 Exhibit B.** Does any depended-upon file carry "Incompatible With Secondary Licenses"? A ten-minute grep with a week-scale consequence: if it does, the L2 telemetry layer is unusable for **all three** vendors, and IGCL and ADLX are already rejected. **Run this in the first hour of P0** |
| M5 | **Do LHM GPU sensors work unelevated, without PawnIO?** This decides whether the default unelevated Agent has temperatures at all, and therefore how ADR-9 reads to users |
| M6 | Can a documented one-time "add me to Performance Log Users" step, or the PresentMon Service, restore genuinely elevation-free Tier 2? Would change the README, Disclaimer and EULA wording back |
| M7 | `18_GPU_VENDOR_APIS` §Runtime policy says telemetry is never read from the game process, but `17_HOOK_ENGINE` reads per-process VRAM and Reflex latency there. Reconcile the wording — the rule means "no vendor SDK polling loops in the game", not "no measurement in the game" |
| M8 | The `GpuSample` type has no latency field, yet L3 is credited with Reflex/PC latency. Latency is per-frame and arrives via the ring, not the 1 Hz sample. Fix the layering description |
| M9 | The **P0 accuracy baseline does not exist.** `15_ROADMAP` requires comparing against "what the *old* file/module-based detection reported" and putting the improvement in the README — but no prior implementation is in this repository. Either locate it, or add "build a minimal static-hint detector as the baseline" to P0 scope. Without it the headline justification for ADR-7 is unfalsifiable |
| M10 | PDH `\GPU Engine(*)\Utilization Percentage` summed across engines does not reproduce the Task Manager figure the doc invokes. Decide what we actually report and label it accordingly |

---

## G — Missing specifications. Block P2–P4.

Each of these is referenced by an existing doc but specified nowhere.

| Area | What is missing | Referenced by |
|---|---|---|
| **Agent lifecycle** | Scheduled-task definition, start-at-logon, how elevation is requested and persisted, what "Repair" repairs | `08_UI` Settings, `11_UPDATER`, `12_BUILD` flags |
| **Session identity** | `sessions` has no GUID column, yet `SessionStarted`/`StopSession`/`.partial` files are all keyed by `sessionGuid`. Also: the `.partial` file format is undefined, and it is the crash-recovery artifact | `07_IPC`, `04_CAPTURE`, `06_DATA_MODEL` |
| **Settings registry** | The `settings` table is key/value with no key list, defaults, types, or validation — and no message for the UI to push a changed setting to the Agent | `06_DATA_MODEL`, FR-10 |
| **Error taxonomy** | `07_IPC` lists `CaptureError` codes; no canonical mapping to resx keys and user-facing text, though `09_I18N` requires safety strings to be reviewed as legal text | `07_IPC`, `09_I18N` |
| **Threading model** | Which component owns which thread, UI-thread rules, and how the 1 Hz telemetry poller, 10 Hz drain and pipe reader interact | `04_CAPTURE`, `18_GPU_VENDOR_APIS` |
| **Pipe/shm security** | Stated as policy ("DACL granting the current user's SID") but not as implementable SDDL, nor who creates the objects with what rights | `07_IPC` |
| **Legal doc versioning** | FR-11 re-shows documents "when a document version increments". Nothing says where the current version lives, who writes `legal_acceptance.version`, or how the markdown is rendered in-app | FR-11, `06_DATA_MODEL` |
| **Accessibility / DPI** | NFR-9 states requirements; no design anywhere | NFR-9, `08_UI` |
| **Uninstall / data deletion** | One Velopack clause. No in-app "delete all my data", despite the privacy position | `12_BUILD`, `legal/PRIVACY_POLICY.md` |
| **Cover art** | Appears in the schema, the data directory and the UI; nothing says where it comes from or the licence position on downloaded store art | `06_DATA_MODEL`, `05_DETECTION` |
| **PresentMon distribution** | `12_BUILD` says "bundled … pinned, SHA-256 verified at build" — is the binary committed to the repo or fetched at build time? Affects `.gitignore`, CI and the release package | `12_BUILD`, `THIRD_PARTY_NOTICES` |

---

## R — Roadmap resequencing

Fold into `15_ROADMAP` once agreed.

1. **The guard is P0 item 8, after two items that inject into real games.** This
   contradicts CLAUDE.md rule 2 and P1's own statement that the guard "ships
   before the first real injection, not after". Move it to item 0.
2. **Move the Vulkan layer passthrough test early** (currently item 7). A
   passthrough bug loads FrameLedger into every Vulkan process on the machine,
   including anything Vanguard-protected. It is the highest blast-radius item in
   the spike and it is scheduled seventh.
3. **Run the LHM Exhibit B and NVAPI licence checks first** (M3, M4). Both are
   pure reading, both can invalidate a whole telemetry layer, and neither needs
   hardware.
4. **P0's exit criteria silently import P2 work.** "Records a real session" with
   Agent CPU/RSS budgets needs the drain, aggregate and recorder paths P2
   delivers. Either scope a throwaway drain harness into P0 or move the
   FPS-impact criterion to the end of P1.
5. **P0 item 5 asks for an AFMF decision on an RTX 5080.** AFMF is AMD driver
   -side. Reword to "validate on NVIDIA; record AFMF as untested", per the
   precedent `14_TESTING` already sets for absent hardware.
6. **`18_GPU_VENDOR_APIS`'s capability matrix cannot be filled for AMD/Intel** on
   the stated dev machine, yet it drives what the UI advertises as available.
   Leave explicit "untested" markers rather than `?`.
7. **CI is scheduled for P5**, but CLAUDE.md makes green C# **and** C++ builds
   plus passing tests a per-PR gate from the first PR. CI belongs in bootstrap.
8. **P1 at 1.5 weeks is the largest under-estimate** — present hooks for three
   API families, feature hooks for four vendor SDKs, RT and PSO hooks, ring
   writer, fault policy, unhook path, native logging, injector launch *and*
   attach modes, plus the fully-tested guard that `19_SAFETY` calls the one
   component where a bug can cost someone an account.
9. **The `ja` safety-string reviewer is on the critical path but appears in P4.**
   `09_I18N` fails the build until a human signs off on safety translations.
   Identify the reviewer during bootstrap and draft the `Safety_*` keys as soon
   as the consent wording is stable.
