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
- ~~Nothing here is a known bug in shipped code — there is no code yet.~~ **No
  longer true, and the change matters for how this file reads.** There is a built
  guard, a Vulkan layer, a rules seeder and 11 registered ctests, and §S21 records
  a fail-open found in *shipped* guard code. Entries here are now a mix of open
  questions and recorded defects.
- **A ✅ entry is closed.** They are kept rather than deleted, with their
  reasoning, so counting entries overstates the open work — read the markers.

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

> **§S1 does not gate the Vulkan path.** Added 2026-08-03, because "launch mode
> is blocked by §S1 anyway" was being used as a reason to deprioritise §S18 and
> it is false for an entire Tier-1 API family. The Vulkan layer performs **no
> injection**: there is no `CREATE_SUSPENDED` target, no empty module list, and
> the session goes `Detected → Guarded → Capturing` without ever entering
> `Injecting`. Vulkan Tier 1 is nevertheless launch-mode-only
> (`17_HOOK_ENGINE:161` — only the launching process can set
> `FRAMELEDGER_ENABLE_VK_LAYER=1`), and §S18 was its sole blocker.
>
> **§S18 closed 2026-08-04, so that sentence is history.** What now blocks
> Vulkan Tier 1 is `vkQueuePresentKHR`, which is P1 and not started.

`04_CAPTURE` §Launch mode prefers `CreateProcess(CREATE_SUSPENDED)` → guard →
inject → `ResumeThread`. But a suspended process has **loaded no modules yet**,
so `EnumProcessModulesEx` returns essentially nothing and the primary
pre-injection check (`19_SAFETY` §Pre-injection checks item 1) is a no-op in the
*preferred* path. Anti-cheat that would have been caught in attach mode sails
through in launch mode.

> **Measured 2026-08-02** (`spike-notes.md` §1, ctest `fl_guard_apis`), and the
> result is sharper than this entry assumed. `EnumProcessModulesEx` against a
> `CREATE_SUSPENDED` target does **not** return an empty list — it *fails*, with
> `ERROR_PARTIAL_COPY (299)`. That is the safer of the two shapes: an error
> cannot be mistaken for a clean scan the way an empty success can. The rule
> ("any failure means REFUSE") is now in `19_SAFETY` item 1.
>
> So the *hazard* in this entry is downgraded — launch mode cannot silently
> pass a blind scan — but the *constraint* is confirmed: check 1 genuinely
> cannot run before the target's loader has. The decision below is still open.

**Proposed:** treat the static pre-scan and the driver scan as the gates that run
while suspended; resume, then re-run the module scan and only install hooks once
it passes and the first present is observed. This costs the early-init upscaler
data launch mode exists to capture — quantify that loss in P0 before accepting
it. Decide whether the answer is "inject late" or "no launch mode at all".

**Why the loss is still unquantified.** It needs a title that loads a
presentation runtime lazily. The local fixtures are `hook-harness` (creates D3D
at startup) and a 2D GOG prologue; a number from either would not generalise,
which is worse than recording it unmeasured. This is the one input §S13(c)
still lacks.

### S2 ◐ · The Vulkan layer has no guard — **both halves done; mid-session unhook still open**

An implicit layer is machine-wide and loads **before** anything of ours runs, so
the injection guard cannot cover it: no module scan, no driver scan, no
blocklist, no multiplayer heuristic. And `19_SAFETY` §During a session — "the
single most important runtime behavior in the whole capture layer" — is
Agent-driven and Overlay-targeted; with the Agent not running there is no
runtime guard inside a layered process at all.

**✅ Half one, done and measured** (`spike-notes.md` §2, `17_HOOK_ENGINE`
§Vulkan). `enable_environment` is in the manifest and verified against loader
1.4.357: with the variable unset, the loader locates the manifest and never maps
the DLL — and it compares the variable's *value*, so a stray `=0` does not
enable us. The cost is accepted: **Vulkan Tier 1 is now launch-mode-only.**
`tools/vklayer-blastradius.ps1` runs the check and unregisters in a `finally`.

> **That cost read larger than it was.** Recorded 2026-08-03: launch mode was
> blocked by §S18, and §S1 does *not* cover the Vulkan path (no injection, no
> suspended target), so §S18 alone blocked every Vulkan Tier-1 session — which is
> why it was not merely "launch mode, which is blocked anyway".
>
> **§S18 closed 2026-08-04.** The remaining blocker is the layer's own
> `vkQueuePresentKHR`, P1 and not started.

> **The measurement also killed the design that looked obvious.** Declining
> `vkNegotiateLoaderLayerInterfaceVersion` for a process that did not opt in
> does *not* make the loader skip the layer — it access-violates the host
> application. That would have crashed every Vulkan program on the machine
> outside our enable-list. The layer now always accepts and always forwards;
> the enable-list decides what we *intercept*, never whether we *load*.

**✅ Half two, done.** The layer scans its OWN process against the blocklist
at init and goes fully passthrough on any hit, using the SAME matcher and the
SAME rules file as the injection guard (`fl_ac_rules.h`, compiled into both). A
layer with its own blocklist would be a second matcher that can disagree with
the first, which is the defect the managed facade was built to avoid.

Every uncertainty resolves to inert: rules unreadable, malformed or incomplete,
module enumeration failed, a truncated list, a module that could not be named,
or an actual hit. That is the opposite *polarity* from the injection guard —
where an unknown means refuse to inject — but the same principle: do the thing
that leaves the host alone.

Verified by `fl-probe-vklayer` (ctest `fl_vklayer_selfscan`), which asserts
**both** directions — a clean process is *not* forced inert, and a process
carrying a planted module *is*. The planted module is our own DLL copied under a
blocklisted name, per `14_TESTING` §Integration tests; no real anti-cheat
software is shipped, downloaded or executed. Proven red by making the matcher
stop matching.

The probe installs the repository's seed rules to the product's one rules
location when nothing is there, and removes them afterwards. That is deliberate:
there is no way to point the layer at a different rules file (§S3), so without
it the test silently skipped on any machine that had not run the product — and a
ctest that always skips is a gate that cannot fail.

### ◐ Part three: mid-session guard inside a layered process — decided, half built

**The mechanism is decided.** The layer does NOT re-scan on its own after init.
Four options were pressure-tested (2026-08-02); the reasons the other three lost
are worth keeping, because each looks reasonable until costed:

- **A layer-owned worker thread** re-running the self-scan on a timer. Repeating
  a ~1.15 MB transient allocation and a full module enumeration every 30 s inside
  a game, against an 8 MB total budget — and, if the driver half were added,
  `NtQuerySystemInformation` plus SCM probing *from inside a game process*, which
  is the behavioural signature of anti-analysis code. CLAUDE.md rule 3 forbids
  hiding from anti-cheat; looking like the thing you are trying not to be
  mistaken for is the opposite failure. Also: the Vulkan loader owns the layer's
  mapping, and a thread outliving it is an access violation in a host we do not
  own — the same shape as the crash already measured in §S2.
- **Driving the re-scan from the present hook.** Rejected outright. It violates
  CLAUDE.md rule 5 structurally; it fails NFR-1 worst exactly where the user has
  least frame budget; and — decisively — **the clock stops when presents stop**,
  which is the scenario the requirement was written for. It would also inject a
  periodic stall into the frame-time series this product exists to measure.
- **Dropping Vulkan to Tier 2.** Not fatal to the product, but the stated form is
  false: Tier 2 needs an elevated Agent, so for most users the real proposition
  is "duration and sensors only".

**What is decided:** supervision is the Agent's job, and a capture side that
cannot confirm supervision stops observing. That is the same polarity the rest of
the layer already has — every uncertainty resolves to inert.

> **"Publishes" is wrong, and the ✅ covers less than it reads as.** Corrected
> 2026-08-04. `GuardSupervisor` **counts** completed evaluations in a private
> setter; nothing writes `FlControlBlock.guardTicks`, because nothing in either
> language maps the shared memory at all (`grep guardTicks` over `src` and
> `tests` returns comments, a declaration, a `static_assert` and the layout
> dump). It also has **no production call site** — `Program.cs` seeds the rules
> file and exits. What is built and tested is the *counting discipline*: the tick
> counts evaluations rather than seconds. What is not built is the supervision.
> A reader ticking this off as delivered would then find `07_IPC` §Supervision
> loss unimplementable, which is exactly what happened.

**✅ The Agent half's counting discipline is built and proven.** `GuardSupervisor`
(`FrameLedger.Application`) computes what will become `guardTicks`, and the load-bearing property
is tested: **the tick counts completed evaluations, not seconds.** A timer-driven
heartbeat attests that the Agent *process* is alive while the guard loop can be
dead — a swallowed exception, a blocked service query, a stall on one unreadable
process in the §S16 scan set — and the capture side would then keep observing
*because* the thing supervising it had stopped. Seven tests force each of those.
`07_IPC` and `fl_shm.h` are corrected, and so is the polarity at the other end:
"the Overlay keeps writing (harmless)" on heartbeat loss described an
unsupervised hooked process as harmless.

**◐ The in-layer half is deliberately NOT built yet, and this stays open.**
The layer intercepts nothing — `vkQueuePresentKHR` is P1 — so a `ShouldObserve()`
today would be a predicate whose wrong answer *in either direction* changes
nothing observable. That is a gate on something that does not exist, which is the
defect class this file keeps recording. It lands in the PR that adds the present
hook, where a fake loader chain can drive it end to end.

**Residual, to state rather than discover:** even once built, the mid-session
*driver* case is invisible from inside a layered process, and a layer cannot
leave a running game's loader chain — "stops" means passthrough, not unhook.
`legal/DISCLAIMER.md` and `README.md` now say so.

### S3 ✅ · `UpdateRules { path }` — **closed, and it was not alone**

Fixed in `07_IPC` §Messages, with the reasoning kept in a new §The pipe is not a
trust boundary so the conveniences are not reintroduced.

`UpdateRules` is now a bare trigger — the rules *source* is no longer a
parameter. Auditing the rest of the message table for the same shape found two
more, neither previously recorded:

- **`SetHookEnabled` took `consentAt` from the client.** The per-game informed
  consent FR-2.1 and `19_SAFETY` make the basis of every injection was
  client-asserted and forgeable. The Agent now stamps it.
- **`SetWatchlist` carried `hookEnabled` with no pre-scan**, while the adjacent
  `SetHookEnabled` re-scans and may reply `Refused` — and the UI re-sends the
  full watchlist on every connect, so a value forced once would be re-asserted
  forever. It now carries identity only.

The rule that generalises them, now written down: **no inbound message may
assert a safety fact.** It may request a state change; the Agent establishes the
fact itself.

### S4 ◐ · Trust model for the enable-list and the rules feed — **two of three closed**

**Enable-list: specified** (`17_HOOK_ENGINE` §The enable-list) — location,
format, bounds, exact case-insensitive image-name matching, sole writer, ACL,
and the rule that *every* failure is passthrough. Written down with it: the ACL
is not strong (anything running as the user can append a line), but a line there
only causes us to observe a Vulkan process, grants no injection, and cannot
disable the layer's own blocklist scan.

**Staleness: specified** (`05_DETECTION` §Trust and staleness of the rules feed)
— one read location, replace-only-if-valid with the last valid copy kept, and
staleness that *warns* and never disables. An expiry that weakens a gate is an
override with a timer on it.

**Signing: still open, and deliberately not dismissed.** HTTPS authenticates the
host, not the content. The rules above let a compromised feed be *rejected* but
not a genuine one *proven*. Residual risk, recorded rather than closed.

### S5 ✅ · `detection-rules.json` schema — **closed**

`rules/detection-rules.schema.json` (JSON Schema 2020-12) now fixes the shape,
and `tools/rules-validate.ps1` enforces it. The seed gained the representations
the `19_SAFETY` blocklist table needed and the abbreviated shape in
`05_DETECTION` could not express: `directories`, `services`, `files`, and a
`heuristic` block for the unknown-but-suspicious rule.

**`Test-Json` fails OPEN on a malformed schema** — measured on PowerShell 7.6.4,
`-Schema '{'` returns `$true` while writing a parse error to the error stream.
A truncated schema file would therefore make every rules file "valid", including
one with an empty blocklist. The validator now proves the schema is
*discriminating* before trusting it: a canary document that must fail is checked
first, and if the canary passes we refuse rather than report success.

Still imperative, because a schema cannot express them: required families still
present, no case-insensitive duplicate values, no prefix so short it would shadow
a system DLL.

**Two families remain unrepresented in the data** — Activision Ricochet (driver
and service names unconfirmed) and Valve VAC (needs `blockedStoreIds`). Recorded
in the seed's own `$comment`. The `heuristic.trustedSigners` list is a guess and
is marked UNVERIFIED.

### S13(c) · Is launch-mode injection salvageable?

> **(a) and (b) are decided (2026-08-02) and no longer open.**
>
> **(a) The guard stays in the C++ `FrameLedger.Injector`,** as `19_SAFETY` §The
> anti-cheat guard and CLAUDE.md §Solution layout always said. The managed
> proposal is rejected. Four consequences travel with that choice and are
> tracked as §S15 rather than left implicit — each is a fail-open or a dead test
> if forgotten.
>
> **(b) No clearance escapes the guard.** The guard **owns the chokepoint**: it
> collects evidence, matches, and calls the injection primitive itself. A token
> that escapes can be ignored — a caller can decline to ask for one — whereas a
> primitive with no reachable symbol cannot be called at all. In C++ that is
> internal linkage in the guard's own translation unit, which is a stronger
> mechanism than the C# accessibility trick §S8 disproved. It also deletes the
> staleness question: mint-to-inject shrinks to nothing because nothing is
> minted.

A `CREATE_SUSPENDED` target has loaded almost nothing — **measured**: the module
scan does not merely come back thin, `EnumProcessModulesEx` *fails* with
`ERROR_PARTIAL_COPY` (§S1, `spike-notes.md` §1). The originally proposed "defer
hooks until first present" gate is also circular, because the ring handshake that
would signal first-present is published by the Overlay *after* injection.

An externally observable proxy is needed instead — e.g. resume, then poll until
the target has mapped a presentation runtime (`dxgi.dll` plus `d3d11`/`d3d12`, or
`opengl32`, or `vulkan-1`) *and* the scan passes *and* the blocklist is clean.
Decide whether that is acceptable, or whether launch-mode injection is dropped.

**The one input still missing** is how much early-init data injecting late
actually costs. It needs a title that loads a presentation runtime lazily; the
local fixtures (`hook-harness`, which creates D3D at startup, and a 2D GOG
prologue) cannot produce a figure that generalises.

### S15 ✅ · The four consequences of putting the guard in C++ (§S13(a)) — **closed**

Not questions — commitments the §S13(a) decision creates. **All four are done**,
each with the tests that keep it true listed below.

> **This section said "Three of four are done; item 1 is the one still open"
> while every one of its four bullets said DONE, item 1 included.** Corrected
> 2026-08-04. Recorded rather than silently fixed because it is this file's own
> recurring defect wearing a different costume: a status line whose verdict was
> decided before anyone read the list under it. The cost was real — a phase
> planned against this ledger over-scopes, and §S15 was carried into the
> 2026-08-04 next-phase review as open work.

- **1 — one matcher, not two: DONE.** `FrameLedger.Guard.dll` exposes a C ABI;
  `Infrastructure`'s `NativeAntiCheatGuard` is a thin P/Invoke facade over it,
  and `Application`'s `IAntiCheatGuard` exposes only the two questions the guard
  answers — no rules, no blocklist, no evidence. `04_CAPTURE` and
  `01_ARCHITECTURE` are corrected. Two tests keep it true: one asserts no
  managed type carries a blocklist token and the port accepts no evidence, the
  other asserts the managed `AntiCheatRefusalReason` has not drifted from
  `fl::guard::Reason` by reading every name back through the ABI.

  Three things that came out of building it:

  - **The guard DLL is loaded by absolute path**, via a
    `NativeLibrary.SetDllImportResolver`, and never by search. A planted
    `FrameLedger.Guard.dll` earlier on the probe order would replace the entire
    gate with whatever an attacker wanted it to say — a worse outcome than any
    other DLL-hijack in this application. CA5393 rejects `ApplicationDirectory`
    for exactly that reason, and no "safe" search path fits a DLL of our own,
    so the search path is not merely restricted but never consulted.
  - **A default-constructed `AntiCheatVerdict` must not read as permission.**
    The native `Verdict` gets a member initialiser; a C# struct zeroes every
    field and `Allow` is 0 — which it must stay, because it mirrors the native
    enum. The verdict therefore records whether it came from an evaluation at
    all: a value nobody assigned has evaluated nothing and permits nothing.
  - **`FrameLedger.Application` shadows `System.Windows.Application`** inside
    `FrameLedger.App`, because a sibling namespace beats a `using`. The WPF
    `App` class now says `System.Windows.Application` in full.
- **2 — Catch2: DONE.** `src/native/tests/guard_test.cpp`, ctest `fl_guard`.
  The seams are `fl::guard::Sources`, plain function pointers so the guard
  allocates nothing, and every failure in `14_TESTING`'s matrix is forced
  through a fake rather than hoped for.
- **3 — C++ JSON: DONE.** jsmn, pinned by commit. Chosen for what it does not
  do: no allocation, no exceptions, no recursion beyond a token array we own,
  and failure reported as a return code. Every parse failure is a REFUSE with a
  distinct reason.
- **4 — the override mechanism: DONE, and now enforced.** `14_TESTING` is
  rewritten, and `tools/chokepoint-check.ps1` fails the build if any translation
  unit other than the guard's names the injection primitive *or* the Win32 calls
  that constitute injection. Proven red both ways.

> **The injection primitive is now real**, in the order CLAUDE.md rule 2
> requires: the guard and its full matrix landed first, then the primitive.
> `VirtualAllocEx` + `WriteProcessMemory` + `CreateRemoteThread` on documented
> `LoadLibraryW`, verified end to end against `hook-harness --hold`.
>
> Hardening that came out of writing it: **the evidence seam is compiled out of
> everything that ships.** `GuardedInject` and `Evaluate` take no `Sources` and
> always use `SystemSources()`; the injectable versions exist only under
> `FL_GUARD_TESTABLE`, which only `src/native/tests` defines. The guard sources
> are compiled *into* the test binary rather than linked from the static lib, so
> `FrameLedger.Injector.lib` contains **zero** `WithSources` symbols — verified
> with `dumpbin`, not assumed. While the primitive was a stub a caller passing
> all-clean fakes was theoretical; the moment injection became real it would
> have been a route into a game process that consulted no genuine signal at all.

1. **One matcher, not two.** `04_CAPTURE` §The guard writes
   `AntiCheatGuard.Check(pid)` and `01_ARCHITECTURE` draws the guard inside the
   Agent box, while `19_SAFETY` says "implemented in `FrameLedger.Injector` and
   re-checked by the Agent". The managed side must be documented as a **P/Invoke
   facade over the single native implementation**, living in `Infrastructure`
   (CLAUDE.md: the native layer is reachable only through `Infrastructure`). Two
   blocklist matchers that can disagree is a fail-open by construction.
2. **Catch2 is now a prerequisite, not a P1 nicety.** `14_TESTING` §Safety-guard
   tests requires forcing `EnumProcessModulesEx` failure, a partial module list
   and an unreadable process — all native tests now.
   `src/native/tests/CMakeLists.txt` still defers Catch2 to P1, and the guard
   needs seams (injectable enumerator function pointers) before those failures
   are forceable at all.
3. **The Injector must parse `detection-rules.json` in C++.** Record the parser
   choice, pinned by commit like MinHook, and the exceptions policy for that
   target — `-D_HAS_EXCEPTIONS=0` is Overlay-specific and the guard is *not* a
   hook path, so STL and allocation are fine there. Parse failure ⇒ **REFUSE**.
4. **`14_TESTING`'s absence-of-override mechanism must be rewritten.** It
   currently specifies a C# `sealed` token type that §S8 disproved by compiling.
   The replacement is (b) above plus a build-time check that no translation unit
   other than the guard's references the injection primitive.

### S16 ✅ · *Which* process does the guard scan? — **closed: the game's own subtree**

Decided 2026-08-02, specified in `19_SAFETY` §Pre-injection checks item 1.

**The injection target, its descendants, and its ancestors up to but excluding
the first known platform launcher.** Neither obvious reading survived: the
presenter alone misses a game launcher that initialises anti-cheat before
spawning the renderer, and unbounded ancestors would scan `steam.exe` — which
loads VAC modules — and so refuse every Steam title. A gate that refuses
everything is not a strict gate but a broken one, and it is how a user ends up
looking for the override CLAUDE.md rule 2 says does not exist.

A hit anywhere in the set refuses; a process in the set that cannot be inspected
refuses too. Sibling *services* are covered by name via the rules data rather
than by tree walking, which is more reliable. The runtime re-scan recomputes the
set rather than caching it.

<details><summary>The question as originally recorded</summary>

Found 2026-08-02. Not previously recorded anywhere, and it changes the guard's
own signature, so it belongs before the guard is written rather than after.

`04_CAPTURE` §The guard is `AntiCheatGuard.Check(pid)` — **singular**. But
`04_CAPTURE` §Process watcher says the capture target is "the descendant that
actually presents", elected from a ppid tree, and in attach mode elected only
after we see a ring handshake or count presents. So the guard is specified
against one pid while the subject of the capture is a *tree* whose presenting
member is identified later.

That matters because **anti-cheat frequently does not live in the presenting
process.** A launcher starts, loads the anti-cheat, and spawns the renderer as a
child; or a sibling service process holds it. Scanning only the elected
presenter would miss exactly that arrangement — and it is a common one, not an
exotic one.

**Needs a decision, and it is not obviously "scan everything":**

- Scanning the whole tree makes the guard stricter but raises the false-refusal
  rate, since a launcher may legitimately carry components a game process never
  loads.
- Scanning the presenter only is the narrow reading and is what the current
  signature implies.
- Whatever is chosen, the *machine-wide* driver check (check 2) already covers
  the worst case independently of process identity, which lowers the stakes but
  does not answer the question.

Also unspecified: what the guard does when the tree changes mid-session
(level-transition relaunches re-elect the presenting pid — `04_CAPTURE`
§Process watcher), and whether a newly appearing sibling is re-scanned.

</details>

### S17 ✅ · The schema accepted rules files the guard refuses to parse — **closed**

Found 2026-08-03 while scoping P0 item 3. Recorded rather than fixed silently,
because it was live in shipped artifacts and the shape recurs.

`rules/detection-rules.schema.json` and `fl_ac_rules.h` bounded the same things
with different numbers, and **the schema was looser in every case**. That matters
more than it sounds: an over-cap entry does not drop the entry. `ReadFamily`
returns false, `ParseRules` returns `kMalformed`, and `19_SAFETY` turns an
unparseable rules file into **REFUSE — for every title on the machine**. Rules
ship as updatable data pushed to every client and `tools/rules-validate.ps1`
validated against the *loose* schema, so a CI-green rules edit could have taken
the product out in the field.

| Bound | Schema said | Parser accepts |
|---|---|---|
| `blockedExecutables` / `blockedStoreIds` element | **object** | **bare string** — shape, not size |
| values per family | 64 | 16 (`kMaxValuesPerFamily`) |
| token length | 128 | 95 — `CopyToken` rejects at `len >= cap` |
| family name | 64 | 63, same off-by-one |
| prefix floor | 3 | 4 (`kMinPrefixLen`) |
| per-title arrays | 4096 | 256, now `kMaxTitleRules` |
| `nameFragments` / `trustedSigners` | 32 / 64 | 16 / 16, hardcoded |
| families across all five groups | 1280 | 64 (`kMaxFamilies`) |

**Proven, not argued:** a 17-value family passes the *old* schema (`exit 0`)
while making `ParseRules` return `kMalformed`; under the calibrated schema it
fails. Same input, both directions.

**The shape mismatch was the worst of them.** Both per-title arrays are objects
in the schema (`family`, `match`, `values`, `reason`) and were read by
`ReadStringArray` as bare strings. The first entry anyone added would either
overflow `kMaxValueLen` with its JSON text and refuse the whole file, or fit and
be stored as an unmatchable blob. Only the two empty arrays kept that theoretical.
The parser now reads the objects and composes `store` + `id` into the joined
`"steam:730"` form `fl_ac_rules.h` always promised.

Closed by: calibrating the schema to the parser, deriving nothing by hand
(`tools/rules-validate.ps1` reads the thresholds out of the header by regex and
**fails rather than skips** if it cannot, and ctest `fl_rules_budget` generates
its boundary cases from the same constants). That ctest also asserts something
nothing asserted before — **that the rules file we actually ship parses in the
guard at all.** Every case in `guard_test.cpp` parses an inline fixture.

**Two things this turned up that are not the schema's fault:**

- **A `static_assert` that could not fire on the change it existed to catch.**
  `fl_guard_abi.cpp` pinned `kRulesIncomplete == 16` to protect
  `FlGuardReasonCount() == 17` — but `kRulesIncomplete` was the *last*
  enumerator, so appending a `Reason` left it at 16, the assert passed, the
  exported count stayed stale, and the managed mirror test iterated 0–16 and
  never compared the new value. Replaced with a `Reason::kCount` sentinel the
  count is derived from. Verified: appending a reason now moves the count to 18
  and `GuardMirrorTests` fails with a precise message.
- **`ReasonName`'s exhaustiveness was not compiler-enforced, though a comment
  said it was.** Omitting `default:` does *not* make MSVC object: C4061/C4062 are
  off by default even at `/W4`, measured by appending an enumerator with no case
  and watching `/W4 /WX` build clean. That is a gate that existed only in prose.
  It is now ctest `fl_guard`'s "every Reason has a distinct name", proven red.

### S18 ✅ · The guard refuses itself — **closed 2026-08-04**

Found 2026-08-03 during the first real injection (`spike-notes.md` §7), and
isolated on one title with everything else held constant:

| Evaluating process | Verdict |
|---|---|
| An **ancestor** of the game, with `FrameLedger.Guard.dll` loaded | `SuspiciousUnsigned`, signal `FrameLedger.Guard.dll` |
| Not an ancestor | `Allow` |

§S16 walks the game's ancestors up to the first platform launcher. The name
`FrameLedger.Guard.dll` contains **`guard`**, one of the heuristic's
`nameFragments`; the signer half is deliberately unwired, so an unchecked
signature is untrusted by definition (`fl_guard.cpp`, "that is the correct
direction") and fragment-plus-untrusted refuses. In launch mode the Agent is the
game's parent (`04_CAPTURE` §Launch mode) and hosts that exact DLL. Same shape as
the `EasyAntiCheat_EOS` service defect: **a gate that cannot pass is not strict,
it is broken.**

**Not fixable by signing.** The project ships unsigned — CLAUDE.md rule 9 and the
pinned-stack Packaging row. (§S18 and `spike-notes.md` §7 previously cited
`12_BUILD` §Packaging for this; that section contains no signing text. Citation
corrected, conclusion unchanged.)

> **The KEYING below was superseded on 2026-08-04 by §S22(b); the principle was
> not.** Everything here about *why* the exception exists, why it is confined to
> the fuzzy tier, why it never covers the target, and why identity is by
> directory-file-id rather than by name still holds and is still implemented.
> What changed is the SUBJECT: the exemption asked whether the scan-set
> **process** lived in our directory, and now asks whether the **module that
> matched** is ours. That fixed a measured refusal of every FrameLedger host not
> sitting beside `FrameLedger.Guard.dll`, and it is strictly narrower. Read the
> "Not by module name" bullet below with that in mind — it rejected a *name*
> allowlist, and it was right to; a file-id check on the module is a different
> mechanism and is not what it argued against.

#### The decision — identity by **install root**, heuristic tier only

Taken 2026-08-03 after a four-lens panel, three adversarial refuters and a
completeness critic. **All three refuters refuted the panel's own first answer**;
what follows is what survived.

Suppress **only** the `sawSuspicious` fragment tier, **only** for a scan-set
process whose image path resolves under FrameLedger's own install directory, and
**never** for the injection target. Everything else in `CheckModules` is
untouched: the exact blocklist (`st.hit`) still returns first, `kFailed` still
gives `kProcessUnreadable`, `kIncomplete` still gives `kModuleScanFailed`. The
ancestor walk keeps climbing **past** us rather than stopping at us.

Why install root and not the three obvious alternatives:

- **Not by module name** (the original option 1). `HasSuspiciousFragment` is
  called for every process in the scan set, so a name allowlist suppresses that
  name *inside the game too* — and it is spoofable by a DLL that borrows the name.
- **Not by `GetCurrentProcessId()`** (the panel's first answer). It identifies
  the process *executing* the guard; the defect is a property of the **binary**.
  `FrameLedger.App` also carries `FrameLedger.Guard.dll` — `Infrastructure.csproj`
  copies it to "everything that references it", and FR-2.2's pre-launch question
  loads it in the UI process. A second FrameLedger process in the ancestor chain
  refuses exactly as before. This is what the third refuter broke, correctly.
- **Not by stopping the ancestor walk at us**, the way `IsPlatformLauncher` stops
  it at `steam.exe`. That boundary is justified because everything above
  `steam.exe` is shared platform infrastructure that legitimately loads VAC.
  Everything above the Agent is an *undefined* category — a shell, an IDE,
  Playnite — so truncating there deletes processes we merely did not want to
  think about. That is "could not look" recorded as "looked and it was clean",
  which this file exists to prevent. Skipping one process and continuing costs
  the same and is strictly more conservative.

Install root has the trust base the other three lack: an attacker who can place
a binary inside our install directory can already replace `FrameLedger.Guard.dll`
itself, which `NativeAntiCheatGuard.cs:48` calls a worse outcome than any other
DLL-hijack in the application. It also costs one `QueryFullProcessImageNameW` per
scanned pid — the call `ImageDirectoryImpl` already makes for check 4 — and it
covers `FrameLedger.exe`, `FrameLedger.Agent.exe` and every future FrameLedger
process for free. "Could not read the path" refuses, as everywhere else.

**Explicitly rejected, with the reason, so nobody re-proposes them:**

- **Re-parenting the launched game** (`PROC_THREAD_ATTRIBUTE_PARENT_PROCESS`) so
  we are never an ancestor. This is lineage spoofing: anti-cheat reads parent
  lineage precisely to see who started the game. CLAUDE.md rule 3 — rejected on
  principle before merit. Recorded because it is the clever answer someone
  proposes next.
- **Running the fragment tier on the target subtree only, never on ancestors.**
  Attack cost zero: ship the anti-cheat in the launcher under a name not in
  `modules`. It deletes coverage for exactly the arrangement §S16 was written to
  catch.
- **Deleting the `protect` fragment** to cure the false positive in §S19 below.
  It is a detection removal in a hard gate, on the mode that ships. See §S19.
- **Deferral alone** — "attach mode only, launch mode deferred". Correct as a
  scope statement, not an answer: §S19 shows the same code path refuses
  *attach*-mode titles.

#### Why this is worth more than it looked

The old text said §S18 blocks "the early-init upscaler data §S1/§S13(c) are
about". That understated it. **§S18 is the sole blocker of the entire Vulkan
Tier-1 path.** The layer is gated by `enable_environment`
(`FRAMELEDGER_ENABLE_VK_LAYER=1`), which only the process that *starts* the game
can set — `17_HOOK_ENGINE:161` states this makes Vulkan Tier 1 launch-mode-only.
And §S1 does **not** apply there: the Vulkan path performs no injection, so there
is no suspended target and no `ERROR_PARTIAL_COPY`. "Launch mode is blocked by
§S1 anyway" is therefore false for an entire Tier-1 API family.

#### ✅ Closed 2026-08-04 — implemented, and all three blockers answered

1. **The test vehicle exists, and it is not the one this entry asked for.**
   ~~`fl_guard_test` calls `SystemSources().ProcessIsOurOwn` directly~~ — **that seam was deleted by §S22(b)**; the live test is now "§S22(b) — the real ModuleIsOurOwn answers both directions" and calls `SystemSources().ModuleIsOurOwn`, both
   directions: this process (which runs the guard code from its own directory)
   is ours; a freshly spawned `System32\cmd.exe` is not. That tests the
   *predicate* against a real machine, which is what the exception rests on —
   loading `FrameLedger.Guard.dll` into the test just to make a module NAME
   appear would have proved the fragment matcher, which `fl_guard` already
   covers, and would have exercised the boundary logic not at all.
2. **The two-pid case is a fixture, and the thing that blocked it was the FAKE.**
   `FakeEnumModules` ignored its pid, so "two distinct FrameLedger processes,
   both carrying the DLL" could not be written down. With a per-pid map it is
   seven lines. That was ~10 lines of test-side code standing in front of a
   safety item for a day.
3. **Ratified: the Agent is the sole host.** `05_DETECTION` and `07_IPC` already
   assigned it ownership of `%LOCALAPPDATA%\FrameLedger`; the guard DLL now ships
   only to `FrameLedger.Agent` (and to `Infrastructure.Tests`, which P/Invokes
   it), via `/FrameLedger.Guard.targets` instead of a `None` item in
   `Infrastructure.csproj` that flowed to every referencing project.

> **The ratification does not retire the install-root mechanism, and it is worth
> saying why rather than leaving it to look like belt-and-braces.** With one host,
> process identity would be sufficient *today* — but "sufficient today" is what
> made `GetCurrentProcessId()` look right the first time. Directory identity
> covers every future FrameLedger process for free, costs one `OpenProcess` on a
> path that a measured machine reaches approximately never, and does not have to
> be revisited when a second host appears. What ratification bought is a smaller
> surface, which is the better half of the trade.

> **And the justification recorded in `19_SAFETY` was false.** It said our own
> module set "can produce false refusals and never a true one". Measured across
> 290 live processes, three carried a fragment-matching module, none of which
> needed write access to anything of ours — an AppInit DLL or an AV hook can put
> one in a FrameLedger process too. The exception is justified by *trust*, not by
> information: an attacker who can write to our install directory can already
> replace the guard itself. `19_SAFETY` now says that instead, with the unsigned
> -shipping residual stated.

Six fail-closed cases hold the exception narrow, and each was proven red against
a specific plausible mistake — dropping the target exclusion (`guard_test.cpp:516`),
suppressing regardless of the tri-state (`:527`), and an implementation that calls
every process ours (`:612`).

**Measured on three real titles, not only in fixtures** (`spike-notes.md` §7). A
stand-in for the Agent, built into the Agent's own output directory beside a real
`FrameLedger.Guard.dll`, loads it and launches the game — so our binary is the
game's ancestor and carries the offending module name. Deadly Heart Gambit, Lies
of P and Alan Wake 2 all go `SuspiciousUnsigned` → **`Allow`**. Evaluate only;
`FlGuardedInject` was never called.

That measurement also validated the assumption the fix rests on and the unit
tests structurally cannot reach: **the Agent's executable and the guard DLL land
in the same directory**, which is what makes "equality, not containment" the right
comparison. `12_BUILD` publishes both executables into one `out/app`, so the
shipped shape matches.

### S19 · The unknown-but-suspicious heuristic has five defects of its own

Found 2026-08-03 by the §S18 panel and **re-measured by hand before recording**,
because three of them change what a shipped gate does. They are separate from
§S18 — no self-exclusion touches any of them.

> **Heading said "four" over a body running (a) to (e).** (e) was appended
> without updating the count. Corrected 2026-08-04, along with the claim that (b)
> "lands in attach mode, the only mode that ships today" — see the measurement
> under (b), which does not support it as written.

**(a) `gameguard` can never fire.** `HasSuspiciousFragment` is case-insensitive
`IContains`, and `"gameguard"` **contains** `"guard"`. With both in the list, the
`guard` fragment matches everything `gameguard` would, first. A shipped rule
incapable of firing independently — the file's own recurring defect, sitting
inside the safety gate. No coverage is lost today (`guard` subsumes it), so this
is cosmetic *until* someone removes `guard` believing `gameguard` still covers
nProtect. Fix by deleting the redundant entry or by documenting the subsumption
where the list is defined; do not leave it looking like two rules.

**(b) `protect` produces measured false refusals, and they are not hypothetical.**
Unelevated, Windows 11 26300, **331 processes, 0 access-denied, 4 hits, none
anti-cheat**:

| Process | Module matching `protect` |
|---|---|
| `WhatsApp.Root` | `mskeyprotect.dll` — *Microsoft Key Protection Provider*, 10.0.26100.1746, `C:\WINDOWS\system32\` |
| `WidgetService` | `mskeyprotect.dll` |
| `ProtonVPN.Client` | `System.Security.Cryptography.ProtectedData.dll` |
| `Malwarebytes` | `Malwarebytes.Protection.Interop.dll` |

**The fix is not to delete the fragment** — that is a detection removal in a hard
gate, and `antitamper`/`protect` is the only tier covering families the seed
admits it has no data for (Ricochet, VAC). Three refuters agreed.

> #### Re-measured 2026-08-04, and the entry above was wrong in both directions
>
> Same machine, unelevated: **290 processes, 0 inaccessible, 3 hits** (not 4 —
> `WhatsApp.Root` was not running this time; the module set is the same).
>
> | Module | Signature | `O=` | Would the signer half suppress it? |
> |---|---|---|---|
> | `mskeyprotect.dll` | **Catalog** | Microsoft Corporation | **No** |
> | `System.Security.Cryptography.ProtectedData.dll` | Authenticode | Microsoft Corporation | Yes |
> | `Malwarebytes.Protection.Interop.dll` | Authenticode | **Malwarebytes Inc** | **No** |
>
> **The proposed fix would have addressed one of three.** `mskeyprotect.dll` —
> the module this entry was written about — carries no *embedded* signature at
> all; it is catalog-signed, as are `kernel32.dll` and `nvapi64.dll`. A
> `WinVerifyTrust(WTD_CHOICE_FILE)` implementation, which is what "wire the signer
> half" means to everyone who has read this entry, recovers nothing for it and
> `IsTrustedSigner(nullptr)` is false by contract, so it still refuses. Doing it
> properly needs `CryptCATAdminAcquireContext2` / `CalcHashFromFileHandle` /
> `EnumCatalogFromHash` and `WTD_CHOICE_CATALOG`, which nothing here budgeted.
> And `Malwarebytes.Protection.Interop.dll` is validly signed by a publisher
> absent from `trustedSigners`, so **no signer implementation fixes it** — only a
> data change to an allowlist that no CI gate reviews.
>
> **"A game that loads a Windows key-protection provider is refused today, in
> attach mode" is also not supported by the measurement.** All three hits are
> desktop processes — a browser helper, a VPN client, an AV service — and none can
> enter a game's scan set: `EnumerateScanSetImpl` walks ancestors only up to the
> first `IsPlatformLauncher` match, and that list includes `explorer.exe`,
> `services.exe` and `svchost.exe`, so a normally-launched game's chain terminates
> one hop above it. Three real titles have been scanned with no fragment hit.
>
> The defensible claim is narrower and still worth acting on: **the `protect`
> fragment matches a benign, widely-loaded Microsoft system DLL, and has not been
> shown to match inside any game's scan set.** A game process that loads DPAPI or
> CNG for save encryption or a launcher token would trip it, which is plausible
> and unmeasured — not "refused today".
>
> §S19(b) is therefore **deferred with this written rationale** rather than
> scheduled: it fixes one measured case of three, its true shape is a
> `CryptCATAdmin*` PR, and `WinVerifyTrust`'s default `WTD_REVOKE_WHOLECHAIN`
> performs CRL/OCSP network I/O from inside the hard gate — against **NFR-10
> offline-first** (`02_SPEC:105`) as well as CLAUDE.md rule 8. Build
> `fl-probe-signer` first, in the shape `fl-probe-guard` established, and answer
> those three questions with measurements before any design is fixed.

> Note what it costs beyond that: a per-module signature cost inside a gate that
> re-runs every 30 s (§S6), and a new fail-closed matrix row for "signature could
> not be checked". It is its own PR.
>
> **Two corrections to the cost as first recorded.** It said the check needs
> `WinVerifyTrust`/`CryptQueryObject` "in a TU compiled with no exception model" —
> `_HAS_EXCEPTIONS=0` comes from `fl_hostile_env_flags`, which
> `src/native/CMakeLists.txt` states explicitly is **not applied to the Injector**.
> That constraint becomes true only if the collector is placed in
> `fl_ac_rules.cpp`, which the Vulkan layer compiles in — which is its own reason
> to put it in `fl_guard_sources.cpp` instead, rather than drag wintrust and
> crypt32 into a DLL mapped into every Vulkan process on the machine.
>
> And it is **not** a wiring change. `NameSink` is `bool(*)(void*, const char*)`
> fed by `GetModuleBaseNameA`, so the evidence the check needs — a module's full
> path — does not reach the decision point. The signer half requires widening the
> module seam, which is a new row in the fail-closed matrix, not a call added at
> `fl_guard.cpp:293`.
>
> **And it must not be placed at `fl_guard.cpp:293`.** `NameSinkFn` latches the
> FIRST fragment-matching module per process (`!st->sawSuspicious`) and discards
> every later one. Harmless while any hit refuses; the moment a signer can CLEAR
> the latched name, a process that loads a trusted fragment-module before an
> untrusted one returns `Allow` with the second never recorded — a fail-open
> reachable by load order. The detection half at `fl_guard.cpp:203` has to be
> restructured, not extended.

**(c) `signerField` and `action` are required by the schema and parsed by
nobody.** Both are `required` in `detection-rules.schema.json`, both carry
safety-relevant `$comment`s — `action` "is a const, not an enum, so `allow` and
`warn` are unrepresentable" — and neither appears anywhere in `fl_ac_rules.cpp`.
The heuristic's policy is hardcoded at `fl_guard.cpp:293`. So the
**warn-and-refuse** behaviour `19_SAFETY` §Blocklist seed describes is not
configurable and never was; the field that would express it has no consumer.

> Two corrections, 2026-08-04. This cited **`19_SAFETY:264`**, which is a
> blocklist table row (`| mihoyo protect | drivers | prefix | mhyprot |`); the
> sentence is at `19_SAFETY:281`. And "hardcoded at `fl_guard.cpp:293`" names only
> the *refusal* half — the *detection* half is at `fl_guard.cpp:203-206`, which is
> where the first-hit latch lives and where anyone implementing (b) has to work.
> Line numbers were dropped from this entry rather than corrected: they are what
> went stale.
>
> `action` is worth costing honestly before anyone wires it. It is a schema
> `const` with exactly one legal value, so a parser reading it can only ever see
> `warn_and_refuse` — a predicate whose answer changes nothing observable, which
> is the defect class this file exists to record. Wiring it means first deciding
> that `warn` or `allow` may exist, and CLAUDE.md rule 2 says they may not.

**(d) ◐ The fragment list has unreconciled copies — more than three. The runtime
hole is CLOSED.** `rules/detection-rules.json`, `guard_test.cpp` (the inline
`GoodRulesJson()` fixture) and `rules_budget_test.cpp` each carry their own, and
re-counting on 2026-08-04 found two the entry had missed: the prose in
`19_SAFETY` §Heuristic tier, and a `$comment` in `detection-rules.schema.json`
that restates the **four-fragment** version — the exact staleness §S19(e) was
raised about, sitting in the schema that gates the data. The generated floor is
not a copy: it is derived at build time and cannot drift.
`ParseRules` guards the entire heuristic read behind `heurTok >= 0` and
`IsCompleteEnoughToGate` never looks at fragments, so a rules file with no
`heuristic` block parses `kOk` and the tier silently stops existing.

> **Half of this entry was wrong, and the wrong half was the one that made it
> sound urgent.** It said "a rules push can empty `nameFragments` … and every gate
> stays green". Measured 2026-08-04 with `Test-Json` against the real schema:
> emptying the array fails (`minItems: 1`), and deleting the block fails
> (`heuristic` is in `anticheat.required`). `tools/rules-validate.ps1` runs that
> schema behind a discriminating canary, so **CI already refuses both**.
>
> `rules-validate.ps1` does contain zero *imperative* heuristic checks, which is
> what was actually observed and then over-read. Adding one would be a gate whose
> red input the schema already eats.
>
> What genuinely remains is narrower and sharper:
>
> - ~~**The runtime hole**, above, sequenced with §S20.~~ **Closed 2026-08-04 by
>   the generated floor**, and by a mechanism this entry did not consider: the
>   fragments are seeded into `Rules` before the file is read, so the tier cannot
>   stop existing because the data never supplied it. That needs no new
>   `ParseResult` cause, so `kRulesIncomplete`'s signal stays true and `layer.cpp`
>   is not driven inert. See §S21's floor note.
> - **The schema canary does not discriminate on this constraint.** It is
>   `{"schemaVersion":"not-a-number"}`, which any schema still pinning
>   `schemaVersion` rejects. Delete `minItems` from `nameFragments` and the canary
>   passes, `Test-Json` passes, and the CI floor silently ceases to exist. The
>   check worth building is a canary carrying `nameFragments: []` that must be
>   rejected — not an imperative duplicate of the schema.
> - **✅ `trustedSigners` now has a gate in the direction that matters — closed
>   2026-08-05, and it took two attempts.** The original entry was right: the job
>   failed on family *removals* while an *addition* to the signer allowlist was
>   invisible, and that field suppresses refusals, which `19_SAFETY:74-81` already
>   forbids for the launcher list ("a data-driven cutoff would let a rules push
>   widen the hard gate's blind spot … the boundary of what the gate looks at is
>   code").
>
>   `cea744e` appeared to close it and did not. It added `trustedSigners` to
>   `Get-Tokens`, whose sole consumer is `$removed = old − new` — so an addition,
>   present only in *new*, still could not appear. Worse, removals then **did**
>   fire, and removing a trusted signer makes the guard stricter: the gate blocked
>   the safe direction and passed the dangerous one, beneath a comment asserting
>   "rules-publish cannot see such an addition. **It can now.**" A shipped comment
>   claiming a capability the code structurally cannot have, which is this file's
>   own recurring defect, in the commit that wrote this bullet's neighbours.
>
>   The field now has its own `$new − $old` comparison. Proven by extracting the
>   shipped step from the YAML and running it against the real seed — five cases,
>   before and after: adding a signer `PASS → FAIL`, removing one `FAIL → PASS`,
>   the three pre-existing cases unchanged. **Still a prerequisite of §S19(b)** in
>   the sense that matters: the gate makes an addition *reviewable*, not
>   impossible, and the field stays inert until the signer half is wired.

> Adding a runtime floor is right but not free: routing it through the existing
> `kRulesIncomplete` would make that reason's hardcoded signal — *"a required
> anti-cheat family is missing"* — a lie, which is the exact defect the
> `InjectionFailed` fix closed one commit earlier. And `layer.cpp` treats any
> non-`kOk` parse as **inert passthrough**, so a heuristic-only floor failure
> would silently disable the Vulkan layer machine-wide — over a tier the layer
> never uses, since `RunSelfScan` calls only `MatchName`. Both need a
> `ParseResult` that distinguishes causes.
>
> Note the shape of that prerequisite precisely: `ParseResult` **already** carries
> a cause (`kOk`/`kMalformed`/`kTooLarge`/`kIncomplete`, mapped to three distinct
> reasons). What is missing is a finer distinction *inside* `kIncomplete`, plus a
> layer-side policy for a non-safety floor failure. Stated as "needs a ParseResult
> that carries its cause", this entry sends someone to build an enum that exists.

**(e) ✅ Closed — the normative doc was already fixed, and this entry was the
stale one.** It claimed `19_SAFETY` lists four fragments. As of commit `bb0da0a`
— the same commit that recorded §S19 — `19_SAFETY:281` lists all five, and the
lines beneath it carry a correction block covering the fragment count, the
`gameguard` subsumption and the unread `action` field.

What survives is not a doc error but a missing gate: **nothing prevents the drift
recurring.** `rules-validate.ps1`'s doc/data cross-check parses only the
§Blocklist seed *table*, so the fragment sentence is invisible to it. That check
is worth extending; the claim that the doc is wrong is not.

> Recorded rather than deleted, because the entry contained the exact defect it
> complained about — a doc citation (`19_SAFETY:264`) pointing at text that is not
> there. Line 264 is a blocklist table row.

### S20 ◐ · Delivering the rules file — **seed half done, feed half open**

The guard reads its blocklist from one place under Local AppData, and until
2026-08-04 nothing in this repository ever wrote a file there. On any machine that
had not hand-installed one the guard answered `RulesUnreadable` for every title —
correct fail-closed behaviour, and also the whole story: the first real
injection's opening refusal was exactly this.

> **The entry's own opening sentence was not quite true**, and the exception
> matters for anyone reading it as a survey. `fl-probe-vklayer` DOES seed the
> file — install-only-if-absent, path resolved through `fl::guard::RulesFilePath`
> — and then deletes what it installed. So the repository had a writer; it had no
> *product* writer, and a ctest that removes its own work leaves the machine
> exactly as it found it.

**✅ The seed half is built.** `rules/detection-rules.json` now ships in the
Agent's output (`FrameLedger.Rules.targets`), and `RulesSeeder` installs it to the
product location. The Agent is the sole writer, per §S18's ratification, and it is
a real caller rather than a component nothing invokes: `Program.cs` runs it and
reports the outcome.

Measured end to end on this machine — remove the file, the guard answers
`RulesUnreadable`; run the Agent; the guard reads its rules and reaches check 1.

Four decisions worth keeping, each of which replaced something the design review
disproved:

- **Provenance, not `rulesVersion`.** The first design replaced the installed file
  when the packaged seed was strictly newer. Measured against this repository's
  own history, every commit that changed the `anticheat` block left `rulesVersion`
  untouched and the one commit that bumped it changed the block not at all — so
  the rule would have delivered **none** of the changes it existed for. The seeder
  records a hash of what it installed instead.
- **`ReplaceFileW`, not `MoveFileEx`.** See §S21: the prescribed primitive returns
  `ERROR_ACCESS_DENIED` against a live reader. `ReplaceFileW`'s backup parameter
  also makes `05_DETECTION`'s "the last valid copy is kept" a mechanism rather
  than a sentence.
- **Validated by the guard's own parser.** `FlGuardCheckRules` is a new
  observation-only export, because `DetectionRulesFile` never reads the
  `anticheat` block (§S15) — validating with it would have checked everything
  except the half the hard gate consumes, and could have installed a document the
  guard then refuses for every title while reporting success.
- **A usable file we did not write is left alone.** Safe only because §S21's floor
  is now generated from the shipped blocklist: a rules file can ADD and cannot
  remove. Under the narrow floor the same rule would have handed permanent control
  of the blocklist to whoever created the file first.

#### What is still open, and it is not small

- **The feed half.** `05_DETECTION` §Trust and staleness specifies an HTTPS fetch
  with validate-then-replace; none of it exists. So **FR-7.3 is unmet**: anti-cheat
  entries cannot reach a machine on their own schedule, only with a release. A
  rules edit still changes nothing on any installed machine until the next build.
- **No binary/data handshake inside the guard.** `ParseRules` still walks only the
  `anticheat` subtree and never reads `schemaVersion` or `rulesVersion`. Teaching
  it to refuse an unknown version would be a second machine-wide refusal lever
  pulled by data, and `rules-publish.yml` gates neither field — see `13_CI_CD`,
  which used to claim a bump check that does not exist.
- **`trustedSigners` is the one allow-widening field a foreign file still
  controls.** Deliberately not floored, because flooring an allowlist has the
  wrong polarity. Inert today — `IsTrustedSigner` has no production call site
  while §S19(b) is deferred — and **live the moment the signer half is wired**,
  which makes gating it a prerequisite of §S19(b) rather than of this item.
- **This is what arms the Vulkan layer's self-scan for the first time.** `layer.cpp`
  §S2's second half could only ever answer "inert" while the file never existed.
  It is now reachable, on a machine where the file is present. The layer still
  intercepts nothing (`vkQueuePresentKHR` is P1), so nothing observable changes
  yet — recorded here so it is not discovered as a surprise when the present hook
  lands.
- **A seed replacement is a rules update in FR-2.3's sense**, and the obligation
  attached to that event — re-run the pre-scan, force-disable a game that has
  started matching — is unimplemented. There is no `games` table yet, so there is
  nothing to write; named here so whoever builds persistence has an anchor.

### S21 ✅ · The hard gate's data source was caller-nameable, and its completeness floor never read the values — **closed**

Found 2026-08-04 by an adversarial review over the next-phase option set, and it
outranked every item that review was convened to sequence. Recorded in full
because it is the first *fail-open* found in the guard since `EnumDeviceDrivers`,
and because both halves passed every gate in the repository.

Two facts that were individually defensible and jointly an override:

| | |
|---|---|
| `RulesFilePath` built the path from `_dupenv_s(&base, &len, "LOCALAPPDATA")` | The CRT environment is **inherited**. In launch mode the guard's host is started by a shortcut, a `.bat`, or `04_CAPTURE:60`'s Steam launch-option wrapper — all of which choose that variable |
| `IsCompleteEnoughToGate` checked three family **names** in the right **groups** | It never read `values`. "Is this shaped like a blocklist" was standing in for "is this a blocklist" |

So a twelve-line rules file naming `Easy Anti-Cheat`/modules, `BattlEye`/modules
and `Riot Vanguard`/drivers with junk values parsed `kOk`, matched nothing, and
`Evaluate` returned `Allow` on a machine running Vanguard. **No admin, no write
to our install directory, nothing left on disk.** That is the override CLAUDE.md
rule 2 says does not exist.

`05_DETECTION:75-77` asserted the source "is not a parameter and cannot be
redirected". True of the pipe — §S3 closed that — and false of the environment,
which nobody re-asked. **When a doc says an input cannot be redirected, the
question is "through which channel", and the answer has to enumerate the rest.**

**Fixed by the floor, not by the path.** `fl::guard::FloorFamilies` carries the
blocklist inside the binary; `ParseRules` seeds it before reading a byte and
nothing merges, rewrites or removes it. §S8's mechanism applied to data: a family
data cannot remove cannot be bypassed. Path resolution moved to
`SHGetKnownFolderPath` as well, and that is written down as a **narrowing, not a
guarantee** — a user can still move their own Local AppData; what is gone is the
per-launch vector.

> #### The floor shipped too narrow, and that is worth recording (2026-08-04)
>
> As first written it carried **exactly the three families `IsCompleteEnoughToGate`
> required**, kept minimal on the grounds that a larger hand-written table would
> be a second copy of the blocklist and drift from the data. Measured against the
> shipped seed, that bought **4 of 22 values, 2 of 5 groups and 0 of 5 name
> fragments**.
>
> So this entry closed *"a crafted rules file makes the guard allow everything"*
> and left open *"a crafted rules file removes most of the blocklist"* — Denuvo,
> GameGuard, Xigncode3, mhyprot, FACEIT, ESEA, PunkBuster, EAC's directories and
> services, BattlEye's directories, Vanguard's service, and the entire fuzzy tier.
> The write-up above read as though the floor bounded more than it did.
>
> That gap was tolerable only while nothing delivered a rules file to any machine.
> The §S20 review is what surfaced it, because a seeder turns a repo-only defect
> into a **push channel**.
>
> **The floor is now GENERATED from `rules/detection-rules.json` at build time**
> (`tools/gen-ac-floor.ps1`), which removes the objection that kept it small — a
> table derived from the data cannot drift from it. It carries the whole shipped
> blocklist and the name fragments; `trustedSigners` is deliberately excluded,
> because it is an ALLOW-widening list and "data may only add" has the wrong
> polarity there.
>
> Two consequences worth stating. A file family identical to a floor entry is
> **deduplicated**, or an unmodified seed would spend `kMaxFamilies` twice — so
> `rules-validate.ps1` now bounds the file at **half** the cap, which is the
> worst case of a drifted file duplicating none of the floor. And completeness is
> judged on what the file **supplied**, not on what was stored, because an
> unmodified seed now stores nothing.
>
> **It also delivers §S19(d)'s substance** — a rules file with no `heuristic`
> block can no longer make the fuzzy tier stop existing — without the new
> `ParseResult` cause that entry proposed, which would have made
> `kRulesIncomplete`'s signal a lie and driven `layer.cpp` to machine-wide inert
> passthrough. A floor needs neither.

Kept able to fail: the completeness check now runs over the **file's** families
only. Running it over the merged set would have retired a real refusal by
accident, since the floor satisfies it by construction — a gate that cannot fail,
arrived at while fixing a different bug.

**A second, independent total failure in the same six lines.** The path was ANSI
(`char[MAX_PATH]`, `CreateFileA`), so a profile directory the system code page
cannot spell became a path containing `?`, and the guard refused **every title
for that user, permanently**, naming no cause. Measured 2026-08-04, system ACP
**1252**:

```
C:\Users\田中\AppData\Local    ->  C:\Users\??\AppData\Local
C:\Users\Nguyễn\AppData\Local  ->  C:\Users\Nguy?n\AppData\Local
```

Note what that measurement says and does not say. The trigger is the **system**
code page, not the user's language — a Japanese install (ACP 932) spells 田中 and
still mangles Nguyễn. So the honest form is "broken for any user whose profile
path the machine's ACP cannot represent", which the `ja`/`vi` shipping locales
make likely and which an ASCII profile can never expose. Now `wchar_t` +
`CreateFileW` throughout, including the Vulkan layer's enable-list, which had the
same defect and would have made Vulkan Tier 1 silently never work for those users.

**Three resolvers became one.** The guard, the Vulkan layer's enable-list and
`fl-probe-vklayer` each resolved Local AppData their own way, and
`DetectionRulesFile` made a fourth on the managed side while its own comment
claimed to reach "the same directory the native guard uses". `fl_ac_rules.h`
calls this "the ONE location" and says a second reader would be "a second
blocklist by accident" — there were four. `FlGuardRulesFilePath` now exports the
guard's answer for **observation only** (no setter, no path parameter), and
`RulesPathAgreementTests` asserts the managed resolution matches it. That
assertion matters most for §S20: a seeder writing where the gate does not read
reports success, and its own test would agree with it.

Sharing mode unified too — all readers now `FILE_SHARE_READ | FILE_SHARE_WRITE |
FILE_SHARE_DELETE`. The guard denied delete sharing and the layer denied nothing.

> **The primitive this prescribed was wrong, and it is corrected rather than
> quietly edited.** It said §S20's replace must be temp-file +
> `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`. Measured 2026-08-04 against a handle
> opened exactly as the guard opens it:
>
> | Call | Against a live reader |
> |---|---|
> | `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` | `ERROR_ACCESS_DENIED (5)` |
> | `ReplaceFileW`, backup file named | succeeds |
>
> Delete sharing is **necessary and nowhere near sufficient**. The unification is
> still right — it is what lets `ReplaceFileW` proceed — but the named call was
> the one that fails on exactly the machines where the guard is busy, and its
> error goes nowhere, so the update would simply not have happened. Same shape as
> the two `layer.cpp` comments that documented measured-wrong designs: a reader
> designs a gate around them.

Proven red before being called done. Two of the four canaries named here were
rewritten when the floor became generated — `kFloorFamilyCount` no longer exists
and no floor value can be hand-pointed — so the current set is: a generator that
drops four of the five groups, a floor keeping one of five fragments, a
`kMaxFamilies` below twice the seed, and a `kMaxNameFragments` below twice its
fragments. Emptying `FloorFamilies` still makes the disarmed-rules test allow, and
running the completeness check from index 0 still lets a file with no BattlEye
through.

#### Measured end to end, not only in fixtures (2026-08-04)

- **The override, on two real binaries.** Same input to both: a rules file naming
  the three required families with junk values, and a real
  `EasyAntiCheat_x64.dll` loaded in the target. `bb0da0a` → **`Allow`**; after
  the floor → **`BlockedModule`, family `Easy Anti-Cheat`**. The pre-fix half took
  the crafted file through an inherited `LOCALAPPDATA`; the post-fix half had to
  place it at the real path, because the environment no longer selects it.
- **The ANSI half, on the real Win32 calls.** System ACP 1252. A rules file
  written into `…\田中\AppData\Local\…` and `…\Nguyễn\AppData\Local\…` exists on
  disk and `CreateFileW` opens it; **`CreateFileA` fails with
  `ERROR_INVALID_NAME (123)`**. The same file under an ASCII profile opens both
  ways — which is precisely why a dev box cannot see this.

#### Two residuals, measured rather than asserted

- **The known-folder path is still user-relocatable, and that was the claim.**
  `HKCU\…\Explorer\User Shell Folders\Local AppData` matches what the API returns,
  and the key is `FullControl` for the current user with no elevation. So the
  wording in `05_DETECTION` holds exactly as written: this removes the
  **per-launch, per-process** vector, not every redirection. Deliberately not
  tested by repointing it — that would change the machine's shell configuration
  for every application.
- **The Vulkan layer gained two DLL dependencies, and it is mapped machine-wide.**
  `dumpbin /dependents`: `KERNEL32` → `KERNEL32, ole32, SHELL32`. Measured
  residency across 189 inspectable processes: shell32 **62%**, ole32 **61%** — so
  for roughly a third of processes these are genuinely new. The exposure is
  narrower than that number suggests, because `enable_environment` means the
  layer only *maps* into processes the Agent launched (§S2), and a game is far
  more likely than a browser to have both already. Recorded rather than waved
  away; delay-loading would not help, since the layer resolves the path at init.

### S22 ✅ · The guard gated the TARGET and nothing gated the PAYLOAD — **both halves closed**

Found 2026-08-04 by a completeness critic over the next-phase plan, in code that
had been merged for days and passed every gate in the repository. Recorded in
full because it is the second *fail-open* found in the guard, and because the
reason it stayed invisible is instructive: every reader, every doc and every
review framed the guard as a gate whose subject is **the process we are entering**.
Nobody asked what we were putting there.

#### (a) ✅ Any DLL, into any process without anti-cheat — **closed**

`FlGuardedInject(targetPid, dllPath, out)` is an exported C ABI on the shipped
`FrameLedger.Guard.dll`. Between that boundary and `CreateRemoteThread` the only
thing ever asked of `dllPath` was `GetFileAttributesW` — *exists, and is not a
directory*. Measured through the shipped DLL with no test seam:
`C:\Windows\System32\winmm.dll` into a live process, verdict `reason=0 (Allow)`,
module present afterwards.

That is **§S9's user-runnable injector**, which this project refused to ship,
re-shipped as a documented export with a published calling convention. §S9's own
reasoning names the hazard exactly and then stops one step short — it rejected a
standalone injector as "a path into a game process **that the guard did not stand
in front of**", and `fl_guard_abi.h` concluded "there is no entry point here that
skips a check". Both true, and both about the target.

Note what the shape is, because it is this file's recurring one: not a wrong
assertion, a **missing** one, behind a scope sentence that reads as though it
covered everything. Every existing test passed `FL_OVERLAY_DLL` as the payload,
and the one negative case used a *missing* path — so the whole matrix exercised
"is there a file there" and nothing exercised "whose file is it".

**Closed by a new seam and a new reason.** `Sources::PayloadIsOurOwn` resolves
the payload — through symlinks, 8.3 names and junctions, via
`GetFinalPathNameByHandleW` — and requires its directory to be the one the
guard's own code was loaded from, compared by file id. Equality, not containment,
reusing §S18's `OwnDirectory`/`SameDirectory`, which until now existed only to
*relax* this gate. A null seam, a seam that cannot answer, and "not ours" are one
outcome: `Reason::kPayloadNotOurs`.

**What it does NOT cover, with the boundary stated rather than implied:**

- It is not proof the payload is `FrameLedger.Overlay.dll`. It is proof of
  **where the bytes live**. Anyone who can write to that directory can already
  replace `FrameLedger.Guard.dll` itself — the same trust base §S18 rests on, and
  the project ships unsigned (CLAUDE.md rule 9), so there is no integrity check on
  that directory's contents.
- It is not atomic with the load. The remote `LoadLibraryW` re-resolves the path,
  so a file swapped between check and call is not caught. Same trust base again.
- **Absent** and **foreign** are kept distinct on purpose: a damaged install and a
  misuse of the ABI need opposite responses, so a path naming no file still
  returns `kInjectionFailed`. The existence test therefore runs *before* the
  identity test — otherwise "not ours" absorbs "not there", since the identity
  seam works by opening the file. Caught by the pre-existing test that asserts
  exactly this distinction.

**A layout bug came with it, and it had to be fixed in the same change or the
gate could not pass.** Nothing in this repository copied `FrameLedger.Overlay.dll`
anywhere. The native build left it in `FrameLedger.Overlay/` while the guard
built into `FrameLedger.Injector/`, and `dotnet publish` produced an `out/app`
with the Agent, the guard and the rules seed **but no payload at all**. Invisible
while the Agent had no injector control; it would have surfaced as an
unexplained `kPayloadNotOurs` the first time one was wired.
`FrameLedger.Overlay.targets` now ships it beside the Agent, and
`src/native/tests` stages its own copy beside `fl_guard_test` — where the guard
is compiled *into* the test binary, so "the guard's directory" is the test's.

Proven red **and** green, and proven to recover: three canaries (the refusal
removed, a seam failure treated as ours, a null seam allowed through) each turn
`fl_guard` red. The green direction is asserted separately, because a gate that
refuses every payload carries as much information as one that accepts every
payload — the staged Overlay must be accepted, the *same file staged elsewhere*
must not, and the test refuses to run at all if those two paths ever resolve to
one directory.

> **The canary harness lied again, and the mechanism is worth writing down.**
> Restoring the file from a backup with `Copy-Item` preserved the backup's
> **old** timestamp, so ninja judged the source up to date and never rebuilt —
> leaving the last canary's binary in place while the tree read clean. It then
> segfaulted on the exact case that canary had disarmed, which is a symptom that
> invites diagnosing the code instead of the harness. Restores must stamp the
> file. That is the fourth time this session's own verification tooling was the
> broken thing.

#### (b) ✅ The §S18 exemption asked about the PROCESS, not the module — **closed**

Same probe, same binary, same target; only the caller's directory differs:

| Caller's location | Verdict |
|---|---|
| Beside `FrameLedger.Guard.dll` | `Allow` |
| Anywhere else | `SuspiciousUnsigned`, family `unknown`, signal `FrameLedger.Guard.dll` |

`SuppressFragmentTier` delegates to `ProcessIsOurOwn`, which compares the
scan-set **process image** directory to `OwnDirectory()`. The module that matched
is *our own guard DLL* — it contains the fragment `guard` — but the exemption
never asks about it. So any FrameLedger-family host that does not sit beside the
guard poisons its own scan set. The Agent works by accident:
`FrameLedger.Guard.targets` copies the DLL beside `FrameLedger.Agent.exe`.

This matters now rather than in the abstract, because every throwaway
inject-host proposal puts the tool in its own build directory — the arrangement
measured RED — and a launch-mode host is by construction in the §S16 scan set.
Whoever hits it will read the refusal as the fuzzy tier being over-eager (§S19
says so, in writing) and go rewriting a safety gate instead of moving a file.

**The right fix is to key the exemption on the MATCHED MODULE** — a scan-set
process is exempt when the module that tripped the fragment tier is ours by file
id. Strictly narrower than today's process-level exemption, and it fixes launch
mode. It also removes a real over-reach nobody had costed: today a genuinely
foreign suspicious module loaded into a FrameLedger process — an AppInit DLL, an
AV user-mode hook, an IME — is suppressed along with our own.

**Done, and it was not the size the plan assumed.** `NameSink` was
`bool(*)(void*, const char*)` fed by `GetModuleBaseNameA`, so the module's path
never reached the decision point — the constraint §S19(b) already recorded. What
it actually took:

- **A new sink type.** `ModuleSink` carries `(name, path)`; `EnumerateModules`
  takes it, drivers keep `NameSink`. `GetModuleFileNameExW` failing is **not**
  `kIncomplete` — the base name is what the blocklist matches on, so the scan is
  complete in the sense that matters. What is lost is only the ability to
  *exempt*, and a null path is treated as "not ours". The failure narrows what we
  allow rather than widening it.
- **The restructure §S19(b) predicted.** `NameSinkFn` latched the FIRST
  fragment-matching module and skipped the fragment test for every module after
  it. Harmless while any hit refused, because the latched name only had to be *a*
  reason. Per-module suppression turns it into a **fail-open reachable by load
  order**: our guard DLL matches first, is exempted, and a genuinely suspicious
  module loaded afterwards is never recorded. `ModuleSinkFn` therefore skips an
  exempt module and **keeps looking**, latching the first non-ours one — and
  still does not stop, because an exact blocklist hit later in the same process
  outranks the fuzzy tier.
- **`ProcessIsOurOwn` is gone**, along with its implementation. A seam nothing
  calls is dead weight, and keeping both forms would be two answers to one
  question.
- **One implementation, two seams.** `ModuleIsOurOwn` and `PayloadIsOurOwn` ask
  the identical question and `SystemSources()` wires both to `FileIsOurOwnImpl`.
  They are separate *pointers* only so the matrix can force each failure
  independently; a live test asserts the two pointers are equal, so a future
  divergence fails rather than quietly becoming a second matcher (§S15 item 1).

The module form is also **strictly narrower**, which nobody had costed: a
genuinely foreign suspicious module loaded into a FrameLedger process — an
AppInit DLL, an AV user-mode hook, an IME — used to be suppressed along with our
own, and now is not.

Five canaries, each proven red: the exemption stopping the scan (the load-order
fail-open), the target exclusion removed, an unlocatable module treated as ours,
the module seam's failure ignored, and — re-run precisely — the payload seam's
failure ignored.

> **Two of this item's own tests were passing for the wrong reason, and the
> canaries are what said so.**
>
> The first: `FakeModuleIsOurOwn` returned `kFailed` *without touching the
> out-param*, so "the seam cannot determine" was indistinguishable from "not
> ours" and the test held whether or not the caller read the return code. The
> fake now writes **true and then fails**, which is the realistic shape and the
> only one that can catch a caller which ignores the code.
>
> The second was found by a canary going GREEN: removing the guard's
> `modulePath == nullptr` clause changed nothing, because the fake *also*
> rejected null. The test was covering the fixture, not the code. The fake now
> answers "ours" for a null path — wrong on purpose — so the guard's own clause
> is the only thing standing between a sloppy seam and an exemption. **A
> redundant check in a fixture can make a real clause untestable, and the only
> way that surfaced was disarming the clause and watching nothing happen.**

> **The §S22(a) canary that ran a day earlier was mislabelled**, and it is
> corrected rather than left. It replaced the payload check's condition with
> `false && sources.PayloadIsOurOwn(...)`, which **short-circuits the call
> entirely** — so it proved "removing the seam call breaks the suite", not "a
> seam failure that is ignored breaks the suite". The precise form ignores only
> the failure code and leaves the call in place; it is red too.

### S23 · What the 2026-08-05 handoff audit found and did NOT fix

Eight PRs merged in one session; the audit ran afterwards, as
[adversarial review] prescribes, and found 53 items. The staleness was fixed in
the same PR that records this. What follows is the residue — real gaps, recorded
so the next session finds them by reading rather than by tripping over them.

**1. `FL_BUILD_ID` has a writer and no reader.** §S22-era work gave
`FlShmHandshake::buildId` a producer, which it had never had. But the contract is
a *comparison* — `07_IPC` "the Agent compares … against its own",
`04_CAPTURE` "validate layout version + build id against our own" — and the
managed side has no build id and no way to obtain one: `FL_BUILD_ID` is a CMake
INTERFACE compile definition visible only to native targets, and `grep -rni
buildid` over `src/**/*.cs`, `*.csproj` and `*.props` returns **zero**. So the
refuse-to-attach-on-mismatch gate still cannot run. Half a mechanism reads as a
whole one in the CHANGELOG, and that is corrected here rather than there.

**2. `rules-publish`'s removal check is not a required status check.** `main`'s
required contexts are exactly `check`, `analyze (csharp, none)` and
`analyze (cpp, manual)`. The `validate` job is path-filtered and runs on
`pull_request` only, so a red removal check **does not block the merge button** —
the gate that exists to make the anti-cheat blocklist un-removable is advisory.
Fixing it is a branch-protection change, i.e. the owner's, not a code change.

**3. `08_UI` describes a refusal notice that is wrong in the commonest case.** It
promises "the specific signal named in plain language". Measured (`spike-notes`
§13): while any Easy Anti-Cheat title is running, **every** target — including a
freshly spawned, unrelated process — refuses with `BlockedService` naming
`EasyAntiCheat_EOS`, because checks 2 and 2b are machine-wide. The user is shown
the name of a game they are not playing. The requirement created by that
measurement was written only into the spike log; it needs to reach `08_UI` and
the `Safety_*` resx keys before any UI exists.

**4. The runtime re-scan loop is described as two checks and implements four.**
`19_SAFETY` §During a session reasons explicitly about the loop's composition and
names the module and driver scans. `GuardSupervisor.ScanOnceAsync` →
`FlGuardEvaluate` → `EvaluateImpl` runs drivers, **services** and the **static
pre-scan** as well. That omits the only tier ever measured firing on real
anti-cheat (services) and the only one touching the filesystem (check 4) — whose
cost this session raised by deepening the walk. The paragraph that decides what
the loop costs is missing the expensive half.

**5. A fourth statement of the gate's composition lives in the shipped data.**
`rules/detection-rules.json`'s own `$comment` describes checks "1, 2 and 4",
omitting 2b and omitting `services` from the signals that catch the seed — in the
one copy that ships to users. Three doc-side variants were reconciled this
session; this one was not, and `rules-validate`'s doc/data cross-check reads the
blocklist table, not the comment.

**6. `legal/` is audited by nothing.** Every other document is bound to the code
by something — `rules-validate` cross-checks the blocklist against `19_SAFETY`,
`static_assert`s bind `fl_shm.h` to `07_IPC`, `versioninfo-check` and
`chokepoint-check` bind claims to binaries. `legal/` has one hand-written
accuracy block, in one of four files, with a hardcoded count. It went stale
within hours: a fourth unimplemented promise was added directly beneath a header
saying "Three". The block now says so about itself, which is a note and not a
gate.

> **◐ Partly closed 2026-08-05, and what it cost to find is the point.**
> `license-check.ps1` now binds `THIRD_PARTY_NOTICES.md`'s bundling claims to the
> filesystem, bidirectionally, failing rather than skipping on a renamed row. That
> is the first real gate over anything in `legal/`.
>
> It was written because the un-audited half was **already false in two rows**:
> NVAPI ("**Yes** — headers and import library vendored. **Verified 2026-08-02**")
> and Intel PresentMon ("Bundled as a pinned native binary; SHA-256 verified at
> build"). Neither exists — `src/native/third_party/` holds `CMakeLists.txt` and
> `vulkan-headers`, and `assets/` is not a directory. The old check keyed on the
> path a component *would* occupy, so it could only ever fire on
> vendored-without-a-licence; the reverse was structurally invisible.
>
> **Still open, and it is the larger half:** the *accuracy blocks* — DISCLAIMER's
> four-going-on-five unimplemented promises, and now this file's bundling audit —
> remain hand-maintained prose that nothing verifies. A gate over one table is not
> a gate over `legal/`.

### S14 ◐ · Pre-injection check 3 is **unwired**, and has no "cannot determine" state

Found 2026-08-02 while hardening the rules toolchain. `19_SAFETY` §Pre-injection
checks lists a per-title blocklist as check 3, but `anticheat.blockedExecutables`
and `anticheat.blockedStoreIds` are **both empty arrays**, so the check matches
nothing.

> **Corrected 2026-08-03, and the true cause is worse than the recorded one.**
> The empty arrays are the *second* reason check 3 matches nothing. The first is
> that `MatchesBlockedExecutable` and `MatchesBlockedStoreId` have **no call site
> anywhere in the tree** — `EvaluateImpl` runs `LoadRules → CheckDrivers →
> CheckServices → CheckModules` and stops. Populating the data would change
> nothing. Check 3 is unwired, not unpopulated.
>
> The sentence below that read "checks 1, 2 and 4 run" was wrong twice: **check 4
> had no implementation either** — `Reason::kAntiCheatDirectory` and
> `kAntiCheatFile` were declared, named in `ReasonName` and mirrored into the
> managed enum, and nothing produced either.
>
> **Check 4 is now implemented** (`fl_prescan.cpp`, inside `EvaluateImpl`), so
> checks 1, 2, 2b and 4 run. **Check 3 remains unwired** and this item stays
> open on that.
>
> The parser now reads both per-title arrays in their real object shape, so the
> data can be written before the wiring lands — it used to read them as bare
> strings, and the first entry ever added would have refused the whole rules
> file (§S17).

The gate is not currently weakened — checks 1, 2, 2b and 4 run, and every
family in the seed is caught by a module, driver, service or directory signal —
but a documented check that does nothing will read as "this title is not a known
online title" to the next person who trusts it.

Two decisions, both the owner's:

1. **Which titles.** Seeding a list of competitive/online games is a product
   decision with false-refusal consequences, not a mechanical fill-in.
2. **What "unknown" means.** A title the user added by exe path has no store id,
   and store metadata is opt-in (CLAUDE.md rule 8). "No store id" must not read
   as "not blocked", and a renamed exe defeats `blockedExecutables` the same
   way. Whatever ships, an unresolvable identity has to land on *unknown*, and
   the doc must say where unknown goes — which today it does not.

Until both are answered the inertness is recorded in the data's own `$comment`
and beside check 3, rather than being inferable only from two empty arrays.

### S6 · The 30 s scan window is the weakest part of the most important behavior

Now disclosed honestly in the Disclaimer and README. The open question is whether
to shrink it.

> **"Already installs" was false, and it changed how this item read.** Corrected
> 2026-08-04. `src/native/FrameLedger.Overlay/` contains two files and
> `dllmain.cpp` exports exactly one function; there is no `MH_CreateHook` outside
> `fl-probe-hookprofile` anywhere in the tree. So §S6 is **not** "the cheapest
> available improvement" — it is downstream of the entire P1 hook layer, and a
> reader was being told a mechanism sat there unused. `17_HOOK_ENGINE` §DLL entry
> words the same thing correctly, as *specification*. The sentence below is
> retained in its original form because the plan it describes is still the plan.

The Overlay is specified to install a `LoadLibrary` hook for lazily
loaded graphics DLLs (`17_HOOK_ENGINE` §DLL entry); the same hook could raise the
control-block flag the moment a blocklisted module name loads, turning a 30 s
poll into near-immediate detection. This is the cheapest available improvement to
the behavior the product treats as most critical.

**Needs:** confirmation that a name comparison against a small fixed table is
acceptable in that hook under the no-allocation rule (it should be), and a
decision on whether it supplements or replaces the poll. Supplements — the poll
also catches modules loaded before we hooked.

### S7 ✅ · Guard handle rights and WOW64 — **closed**

Measured unelevated 2026-08-02 by `src/native/tools/fl-probe-guard` (ctest
`fl_guard_apis`); evidence in `spike-notes.md` §1, rule now written into
`19_SAFETY` §Pre-injection checks item 1.

The read handle rights are sufficient. `LIST_MODULES_ALL` is mandatory: on a
live 32-bit target the default filter returned **7 of 15** modules *as a
success*, which is the under-report that would read as clean. And every failure
of the call — `ERROR_ACCESS_DENIED (5)` on a protected target,
`ERROR_PARTIAL_COPY (299)` on a suspended one — means REFUSE, never "no modules
found".

The one branch that could **not** be measured here is a service query returning
`ACCESS_DENIED`: a standard user holds `SERVICE_QUERY_STATUS` on the stock
service set, so it has to be driven from a unit-test fake. Recorded in
`spike-notes.md` §1 rather than left to look covered.

### S8 ✅ · The absence-of-override mechanism — **closed**

The intent was right and is kept; the mechanism was wrong and is replaced.
Rewritten in `14_TESTING` §Safety-guard tests.

The original — a `sealed` token type "only the guard can produce", passed as a
constructor argument — was disproved by compiling: C# accessibility flows
*inward*, so an enclosing type cannot reach a nested private constructor
(`CS0122`). (`internal` *does* hold across an assembly boundary — `CS1729` when
`Infrastructure` tries to forge one — but that was never the weak part.)

The real weakness is that **any token which escapes can simply be ignored**: a
caller can decline to ask for one. §S13(a) put the guard in C++, which makes the
stronger shape natural — the guard **owns the chokepoint** and calls the
injection primitive itself, the primitive has internal linkage in the guard's own
translation unit so no other TU has a symbol to call, and a build-time check
asserts nothing else references it. A token that escapes can be ignored; a symbol
that does not exist cannot be called.

### S9 ✅ · `FrameLedger.Injector.exe` — **closed: it does not exist**

Decided 2026-08-02, written into `12_BUILD` §Targets. `FrameLedger.Injector` is
a **static lib only**. A user-runnable `LoadLibraryW` injector is a path into a
game process that the guard does not stand in front of, and no invocation
contract makes that safe — a guard result crossing a process boundary is exactly
the forgeable clearance §S13(b) rejects. The Agent links the lib; the guard owns
the chokepoint inside it, so there is no entry point that skips the gate. Manual
testing uses `hook-harness`.

### S10 ✅ · Machine-wide layer registration without consent — **closed**

Written into `12_BUILD` §The Vulkan layer is not registered at install time.
The install-time registration is removed; the rule is now the one
`17_HOOK_ENGINE` §Vulkan always stated — **registered only while at least one
Vulkan game has hooking enabled**, unregistered when the last is disabled and on
uninstall, under `HKCU`. The `--register-vklayer` flag and the Settings button
are repair tools that reflect that state, not independent grants of machine-wide
reach.

Note this is a *consent and blast-radius* fix, not the §S2 guard fix. A
registered layer still loads into every Vulkan process while it is registered;
narrowing *when* it is registered reduces the window, and `enable_environment`
plus the in-layer scan (§S2) is what closes the hole.

### S11 ✅ · FR-2.2 / FR-2.3 interaction — **closed**

Specified in `19_SAFETY` §A game already enabled can become blocked later. The
pre-scan re-runs on every rules update and every exe change; a new match forces
`hook_enabled = 0` and sets `hook_blocked_reason` — **no schema change needed,
the column already exists** (`06_DATA_MODEL` §games) and a non-null value already
means "toggle disabled". `hook_consent_at` is preserved: the block is not a
withdrawal of consent. A rules update that removes a match does **not** re-enable
hooking on its own, because that would let the rules feed switch injection on for
a game with nobody looking.

### S12 ✅ · "Cautious mode" — **closed by deferring it, explicitly**

Deferred to v1.1 with the overlay, recorded in `19_SAFETY` §Crash & stability
safety. It was defined as "hooks installed, overlay drawing disabled" and **v1
draws no overlay** (FR-15 is v1.1), so it disabled nothing — a no-op dressed as a
safety measure, which is worse than an absent feature because it reads as
coverage. The breadcrumb is still written and still read in v1; the behaviour
that actually protects the user is the existing two-crashes-in-60s auto-disable.

---

## H — Native hook layer. Blocks P1.

> **H1 and H3 are resolved, and both flags are now applied** to the Overlay and
> the Vulkan layer via the `fl_hostile_env_flags` interface target. Evidence in
> `spike-notes.md` §3; the probe lives at `src/native/tools/fl-probe-hookprofile`
> and runs as ctest `fl_hook_profile`, so a regression fails the build rather
> than reaching a game.
>
> Two residual risks were recorded rather than waved away, and both feed §H8:
> CFG **strict mode** was off during the measurement, and `-D_HAS_EXCEPTIONS=0`
> turns a would-be throw into `__fastfail` — an uncatchable kill of the host
> process.

### H2 ◐ · `LoadLibrary` hook, the loader lock, and MinHook's thread suspension

**Partly answered — the safe path is verified, the hazard is still an argument.**

`fl-probe-hookprofile` ran 20 MinHook enable/disable cycles, each suspending
every other thread to patch, while a worker performed 4,510
`LoadLibrary`/`FreeLibrary` cycles. No deadlock. So the **deferred** pattern
`17_HOOK_ENGINE` §DLL entry mandates is sound under heavy loader contention.

What is *not* proven is that the naive inline install deadlocks. A probe that
deadlocks cannot report its own result, and hanging CI to demonstrate a hazard
we have already chosen to avoid buys nothing — so the case against inlining
still rests on the mechanism (suspending a thread that holds the loader lock),
not on a measurement.

**Remaining:** exercise the deferred path against a real game that loads D3D12
lazily, which is the case the hook exists for. Until then, keep the rule and do
not let anyone "simplify" it back to an inline install on the grounds that the
probe never showed a deadlock.

### H5 ◐ · Proxy swapchains defeat the dummy-vtable assumption

**Partly answered, and the news is better than expected.** `hook-harness
--probe-proxy` builds a real forwarding `IDXGISwapChain` wrapper — what
`sl.interposer` and ReShade hand the application — and presents through it with
our hook on the **real** vtable. The hook still fires: the proxy forwards via
`real_->Present(...)`, an ordinary virtual dispatch, so patching the real vtable
catches it one layer down. A proxy having its own vtable does *not* by itself
make us miss the present.

**What still has to be measured on a real title:**

1. A forwarding proxy is the easy case. DLSS-G presents *interpolated* frames
   the application never submitted — whether those reach a real-vtable hook is
   what actually matters for the FG counting in `03_METRICS` §Frame Generation,
   and the harness cannot simulate it.
2. We observe the post-proxy call, so parameters the proxy rewrote are what we
   see, not what the game passed.
3. A proxy that owns a *different* swapchain, rather than wrapping ours, would
   still be invisible.

Needs a real Streamline/DLSS-G title, ideally with RTSS and the Steam overlay
also active (`spike-notes.md` §Environment — that machine already has both).

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

### H7 ✅ · Vtable restore on unhook clobbers later hookers — **closed**

Fixed and specified in `17_HOOK_ENGINE` §Compare-and-restore, never
unconditional restore. Verified by `hook-harness --probe-unhook`, ctest
`fl_unhook_preserves_foreign`.

The "cleaner uninstall" claim was backwards: a later hooker saves **our detour**
as its original and chains through it, so writing the pristine address back
removes their hook silently. Both halves of the contract are asserted — we
decline to restore when the slot changed, **and** we do restore when it did not,
because a compare-and-restore that never restores is not a fix.

Simulated rather than depending on RTSS being installed, so it is deterministic
and runs on CI. Confirming against the six overlays actually resident on the dev
machine (`spike-notes.md` §Environment) is still worth doing once the Overlay has
real hooks, but the mechanism no longer rests on that.

### H8 ✅ · "Never crash the game" — **closed**

NFR-3 reworded in `02_SPEC` to what the mechanism can actually support: faults
*originating in our own hook bodies* are contained, counted and self-disable
after three. The absolute promise is gone, and stays out of user-facing text —
the Disclaimer's existing "injection carries risk" wording is the honest one.

Recorded alongside it, because it is the sharpest instance: `-D_HAS_EXCEPTIONS=0`
turns a would-be STL throw into `__fastfail`, which SEH cannot intercept
(`spike-notes.md` §H3). The "no throwing STL in hook paths" rule is therefore
load-bearing rather than stylistic.

### H10 · Per-process VRAM thread inside the game process

`17_HOOK_ENGINE` §Memory calls `QueryVideoMemoryInfo` "once per second from our
own thread". Spawning a thread inside a host process we do not own is a heavier
footprint than the rest of the design's posture, and DXGI calls off the render
thread need checking. Consider sampling on the present hook every N frames
instead, which needs no thread at all.

---

## M — Metrics and telemetry. Blocks P2.

> **M3 and M4 are resolved — both licence questions came back clean.** They were
> the two items that needed no hardware and could each have invalidated an
> entire telemetry layer, so they were answered first. Evidence and the exact
> reasoning are in `spike-notes.md` §0; the licence texts now ship in
> `legal/licenses/`. The remaining M-items still need measurement.


| # | Question |
|---|---|
| M1 | Can PresentMon 2.x `FrameType` see **driver-level** frame generation (AMD AFMF)? `03_METRICS` now says v1 cannot detect it at Tier 1. If Tier 2 can, that is a genuine and surprising capability inversion worth surfacing in the UI |
| M2 | Does the pinned **PresentMon console binary** still exist as a bundleable artifact, run unelevated, and emit the 2.x column set over stdout? `15_ROADMAP` parks the Service + API2 in v2, so there is no planned fallback if the console is gone |
| M5 | **Do LHM GPU sensors work unelevated, without PawnIO?** This decides whether the default unelevated Agent has temperatures at all, and therefore how ADR-9 reads to users |
| M6 | Can a documented one-time "add me to Performance Log Users" step, or the PresentMon Service, restore genuinely elevation-free Tier 2? Would change the README, Disclaimer and EULA wording back |
| M7 | `18_GPU_VENDOR_APIS` §Runtime policy says telemetry is never read from the game process, but `17_HOOK_ENGINE` reads per-process VRAM and Reflex latency there. Reconcile the wording — the rule means "no vendor SDK polling loops in the game", not "no measurement in the game" |
| M8 | The `GpuSample` type has no latency field, yet L3 is credited with Reflex/PC latency. Latency is per-frame and arrives via the ring, not the 1 Hz sample. Fix the layering description |
| M9 ✅ | **Closed 2026-08-02 by a decision.** The old file/module detection does not exist in this repository and the owner confirmed there is no copy elsewhere, so **"build a minimal static-hint detector as the measurement baseline" is now P0 item 3** (`15_ROADMAP`). It needs no guard and no injection. Until it exists, ADR-7's headline claim stays out of the README rather than being asserted unmeasured |
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

~~1–4 are folded into `15_ROADMAP` (2026-08-02) and are no longer proposals:~~
the guard is **item 0**, the Vulkan passthrough test is **item 1**, the two
licence checks were run first and came back clear (`spike-notes.md` §0), and
P0's exit criteria no longer import P2 work — the FPS-impact measurement moves
to the end of P1, while the harness-level per-present cost (no game, no Agent,
no drain) stays in P0. Items 5–9 below are still open.
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
10. **Four artifacts the documents name do not exist, and one of them is claimed
    as a gate that runs.** Found 2026-08-04 by an audit of the repository's own
    status records, and grouped here rather than fixed one by one because the
    decision is the same for all of them: build it, or stop describing it as
    present.

    | Named where | Artifact | State |
    |---|---|---|
    | `12_BUILD:139`, `13_CI_CD:11`, `09_I18N:28`, `CLAUDE.md:66` | `tools/resx-audit` | Absent. **Honestly handled** — `build.ps1:309` skips it loudly with a reason, and no `.resx` files exist yet |
    | `12_BUILD:136`, `13_CI_CD:11` | the managed **struct-mirror** test | Absent. Nothing under `tests/` references `FlFrameRecord`. Both docs state `dotnet test` includes it, so this one reads as a gate that runs |
    | `13_CI_CD:21`, `12_BUILD:121`, `CHANGELOG:9` | `.github/workflows/release.yml` | Absent. `CHANGELOG`'s header instructs an author to write for a consumer that does not exist |
    | `12_BUILD` §Local quality gate | three gates omitted from the list | `build.ps1` runs **13** steps; the doc lists 10, missing `coverage-gate`, `versioninfo-check` and `chokepoint-check` |

    The struct-mirror row is the one that matters. `CLAUDE.md` §Struct mirroring
    makes that test the mechanism protecting the shared-memory ABI, and a doc that
    says a gate runs is worse than a doc that says it is missing — the second
    prompts someone to write it. **`build.ps1`'s skip-loudly discipline is the
    right answer here**: a gate that is not written should be named and skipped,
    not omitted.

    Not urgent — the ring is P1 and there is nothing to mirror yet. Recorded so
    it is found by reading, not by trusting.
