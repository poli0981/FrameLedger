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

**✅ The Agent half is built and proven.** `GuardSupervisor`
(`FrameLedger.Application`) publishes `guardTicks`, and the load-bearing property
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

### S15 ◐ · The four consequences of putting the guard in C++ (§S13(a))

Not questions — commitments the §S13(a) decision creates. **Three of four are
done; item 1 is the one still open.**

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
to shrink it. The Overlay **already installs a `LoadLibrary` hook** for lazily
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
