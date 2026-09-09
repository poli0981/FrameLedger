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
| — | **D3D9 is not a Tier-1 API in v1.** The Overlay is x64-only; an x64 DLL cannot load into a 32-bit process, and D3D9 titles are almost entirely 32-bit | **The consequence hardened on 2026-08-28 without a word of this row changing**, which is why it is called out: that catalogue used to be "Tier 2", meaning frame times without injection. Tier 2 now measures nothing, so those titles are **unmeasurable in v1**. The VN / JRPG / older-indie catalogue is Tier 2. Reversing this means a second 32-bit Overlay **and** injector, doubling the native build matrix and adding a second struct-mirror surface. Revisit only with evidence that users care more about those titles than about the maintenance cost |
| — | **`ci.yml` is repo-local**, not a caller stub | The ops repo's `reusable-desktop-csharp.yml` runs `dotnet` directly with no native pre-step input, and `12_BUILD` requires CI and local to run the same script |
| — | **No `v1 → v2` migration** | Nothing shipped, so no such database exists. `0001_init.sql` creates the current schema |
| — | ~~**Multi-process titles are refused as `TargetAmbiguous` in v1**~~ — **the Chromium case is RESOLVED the same day (2026-09-03), by the vendor's own label** | Chromium-based runtimes — NW.js, Electron, RPG Maker MV/MZ, a large share of the VN catalogue — run several processes from ONE image path (browser, renderer, GPU), and *Flower in Us* refused as `TargetAmbiguous`. The presenting process is Chromium's GPU process, which owns no window, so "the one with the window" and "the parent" were both wrong picks. **What resolves it:** Chromium marks that process with `--type=gpu-process` on its own command line. `TargetResolver` now reads each candidate's command line through `NtQueryInformationProcess(ProcessCommandLineInformation)` — **served by the kernel, no `ReadProcessMemory`, no PEB walk, `PROCESS_QUERY_LIMITED_INFORMATION` only**, which is the rule-4 line `HeldProcessHandle` already draws — and resolves only when **exactly one** readable candidate carries the flag as a whole argument. Two GPU processes (two instances), none, or any unreadable candidate still refuse. §S27 is untouched: consent is keyed on the path, every candidate is that path, the guard scans the pid chosen, and `--pid` stays forbidden. **Measured the same evening, and the first rule did not fire:** *Flower in Us* runs **six** processes — browser, `crashpad-handler`, three `utility`, `renderer` — and **no `--type=gpu-process` at all**. NW.js runs the GPU **in-process, in the browser**, which is the one process Chromium leaves untyped. Second rule added: exactly one untyped candidate, every other candidate typed, no GPU process ⇒ the browser is the target; two untyped candidates are two instances and refuse. The refusal line now prints the tree (`browser=1, renderer=1, utility=3, …`) so the next unknown shape is data rather than a bare `TargetAmbiguous`. **Still unmeasured:** whether the Overlay loads and sees presents inside that browser process |
| — | **Intel XeSS / XeFG identity is closed at the census — no hook on any `libxess*.dll` export** (decided 2026-09-04, delegated by the owner) | The XeSS SDK failed `18_GPU_VENDOR_APIS` §Checklist (Intel Simplified Software License: binary-only grant, no reverse engineering, termination; headers forbid copying), so no declaration may be vendored or re-declared. The signature-free counter that seemed to survive that — an assembly thunk on a named export — still inline-patches Intel's code inside the game process, which the same licence forbids as *modification*; it would also be the only entry outside `FL_HOOK_GUARD` and a second build language, for identity alone. An Intel title therefore reports upscaler `NOT_REPORTED` and, with `libxess_fg.dll` in the census, Presented FPS under rule 6's *"MAY include generated frames"* qualifier — honest, and the same answer NGX-direct got. **Reversal condition:** Intel relicenses the headers so the checklist clears; then §H11's AMD route applies to Intel verbatim |
| — | **A title whose upscaler or frame generation is compiled into its executable is Presented FPS + the census's qualifier, in attach mode, and that is the ceiling for IDENTITY** (recorded 2026-09-04; **two such titles found the same night — Rune Factory, and Black Myth: Wukong for its upscaling, which ships and loads the 1.1.x monolith and never dispatches to it**) | No module for the census, no export for the inventory: nothing by name. What survives static linking is behaviour on interfaces we hook — the FSR3 frame-generation proxy swapchain at *creation* (launch mode only, §S1 deferred) and queue-side cadence with no vendor name on it. ~~**No installed title has this shape**~~ — **Rune Factory: Guardians of Azuma has it**: `amd_fidelityfx_dx12.dll` ships beside the executable and is never loaded (the census has no AMD module), FSR frame generation is on, and the report read `59.99 → 119.95 (×2 FG)`, *technology not identified* — because on a UE5 title **Streamline's frame token is requested whether or not NVIDIA's features are the ones running**, so the application-frame COUNT survives static linking even though the identity cannot. The ceiling is therefore lower than this row first said: the trio with an unidentified technology, not Presented FPS alone (§H11's run table). Identity for such a title stays out of reach by name |
| — | ~~**Tier 2 requires an elevated Agent**~~ | **CLOSED 2026-08-28 by the two-rung ladder.** The requirement came from Windows restricting ETW trace sessions; there is no ETW rung, so **no capture tier needs elevation**. Elevation is optional everywhere and buys exactly two things (CPU temperatures via PawnIO, attaching to elevated targets — ADR-9). An unelevated Agent whose Tier-1 attempt fails lands on Tier 2, which is what every unhooked session lands on regardless of privilege |

---

## S — Safety. Blocks the first real injection.

The project's central promise is that injection is opt-in per game, the guard is
a hard gate with **no override anywhere**, and no evasion is ever implemented.
Each item below is a place where the documents themselves leak a gap.

### S24 · The S-series ledger — read this before planning against the entries below

P0 exit criterion 2 (`spike-notes.md` §Exit criteria) is *"every S-series item
resolved, **or explicitly deferred with a written rationale**"*. That is not
auditable by reading 1,700 lines and counting ✅ marks — the markers are
load-bearing but they distinguish two states where the criterion needs four.
This table is the index; the entries remain the authority.

Added 2026-08-05. **Keep it current in the same PR that changes an item's state**,
or it becomes the next stale status claim this file exists to record.

> **The glyph legend, and it is the whole point of this table.** Unified 2026-08-27;
> before that, one disposition wore three marks and the criterion could not be audited
> by counting them, which is the only thing the table is for.
>
> | Mark | Means | Counts against exit criterion 2? |
> |---|---|---|
> | ✅ | resolved | no |
> | 🅓 | deferred, with a written rationale | no — the criterion allows this explicitly |
> | ◐ | partly closed | **yes** |
> | ⏳ | open, sequenced behind other work | **yes** |
> | ❓ | open, disposition undecided | **yes** |
> | 🔴 | open defect, found by an audit | **yes** |
> | 🚫 | owner-only — no PR can close it | counted separately, below |

| Item | State | Note |
|---|---|---|
| S3, S5, S7, S8, S9, S10, S11, S15, S16, S17, S18, S21, S22 | ✅ **resolved** | Thirteen. Reasoning kept in place rather than deleted |
| **S25, S26** | ✅ **resolved 2026-08-05** | The two runtime stops unreachable in a non-presenting process; occlusion probes recorded as frames |
| **S28** | ✅ **resolved 2026-08-05** | The guard's entry points share process-wide statics; a concurrent call cleared the blocklist mid-match and every check fell through to `Allow`. Reproduced by CI, not argued |
| **S27** | ✅ **resolved, and restated 2026-08-06** | The chokepoint is the anti-cheat gate, not the consent gate. It was closed by *not building* a capture host; item 1 built one, and the ✅ survives on the clause that was always load-bearing — **no injecting entry point on any SHIPPED binary**. `FrameLedger.CaptureHost` is unpublished, and `tools/package-closure-check.ps1` makes that a gate rather than a fact about today's csproj. `HookRequest` is get-only behind one factory, so this entry's own rejected synthesis no longer compiles |
| S12 | 🅓 **deferred, rationale written** | Cautious mode → v1.1; it disabled nothing in v1. *(Marked ✅ until 2026-08-27 — same disposition, different glyph.)* |
| **S1** | ◐ **launch mode built as "inject late" 2026-09-06 (P1 item 2); the cost is now measured per title** | `FlGuardedInjectWhenReady` polls until a presentation runtime is mapped, then runs the full guard; `dxgiPresentsBeforeHook` and the reported wait are the input the 2026-08-05 deferral lacked. The ruling "inject late vs. no launch mode" is answered by construction — inject late exists — and whether it is *preferred* per title is what the numbers decide |
| **S13(c)** | ✅ **salvageable, and salvaged 2026-09-06** | The externally observable proxy the entry proposed is what was built; the missing input is produced by every launched session |
| **S19(b)** | 🅓 **deferred, rationale written — and MEASURED** | CI 2026-08-05: the fragment fires on `System.Security.Cryptography.ProtectedData.dll` loaded by a .NET **test host**, i.e. inside a real scan set in the launch-mode arrangement — refusing our own injection. The entry's "plausible and unmeasured" is superseded; the deferral rationale (a `CryptCATAdmin*` PR doing network I/O inside the hard gate, NFR-10) still stands. *(Marked 🔴 until 2026-08-27. The measurement belongs in this cell, not in the marker — a glyph that means "deferred" and a glyph that means "open defect" cannot be the same one in a table audited by counting.)* |
| **S14** | 🅓 **exe half wired 2026-08-05; store-id half deferred to P4, rationale written 2026-09-06** | The store-id half's only in-policy route is P4's platform metadata extractors feeding a guard-side resolver; a resolver with no extractor would refuse every title. No title is blocked by store id today because none is listed |
| **S23-1** | ✅ **resolved 2026-08-05** | `FlGuardBuildId` gives the Agent a build id of its own, and `ShmHandshakeValidator` performs the comparison `07_IPC` and `04_CAPTURE` both specify. Every refusal path is driven, including **both ids empty** — the shape the feature shipped in, where `"" == ""` reported `Ok` for every process on the machine |
| **S23-4** | ✅ **resolved 2026-08-05** | `19_SAFETY` §During a session said "the module scan and the driver scan"; `EvaluateImpl` runs four. Reworded to "every pre-injection check" so it cannot go stale when a check is added, with the two omissions named — `services` is the only tier measured firing on real anti-cheat, and the pre-scan is the only one touching the filesystem |
| **S2 part three** | ✅ **built 2026-09-06 (P1 item 3)** | In-layer supervision on the present path, landed with `vkQueuePresentKHR`; `unhookRequested` and the tick deadline both stop the layer (passthrough with the reason on the mapping), proven by a fake loader chain and a 1.5 s test-only flavour of the DLL |
| **S4 signing** | 🅓 **deferred to P4, rationale written 2026-09-06** | Signing is a property of the feed and there is no feed (§S20); every rules file today shipped inside a release whose checksum attests it. The P4 decision (compiled-in key, detached signature, fail-closed) is written in the entry |
| **S6** | ✅ **built 2026-09-06 (P1 item 1)** | The detour on `kernelbase!LoadLibraryExW` stops the Overlay from inside on an exact-floor MODULE match, within one present or one watchdog iteration, and wakes the lazy installers within milliseconds for an inventoried module; measured against the real Overlay in ctest `fl_guard` `[loader]`/`[S6]`. Exact names only — fragment and signer stay the host's 30 s scan, so the disclosure does not change |
| **S19(a)** | ✅ **resolved 2026-08-05** | `gameguard` could never fire — `guard` subsumed it. Removed; `rules-validate` now fails when any fragment contains another, so the class cannot return. Zero coverage lost: nProtect has its own named module family |
| **S19(c)** | ✅ **resolved 2026-09-06** | The fields are constants the schema pins and the guard never reads, because the policy is code by CLAUDE.md rule 2 and `19_SAFETY`; `rules-validate` now proves the schema rejects `action: "allow"` and `signerField: "CN"` (canary 3), so the data cannot say otherwise |
| **S19(d) residual** | ✅ **resolved 2026-08-05** | A second canary, **derived from the shipped document** rather than hand-written, carries `nameFragments: []` and must be rejected. The entry's stated consequence was overstated and is corrected in place: the generated floor would not have disappeared, `gen-ac-floor.ps1` hard-errors and the native build fails — what was unguarded is the *schema* half |
| **S20 feed half** | 🅓 **deferred to P4, rationale written 2026-09-06** | **FR-7.3 stays unmet and written as unmet**: the fetch needs a shipped Agent, §S4's signing and the privacy policy's rows, all P4; until then a rules edit ships with the next release, checksum-attested |
| **S23-3** | 🅓 **deferred to P3, rationale written 2026-09-06** | `08_UI`'s refusal notice is wrong in the commonest case; the requirement reaches `08_UI` and the `Safety_*` keys in the PR that creates the first refusal UI — a string with no view would read as done |
| **S23-5** | ✅ **resolved 2026-08-05** | Closed the way §S23-4 closed the same class — by **removing** the restatement, not correcting it — and `rules-validate` now fails when any `$comment` enumerates checks again. The rule's own first version was scoped to the wrong object and could not fire; it now walks every `$comment` and fails if it finds none |
| **S23-6** | ✅ **closed 2026-09-06** | `license-check` binds `THIRD_PARTY_NOTICES` bidirectionally, and the accuracy blocks are now ONE text — `legal/ACCURACY.md`, embedded verbatim in `README.md` and `DISCLAIMER.md` §4, gated by `tools/accuracy-check.ps1` (self-tested both directions). Truth stays a human's job; drift between copies no longer is |
| **S23-2** | 🚫 **owner only — no PR can close it** | `Rules / validate` is **not** a required status check on `main`. **Re-verified against the live branch protection 2026-08-05**, not repeated from the entry: `required_status_checks.contexts` is exactly `["check", "analyze (csharp, none)", "analyze (cpp, manual)"]` (`strict: true`, `enforce_admins: true`, `required_linear_history: true`). The `validate` job is path-filtered and `pull_request`-only, so a red removal check does not block the merge button. **The gate that exists to make the anti-cheat blocklist un-removable is advisory.** This is a branch-protection setting |

| **S29** | 🅓 **six ✅ and one 🅓 on 2026-09-06 — nothing 🔴 remains** | (a) ✅ **closed 2026-09-06**: the managed drain is in the merge gate — §S19(b)'s signer half removed the refusal and `ci.yml` dropped `-SkipIntegration` in the same PR, both mechanisms at once, as the sharpening below said they had to be. *(History: ◐ CORRECTED — the honesty assertion *was* in the merge gate natively (`fl_guard`, 20.58 s on CI); only the **managed** drain was ungated, and §S19(b) was **not** a prerequisite of the feature hooks. Sharpened 2026-08-06: fixing §S19(b) alone would still have gated nothing, because `build.ps1`'s `-SkipIntegration` applied `--filter 'Category!=Integration'` before the guard was ever asked.)* (b) ✅ `fl_vtable_indices` now pins the Overlay's indices through a shared header; (c) ✅ **closed 2026-08-06 by deleting `ShouldUnhookAsync`** — zero production callers, strictly weaker than `ScanOnceAsync`, and inverted in polarity; the gate's public instance surface is now pinned to `{StartAsync}`; (d) 🅓 **deferred 2026-09-06 on a measurement** — `ci.yml` now prints a Vulkan runtime census, and the script runs as a CI step the day a runner has a loader; until then it stays hand-run before any layer PR, with the P1 Vulkan entry as the reminder (the pre-commitment in the entry covers both rows); (e) ✅ **closed 2026-08-06 host-side**, with a held process handle and a classifier that takes no elapsed-time parameter, so a frozen `writeIndex` can never end a session; (f) ✅ **closed 2026-08-20**: the normative contradiction between CLAUDE.md rule 7 and `03_METRICS` about inline RayQuery is settled the way the entry proposed — AS-build activity proves ray tracing is happening, *naming the technique* is what needs a DXIL scan — and rule 7 is amended in the PR that wrote the hooks. Its second half closes with it: `FL_MEASURED_RT` now has a producer (both command-list detours, installed off the game's own device), so all three conjuncts of the `No` branch are live and **`Yes` and `No` are both reachable for the first time**, proved by injection in both directions; (g) ✅ the present-only writer claimed `FL_MEASURED_OUTPUT_RES` unconditionally, including on records with no size |
| **S30** | ✅ **closed 2026-08-15** | Answered on the title that raised it: with Ray Reconstruction on, Cyberpunk 2077 evaluates `kFeatureDLSS_RR` on every application frame and `kFeatureDLSS` **not once**, so RR was doing the upscaling and the decode had no arm for it. Evidence rather than inference: `renderW/H` are published only on a frame that drained an evaluation and read 1485×835 = 0.58 × 2560×1440, so the scaling-input tag arrives ON the RR evaluation. Two further defects fell out — the pre-committed decision table had two holes, and the fix contaminated the census that found it (it derived the id from the decoded byte) |
| **S31** | ✅ **resolved 2026-09-04 — row P1, and `tokens/batch = 1.00`** | Is a drained Streamline batch an application frame? **Yes.** The `slGetNewFrameToken` count (#103, #105) reads 1.00 / 2.99 / 3.99 against Cyberpunk's own off / ×3 / ×4 and 4.00 on Hell Is Us, and on every leg with batches the two counts agree to the frame. Two independent per-frame API counts are the oracle four instruments could not be. `fg_factor` is published; `none` is reachable by counting. *(The row as it stood while open:)* ◐ **row P2, 2026-08-27 — the ORACLE is retired, the QUESTION is not** | Is a drained Streamline batch an application frame? The PresentMon 2.x `FrameType` test, with its decision table pre-committed BEFORE the run and `tools/frametype-oracle.ps1` producing the input as two dimensionless ratios. **Two obstacles measured before it could be run**: the console binary will not start a trace session unelevated on the dev box (exit 6; the account is in neither Administrators nor Performance Log Users, and the running shared service does not help), and `--track_frame_type` is a **beta** option whose own help says it needs application or driver instrumentation of the Intel-PresentMon provider — so it may be as unavailable as `fl-baseline-probe` proved to be. Two of the table's six rows retire it outright. Blocks HANDOFF item 3's producer decision |
| **H11** | ◐ **AMD ffx-api BUILT and RUN 2026-09-04 (A1–A3 accepted; the loader row added the same evening after three loader titles read silent); Intel closed at the census by licence; FSR 3.0 host route deferred to its own PR** | XeFG and FSR3-FG identity. The AMD half is measured on nine titles: the three ffx-api leaves AND the SDK 2.x loader are hooked (the loader turned out to be the game's entry on loader-shipping titles, with the leaves silent behind it), the vendored MIT headers (tag v2.3.0) decode the dispatch descriptor, and rows A1–A3 landed as pre-committed and **R6 closed on Dying Light: The Beast and KCD2 the same night** (`FSR (3.1 or 4 …)` / `FsrFg` / ×2 / 1.01 and `none` / 1.00 through the loader row). Wukong's FSR upscaling is compiled in (Rune Factory's shape; the title has no FSR frame generation), so nothing more is owed on the AMD half; the FSR 3.0 host route is the one deferred piece Intel's SDK failed the licence checklist, so XeSS/XeFG stay at the census (§Scope decisions). `ffx_fsr3_x64.dll` (FSR 3.0, Cyberpunk only here) ~~needs ten host headers from tag v1.1.4 and is deferred with that costing written~~ **built 2026-09-05** from tag `fsr3-v3.0.4` (the docs' `v1.1.4` was the wrong tag: twelve exports match 3.0.4, not 1.1.4's fifteen, and 1.1.4's descriptor appends fields after `renderSize`); one row, the host's UPSCALE; **rows B1–B4 owed** (below) |

~~**Six items are ❓ and one is 🚫.**~~ **Recounted 2026-08-05: TWELVE items block
exit criterion 2, not seven, and the undercount came from reading only the ❓ rows.**
The criterion is *"resolved, **or** explicitly deferred with a written rationale"* —
so ⏳ (open, sequenced) and ◐ (partly closed) are just as open as ❓, and each needs
either work or a rationale written down:

- ~~**six ❓**~~ ~~three ❓~~ ~~four ❓~~ **three ❓** — S6, S19(c), S20 feed half. ~~**S30**~~ ~~**S31**~~ *(S19(a),
  S19(d) residual and S23-5 resolved 2026-08-05, each by a mechanism rather than by a
  correction. **S30 closed 2026-08-15 and S31 opened 2026-08-20**, and this bullet went on
  naming S30 until 2026-08-27 — so the total below stayed right by coincidence rather than
  by count.)*
- **three ⏳** — S2 part three, S4 signing, S23-3
- ~~two ◐~~ ~~**three ◐**~~ **two ◐ (2026-09-04)** — S14, S23-6. ~~**S31**~~ *(moved out of ❓ on 2026-08-27 when its test landed on row P2; **resolved 2026-09-04** when the token count landed on row P1 with `tokens/batch = 1.00` — the question answered by a second count rather than by an oracle)*
- **one 🔴** — S29, added by the audit that produced this recount; **five of its seven
  findings are now closed**, and the residue is (a) the ungated managed drain and (d) the
  hand-run blast-radius script. *(This bullet listed **three** residues beside a count of
  five — five closed plus three open is eight, out of seven — because (f) closed on
  2026-08-20 and only the count was updated. The count was right and the list was wrong,
  which is the same shape as the ❓ bullet above. Corrected 2026-08-27.)*
- **plus 🚫 S23-2**, which no amount of code will do — the owner has to change a
  branch-protection setting.

~~**Nine, down from twelve.**~~ **TEN on 2026-08-15**, and it went UP — S30 is a defect a real-title run found, not a doc drift. Keep this list in the same PR that changes a row, or
the recount becomes the next thing that needs recounting.

> **STILL TEN on 2026-08-20 — and this time nothing moved at all: one out, one in.** S30
> closed 2026-08-15, S31 opened 2026-08-20, so the total is unchanged while the membership
> is not. Recorded because the bullets above went on naming S30 for twelve days and the
> total stayed *right by coincidence*. **A count that is correct while its own list is wrong**
> is this table's characteristic failure, and it is exactly what the sentence above asks the
> next author to prevent by moving the list and the count in one PR. Corrected 2026-08-27,
> in a pass that touched no code — the §Traps entry *"a document can go stale by NOT being
> touched"* wearing the ledger's own name.

> **NINE on 2026-09-04.** S31 ✅ — the first S-item closed by a *measurement that answered
> the question* rather than by a decision to stop asking it, since S30. ❓ three, ⏳ three,
> ◐ two, 🔴 one, plus 🚫 S23-2. The list above moved in the same edit as this count.
>
> **ZERO on 2026-09-06 — exit criterion 2 reads as met, with one item outside any PR.** The
> owner's closing sweep ("theo bảng"): S19(c) and S23-6 resolved by a gate each, S29 down to
> one 🅓 on a pre-committed measurement, and six items deferred with their rationale written
> in the entry and their phase named in this table — S6 and S2 part three to P1, S23-3 to P3,
> S4 signing, S20 feed half and S14's store-id half to P4. ❓ zero, ⏳ zero, ◐ zero, 🔴 zero.
> **What remains is 🚫 S23-2**, a branch-protection setting only the owner can change; it is
> not resolved and not deferred, it is outside what a PR can do, and the criterion's own
> wording does not reach it. The bullet list above is history as of 2026-09-04 and is left as
> it stood; this paragraph is the current count.
>
> **Corrected 2026-09-09: ◐ is TWO, not zero.** S1 and H11 carry ◐ in their own headings and in
> the rows above — both *built, with a residue that is an owner measurement* (S1: the first
> real-title launch and the descendant election, now P2's; H11: rows B2–B4 unfired). The count
> *against exit criterion 2* is unchanged — neither is a safety gap and P0 stays closed — but the
> glyph column said ◐ while the sentence above said ◐ zero, which is exactly the failure this
> table records about itself (*"a count that is correct while its own list is wrong"*). Kept
> as ◐ two here rather than re-glyphing the rows: the rows were right and the paragraph was
> wrong, and the residue of each is a run, not a rationale. P2's sequencing is `HANDOFF` §P2.
>
> **STILL TEN on 2026-08-27, for the third consecutive movement.** S31 moved from ❓ to ◐,
> so ❓ goes four to three and ◐ goes two to three. **The number has now stayed at ten across
> three different changes**, and that is worth saying out loud precisely because a reader
> checking only the total would conclude nothing had happened since 2026-08-15. Three things
> have. The total is the least informative line in this section and it is the one people read.

> **A rising count here is the ledger working, not failing.** Every previous movement was downward and came from closing something. This one is upward because the first capture from a real game produced a wrong answer that no fixture had been able to produce — which is the whole reason exit criterion 1 asks for a real title rather than a harness.

> **Still nine on 2026-08-06.** Item 1 closed §S29(c) and §S29(e) and corrected
> §S29(f)'s premise, but §S29 is one row and stays 🔴 while (a), (d) and (f) are open,
> so the count does not move. Recording that explicitly, because "we closed two things
> and the number is the same" is exactly the kind of thing a later reader assumes is a
> mistake in the table.
>
> **Six owner decisions surfaced by item 1 and answered by nobody yet** — they are not
> S-series items and are not counted above, but they block nothing today and each is
> recorded where it belongs rather than only here: who may clear `hook_blocked_reason`
> (`06_DATA_MODEL` §games); whether per-game consent carries a wording version (decided
> **yes** and implemented, because it cannot be retrofitted — reversing it is still the
> owner's); what an operator must be shown before an `UnshippedHostOperator` record may
> be written, and whether that text is reviewed like a `Safety_*` string (`09_I18N`);
> whether FR-2.4's global kill switch is an input to `HookedCaptureGate` or is enforced
> upstream of it — the gate's own remark calls itself "the ONLY managed logic between
> the user's intent and the guard" and has three inputs and no kill-switch input;
> whether accepting the Legal Gate (FR-11) is a precondition of stamping consent; and
> whether §S18 blocker 3's "the Agent is the sole host of the guard" is re-ratified now
> that a third project imports `FrameLedger.Guard.targets`.

> **2026-08-09, and the count does not move: still nine.** The upscaler identity hook
> landed (`HANDOFF` item 2) and closed nothing in this table, which is worth stating so
> nobody looks for a decrement. What it *did* do belongs here anyway:
>
> - **§S6's premise weakened again, in the direction of deferral.** The lazy feature-hook
>   install now runs on the watchdog and is under test, so "the first time their module
>   appears" is handled **without** a `LoadLibrary` hook. §S6 is about shrinking the 30 s
>   blocklist-detection window, which is a different job — but the mechanism it assumed it
>   would have to build is now built and unnecessary for P0. Its disposition is still an
>   open owner decision; the cost estimate attached to it is now too high.
> - **A licence question this file never asked has been answered, and it blocks a
>   documented plan.** `17_HOOK_ENGINE` §The NGX parameter surface recommends hooking
>   `NVSDK_NGX_Parameter_SetUI`; the NGX/DLSS SDK is the proprietary RTX SDKs Licence, so
>   `18_GPU_VENDOR_APIS` §Checklist step 3 forbids vendoring it **and** re-declaring it.
>   `FL_MEASURED_UPSCALER_PARAMS` therefore has no producer on the route the docs
>   specified, and P0 exit criterion 1 needs it. Corrected in `17_HOOK_ENGINE`; the
>   in-policy route is Streamline's own MIT surface. **Whether to revisit the checklist
>   for NGX is an owner decision no PR may take.**
> - **`spike-notes` §5's "blocked on a licence decision, not on hardware" is unblocked.**
>   Streamline is MIT, so §H5 case 3 — the risk that would make `fg_factor` structurally
>   1.0 on every Streamline title — is now reachable with `slInit`, and it belongs to
>   item 3.
> - **A gate that could not fail was removed from the native suite**, though it was not on
>   this list: a failed `REQUIRE` terminated the whole binary, so on this dev box one
>   environmental failure silently deleted the end-to-end honesty coverage §S29(a) rests
>   on. See `HANDOFF` §Traps.

~~**The markers themselves are part of the problem and should be unified.** One
disposition — *deferred, with a rationale written* — currently wears three glyphs:
S12 is `✅ deferred`, S1 and S13(c) are `🅓 deferred`, S19(b) is `🔴 deferred, but
now MEASURED`. This table exists so the criterion can be audited by *counting
markers*; three glyphs for one state defeats the only thing it is for.~~

**Unified 2026-08-27, and struck rather than deleted because the reasoning is the part that
generalises.** `🅓` is now the single mark for *deferred with a written rationale*: S12 moved
off `✅` and S19(b) off `🔴`, and the legend under §S24's heading states what every mark means
and whether it counts against exit criterion 2. **Neither move changes a disposition.**
S19(b) is as deferred, and as measured, as it was — its measurement lives in its note cell,
where it always did. What changed is that a marker no longer carries two meanings at once,
so the count and the glyphs can now disagree only if someone makes them.

### S26 ✅ · The ring counted occlusion probes as frames — **closed 2026-08-05**

`DXGI_PRESENT_TEST` runs the presentation test and **submits nothing**. The writer
recorded them like any other present, so a minimised or fully occluded game — which
issues them continuously — produced records that `03_METRICS` would have turned into
a frame rate it was not rendering, and into frame-time intervals that bound no frame.

**Who was responsible for filtering was assigned to nobody.** `07_IPC` did not say,
`03_METRICS:9` lists `presentFlags` among the consumed fields and is silent on this
value, and the harness's own history shows why that matters: every present in
`hook-harness` was once a probe, which made "N presents → N records" satisfiable
**only** by a writer that counts non-frames (`gates-that-cannot-fail`, #35).

**Decided: the writer drops them**, so the ring means one thing. The filter is placed
*after* the safety checks, so a probe-only process still evaluates the stop rather
than going unsupervised because it stopped drawing.

**Measured, both directions.** `hook-harness --hold-presenting 12` *without* `--real`
— a live, hooked, supervised target presenting nothing but probes — puts **142
records** in the ring against the pre-fix writer and **0** after. The test asserts
`status == READY` and `faultCount == 0` alongside the count, so "empty because we
unhooked" and "empty because we faulted" cannot pass for the right answer.

### S28 ✅ · The guard is not re-entrant, and nothing enforced that — **closed 2026-08-05**

Predicted by the drain design review, then **reproduced by CI** rather than argued.

`fl_guard_abi.h` states the one-at-a-time contract and three more comments in the
native tree repeat it. It was a comment, not a mechanism: `NativeAntiCheatGuard`
dispatched every call onto an arbitrary thread-pool thread, and `grep` for
`lock`/`SemaphoreSlim`/`Interlocked` over `Application` and `Infrastructure`
returned nothing.

**All four entry points share process-wide statics.** `ParseRules` keeps a
`static jsmntok_t toks[]` and is reached by `FlGuardEvaluate` and `FlGuardedInject`
through `LoadRules`, by `FlStaticPreScan`, and by `FlGuardCheckRules`.
`EvaluateImpl` additionally clears a function-scope `static Rules` on entry.

**Why that is a fail-open and not merely a race.** A second concurrent call clears
the blocklist while the first is matching against it — and an **empty blocklist
matches nothing**, so `CheckServices` iterates zero families, `CheckModules` and
`CheckDrivers` fall through, and `Evaluate` returns `Allow` from checks that looked
at nothing. `CheckModules` already refuses an empty *scan set* on the grounds that
it "must never read as clean"; the same rule had never been applied to an empty
*blocklist*.

**How it surfaced.** The drain integration test injects through the guard;
`RulesSeedingEndToEndTests` validates a document through `FlGuardCheckRules`; xUnit
runs test classes in parallel. The seeder test returned the wrong outcome. Two
callers, two entry points, one static — and neither test is about concurrency.

**Closed by a `static SemaphoreSlim(1,1)` in `NativeAntiCheatGuard`**, around every
native call including the synchronous `NativeCheckRules`. It goes in the facade
because that is the one place every native call passes through (§S15: one matcher,
not two), and it is `static` because the contract is a property of the loaded DLL
rather than of an instance.

> **What this does NOT do**, said rather than left to be assumed: it serialises
> *managed* callers. The native contract is still unenforced for anything that
> reaches the ABI another way — the Vulkan layer compiles `fl_ac_rules.cpp` in and
> parses its own copy at init, which is a different process and therefore fine
> today, and `fl_guard_test` drives the guard directly. If a second managed host
> ever loads the DLL, this lock does not span processes.
>
> > **A second managed host now exists, and it needs no lock — recorded 2026-08-06.**
> > `FrameLedger.CaptureHost` loads `FrameLedger.Guard.dll` alongside the Agent, and a
> > design panel over that PR proposed a machine-wide `GuardProcessLock` on the strength
> > of the sentence above. **It was wrong, and the refutation is this entry's own text.**
> > The fail-open is a race on PROCESS-LOCAL statics — `ParseRules`' `static jsmntok_t
> > toks[]` and `EvaluateImpl`'s function-scope `static Rules` — and a second process
> > gets its own image of the DLL and its own copies, which is exactly what this entry
> > already concedes for the Vulkan layer: *"a different process and therefore fine
> > today"*. The trailing sentence scopes what the `SemaphoreSlim` covers; it does not
> > assert a cross-process hazard. Wrapping a hard safety gate in a named mutex would
> > have added abandonment and deadlock failure modes to the gate for a hazard that does
> > not exist.
> >
> > The one genuine cross-process interaction is the rules FILE, and `FileSystemRulesStore`
> > already handles it: `ReplaceFileW` with a named backup, `FileShare.ReadWrite |
> > FileShare.Delete` on the read, both measured.

### S29 · What the 2026-08-05 P0 completion audit found — five gates and one contradiction

A five-slice audit of the tree against the docs, with every claim re-checked by a
refuter told to default to *refuted*. The ledger drift it found is fixed in the PR
that records this. What follows is the residue: things that need code, recorded so
the next session finds them by reading.

**(a) ✅ The honesty contract is in the merge gate — natively, and since 2026-09-06 the MANAGED half too.**

> **Closed 2026-09-06 by §S19(b)'s signer half and the same PR's `ci.yml` change.** The .NET
> test host still loads `System.Security.Cryptography.ProtectedData.dll`; the guard now verifies
> its embedded signature offline, reads `O=Microsoft Corporation` on the compiled-in bound, and
> keeps looking. `ShmDrainIntegrationTests`, `CaptureHostEndToEndTests` and the LHM hardware case
> run in CI under `./build.ps1 check` with no switches. The entry below is the history.

> **This entry was WRONG when it was written on 2026-08-05, and the correction
> matters because the wrong version was used to re-order the work.** It claimed the
> assertion catching a violation *"lives only in `ShmDrainIntegrationTests`, which
> CI skips"*, and concluded that §S19(b) was a prerequisite of the item-4/6/7 hooks.
> Both halves are false. Found by a design panel refuting the claim rather than
> repeating it.

`measuredMask` and `rtFlags` are what stop a present-only writer asserting "no
upscaler, no frame generation, no ray tracing" as measured fact (CLAUDE.md rules 6
and 7). **The assertion exists in the NATIVE suite and CI runs it:**
`guard_test.cpp`'s *"the injected Overlay records real presents into the ring"*
injects into `hook-harness` and requires `measuredMask == FL_MEASURED_OUTPUT_RES`
and `rtFlags & FL_RT_NOT_MEASURED` on every drained record. It is ctest `fl_guard`,
and CI ran it in 20.58 s on 2026-08-05.

**Why the native path works where the managed one does not, which is the whole
shape of §S19(b):** `fl_guard_test.exe` is a *native* host. It never loads
`System.Security.Cryptography.ProtectedData.dll`, so the `protect` fragment never
fires and the guard does not refuse it. A .NET test host does, which is exactly why
`ShmDrainIntegrationTests` is skipped and this is not.

**What is genuinely not gated**, stated narrowly this time: the **managed** drain —
`ShmRingReader`, `ShmHandshakeValidator` against a live writer, and the
`PublishGuardResult` round trip. A regression there is caught by nothing automated.
Fixing §S19(b) buys that, and it is worth doing; it is **not** a prerequisite of the
feature hooks, and this entry should not be used to sequence them.

**(b) ✅ `ctest fl_vtable_indices` did not pin the Overlay's vtable indices — closed 2026-08-05.**
`hook-harness` declared `kPresentIndex = 8` / `kResizeBuffersIndex = 13` /
`kPresent1Index = 22` as its own literals, textually duplicated from the inline
values in `dllmain.cpp` with no shared header. Changing the Overlay's 8 to a 9 left
the ctest green: it proved a fact about `dxgi.dll`, not a fact about
`FrameLedger.Overlay`. The only test coupling the two is the integration class CI
skips — so in the merge gate the coupling was absent entirely.

**Closed by one header and an INTERFACE target.**
`FrameLedger.Overlay/include/fl_dxgi_vtable.h` holds the three slot numbers;
`fl_dxgi_vtable` lets `hook-harness` consume them without linking the DLL. Proven:
setting `kPresentIndex = 9` leaves the native build **green** and turns
`fl_vtable_indices` **red** (with three neighbouring harness tests, which depend on
hooking working). Restored: 16/16.

> **What this does not do:** the indices are still not *trusted* — the header is
> where the assumption is written once, and `--probe-vtable` calling each slot on a
> real swapchain is what makes it a measurement. It also does not license a
> hardcoded vtable *pointer*: the Overlay still reads the vtable off a throwaway
> WARP composition swapchain and releases it. What is ABI-constant is the slot
> index, not the address.
>
> P0 item 2's ✅ rests partly on "vtable indices proved by behaviour", and until now
> that proof did not reach the shipped values. It does now.

**(c) ✅ `HookedCaptureGate.ShouldUnhookAsync` — closed 2026-08-06 by DELETING it.**

Grep decided it: the only occurrences were the declaration and two tests. **Zero production
callers**, so removing it touched no shipping path. Its whole body was
`!(await _guard.EvaluateAsync(pid, ct)).IsAllowed` — strictly weaker than
`GuardSupervisor.ScanOnceAsync`, which additionally short-circuits on the latch, increments
`CompletedEvaluations` at one site on the far side of a returned verdict, latches
`UnhookRequested` and records `LastVerdict`. Routing it through the supervisor would have given
the gate per-session state on a class whose contract is that it "deliberately adds no judgement
of its own", for no new capability.

**The polarity trap was real and is worth recording**: `ShouldUnhookAsync` returning `true` meant
STOP, while `ScanOnceAsync` returning `true` means MAY CONTINUE. Two supervision APIs on adjacent
objects with inverted senses is a defect waiting for a hurried caller.

**Its two tests are replaced by one that is stronger.** They asserted the boolean and never the
tick or the latch — so they certified the API as sanctioned while saying nothing about what was
wrong with it, which is exactly how it came to read as sanctioned.
`TheGateExposesNoSecondInSessionRescanPath` pins the gate's public instance surface to exactly
`{StartAsync}`. Proven: re-adding the method leaves the build green and turns that Fact red.

> **The original entry follows, struck rather than deleted**, because "it is unit-tested, so it
> reads as sanctioned" is the observation that decided the disposition.

~~**(c) 🔴 `HookedCaptureGate.ShouldUnhookAsync` is a second in-session re-scan path**~~
that publishes no tick and does not latch — the two properties `GuardSupervisor`
and `fl_shm.h` §`guardTicks` spend paragraphs establishing as the point of the
design. It is unit-tested, so it reads as sanctioned, and it is the **more
discoverable** of the two APIs because a drain loop is already holding the gate. A
loop wired to it supervises the target while the Overlay's watchdog counts down to
a supervision-loss stop that never resets. Either route it through
`GuardSupervisor` or delete it; leaving both is how the counter quietly stops
meaning what it says.

**(d) ◐ `tools/vklayer-blastradius.ps1` case 3 could not fail — the assertion
half is closed, the regression-net half is not.** It printed in both branches and
never added to `$errors`. That is the only exercise of `enable_environment`, the
gate `15_ROADMAP` calls the highest blast radius in the spike — a passthrough bug
loads FrameLedger into every Vulkan process on the machine.

**It is now an assertion**: a non-matching value that enables the layer fails, and
says that `spike-notes` §2's measurement no longer holds. Being an observation was
*correct while the answer was unknown* — the step existed to discover the loader's
behaviour, and a discovery step that fails is just a step with an opinion. What
was wrong is that the discovery was made on 2026-08-02, recorded as settled, and
the step went on printing about it in green either way. **When a measurement
becomes a recorded fact, the step that produced it has to become the thing that
defends it**, or the fact stops being checked while a script that still mentions
it reads as coverage.

**Still open:** the script is excluded from `build.ps1` and CI by design (it
writes `HKCU`, and a CI runner is the wrong place to register machine-wide Vulkan
layers) and needs `vulkaninfo` on `PATH`. So the assertion only fires when someone
runs it by hand. Nothing reminds them, and a loader upgrade is exactly when it
would matter.

> #### Disposition 2026-09-06, pre-committed on a measurement the same PR takes
>
> The "wrong place" argument inverts on a hosted runner: it is disposable, so an implicit
> layer registered under its `HKCU` harms nothing, and it IS the place a loader upgrade
> would be noticed. What decides is whether the runner has a Vulkan loader and
> `vulkaninfo.exe` at all — they ship with a GPU driver's runtime, not with Windows.
> `ci.yml` now prints a **Vulkan runtime census** (`vulkan-1.dll`, `vulkaninfo.exe`, the
> ICD registry key) as a measurement step. **Rows:** present → the next PR runs
> `vklayer-blastradius.ps1` as a CI step and (d) closes ✅; absent → (d) is 🅓 **deferred on
> that measurement**: the script stays hand-run before any layer PR (`HANDOFF` §Traps), and
> the reminder is the P1 Vulkan entry that cannot land without running it. Either row ends
> S29 as an S-item; the count below is written for the absent row and corrected if the
> census says otherwise.
>
> **2026-09-06 (P1 item 3): the assertion now lives in a ctest, and it no longer needs the registry.**
> `fl_vklayer_real` starts the harness's `--vulkan` mode with `VK_ADD_IMPLICIT_LAYER_PATH` pointed at the
> build tree's manifest and the enable variable set, and asserts the loader mapped the layer and the layer
> recorded the presents — on every dev-box build, with no HKCU write and nothing left behind. It SKIPS
> where there is no loader or no presentable device, which is CI's row; the census still decides (d)'s
> CI leg. `vklayer-blastradius.ps1` remains the hand-run check of the *unset-variable* case (it was run
> before this PR, green), and the loader-upgrade reminder is now the ctest rather than a HANDOFF trap.
>
> **Read 2026-09-06 off the sweep PR's run (`windows-latest`): `vulkan-1.dll` PRESENT in
> System32, `vulkaninfo.exe` absent (not in System32, not on PATH), no ICD registry key.** The
> absent row: **🅓 deferred on that measurement.** The loader is there but nothing to drive it
> with and nothing for it to drive — `vulkaninfo.exe` ships with a GPU driver's runtime, and
> without an ICD `vkCreateInstance` fails before the layer question is fully asked. The script
> stays hand-run before any layer PR. **Reopen condition, written now:** a Vulkan mode of
> `hook-harness` (unwritten, `12_BUILD` §Targets) could replace `vulkaninfo.exe` as the driver,
> and whether the loader's layer discovery and `enable_environment` behaviour is observable
> with no ICD is one measurement away — the P1 Vulkan entry is where it is taken.

**(e) ✅ The reader cannot tell a dead target from a quiet one — closed 2026-08-06,
host-side, with no ABI change.**

The diagnosis stands and got worse on inspection. `ShmRingReader` holds the section
open, so a game that exits leaves `writeIndex` frozen and `status` `READY` —
byte-for-byte identical to a loading screen or an alt-tabbed window. `DllMain` handles
only `DLL_PROCESS_ATTACH` and deliberately has no `DLL_PROCESS_DETACH` teardown
(`17_HOOK_ENGINE` §Unhooking), so **nothing is written on the way out**. And §S26
removed the accidental heartbeat: an occluded title used to emit a steady stream of
`DXGI_PRESENT_TEST` probes, and the writer now drops them, so an occluded game writes
*nothing at all*.

**The signal comes from the OS, not from the mapping.** `CaptureLoop` opens a process
handle **before** `FlGuardedInject` and holds it for the session. Held rather than
re-resolved, because pids recycle and the ring is named after one; the kernel will not
reuse a pid while a handle to it is open. `Infrastructure.Io.HeldProcessHandle` opens it
with `SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION` — the narrowest rights that
answer the question, named explicitly as `05_DETECTION` asks, with no `VM_READ` and no
`VM_WRITE`, so CLAUDE.md rule 4 is untouched. Putting liveness on `ShmRingReader` was
rejected: it would make the *reader* open the target, which is new surface for nothing.

> **THIS ENTRY WAS CLOSED ON A MECHANISM THAT DID NOT EXIST, and the correction is the
> part worth keeping.** The first version used `System.Diagnostics.Process.GetProcessById(pid)`
> and read `HasExited`, with a comment asserting that the handle kept the pid pinned.
> **Measured**, on .NET 10.0.10, by a probe over the live object: `_haveProcessHandle`
> is `false` and `_processHandle` is `null` both after construction and after reading
> `HasExited` — `GetProcessById` opens nothing, and `HasExited` opens a transient handle
> and releases it in its own `finally`. **No handle was held at any point and the pid was
> never reserved.** Three source comments, this entry and a CHANGELOG line all asserted
> it; nothing checked it.
>
> It cost two things. Between resolving the pid and injecting there is an awaited file
> read — long enough for an exit-and-recycle, after which `FlGuardedInject` would load
> the Overlay into a stranger on the strength of a consent record for a different binary.
> Not a rule-2 bypass, because the guard still scans the real pid; a rule-1 one, which is
> the class §S27 exists for. And mid-session, `HasExited` against a recycled pid answers
> about the *new* process, so the session would never end.
>
> Found by an adversarial review of the diff whose verifier wrote the probe rather than
> arguing from the docs — the ninth entry for `measure-windows-apis-dont-trust-them`, and
> the first where the API misled by doing *less* than its name implies. `TryOpen`
> returning null is now a refusal (`TargetCannotBePinned`), because a pid we could not
> pin must not be injected into, and `HeldProcessHandleTests` asserts the property rather
> than the outcome: it answers about a process that has fully exited, at a pid
> `Process.GetProcessById` refuses to resolve at all — which an implementation holding
> nothing cannot do.

**`SessionEndClassifier` takes no elapsed-time parameter, and that absence IS the fix.**
A frozen `writeIndex` never ends a session, however long it has been frozen; a
"how long since the last record" parameter is precisely how this defect would come back.
Its test drives five simulated minutes of silence with `status` `READY` and asserts
`Running`.

**It also separates two stops the mapping cannot.** `StopObserving` stores
`FL_STATUS_UNHOOKED` for the safety stop *and* for supervision loss, so only the side
that caused one knows which — and `legal/DISCLAIMER.md` §2 discloses them differently
("the guard fired" against "contact was lost"). The classifier takes
`weLatchedTheUnhook` for exactly that reason.

**(f) ❓ CLAUDE.md rule 7 and `03_METRICS` §RT disagree about inline RayQuery, and
one of them is normative.** Rule 7 names *"inline RayQuery without DXIL scan"* as a
case where measurement is genuinely impossible and `N/A` is the only honest answer.
`03_METRICS:128` says the opposite in as many words: *"Hooking
`BuildRaytracingAccelerationStructure` is what makes inline ray tracing (DXR 1.1
`RayQuery`) detectable at all"*, and `README:14` promises it to users.

They are reconcilable — AS-build activity proves *ray tracing is happening* in a
RayQuery-only title, while *classifying* the technique as RayQuery is what needs a
DXIL scan — but neither document says so, and the two readings produce opposite
answers for the same title. **Whoever builds P0 item 6 has to settle this before
writing the hook**, because it decides whether a RayQuery-only title reports `Yes`
or `N/A`. Related, and worse: `03_METRICS` defines the tri-state's **`No`** branch
as *"RT-capable device present, no AS builds and no dispatches for the whole
session"* — and nothing measures device RT tier, ~~no record field carries it, and
every byte of the 64-byte record is allocated. **As the record stands, item 6 can
reach `Yes` or `N/A` and never `No`.**~~

> **The PREMISE is stale and the CONCLUSION is understated — corrected in place
> 2026-08-06.** Layout v3 (#58) put `rtTier` at `FlWriterState` @24 and
> `hooksInstalledMask` at @28, and `03_METRICS` §RT/PT/RR now states the `No` branch
> against both as three conjuncts. The record's byte budget was never the constraint:
> these are session facts and went in region 2, not in the 64-byte record.
>
> **What survives is stronger.** A tree-wide grep finds no producer for either field —
> the only hits outside `fl_shm.h`, `ShmLayout.cs` and the consumer are
> `tools/fl-layout-dump/main.cpp`, which prints their offsets. So RT is `N/A` on every
> session today, and **both `Yes` and `No` are unreachable**, not just `No`. The
> throwaway consumer implements all three branches and a test drives each, so the day
> the RT hooks land the tri-state is already correct rather than being written under
> deadline beside them.
>
> The RayQuery half of this entry is **untouched and still open**: it is a normative
> contradiction between CLAUDE.md rule 7 and `03_METRICS`, and it is item 6's to settle
> before the hook is written.

> ### ✅ (f) IS CLOSED — 2026-08-20, ruled 2026-08-14 by the owner and now implemented
>
> **The reconciliation this entry proposed is the ruling.** AS-build activity proves ray
> tracing is happening ⇒ `RT = Yes`; *classifying the technique as RayQuery* is what needs a
> DXIL scan and stays `N/A`. CLAUDE.md rule 7 is amended to say so, in the PR that wrote the
> hooks, so the two documents no longer read in opposite directions. `03_METRICS:128` and
> `README:14` both stand unchanged, which is the outcome the entry expected.
>
> **The second half is closed by the same PR, and the closure is a measurement rather than a
> restatement.** `FL_MEASURED_RT` has a producer:
> `ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure` and `::DispatchRays`,
> installed lazily off a command list created on the game's own device, with `rtFlags` and
> `dispatchRaysVolume` drained per present. `rtTier` and `hooksInstalledMask` already had
> producers, so **all three conjuncts of the `No` branch are live and `Yes` and `No` are both
> reachable for the first time**. Proved by injection, both directions: a fixture that builds
> acceleration structures and dispatches, and one that builds and never dispatches — the
> second is what makes the AS-build hook's whole justification falsifiable.
>
> **What is still `N/A` on purpose**: naming the technique, and path tracing, whose heuristic
> needs `maxTraceRecursionDepth` and `rtStateObjectsCreated` — both still unproduced, because
> `ID3D12Device5::CreateStateObject` is a separate PR. `MeasuredFacts.PathTracing` stays a
> fixed `Tri.NotApplicable`, which rule 7 requires of it in any case.

**(g) ✅ The present-only writer claimed the one thing it may claim — unconditionally — closed 2026-08-05.**

`RecordPresent` set `FL_MEASURED_OUTPUT_RES` on every record, and two paths reach it
with no output size to report:

- `FindOrAdd` is a fixed 16-slot linear scan and returns `nullptr` once they are
  taken, so `outputW/H` are never assigned and stay 0.
- `GetDesc` failing, in `FindOrAdd` or in `ForgetChainSize` after a resize, leaves
  them 0.

So the record said **"output resolution MEASURED: 0 × 0"** — and `03_METRICS`
computes the upscale ratio as `sqrt((outW*outH)/(renW*renH))` from exactly those two
fields. This is the same defect #36 spent two bytes to fix, surviving inside the fix:
the mask distinguishes *looked* from *did not look* for six other fields, and for the
seventh it was a constant.

**Found by a design panel refuting a proposed layout, in the shipped writer rather
than in the proposal.**

Closed: the bit is set only when `outputW != 0 && outputH != 0`, which is what every
other bit in the mask already means.

**Proven both directions, and the second direction needed a new fixture.** Nothing in
`hook-harness` could reach the overflow branch — `--plus-ui` creates *one* extra
swapchain, which is a second stream, not an overflow. `--hold-presenting-overflow`
round-robins 17 chains for the whole hold so the Overlay meets more than it can hold.
The existing end-to-end test asserts the mask is *exactly* `OUTPUT_RES` on a normal
target; the new one asserts it is *exactly* 0 on an overflowed one. Canary: restoring
the unconditional assignment turns the new test red with the build green.

> **The fixture was wrong first, and the test's own vacuity guard is what said so.**
> Its first version filled the table at startup and then held on the 17th chain — but
> the Overlay is injected ~800 ms later and only sees presents made after it hooks, so
> it observed an *empty* table and gave the "overflowed" chain slot 1. The assertion
> `overflowed > 30` reported **0**, which is why the loop full of `CHECK`s inside it
> did not silently pass by never executing. The test also pins the overflowed stream's
> *share* (~1/17), because an absolute floor alone would be satisfied by a harness
> presenting on one chain.

### S30 ✅ · The record names the WRONG upscaler on a real title — **closed 2026-08-15, and the fix broke the instrument that found it**

> **Answered: Ray Reconstruction was doing the upscaling.** With `DLSS_D = True` Cyberpunk
> 2077 evaluates `kFeatureDLSS_RR` on every application frame and `kFeatureDLSS` **not once**
> — 2,523 of 2,523 batches across three 40 s captures, zero DLSS, zero NIS, zero undecoded
> ids. RR **replaces** the super-resolution pass rather than running beside it; the decode
> had no arm for that and fell through to `UNKNOWN`. `spike-notes` §8 carries the run.
>
> **Not the shortcut this entry forbade.** No preference is applied and no id is outranked,
> because only one id ever arrives. What makes it a measurement: `renderW/H` are published
> only on a frame that drained an evaluation, and they read 1485×835 against the title's own
> `DLSS = Balanced` at 2560×1440 — 0.58 exactly — so the scaling-input tag arrives **on the
> RR evaluation**. The evaluation that upscales is the one the decode was already holding.
> Mapped to `FL_UPSCALER_DLSS` and not a new value: layout v3 retired
> `FL_UPSCALER_RETIRED_RAY_RECONSTRUCTION` precisely because RR is an independent axis, and
> `FL_FEAT_RAY_RECONSTRUCTION` already carries it from the same word.
>
> **THE DECISION TABLE BELOW HAD TWO HOLES, AND THE RUN FOUND BOTH.** It was written before
> the measurement, exactly as this entry demanded, and it was still incomplete: **O1 assumed
> `kFeatureDLSS_G > 0`**, so no row covered "batches arrive and DLSS-G never does"; and **no
> row anticipated a single non-super-resolution id accounting for 100% of batches.** The
> table is kept unchanged rather than quietly repaired, because a pre-committed table that
> gets edited after the fact is worth nothing, and because what it got wrong is the useful
> part: the hole was in the *hypotheses*, not in the discipline. Writing it first is what
> made the gap visible instead of invisible.
>
> **AND THE FIX CONTAMINATED THE INSTRUMENT.** `SlCensus` derived "a super-resolution id
> arrived" from the DECODED `upscaler` byte, so correcting the decode made it report 2,569
> arrivals of an id that arrives zero times — a measurement that moves when its subject moves
> cannot be evidence about the subject, which is the only job this entry gave it, and the
> next reader would have taken it at face value. `FL_FEAT_SL_SUPER_RESOLUTION` now carries
> the raw fact from the drain word, independent of any decode. Generalises: **when a decode
> is corrected, check every consumer that derived a fact FROM that decode.**
>
> **What is NOT closed by this.** `kFeatureDLSS_G` is never evaluated either — see
> `spike-notes` §8 and §H5 — so item 3's counter has nothing to count on this route. That is
> a separate finding with its own consequences for HANDOFF item 3, and it does not belong to
> this entry.

<details>
<summary>The entry as it stood while it was open, including the pre-committed table with its two holes</summary>

### S30 ❓ · The record names the WRONG upscaler on a real title, and it is honest while being wrong

**Measured 2026-08-15, Cyberpunk 2077, and it is the first defect a real-title run
has produced** (`spike-notes.md` §8). The title is running DLSS. Every one of the
**2,461** params-carrying presents in a 40 s / 10,169-present capture decoded to
`FL_UPSCALER_UNKNOWN`.

`UNKNOWN` is the honest failure — `fl_shm.h` allows only `NONE` to be aggregated as
a negative, and the writer correctly never says `NONE`. But exit criterion 1 asks
for the **correct** upscaler, and `Unknown` is not it. **A wrong answer that cannot
be called a lie is still a wrong answer**, and it is the exact shape
`17_HOOK_ENGINE` calls the highest false-confidence risk in the spike, reached from
a direction nobody was watching: not a misspelt symbol, but a correctly-resolved
hook attributing the frame to the wrong feature.

**The mechanism, as far as the data shows.** `RecordPresent` drains `g_slSeen` with
`exchange(0)`, so a record is told about whichever Streamline features evaluated
since the *previous* present. Under multi-frame generation the app frame's
`kFeatureDLSS` evaluation and the presents that carry it are not one-to-one: 10,169
presents carried only 2,461 evaluation batches (×4.13, and the title's settings say
`DLSS_MultiFrameGeneration = x4`). The batches that reached a present held
`DLSS_G` / `DLSS_RR` / `OTHER` and not `kFeatureDLSS`, so the decode fell through to
`UNKNOWN`.

**What is NOT yet established, and must not be assumed while fixing it:**

- Whether `kFeatureDLSS` is evaluated at all in this title, or only `DLSS_G` and
  `DLSS_RR`. Cyberpunk ships `sl.dlss.dll`, so it *should* be — but "should" is what
  this ledger exists to distrust. The identity hook fires; **which** ids it sees was
  never printed.
- Whether the same happens without frame generation. One configuration was measured.
- Whether the fix belongs in the drain (accumulate identity across presents until an
  app frame boundary) or in the decode (prefer a super-resolution id over an FG id
  when several are seen). These produce different answers for a title that switches
  upscaler mid-session.

**Whose it is.** Item 3 restructures this exact drain — `g_slSeen` is a bitmask and
frame generation needs a **count** — so the fix lands there rather than as a
separate change that item 3 would immediately rewrite. **Do not "fix" it by making
the decode prefer DLSS without first measuring which ids arrive**: that would turn
a wrong answer into a confident wrong answer.

> ### The instrument is built. The decision table is below, and it is written BEFORE the run.
>
> **Why it is here rather than in the PR that acts on it.** "Measure, then fix" becomes
> "fix, then justify" the moment the table is written after the numbers are known — and
> nobody can tell the two apart afterwards, least of all the person who did it. So the
> mapping from measurement to action is committed first, in the file that owns the item,
> and the run that follows either lands on a row or does not.
>
> **What produces the input.** `FrameLedger.CaptureHost` now prints a Streamline id census
> (`SlCensus`) over the window the identity hook governs: how many presents drained a batch,
> and of those how many carried `kFeatureDLSS`, `kFeatureNIS`, `kFeatureDLSS_RR`,
> `kFeatureDLSS_G` and an **undecoded** id — the last of which is only separable because
> layout v3's `FL_FEAT_SL_UNDECODED` exists. It also prints `evaluations/batch`, which
> tests item 3's unverified premise with no oracle at all.
>
> | # | What the census says | What it means | The decode change |
> |---|---|---|---|
> | **O1** | `kFeatureDLSS = 0`, `kFeatureDLSS_G > 0`, `UNDECODED = 0` | the title does not route super-resolution through `slEvaluateFeature` at all | **none.** `UNKNOWN` is the true answer and the entry closes as "measured, not a defect". The upscaler fact then needs a different producer, which is its own item |
> | **O2** | `kFeatureDLSS > 0` on some batches, `0` on the batches that also carry `kFeatureDLSS_G` | identity and frame generation arrive in *different* batches | **drain**, not decode: accumulate identity across the presents of one application frame. Do NOT prefer DLSS in the decode — that gives the wrong answer for a title that switches upscaler mid-session, which is the whole reason these two options differ |
> | **O3** | `kFeatureDLSS > 0` on the SAME batches that carry `kFeatureDLSS_G`, yet the record still decoded `UNKNOWN` | the decode is losing an id it was handed | **decode.** The if/else-if chain assigns ONE bit per call and the last writer wins within a batch; make the mapping additive and re-check |
> | **O4** | `UNDECODED > 0` on the batches that decoded `UNKNOWN` | an id we do not map is arriving, and may be the super-resolution one under another name | **neither, yet.** Print the raw `sl::Feature` values before mapping anything — a new id decoded by guess is exactly this entry repeating. That needs a writer change of its own |
> | **O5** | `batches = 0`, or `evaluations/batch` far from 1 | the hook is not seeing what we think it sees | **stop.** Neither fix is safe: `evaluations/batch ≠ 1` falsifies item 3's premise outright and the FG factor is wrong by that factor, so that is the defect to chase first |
>
> ### THE x4 LEG, AND WHAT IT DID AND DID NOT SETTLE — 2026-08-16
>
> **The named rival is dead.** Read at x4, the overlay shows `DLSS 268 | FPS 67`,
> `272 | 68` and `260 | 65` across three instants. "Application frames" predicted the
> second field at ~65; "displayed divided by a fixed 2" predicted ~130. It reads 65-68.
> The prediction was written before the screenshots and it discriminated.
>
> Ratio against ratio, which needs no span and no shared window: ours
> `presents / batch` = 4.0000, the overlay's `DLSS / FPS` = 4.0000. At x2 both read
> 2.0000. Two instruments agreeing on a dimensionless quantity across two multipliers.
>
> **A SECOND CONCLUSION WAS DRAWN AND THEN WITHDRAWN, and the withdrawal is the useful
> part.** All five readings are EXACT integer ratios, which requires the true ratio to
> fall inside about +-1% every time. That looked like proof the overlay DERIVES one field
> from the other, because our own capture had measured an achieved factor of 1.84 rather
> than 2.00 — so the real factor seemed to vary while the overlay never did.
>
> **The 1.84 was ALT-TAB.** The operator switched away from the game during that capture.
> Frame generation stops while the title is unfocused, so the window mixed intervals at
> 2.00 with intervals near 1.00 and averaged 1.84. With that explained, the achieved factor
> is exactly N whenever the game has focus — and an INDEPENDENT counter would then also read
> exact integers every time. The evidence for "it derives" evaporates with its premise.
>
> **So the position is: "independent count" and "correct derivation" both survive, and no
> instrument available here can separate them** — each produces the true application rate
> whenever frame generation meets its configured multiplier, which is always, when focused.
> The premise that a drained Streamline batch equals an application frame is therefore
> CORROBORATED ACROSS TWO MULTIPLIERS AND NOT PROVEN.
>
> **The instrument that would settle it is already named in this repository.** PresentMon
> 2.x's `FrameType` classifies each present as application or generated from ETW, by a
> mechanism that divides by nothing. `15_ROADMAP` item 7 and `03_METRICS` rung 2 both
> pre-committed it, which is why they exist. That is the next measurement, not another
> reading of a present-hook overlay.
>
> ### AND THE ALT-TAB EXPOSED A REAL DEFECT, which is the finding with code attached
>
> `FgWindow` has a uniformity guard built for exactly this — split the window into buckets,
> refuse a factor when one bucket departs from the whole, because "averaging across a
> settings change is the classic way benchmark numbers become meaningless"
> (`03_METRICS`:133). **It cannot see this case.** `BucketsOf` sums `fgEvaluations`, which
> is zero on every record on this route, so every bucket is identical and the check passes
> vacuously. The 1.84 run is the proof: a real session where the number a consumer would
> publish was wrong by 8% and nothing in the report said so.
>
> **Consequences, and they are prerequisites rather than nice-to-haves.** If
> `presents / batch` is ever published as `fg_factor`, it needs a uniformity guard OF ITS
> OWN, keyed on the per-bucket `presents / batch` rather than on `fgEvaluations`. And a
> capture whose validity depends on the window being uniform should be able to say when it
> was not: focus loss is observable in-process, and an unfocused interval is not a
> measurement of the title's performance in any case.

> ### THE REPLACEMENT ORACLE DOES NOT SETTLE IT EITHER — corrected 2026-08-16, before it landed
>
> **A draft of this entry claimed the application-frame premise was measured. It was wrong on
> three counts and is recorded here rather than quietly deleted, because the draft read
> exactly like a result.** Steam's overlay, during a ×2 capture, showed `DLSS 162 | FPS 81`
> against a capture whose own numbers imply 161.7 and 80.8 — "0.2% on both".
>
> 1. **THE TWO AGREEMENTS ARE ONE, BY ALGEBRA.** `presents / batch` was 2.0000 exactly and
>    the overlay's own ratio is 162/81 = 2.0000 exactly, so `batches ÷ 81` and
>    `presents ÷ 162` are forced to the same residual — both −0.182%, to three digits. Two
>    independent checks essentially never do that. This is the defect `SlCensus.cs` was
>    corrected for **the same day** — one quantity read twice and cited as two witnesses —
>    reproduced in prose hours later. Fixing the code did not fix the habit.
> 2. **THE SURVIVING COMPARISON IS CIRCULAR.** The span was derived FROM `Displayed FPS`, so
>    `presents / span` is `Displayed FPS` restated. It compares our present count against
>    another present count. No step in the arithmetic touches an application frame.
> 3. **THE RIVAL HYPOTHESIS PREDICTS THE SAME NUMBER.** If the overlay's `FPS` field is
>    *displayed ÷ 2* rather than a count of application frames, then at ×2 the two are
>    numerically identical. The pre-registered prediction separated "application frames" from
>    "displayed frames" and never enumerated this one, so it was not a discriminating test —
>    and calling the result pre-registered borrowed credibility the design did not have.
>
> **And the oracle is not what the draft called it.** Steam's overlay is not "the game's own
> frame counter" and shares more with us than the draft claimed: `17_HOOK_ENGINE` §Coexistence
> records that RTSS and the Steam overlay **also hook D3D presentation in-process**. Which
> layer it counts at is unmeasured, and this title's present path has at least two — with the
> interposer engaged the swapchain the title holds is not an instance of the class whose
> vtable we patch (§H5). If Steam counts at the interposer's input, "FPS 81" is a present
> count one hook over, and equating it with an application frame is the same unverified step
> relocated.
>
> **THE DISCRIMINATING TEST, and it costs one screenshot.** Read the same overlay at **×4**:
> "application frames" predicts the `FPS` field reads ≈ 65 while "displayed ÷ fixed 2"
> predicts ≈ 130 — a factor of two apart. Then read it with **frame generation OFF**, where a
> genuine application counter converges with the displayed one and a fixed halving does not.
> **Compare RATIOS, not rates:** our `presents / batch` against the overlay's `DLSS / FPS`.
> Both are dimensionless and each is internal to one instrument, so neither needs a span —
> which also sidesteps the window defect below.
>
> **A code gap this exposed.** `FgWindow.Seconds` is computed and never printed, so the span
> over the window the batches were actually counted in cannot be read off a report at all.
> The draft reconstructed it from `Displayed FPS`, which spans a DIFFERENT window —
> `MeasuredFacts` runs from record 0 while `FgWindow` starts after the lazy-install prefix —
> and the resulting rate moves across 78.6–83 on window choice alone, ten times the residual
> the draft quoted. Print it unconditionally beside the other FG counts.

> ### THE PRE-COMMITTED ORACLE IS RETIRED, BY ITS OWN RULE — measured 2026-08-16
>
> `fl-baseline-probe --pid <game> --dir "<game-dir>/bin/x64"` against a running Cyberpunk 2077
> with frame generation ON at ×2 reports **all seven capabilities `loaded`** — `dlss`,
> `dlss_g`, `dlss_rr`, `streamline`, `fsr`, `xess` **and `xefg`**.
>
> **That falsifies it as an FG-engagement oracle in ONE run, without needing the FG-off leg
> the falsifier asked for.** `dlss_g` watches `nvngx_dlssg.dll` / `sl.dlss_g.dll` and `xefg`
> watches `libxess_fg.dll` (`rules/detection-rules.json`) — two **mutually exclusive**
> frame-generation implementations. A title cannot be generating frames with both, and both
> read `loaded`. So `loaded` means "mapped into the address space", which is exactly what the
> probe's own header claims and nothing more; §S30 hoped it would stand in for engagement and
> it cannot, in any configuration.
>
> **The probe is not at fault and its real job is unaffected**: it is the static/loaded
> baseline `15_ROADMAP` item 4 compares against, and it answered that correctly. What is
> retired is the use §S30 pre-committed it for.
>
> **So the app-frame premise still has no independent oracle**, and the one remaining
> candidate is the game's own frame counter beside a capture — the other half of
> `docs/HANDOFF.md`'s "the game's own settings menu and frame counter". Writing the falsifier
> down first is what made this a one-command result instead of a temptation: the output reads
> like confirmation (`dlss_g … loaded`) and is not.

> **The independent oracle, and its own falsifier — also written first.** `fl-baseline-probe`
> against a **running** Cyberpunk 2077 at MFG ×4, ×2 and off answers "is `nvngx_dlssg.dll`
> loaded", which is evidence about FG engagement that shares nothing with the writer under
> test. **If it reports LOADED with MFG off, it is not an oracle for this question** —
> plugin loading happens at `slInit`/feature discovery, not at feature engagement — and it
> must be retired in the same `spike-notes` row rather than quietly relied on. The fallback
> is the game's own frame counter.
>
> **Not in the table on purpose:** a factor of exactly 1.0 must NOT be recorded as §H5 case 3.
> At least three causes produce it and this data cannot separate them — generated presents
> never reaching the vtable we patch, frame generation configured off while the feature is
> still evaluated, and an evaluation that FAILED and was counted anyway, because
> `Hook_SlEvaluateFeature` increments before forwarding and ignores the `sl::Result`. The
> report prints all three rather than naming one.

</details>

### S31 ✅ · Is a drained Streamline batch an application frame? — **YES, measured 2026-09-04 by an oracle that divides by nothing**

> ### RUN 2, 2026-09-04 — row **P1**, and the original question answered on the way
>
> Same instrument as run 1, the monotone-index count from #105. Cyberpunk 2077, three
> launches, and Hell Is Us, one; legs identified by the title's own `presents/batch` where it
> has one:
>
> | leg | presents | tokens | **presents/tokens** | batches | tokens/batch | report |
> |---|---|---|---|---|---|---|
> | Cyberpunk **off** (RR on) | 4,922 | 4,924 | **1.00** | 4,922 | **1.00** | `Presented FPS 84.02` — factor refused at 1.0 by the pre-P1 gate, as designed |
> | Cyberpunk **off** (RR off) | 2,664 | 2,665 | **1.00** | 0 | — | `Presented FPS 45.47`, same refusal |
> | Cyberpunk **×3** | 10,567 | 3,534 | **2.99** | 3,532 | **1.00** | `Native 61.37 → Displayed 183.49 (×2.99 FG)` |
> | Cyberpunk **×4** | 13,706 | 3,439 | **3.99** | 3,437 | **1.00** | `Native 58.74 → Displayed 234.09 (×3.99 FG)` |
> | Hell Is Us **DLSS + FG ×4** | 17,942 | 4,485 | **4.00** | 0 | — | `Native 76.54 → Displayed 306.17 (×4 FG)` |
>
> **P1 word for word:** 1.00 at off, 2.99 / 3.99 at ×3 / ×4, 4.00 on the title that never
> drains a batch. P3 is excluded on the title that would have shown it — the DLSS-G plugin
> does not request tokens for the frames it generates — and the off leg at exactly 1.00
> retires P4. **The producer is accepted.** `fg_factor` is `presents / tokens`.
>
> **And the question this entry was opened for is answered by the same numbers.**
> `tokens/batch` reads **1.00** on every leg that has batches: a drained Streamline batch
> IS an application frame on Cyberpunk 2077 — Ray Reconstruction is evaluated exactly once
> per frame the title asked a token for. Two independent counts, one per API call the title
> makes, agreeing to the frame. That is the oracle four instruments could not provide,
> because it divides by nothing: it is a second count, not a ratio against the first.
>
> **Consequences, taken in the PR that records this:** rung 4's `none` is reachable by
> counting (`FgWindow.IsNone`, factor ≤ 1.05 with a published window); the pre-P1 refusal is
> replaced by a refusal of the band between 1.05 and the cadence threshold, which no vendor
> ships; `fgMode` reads `None` / `Active (technology not identified)` from the count when the
> identity hook named nothing. `14_TESTING`'s struck cross-validation regains an in-project
> oracle for the FG factor: two counts that must agree.
>
> *The entry as it stood while open follows, unchanged.*

### S31 ◐ · Is a drained Streamline batch an application frame? — the ORACLE is retired, the QUESTION is not *(superseded above)*

> ### THE QUESTION IS SIDESTEPPED, NOT ANSWERED — 2026-09-03, the producer route is chosen
>
> Nothing divides by batches any more. HANDOFF item 3's producer is **`slGetNewFrameToken`**:
> Streamline hands a title one frame token per application frame by contract, the Overlay
> counts distinct tokens between presents, and `fg_factor = presents / tokens`. The choice was
> taken **with no oracle behind it**, as the entry required the next PR to say — so this table
> was written before the run, and the run is the owner's: **Cyberpunk 2077 at off / ×2 / ×4
> and Hell Is Us at DLSS + FG ×4, one launch each**, reading `presents/tokens` off the
> `FG counts:` line.
>
> | Row | Reads | Consequence |
> |---|---|---|
> | **P1** | ≈ 1.00 / 2.00 / 4.00 on Cyberpunk, ≈ 4.0 on Hell Is Us | Producer accepted. Rung 4's `none` becomes reachable at ratio ≈ 1 in the following PR; §S31 → ✅ by construction, `14_TESTING`'s cross-validation gate regains an inside-the-project oracle for the FG factor |
> | **P2** | tokens = 0 on a title with frame generation demonstrably on | That title does not call the export from where we can see it. Route dead **there**; N/A stays; record which title |
> | **P3** | ≈ 1 at ×2 and ×4 | The DLSS-G plugin requests a token for every frame it generates. Route dead everywhere; the detour comes out; back to the candidate list |
> | **P4** | The off leg reads ≠ 1 (e.g. 0.5), or the ratios are non-integer and stable | The dedupe premise is wrong (several distinct tokens per frame, or one per several). Refine by `frameIndex` before anything is published; nothing above the cadence threshold is trusted until the off leg reads 1 |
>
> **Until a row lands, the consumer publishes a factor only at ≥ 1.5** (`FgWindow.PublishableFactor`),
> because P3 is the one outcome a ratio near 1 cannot be told apart from, and P3 is exactly
> the shape that would print `300 → 300 (×1.0 FG)` on Hell Is Us.
>
> #### RUN 1, 2026-09-03 evening — row **P4**, and the refinement it prescribed is built
>
> Cyberpunk 2077, three launches, read off the `FG counts:` line (`spike-notes` §9):
>
> | leg (by its own `presents/batch`) | presents | tokens | presents/tokens | tokens/batch |
> |---|---|---|---|---|
> | off (1.00) | 5,199 | 24,059 | **0.22** | 4.63 |
> | ×2 (2.00) | 8,544 | 13,259 | **0.64** | 3.10 |
> | ×4 (3.99) | 15,193 | 11,633 | **1.31** | 3.05 |
> | Hell Is Us, DLSS + FG ×4 | 17,788 | 13,783 | **1.29** | — |
>
> **The off leg reads 0.22, not 1**, and `tokens/batch` reads 3.05–4.63: the title asks for a
> frame token **three to four-and-a-half times per application frame** and the interposer hands
> back a **different object each time**, so the pointer-change count read the request rate,
> not the frame rate. That is P4 word for word — *"several distinct tokens per frame"* — and
> not P3: at ×4 the ratio moved (1.31 against 0.64 at ×2), so tokens are not tracking presents.
> The gate held: **no factor was published** on any leg, because every ratio sat below 1.5.
>
> **Refinement built the same evening:** the detour keys on the frame **index** — the
> title-supplied `frameIndex` when given, else the token's own accessor — and counts it **only
> when it is above every index seen so far**, so a frame asked for N times counts once even when
> the requests for frames N and N+1 arrive interleaved from the two or three threads a title
> keeps in flight ("differs from the last index" would count every switch). A jump backwards
> of more than 1,024 re-bases the maximum, so a level reload that restarts the counter does not
> freeze the count at zero. The stub interposer now hands back distinct objects carrying the same
> index and the harness asks twice per frame, so the K = 1 control fails a pointer-keyed writer.
> **The table above is unchanged and the run is owed again**, same four legs; P1 is still what
> makes `none` reachable.

> ### 🔴 RUN 2026-08-27 — ROW **P2**. PresentMon is RETIRED as the application-frame oracle.
>
> Three legs, three game launches, Cyberpunk 2077, frame generation off / ×2 / ×4.
> `spike-notes` §11 carries the numbers. What belongs here is the row and what it does
> and does not settle.
>
> **`FrameType` is present and every single row of all three legs reads `Application`** —
> 1,937 / 6,488 / 10,881 rows, zero rows of any other value, counted off the raw CSVs
> rather than off the tool's summary.
>
> **And the title was demonstrably generating frames, which is the half P2 needs.** The
> two instruments agree on the present rate to within 0.3% on every leg:
>
> | leg | our hook | PresentMon | our `presents/batch` |
> |---|---|---|---|
> | off | 43.16 /s | 43.04 /s | N/A — no batch drained |
> | ×2 | 144.31 /s | 144.18 /s | **2.00** |
> | ×4 | 241.84 /s | 241.80 /s | **3.99** |
>
> So **both instruments are counting the same present stream**, displayed rate tracks the
> configured multiplier, and at ×4 roughly three presents in four cannot be application
> frames — while PresentMon classifies **100%** of them `Application`. That is P2 word for
> word: *"PresentMon sees the presents but classifies none as generated, while the title is
> demonstrably generating them."*
>
> `tools/frametype-oracle.ps1` reported it as a **falsifier** on all three legs and never as
> a ratio of 1.0, which is the one behaviour that entry was written to guarantee.
>
> #### What is NOT yet excluded, and the one command that excludes it
>
> **Whether `--track_frame_type` was in effect is unmeasured.** If `FrameType` appears in
> `--v2_metrics` output *regardless* of the flag, these legs show PresentMon's default rather
> than an absence of vendor events. The discriminator costs five seconds, no game and no
> capture — run PresentMon **without** the flag and look at the header:
>
> ```
> PresentMon-2.5.1-x64.exe --process_name explorer.exe --v2_metrics \
>     --output_file %TEMP%\pm-noflag.csv --timed 5 --terminate_after_timed --stop_existing_session
> ```
>
> No `FrameType` column there ⇒ the column exists only because of the flag ⇒ the vendor
> emitted nothing. Column present ⇒ these three legs measured a default and this entry needs
> re-reading. **It still needs elevation, so it is the owner's** — attempted unelevated
> 2026-08-27 and refused with the same exit 6 this item has been blocked on from the start.
>
> > **AND THE DISCRIMINATOR WAS RUN, AND PRODUCED NOTHING — 2026-08-27.** The owner ran it
> > from an elevated shell and no CSV appeared anywhere: not at the given path, not in either
> > TEMP, not in the repository. **That is not an answer, and it must not be read as one.**
> > The command named `explorer.exe` as the target, and a process that presents nothing through
> > a swapchain PresentMon tracks produces no file — so "no CSV" and "no `FrameType` column"
> > are indistinguishable here, which is the §Traps entry *"a canary that dies before reaching
> > the gate looks exactly like a canary that worked"*. The command was badly chosen; a target
> > known to present (`hook-harness --hold-presenting`) would have settled it.
> >
> > **It is now moot.** PresentMon was dropped outright on 2026-08-27, so the reason this
> > sub-question would have decided — whether a later attempt with a different invocation was
> > worth anyone's time — no longer has anything to decide about. Recorded rather than deleted
> > so nobody re-runs it believing it is untried.
>> **THE ACTION DOES NOT DEPEND ON IT, WHICH IS WHY THIS ROW LANDS ANYWAY.** Both branches end
> at the same place: either the vendor emits nothing, or the invocation produced no frame-type
> data at all. In both, PresentMon did not answer the question **as invoked**, and an oracle
> that does not answer is retired rather than re-run until it does — the discipline
> `fl-baseline-probe` was retired under. What the discriminator changes is the *reason*, and
> therefore whether a later attempt with a different invocation is worth anyone's time.
>
> #### What P2 retires, and what it leaves exactly where it was
>
> **RETIRED:** PresentMon 2.x `FrameType` as the application-frame oracle for NVIDIA frame
> generation. `03_METRICS` §Frame Generation rung 2 is narrowed accordingly.
>
> **NOT ANSWERED:** *is a drained Streamline batch an application frame?* This entry's actual
> question is untouched. `presents / batch` still reads 2.00 and 3.99 against a title's own
> ×2 and ×4, still on the unverified premise that the work is recorded once per application
> frame, and still unpublishable as `fg_factor` for that reason. **Four oracles have now
> fallen** — `fl-baseline-probe` by its own falsifier, two readings of Steam's overlay (§S30),
> and this one. `HANDOFF` item 3's producer decision goes back to the hook routes, which is
> where P1/P2 said it would go.
>
> #### Two things the run produced that nobody was looking for
>
> **The `off` leg drained ZERO Streamline batches**, where `spike-notes` §8 recorded
> `presents/batch = 1.000` at off. The difference is Ray Reconstruction: §S30 established that
> RR is evaluated once per application frame, so with RR on there are batches even at off. RR
> was not on here. **Consequence for anyone rebuilding this comparison: our side had only TWO
> readable legs, not three** — enable RR if you need the off leg to carry a ratio.
>
> **The CSV's FIRST column is named `Application`** — it holds the process name — while the
> value being counted, in column 9, is also the string `Application`. A parser resolving by
> position, or grepping for the word, counts process names and reports a ratio. The
> *"columns are resolved BY NAME from the header, never by position"* rule was written into
> `tools/frametype-oracle.ps1` before it had ever seen a real CSV, against exactly the
> collision that turned out to be there. Recorded because the rule looked like ordinary care
> when it was written and was load-bearing.

**The question HANDOFF item 3 cannot close without.** `presents / batch` reads
1.000 / 2.000 / 4.000 against Cyberpunk 2077's own off / ×2 / ×4, and a *batch* is
"a present that drained a Streamline evaluation", not an application frame. The two
coincide on that title only because Ray Reconstruction happens to be evaluated once
per application frame there. **Three oracles have been tried and all three fell** —
`fl-baseline-probe` by its own written falsifier, and two readings of Steam's
overlay, which §S30 records at length. PresentMon 2.x's `FrameType` was
pre-committed as the next measurement because it classifies each present from ETW
and divides by nothing.

> **TWO OBSTACLES MEASURED 2026-08-20, BEFORE THE RUN, AND THE SECOND MAY RETIRE THE
> ORACLE OUTRIGHT** (`spike-notes` §11):
>
> 1. **The console binary will not run unelevated here.** Exit 6, *"requires either
>    administrative privileges or to be run by a user in the Performance Log Users
>    user group"*. The account is neither. So this is not "one command beside a
>    capture"; it needs an elevated shell or a group change, and both are the owner's.
>    `PresentMonSharedService` is running as LocalSystem and does **not** help — the
>    console starts its own session.
> 2. **`--track_frame_type` is a BETA option whose own help says it *"requires
>    application and/or driver instrumentation using Intel-PresentMon provider"*.**
>    `FrameType` is therefore a report of events a vendor chose to emit, not a
>    classification of any present. Whether NVIDIA's driver instruments Intel's
>    provider is unmeasured, and it decides whether this oracle exists at all for
>    DLSS-G.
>
> Recorded here rather than discovered mid-run, because the previous oracle's output
> *read like confirmation* and was not.

**What produced the input** *(past tense: the tool was deleted on 2026-08-27 with PresentMon; kept here because the reasoning is why the run could be trusted).* `tools/frametype-oracle.ps1`, run against a PresentMon
2.x CSV and, optionally, a saved CaptureHost report. It compares **two dimensionless
ratios** — PresentMon's `displayed / application` against our `presents / batch` —
which is §S30's own correction applied: comparing *rates* needs a shared span, and
the span is where the previous draft went circular. Each ratio is internal to one
instrument, so neither side needs to know when the other started.

**The parser has never seen a real PresentMon CSV**, for obstacle 1. It resolves
columns by NAME, refuses loudly when `FrameType` is absent, prints the whole
`FrameType` vocabulary it saw rather than assuming one, and refuses rather than
dividing by zero when nothing is spelled `Application`. Its `-SelfTest` decision
table runs in `build.ps1 check` on every build, because that table is the only thing
standing behind an unvalidated parser.

> ### The decision table, written BEFORE the run
>
> Committed first, in the file that owns the item, so the mapping from measurement
> to action cannot be assembled after the numbers are known — the discipline §S30
> used, including the part where its table turned out to have holes and was kept
> unchanged anyway. **Run at THREE settings: frame generation off, ×2 and ×4.**
>
> | # | What the comparison says | What it means | What to do |
> |---|---|---|---|
> | **P1** | No `FrameType` column at all | The beta option produced nothing here: NVIDIA does not instrument Intel's provider, or the build/driver combination does not | **RETIRE IT**, in this row, exactly as `fl-baseline-probe` was retired. `03_METRICS` rung 2 must then be narrowed to the vendors it actually covers, and item 3's producer question goes back to the hook routes |
> | **P2** | `FrameType` present, **every row `Application`** at ×4 | PresentMon sees the presents but classifies none as generated, while the title is demonstrably generating them | **RETIRE IT** as above. The tool reports this as a falsifier and never as a ratio of 1.0 — a 1.0 here is an absence, not a measurement |
> | **P3** | Ratios agree (within ~2%) at **all three** settings | Two instruments sharing no mechanism agree on a dimensionless quantity across three multipliers. **A drained Streamline batch IS an application frame** | `presents / batch` may be published as `fg_factor` with `fg_source = etw-corroborated`, keeping `FgWindow.BatchRefusal`. `03_METRICS` §Frame Generation and `fl_shm.h` both need the promotion written down in the same PR |
> | **P4** | Ratios agree at ×2 **only** | The ×2 leg cannot discriminate — §S30 already burned one draft on exactly this, because "displayed ÷ a fixed 2" predicts the same number there | **Not an answer.** Do not promote. Report the ×4 disagreement as the finding and chase it |
> | **P5** | Ratios disagree at ×4 by more than ~2% | Either a batch is not an application frame, or PresentMon is counting at a different layer | **Stop and localise before deciding.** Both instruments count presents somewhere; §H5 records that this title's present path has at least two layers. Print the vocabulary and the per-setting numbers before touching either side |
> | **P6** | `FrameType` present but the run needed elevation the operator does not want to grant | Nothing was measured | **Not a result.** An unrun leg is unrun; do not infer P1 from it |
>
> **A rival this table must not be read as excluding.** Agreement at three settings
> makes "a batch is an application frame" the best-supported reading; it does not
> make it proven. If PresentMon's `Application` classification were itself derived
> from a vendor event that fires once per Streamline evaluation, the two instruments
> would share a mechanism and agree for that reason. Nothing measured so far
> excludes it, and §S30's whole lesson is that two numbers agreeing can be one number
> read twice.

**Blocks:** HANDOFF item 3's producer decision, `03_METRICS` §Frame Generation rung 2's
scope, and — through `presents / batch` — whether `fg_factor` is ever publishable on a
Streamline title.

### S27 · The chokepoint is the ANTI-CHEAT gate, and it is not the consent gate

Found 2026-08-05 by an adversarial review of the drain host's design, before it was
built. Recorded because the design said `FlGuardedInject` "ONLY (the chokepoint)"
and read as though that covered the whole of CLAUDE.md rule 1. It does not.

`fl_guard_abi.h` says so in its own header: the ABI *"deliberately does NOT carry
per-game consent … This ABI enforces the ANTI-CHEAT gate — the part that protects
accounts — not the opt-in."* `fl_guard.h` adds `kHookNotEnabled` / `kConsentMissing`
/ `kPreviouslyBlocked` and states **"the guard itself never returns these"**. The
only producer is `HookedCaptureGate`, and its three inputs — `hook_enabled`,
`hook_consent_at`, `hook_blocked_reason` — come from a `games` table that **exists
in `06_DATA_MODEL` and in no `.cs` file**.

So the proposed `Agent --diag <pid>`, on a binary `12_BUILD` publishes
self-contained, would have loaded the Overlay into any x64 process a user named on
a command line: no game record, no consent stamp, no enablement, and the
`19_SAFETY` disclosure never shown. Rule 1's "never automatic", automatically. Not
an anti-cheat bypass — every anti-cheat check still runs — but a **consent and
disclosure gap in a shipped binary**, which is its own class.

**Two tempting fixes were rejected.**

- *Route it through `HookedCaptureGate` with `HookEnabled = true, ConsentedAt =
  UtcNow` synthesised.* It compiles. It is a gate whose verdict is decided before
  it looks — this file's signature defect, wearing the gate's own name.
- *Build it as a native `fl-session-probe` instead.* Strictly worse: a native tool
  structurally cannot read `games.hook_consent_at`, so the opt-in gate could not
  exist inside it at all, and it is then §S9's user-runnable injector renamed.

**What was built instead.** `ShmDrainIntegrationTests` — nothing packages it, and
the target is `hook-harness`, our own dummy D3D app built from this tree, carrying
no anti-cheat and belonging to no publisher. Injecting into it raises no consent
question: no game, no account, no terms of service. The test asserts that
constraint **on itself**, so it cannot grow into something that injects elsewhere
by increments.

~~**The real gate arrives with the `games` table, not before.** Until then there is
no injecting entry point on any shipped binary, and that is the correct state
rather than a missing feature.~~

> **Restated 2026-08-06, because item 1 built the thing this entry was closed by NOT
> building — and the ✅ survives on a different sentence.**
>
> The load-bearing clause was never "the `games` table"; it was **"no injecting entry
> point on any SHIPPED binary"**. `FrameLedger.CaptureHost` is an injecting entry point,
> and it is not shipped: `12_BUILD` publishes exactly `FrameLedger.App` and
> `FrameLedger.Agent`, and neither references it.
>
> **That is now a gate rather than a fact about today's reference graph.**
> `tools/package-closure-check.ps1` walks the transitive `ProjectReference` closure of
> both publish roots and fails on anything outside the allowlist, naming the edge —
> proven by planting a reference from the Agent, which leaves the build green and turns
> the gate red. Without it, §S27's ✅ would have depended on nobody adding a line to a
> csproj.
>
> **Two more things hold it, and both came from a refuter over the design rather than
> from the build:**
>
> - **The consent inputs are no longer synthesisable.** This entry rejected
>   `HookEnabled = true, ConsentedAt = UtcNow` as *"a gate whose verdict is decided
>   before it looks"* — and `HookRequest` was a `record` with `init` members, so that
>   expression compiled from anywhere and reached `GuardedInjectAsync`. A store and a
>   provenance flag only add an honest path beside it. It is now get-only behind a
>   private constructor with `FromConsent` as the sole entry, so the rejected expression
>   is a compile error.
> - **`GameConsentRecord.Stored` is `internal`.** `FrameLedger.Domain` is inside both
>   publish closures, so a public minting factory there would be a blessed *shipped* API
>   for producing consent nobody gave — and the closure gate structurally cannot see it,
>   because Domain legitimately belongs to both. The `InternalsVisibleTo` list is the
>   reviewable artifact.
>
> **What the record does NOT claim.** `19_SAFETY` requires the timestamp *"stamped by the
> Agent, never supplied by a client"*, and a file on disk is by construction supplied by
> whoever can write it. That property is **not** upheld and `04_CAPTURE` now says so
> rather than implying otherwise; what stands in for it is a build-tree file belonging to
> an unshipped binary, a provenance whose default means no disclosure was shown, and an
> `en`-only operator acknowledgement that states in its first line that it is not FR-2.1
> consent. The real gate still arrives with the `games` table.

> Also settled by the same review, and worth keeping where the next reader will
> look: **`--diag` is already taken.** `10_LOGGING_AND_BUG_REPORTS` assigns it to
> the App as a stdout capability report while `12_BUILD` and `Program.cs` list it
> as an Agent flag. Whatever the eventual capture flag is called, it is not that.

### S25 ✅ · Both runtime stops were unreachable in a non-presenting process, and pause was unreachable on a ticking one — **closed 2026-08-05**

Found by tracing call paths while planning the next phase, in code merged the
same day (#43). Two defects, one root cause: **`MayObserve()` had exactly one
caller.** `RecordPresent` calls it, and only `Hook_Present` and `Hook_Present1`
call that. `Hook_ResizeBuffers` does not.

**(a) Neither stop fired in a process that had stopped presenting.** A game that
has hung, been alt-tabbed, or is sitting in a menu makes no present calls, so
`unhookRequested` was never read and the `guardTicks` deadline was never
evaluated. The hooks stayed patched in for the life of the process.

`fl_shm.h` states over `FL_GUARD_TICK_DEADLINE_MS`, in capitals, that this must
not be driven by the present hook — *"the clock would stop when presents stop,
which is the exact scenario this exists for — a game that has hung, or been
alt-tabbed, while anti-cheat loads behind it"* — and §S2 rejected a
present-driven re-scan for that reason a day earlier. **A normative comment
prescribing the opposite of the code beneath it**, which is the same shape as the
`MoveFileEx`/`ReplaceFileW` prescription §S21 records: a comment that names a
mechanism is a design somebody builds against.

Be precise about the exposure rather than overstating it: a process that is not
presenting is also not *recording*, so nothing false was written. What failed is
`19_SAFETY` §During a session's clean unhook on detection, and the promise
`legal/DISCLAIMER.md` §2 makes to the user in its own words.

**Measured, both directions.** `unhookRequested = 1` against a live injected
`hook-harness --hold` target (240 presents, then sleep): `status` stayed
`FL_STATUS_READY` through 10 s of polling. After the fix it reaches
`FL_STATUS_UNHOOKED`. ctest `fl_guard`, *"the safety stop fires in a target that
has STOPPED presenting"*.

**(b) `pauseRequested` was unreachable on any frame where `guardTicks` changed.**
The tick-freshness check sat between the safety stop and the pause check and
`return`ed `true` as soon as the tick differed from its cached value — so the
first present after every guard evaluation was recorded regardless of pause.

**Measured: 12 leaked records across 12 guard ticks, exactly one per tick**
(`writeIndex` 9 → 21 while paused). Each carries a `qpc` ~30 s after its
predecessor at the real re-scan cadence, which is a **fabricated 30-second frame
interval** in the series `03_METRICS` computes 1% and 0.1% lows from — the exact
artefact `07_IPC:114-119` forbids for torn records. Latent only because nothing
writes `pauseRequested` yet.

**Closed by a watchdog thread** (`17_HOOK_ENGINE` §The watchdog thread), which
takes the deadline off the present path entirely and thereby fixes (b) by
construction — there is no early return left to jump over the pause check. The
present path keeps its `unhookRequested` check, because `07_IPC` requires that
within one frame and a one-second watchdog cannot promise it. `StopObserving` is
now a compare-exchange, since two threads can reach it and `MH_DisableHook` must
run once; the **first** reason wins, so a self-disable is not overwritten by a
safety stop arriving a millisecond later.

**One thing this did NOT close, stated rather than left to look covered.** The
same trace found that `NoteFault` called `MH_DisableHook`, **discarded its return
value**, and stored `SELF_DISABLED` unconditionally — while setting no
`g_observing`, the flag whose own canary comment calls it *"the only thing that
holds if `MH_DisableHook` ever fails"*. That claim was true of the safety stop and
false of the fault policy. It is now routed through `StopObserving`, **but there
is still no fault-policy test at all**, so the fix has no regression net. The
vehicle is the blocker, not the assertions; `src/native/tests/CMakeLists.txt`
records both rejected approaches and why.

### S1 ◐ · The guard is structurally blind in launch mode — **launch mode built as "inject late", 2026-09-06 (P1 item 2)**

> **Built, and the deferral's missing input is now something every launched session produces.**
> The guard gained `GuardedInjectWhenReady(pid, dll, timeoutMs)` (ABI `FlGuardedInjectWhenReady`): it
> polls the target every 50 ms through the module seam — a `Collected::kFailed` (the suspended /
> still-in-loader `ERROR_PARTIAL_COPY`) is "not yet", never a pass — until `dxgi.dll` with
> `d3d11.dll`/`d3d12.dll`, or `opengl32.dll`, or `vulkan-1.dll` is mapped, then runs `GuardedInjectImpl`
> in full. The poll matches nothing against the blocklist; it chooses WHEN the guard runs and nothing
> about whether it passes. Two new reasons end the poll with nothing injected: `LaunchTargetExited`
> (24) and `LaunchNoPresentationRuntime` (25). The CaptureHost's `launch` verb starts the consented
> executable itself (`ProcessLauncher`, `FRAMELEDGER_ENABLE_VK_LAYER=1`, handle held from birth) and
> routes the same gate through `HookRequest.WaitForPresentationRuntimeMs`; it never terminates what it
> launched. Measured, ctest `fl_guard` `[launch]`: the fake-driven poll injects on the first
> enumeration that shows the runtime and not one before (7 polls, ~300 ms), a target that exits first
> and a budget that runs out each answer their own reason, anti-cheat mapped beside the runtime is the
> full scan's refusal, and the real-seam case against `hook-harness` injects within one poll (15 ms).
>
> **The number nobody had:** `FlWriterState.dxgiPresentsBeforeHook` @60 — DXGI's `GetLastPresentCount`
> at the first hooked present on the first hooked chain, i.e. presents that ran before the hook was in
> (0xFFFFFFFF = the hook never saw a present). With the reported wait (`launch: the guard injected N ms
> after the process was started`) that is the early-init cost §S13(c) said needed a lazily loading title:
> it now comes from every title, on every launched session. **What the numbers decide is whether launch
> mode is *preferred* for a given title, not whether it exists.** The 2026-08-05 rationale stands as
> written below; what it deferred is now measurable rather than argued. Owed: the first real-title launch
> (the owner's next captures), which will also be the first look at a launcher-first title landing on
> `LaunchTargetExited` and needing the Agent's descendant election (P2).

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

> #### 🅓 Deferred 2026-08-05, by owner decision, with this as the rationale
>
> The decision between "inject late" and "no launch mode at all" **is not taken**,
> and deferring it is the recorded answer rather than an omission — P0 exit
> criterion 2 admits a written deferral, and this is it.
>
> **Why deferring is defensible and not merely convenient.** The choice turns on
> one number nobody has: how much early-init upscaler data injecting late actually
> costs. Taking the decision without it would fix a design on an assumption, and
> §S13(c)'s own text has said since 2026-08-02 that the input is missing. Choosing
> "inject late" unmeasured risks shipping a mode that captures nothing useful;
> choosing "drop launch mode" unmeasured throws away **Vulkan Tier 1 entirely**,
> because `enable_environment` can only be set by the process that starts the game
> (`17_HOOK_ENGINE:161`). Neither is a decision to take blind.
>
> **What the deferral costs, stated rather than implied.** Launch-mode injection
> does not work today and will not until this is decided; attach mode is the only
> injection path that ships. Vulkan Tier 1 stays unreachable, though it is
> independently blocked on `vkQueuePresentKHR` (P1), so the deferral is not
> currently the binding constraint there.
>
> **What would end the deferral:** one title that loads `dxgi`/`d3d11`/`d3d12`,
> `opengl32` or `vulkan-1` lazily rather than at startup. The owner supplies real
> fixtures on request; this one has not been found on this machine.

### S2 ✅ · The Vulkan layer has no guard — **both halves done; part three built 2026-09-06 (P1 item 3)**

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

> #### ✅ Built 2026-09-06 (P1 item 3), in the PR that added the present hook — as the deferral below said it would be
>
> The layer intercepts `vkQueuePresentKHR` (+ `vkCreateSwapchainKHR` / `vkDestroySwapchainKHR`), owns the
> same ring the Overlay would (`fl_shm_host.h`, one per process) and runs the in-layer check **on the
> present path**: `unhookRequested` → `UNHOOKED` within one present; `guardTicks` not advancing for the
> 07_IPC deadline → `UNHOOKED`; `pauseRequested` → no record, still forwarded. "Stops" is passthrough forever
> with the reason on the mapping. Driven end to end by a fake loader chain (ctest `fl_vklayer`), the deadline
> proven with a 1.5 s test-only flavour of the DLL (`fl_vklayer_supervision`: ticks every 600 ms keep it
> alive past the deadline, 1.8 s without one stops it), and the real loader + the harness's `--vulkan` mode
> on the dev box (`fl_vklayer_real`, 241 records in 4 s; skips on CI, which has no loader). `17_HOOK_ENGINE`
> §Vulkan carries the shape. **The residual below is unchanged** and the Disclaimer's wording with it.
>
> #### 🅓 Deferred to P1, rationale written 2026-09-06
>
> The decision is taken (below: supervision is the Agent's job; a layer that cannot confirm
> supervision goes inert) and half of it is built. The other half is the in-layer check that
> reads `guardTicks` — and it has nothing to run on until `vkQueuePresentKHR` is intercepted,
> which is P1 (`15_ROADMAP` §P1: *"Vulkan layer to parity — `vkQueuePresentKHR` plus the
> in-layer supervision check §S2 gates on it"*). Building the predicate now would be one
> whose wrong answer changes nothing observable, the defect class this file records. Cost of
> waiting: none a user can see — the layer intercepts nothing, so there is no Vulkan capture
> to stop.

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
  false — ~~Tier 2 needs an elevated Agent~~ **and as of 2026-08-28 the premise is dead
  while the conclusion gets STRONGER: Tier 2 needs no elevation and measures nothing,
  so dropping Vulkan there is not a degradation, it is a removal.** For most users the real proposition
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

**~~◐ The in-layer half is deliberately NOT built yet, and this stays open.~~ ✅ Built 2026-09-06, see the top of this part.**
The layer intercepts nothing — `vkQueuePresentKHR` is P1 — so a `ShouldObserve()`
today would be a predicate whose wrong answer *in either direction* changes
nothing observable. That is a gate on something that does not exist, which is the
defect class this file keeps recording. It lands in the PR that adds the present
hook, where a fake loader chain can drive it end to end. *(It did: `MayObserve()` in
`layer.cpp`, ctest `fl_vklayer` / `fl_vklayer_supervision`.)*

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

> #### 🅓 Deferred to P4, rationale written 2026-09-06
>
> Signing is a property of the feed, and there is no feed (§S20's feed half, deferred to
> P4 in the same sweep): every rules file on every machine today shipped inside a build
> whose SHA-256 is published, so the content IS attested, by the release. The decision that
> belongs to P4 — a compiled-in public key and a detached signature over the rules file,
> verified before validate-then-replace, with the same fail-closed polarity as the rest of
> the feed rules — is written here so the feed cannot ship without it. Residual until then:
> none beyond the release checksum, because nothing fetches.

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

> **🅓 Deferred 2026-08-05 by owner decision — the rationale is written once, under
> §S1**, because this item and §S1 turn on the same missing measurement and two
> copies of a rationale is how they drift apart. §S24 lists both as deferred.
>
> Note what the deferral does **not** touch: the externally observable proxy this
> entry proposes (resume, then poll until the target has mapped a presentation
> runtime *and* the scan passes *and* the blocklist is clean) remains the only
> candidate design for "inject late". It is unbuilt, not rejected.
>
> > **✅ Built 2026-09-06 (P1 item 2), exactly that proxy** — `FlGuardedInjectWhenReady`, §S1 carries
> > the mechanism and the measurements. "Salvageable" is answered yes; the cost of injecting late is
> > reported by the session itself (`dxgiPresentsBeforeHook`, and the wait).

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

**(a) ✅ `gameguard` could never fire — closed 2026-08-05.**
`HasSuspiciousFragment` is case-insensitive `IContains`, and `"gameguard"`
**contains** `"guard"`. With both in the list, the `guard` fragment matched
everything `gameguard` would, first. A shipped rule incapable of firing
independently — the file's own recurring defect, sitting inside the safety gate.

**Closed by deleting the entry, and by a rule that stops the class returning.**
`rules-validate` now fails when any `nameFragment` contains another, naming which
one can never fire and why. Proven both directions: re-adding `gameguard` goes
red at a named line; the shipped list is green.

**Why deletion was the safe option here and is not, elsewhere.** The standing
objection to removing a fragment — it is a detection removal in a hard gate — is
correct for `protect` and does not apply to this one. Subsumption means *by
construction* that no module name loses coverage: anything `gameguard` matched,
`guard` matches. And it was redundant twice over, which nobody had checked:
nProtect GameGuard has its **own named module family** in the same file
(`GameGuard`, `npgg`, `GameMon`, exact-prefix), so the fuzzy tier was never its
only route.

**The hazard this closes was always in the future, and that is why "cosmetic" was
the wrong word for it.** With both entries present, someone removing `guard`
leaves a list that still appears to cover nProtect. Now the list is minimal, so
removing `guard` is visibly a removal.

> Removing it trips `rules-publish`'s `$removed = old − new` check. **That is the
> gate working**, not an obstacle: it exists to make a blocklist removal
> reviewable, and this is one.

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
> #### 🔴 MEASURED FIRING IN A REAL SCAN SET, 2026-08-05 — the paragraph above is superseded on its central point
>
> The claim that closed this entry was: *"the `protect` fragment matches a benign,
> widely-loaded Microsoft system DLL, and **has not been shown to match inside any
> game's scan set** … which is plausible and unmeasured — not 'refused today'."*
>
> **It has now been shown.** CI, running the drain integration test:
>
> ```
> the guard refused our own harness: SuspiciousUnsigned unknown
> System.Security.Cryptography.ProtectedData.dll
> ```
>
> The mechanism is the one this entry ruled out. §S16 puts the target's **ancestors**
> in the scan set, and the test host is the harness's parent — the launch-mode
> arrangement, where the Agent is the game's parent. A .NET host loading that
> assembly therefore poisons its own scan set, and the injection it is trying to
> perform is refused. **A gate that cannot pass**, which is the mirror-image defect
> this file records as hiding better than the fail-open, because refusing looks safe.
>
> Three things this does and does not say:
>
> - **It is the §S18 shape with a different module.** §S18 was our own
>   `FrameLedger.Guard.dll` matching `guard`; the exemption built for it is keyed on
>   the matched module being **ours by file id**, and a .NET shared-framework
>   assembly is not.
> - **Attach mode is unaffected.** The Agent is not an ancestor there — a
>   normally-launched game's chain terminates at a platform launcher one hop above
>   it — so this is a **launch-mode** hazard, and launch mode is deferred (§S1).
> - **It passed on the dev box and failed on CI**, because the two hosts load
>   different module sets. Which shipped configuration the dev machine is not, again.
>
> **Not fixed here.** The remedies all have costs this entry already priced: the
> signer half is a `CryptCATAdmin*` PR whose default revocation check does network
> I/O from inside the hard gate (NFR-10), deleting the fragment is a detection
> removal in a hard gate that three refuters rejected, and a location-based
> exemption for the shared framework widens the carve-out §S18 deliberately kept
> narrow. What changed is the **evidence**, not the decision: this is no longer a
> hypothesis, and whoever picks up §S19(b) now has a reproducible case.
>
> #### ⚠ AND IT FIRES ON THE DEV BOX TOO — INTERMITTENTLY — measured 2026-08-28
>
> This entry says the cases *"pass on a host that does not load the assembly"*, and the line
> below adds *"`./build.ps1 check` with no switches still runs them"*. Both read as though the
> dev box were a stable pass. **It is not.**
>
> One full `check` on a docs-only branch went red at
> `ShmDrainIntegrationTests.APausedSessionStopsRecordingAndResumesWhereItLeftOff` with
> `GuardedInjectAsync(...).IsAllowed` **False** — the guard refusing our own harness. The same
> test alone passed immediately afterwards, and the next full `check` passed too. Nothing in
> the branch touched code.
>
> **The mechanism is this entry's own**, one variable further out: the suite runs four test
> assemblies in parallel, so *which* modules a given test host has loaded when the guard walks
> its ancestors varies run to run. A .NET host that loaded
> `System.Security.Cryptography.ProtectedData.dll` refuses; one that has not, does not.
>
> **Two consequences.** A local red here is **not** evidence about the branch — check the
> refusal reason before reading the diff, exactly as `§Traps` now says for `fl_ring`. And the
> defect is worse than "CI-only" made it sound: the population is any host that happens to
> load a `protect`-matching module, which is a property of the run rather than of the machine.
>> The integration tests are traited `Category=Integration` and CI runs
> `./build.ps1 check -SkipIntegration`, which **skips loudly**. `./build.ps1 check`
> with no switches still runs them, and they pass on a host that does not load the
> assembly. A suite that quietly stops running a class is how a gate rots, so the
> skip is named in the CI log and in the gate summary.

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
> ~~And it is **not** a wiring change. `NameSink` is `bool(*)(void*, const char*)`
> fed by `GetModuleBaseNameA`, so the evidence the check needs — a module's full
> path — does not reach the decision point. The signer half requires widening the
> module seam, which is a new row in the fail-closed matrix, not a call added at
> `fl_guard.cpp:293`.~~
>
> ~~**And it must not be placed at `fl_guard.cpp:293`.** `NameSinkFn` latches the
> FIRST fragment-matching module per process (`!st->sawSuspicious`) and discards
> every later one. Harmless while any hit refuses; the moment a signer can CLEAR
> the latched name, a process that loads a trusted fragment-module before an
> untrusted one returns `Allow` with the second never recorded — a fail-open
> reachable by load order. The detection half at `fl_guard.cpp:203` has to be
> restructured, not extended.~~
>
> > **BOTH COSTS WERE PAID BY §S22(b), AND THIS ENTRY NEVER HEARD — struck 2026-08-27.**
> > They are the two clauses anyone costing (b) reads first, and both describe work that
> > has already landed:
> >
> > - **The seam is already widened.** `fl_guard.h` declares
> >   `using ModuleSink = bool (*)(void* ctx, const char* name, const wchar_t* path)`, and
> >   the module walk takes it. The full path reaches the decision point today; §S22's own
> >   entry records the change as *"not the size the plan assumed"*.
> > - **The restructure is already done**, and for this entry's own stated reason. The
> >   detection half no longer latches-and-skips: `ModuleSinkFn` returns *keep looking*
> >   after an exemption, and its comment says so in as many words — *"§S19(b) predicted
> >   exactly this and said the detection half has to be restructured rather than extended.
> >   This return is that restructure."*
> >
> > **What that leaves, and it is smaller than this entry prices:** one new `Sources`
> > member and one call beside `ModuleIsExempt`, in the same shape and with the same
> > fail-closed clauses. **The safety argument is unchanged and is the whole of it** — the
> > new call must return *keep looking* on a trusted module and never *stop*, or the
> > load-order fail-open the struck paragraph describes is reachable after all. It is still
> > a new fail-closed matrix row, and still its own PR.
> >
> > Struck rather than deleted: what went stale here is a **cost estimate**, and a cost
> > estimate that survives the work it was written about is how an item stays deferred
> > after its reason expires.

> #### `fl-probe-signer` EXISTS AND HAS BEEN RUN — 2026-08-27, dev box, unelevated
>
> This entry's deferral rationale asked for exactly one thing before any design is
> fixed: *"Build `fl-probe-signer` first, in the shape `fl-probe-guard` established,
> and answer those three questions with measurements."* It is `ctest fl_signer_probe`,
> and the numbers are in `spike-notes` §1. What belongs here is what they decide.
>
> **SAY THIS FIRST, BECAUSE IT WEAKENS EVERYTHING BELOW.** §S30 and §S31 each carry a
> decision table written *before* its run, so the mapping from measurement to action
> could not be assembled once the answers were known. **This table is not that.** The
> probe had to be run to be built — its acceptance criterion is that it *prints*
> answers, so an unrun probe is an unfinished one — and the dev-box numbers were
> therefore in hand before these rows were written. The rows below are pre-committed
> only for the legs that are still **unrun**: CI, and the adapters-disabled run. For
> the dev box they are a reading, not a prediction, and should be reviewed as one.
>
> **Q1 · the embedded route recovers an organisation for the CI blocker, and NOT for
> this entry's own subject.** Both halves matter and they point opposite ways:
>
> | Subject | embedded, offline | catalog, offline | `O=` |
> |---|---|---|---|
> | `System.Security.Cryptography.ProtectedData.dll` — the CI blocker | **`ERROR_SUCCESS`** | no catalog | `Microsoft Corporation` |
> | `mskeyprotect.dll` — this entry's own subject | **`TRUST_E_NOSIGNATURE`** | **`ERROR_SUCCESS`** | `Microsoft Corporation` |
> | `kernel32.dll` | `ERROR_SUCCESS` | `ERROR_SUCCESS` | `Microsoft Corporation` |
>
> So **the embedded half alone would clear the CI refusal** — the organisation it
> recovers is already in the shipped `trustedSigners` — while **`mskeyprotect.dll`
> still needs `CryptCATAdmin*`**, exactly as this entry predicted. The two are
> separable, and that separation is the affordable part: the merge-gate work does not
> have to buy the catalog half.
>
> `kernel32.dll` carries **both**, which contradicts the assumption this probe was
> written under — that a catalog-signed system binary is catalog-*only*. Recorded
> because it means "catalog-signed" is not a property you can infer from a file being
> a system binary; it has to be measured per file, which is what the probe now does.
>
> **§S19(b) ALSO MIS-DESCRIBES THE MODULE, and the correction closes a tempting
> route.** This entry calls it *"a .NET shared-framework assembly"*. It is not:
> `Microsoft.NETCore.App` 10.0.11 does not contain it. It is a **NuGet package
> assembly, version 6.0.0**, reached transitively as
> `Microsoft.NET.Test.Sdk` → `System.Configuration.ConfigurationManager` →
> `System.Security.Cryptography.ProtectedData`, and copied next to every test binary.
> The tempting inference — *a package reference can just be dropped* — **does not
> survive it**: dropping this one means dropping the test SDK.
>
> **Q2 · about 3.5 ms per module, and WARM IS NOT CHEAPER THAN COLD.** Cold 3.45 ms,
> warm mean of 20 = 3.54 ms. There is no amortisation to plan around: the second
> verification of the same file costs what the first did. Against a 30 s re-scan
> (§S6) that is comfortable *because the scan set is small* — three real titles
> produced no fragment hit at all — and it would stop being comfortable on a title
> that trips the fragment tier many times. A cache **within** one evaluation is
> admissible; a cache **across** evaluations is a re-scan that did not run.
>
> **Q3 · `cryptnet.dll` LOADS ANYWAY, and this is the finding that can stop the
> route.** Under `WTD_REVOKE_NONE | WTD_CACHE_ONLY_URL_RETRIEVAL`, in a census
> bracketing the offline arm **alone**, `cryptnet.dll` is newly loaded into the
> process. Nothing further loads once the default `WTD_REVOKE_WHOLECHAIN` calls run,
> so this is not the default arm leaking into the measurement.
>
> > **The probe's first version could not have told those apart, and it read like an
> > answer.** It took one census at the top and one at the bottom with *both* arms in
> > between, so `cryptnet.dll` appeared and the delta could not attribute it. A census
> > that spans both arms of the comparison it exists to discriminate is not a
> > measurement. Fixed before this entry was written; recorded because it is the same
> > shape as §S30's *"two numbers agreeing can be one number read twice"*.
>
> **What loading is and is not.** `cryptnet.dll` being MAPPED is not a network
> request; it is the module that would make one. This probe has no packet counter, so
> it reports the module and refuses to conclude. **The discriminating run is the
> owner's: adapters disabled, same three subjects, compare the verdicts.**
>
> #### The rows, pre-committed for the legs that are still unrun
>
> | # | What the unrun leg reports | What it means | What to do |
> |---|---|---|---|
> | **G1** | Adapters disabled: all three verdicts UNCHANGED, and CI reproduces the blocker's `ERROR_SUCCESS` + `O=Microsoft Corporation` | The offline flags deliver offline behaviour, and the embedded half alone clears the CI refusal | **Build the embedded half**, and defer the `CryptCATAdmin*` catalog half **with its own written rationale** rather than silently. It is not on the path to the merge gate |
> | **G2** | Adapters disabled: any verdict CHANGES | The flags do not deliver offline behaviour — a verification whose answer depends on connectivity is doing network I/O inside the hard gate | **DO NOT BUILD IT.** §S19(b) stays deferred, now on a *measured* rationale rather than an assumed one. Re-open only with a mechanism that has no URL-retrieval path at all |
> | **G3** | CI reports an `O=` that is not `Microsoft Corporation`, or empty | The `O=` premise does not hold for the copy CI actually loads | **STOP.** A signer half reading the wrong field suppresses the wrong modules. Settle `signerField` in `19_SAFETY` §Blocklist seed first |
> | **G4** | CI's blocker copy returns `TRUST_E_NOSIGNATURE` | CI stages a different copy than the dev box does | Measure *which* copy before deciding. The dev-box answer does not transfer |
> | **G5** | Cost on CI exceeds ~10 ms/module | The 30 s loop absorbs it only for a small scan set | Not a veto alone: bound the scan set or cache **within** one evaluation, and re-measure. Never across |
> | **G6** | Anything not covered above | The probe found something nobody predicted | **Not a result.** Print it, add a row, re-run. Do not promote a surprise into a design |
>
> **And a constraint no row can lift, because it is not a measurement.** Wiring the
> signer half makes `trustedSigners` a **live allow-widening surface**: today the field
> suppresses nothing, and the moment it does, a rules push that adds a publisher widens
> the hard gate. The gate over that is `Rules / validate`, which **§S23-2 measured is
> not a required status check on `main`**. `19_SAFETY` already ruled on this exact shape
> for the launcher list — *"a data-driven cutoff would let a rules push widen the hard
> gate's blind spot ... the boundary of what the gate looks at is code."* **So even a
> clean G1 does not authorise the build**: either `Rules / validate` becomes required
> (§S23-2, owner-only, and the skip-shim lands first), or `trustedSigners` is bounded by
> a compiled-in allowlist the data file can only intersect. **That is an owner decision
> and no PR may take it.**
>
> **Nothing under `FrameLedger.Injector` changed with the probe**, deliberately. This
> is a measurement, and reading a row off the table is a separate PR — including the
> row that says build nothing.
>
> **THE CI LEG WAS NEVER READABLE, found 2026-09-06 when it was finally looked for.**
> `ctest fl_signer_probe` passes on `BLOCKER: (FOUND|NOT PRESENT)` and ctest prints nothing
> for a passing test, so eleven days of green CI carried none of the probe's answers — the
> leg rows G3/G4/G5 are written for was "run" in the sense that a binary exited 0. And the
> native tests run BEFORE `dotnet restore` in `build.ps1`, so on a cold runner the NuGet
> copy the probe searches first may not exist yet at ctest time. `ci.yml` now runs the probe
> as its own step AFTER the gate (`continue-on-error`, a measurement and not a gate), and the
> answers are read off that step's log. **Owner-only, still unrun:** the adapters-disabled
> leg (rows G1/G2 — a network setting, not a PR), and the allow-widening bound that no row
> can lift (below). The recommendation for the bound, so the owner decides between two
> written options rather than one: **bound `trustedSigners` by a compiled-in allowlist the
> data file can only intersect** — the boundary of what the gate trusts stays code, exactly
> as `19_SAFETY` ruled for the launcher list, and it needs no branch-protection change; the
> alternative, making `Rules / validate` a required check, is §S23-2 and is the owner's
> GitHub setting, not a file in this repository.
>
> **THE CI LEG, READ 2026-09-06 off the new step (run on `ci/signer-probe-leg`, `windows-latest`,
> the probe reporting `elevated: yes` — the runner is an administrator; the dev-box leg was
> unelevated).** The blocker at the runner's NuGet cache (`system.security.cryptography.protecteddata`
> 6.0.0, `lib/net6.0`): embedded/offline **`ERROR_SUCCESS`, 3.47 ms, `O= Microsoft Corporation`**,
> no catalog; `mskeyprotect.dll` `TRUST_E_NOSIGNATURE` / catalog `ERROR_SUCCESS`; `kernel32.dll` both;
> cold 3.45 ms, warm 3.53 ms; `cryptnet.dll` newly loaded under the offline flags, as on the dev box;
> the canary `TRUST_E_NOSIGNATURE`; the probe's own verdict *"G1 is a CANDIDATE"*. **So G3 does not
> fire (the `O=` premise holds on CI's copy), G4 does not fire (CI's copy is embedded-signed), G5 does
> not fire (3.5 ms).** What is left before G1 can be read is exactly the two owner-only legs above:
> adapters disabled (G1 against G2), and the bound.
>
> **THE ADAPTERS-DISABLED LEG, the owner's run, 2026-09-06 (dev box, elevated; reported in answer
> to the request to run it with every adapter off — if it was not, this paragraph is withdrawn
> and the leg is unrun).** Every verdict UNCHANGED against `spike-notes` §1: `kernel32.dll`
> embedded `ERROR_SUCCESS` / catalog `ERROR_SUCCESS`; `mskeyprotect.dll` embedded
> `TRUST_E_NOSIGNATURE` / catalog `ERROR_SUCCESS`; the blocker embedded `ERROR_SUCCESS`, no
> catalog, **`O= Microsoft Corporation`**; canary `TRUST_E_NOSIGNATURE`; `cryptnet.dll` still
> mapped under the offline flags and still not evidence of a request. Faster than the online run
> (2.14 ms cold, 2.15 ms warm; 2.97 / 2.13 ms on the system binaries), which is what an offline
> cache-only path should look like. **One detail worth its line:** the blocker's subject now reads
> `CN=.NET` where the August copy read `CN=Microsoft Windows` — the NuGet copy was re-staged with a
> different certificate — and `O=` is the same on both, which is the `signerField: "O"` decision
> confirmed on a subject class it was never measured against. **Row G1 reads.** The embedded half
> alone, offline, clears the CI refusal; the `CryptCATAdmin*` catalog half is deferred with its own
> rationale (it reaches `mskeyprotect.dll`, which is not on the path to the merge gate, and it is
> the `CatRoot` walk §S19(b) priced). **What still stops the build is the bound**, above — the
> owner's, and asked for beside this reading.
>
> #### ✅ BUILT 2026-09-06 — the embedded half, on row G1, under the compiled-in bound (owner: option 1)
>
> **The owner chose the bound the same afternoon: a compiled-in allowlist the rules file may only
> intersect.** `IsCompiledTrustedSigner` in `fl_ac_rules.cpp` names the five organisations the shipped
> rules name, and `IsTrustedSigner` now requires BOTH lists — a rules push that adds `"Evil Corp"`
> widens nothing, which is one assertion in `fl_guard`. `Sources` gained
> `ModuleSignerOrganisation`: `WinVerifyTrust(WTD_CHOICE_FILE)` under `WTD_REVOKE_NONE |
> WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_SAFER_FLAG`, and only after `ERROR_SUCCESS` the signing
> certificate's subject `O=` through `CryptQueryObject` — the probe's shape with fixed buffers, in
> `fl_guard_sources.cpp` (never `fl_ac_rules.cpp`, which the Vulkan layer compiles: wintrust and
> crypt32 stay out of every Vulkan process). `ModuleSinkFn` asks it after the ours exemption and
> BEFORE latching: a trusted module returns *keep looking*, exactly as the exemption does, so the
> load-order fail-open this entry predicted is closed by the same structure — and it applies to the
> TARGET too, because a game loading a Microsoft-signed key-protection provider is the false refusal
> the half exists to remove. **The fail-closed row, added:** no embedded signature (every catalog-only
> system binary — `mskeyprotect.dll` by design), an invalid one, an unreadable `O=`, a null path, a null
> seam, a seam that fails, an organisation on one list but not the other — all untrusted, all refuse.
> The catalog half stays deferred: it reaches only modules that are not on the path to the merge gate,
> and it is the `CatRoot` walk this entry priced.
>
> **Proved in both directions, against the real seams:** `hook-harness --load <dll>` puts a named module
> into the target before the guard looks. The NuGet copy of the CI blocker → **Allowed**, Overlay
> injected (skipped loudly on a machine with no NuGet cache; the managed integration cases are the gate
> that cannot skip it). Our own unsigned Overlay copied under a `protect`-bearing name outside the
> guard's directory → **SuspiciousUnsigned**, naming it, nothing injected. Nine fake-seam cases pin the
> decision (both lists, the target, kFailed, missing seam, pathless, load order, exact-hit precedence).
> **And `-SkipIntegration` is gone from `ci.yml` in the same PR**: the nine `Category=Integration`
> cases are in the merge gate for the first time since 2026-08-05, and CI runs the same
> `./build.ps1 check` a developer does. The switch stays in `build.ps1`, loud, for whoever must.

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
>
> #### ✅ Resolved 2026-09-06 — the consumer is the validator, and the policy is code by rule
>
> Decided the way the paragraph above costs it: `warn` and `allow` do not exist (CLAUDE.md
> rule 2), and the signer field is `O=` by measurement (`19_SAFETY`), so neither field is
> for the guard to read — they exist so that the DATA cannot claim otherwise. What was
> missing was the thing that makes that true: `rules-validate` now carries a third canary
> that mutates the shipped document to `action: "allow"` and to `signerField: "CN"` and
> requires the schema to reject both. Delete either `const` and the gate goes red. The
> guard's hardcoded refusal is the specification, and `IsCompiledTrustedSigner` (§S19(b))
> made the other half of the heuristic code-bounded the same day.

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
> - **✅ The schema canary does not discriminate on this constraint — closed
>   2026-08-05.** It was `{"schemaVersion":"not-a-number"}`, which any schema
>   still pinning `schemaVersion` rejects. Delete `minItems` from `nameFragments`
>   and that canary still passed, `Test-Json` still passed, and the schema half of
>   the floor was gone.
>
>   A second canary now carries `nameFragments: []` and must be rejected. It is
>   **derived from the shipped document** — parse, empty the array, re-serialise —
>   never hand-written, because a hand-written canary is a second statement of the
>   schema's shape and drifts from it, which is the defect this file exists to
>   catch. Mutating the real document makes the constraint under test the *only*
>   difference between the passing and failing cases. Proven red by deleting
>   `minItems` from the schema.
>
>   **The consequence in the bullet above was overstated and is corrected here.**
>   *"the CI floor silently ceases to exist"* is not what would happen.
>   `tools/gen-ac-floor.ps1` hard-errors on an empty fragment list and runs as a
>   CMake custom command, so the native **build** fails. What was unguarded is the
>   *schema* half — a rules file with an empty list reaching a machine, where the
>   compiled-in floor is what saves it. Narrower than written, and worth fixing on
>   its own terms rather than on an inflated one.
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

  > **🅓 Deferred to P4, rationale written 2026-09-06.** The feed is the product's one
  > permitted outbound request after the release check (CLAUDE.md rule 8), and it needs
  > three things P0 does not have: a shipped Agent to run it in, the signing §S4 defers to
  > the same phase, and the privacy policy's rows that describe it (`legal/PRIVACY_POLICY.md`
  > already flags them as describing a request the software does not make). Until then a
  > rules edit reaches a machine with the next release, whose checksum attests it — a slower
  > delivery, not a weaker gate. FR-7.3 stays unmet and stays written as unmet.
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

**1. ✅ `FL_BUILD_ID` has a writer and no reader — closed 2026-08-05.**
`FlGuardBuildId` on `FrameLedger.Guard.dll` gives the Agent the value `04_CAPTURE`
calls "its own", and `FrameLedger.Shared.ShmHandshakeValidator` is the comparison.

**Why the GUARD carries it and not the Overlay.** The Overlay exports
`FlGetBuildId()`, and the Agent cannot call it: reaching that export means
`LoadLibraryW` on `FrameLedger.Overlay.dll`, which starts its init thread and
creates a ring under the **Agent's own pid**. The payload is not something its own
host may load. The guard is already loaded by the Agent, by absolute path from our
install directory (§S22).

**Guard and Overlay agree by construction, not by test, and that is stated rather
than implied.** `FL_BUILD_ID` is one INTERFACE compile definition on
`FrameLedger.Shm`, set once per CMake configure, and both targets link it. No test
asserts the equality because `fl_guard_abi.cpp` is not compiled into
`fl_guard_test` — `guard_test.cpp` says so at the assertion that covers the
Overlay's half.

**The failure this closes is a fail-open, and the test for it was nearly missed.**
The naive implementation compares two strings. With neither side carrying an id,
`string.Equals("", "")` is **true**, so the gate reports `Ok` and permits attaching
to anything — measured, by removing the emptiness guards and watching
`ShmAttachRefusal.Ok` come back. The first draft of the test file asserted "no id
of our own refuses" and "no id in the handshake refuses" as separate cases and
**never put both halves in one call**, which is the only arrangement that reaches
the defect. Recorded because a suite can cover each half of a condition and still
miss the condition.

<details><summary>The finding as recorded</summary>

**1. `FL_BUILD_ID` has a writer and no reader.** §S22-era work gave
`FlShmHandshake::buildId` a producer, which it had never had. But the contract is
a *comparison* — `07_IPC` "the Agent compares … against its own",
`04_CAPTURE` "validate layout version + build id against our own" — and the
managed side has no build id and no way to obtain one: `FL_BUILD_ID` is a CMake
INTERFACE compile definition visible only to native targets, and `grep -rni
buildid` over `src/**/*.cs`, `*.csproj` and `*.props` returns **zero**. So the
refuse-to-attach-on-mismatch gate still cannot run. Half a mechanism reads as a
whole one in the CHANGELOG, and that is corrected here rather than there.

</details>

**2. `rules-publish`'s removal check is not a required status check.** `main`'s
required contexts are exactly `check`, `analyze (csharp, none)` and
`analyze (cpp, manual)`. The `validate` job is path-filtered and runs on
`pull_request` only, so a red removal check **does not block the merge button** —
the gate that exists to make the anti-cheat blocklist un-removable is advisory.
Fixing it is a branch-protection change, i.e. the owner's, not a code change.

**3. 🅓 Deferred to P3, rationale written 2026-09-06 — `08_UI` describes a refusal notice that is wrong in the commonest case.** *(The requirement below must reach `08_UI` and the `Safety_*` keys in the PR that creates the first refusal UI, and not before: there is no UI to be wrong in yet, and a `.resx` string with no view is the kind of fix that reads as done. The spike log carries the measurement; this line is the reminder.)* It
promises "the specific signal named in plain language". Measured (`spike-notes`
§13): while any Easy Anti-Cheat title is running, **every** target — including a
freshly spawned, unrelated process — refuses with `BlockedService` naming
`EasyAntiCheat_EOS`, because checks 2 and 2b are machine-wide. The user is shown
the name of a game they are not playing. The requirement created by that
measurement was written only into the spike log; it needs to reach `08_UI` and
the `Safety_*` resx keys before any UI exists.

**4. ✅ The runtime re-scan loop is described as two checks and implements four — closed 2026-08-05.**
`19_SAFETY` §During a session now reads "every pre-injection check", deferring to the
one list in §Pre-injection checks — including its ⚠ that check 3's store-id half cannot
be called, which the re-scan inherits rather than closes. A list restated in two places
is what went stale; the fix is to stop restating it, not to correct the copy. The two
checks the old wording omitted are named there, with why each matters.

> **This closure was itself stale within hours, and the mechanism is worth naming.**
> The sentence it landed said *"including its ⚠ that check 3 is unwired … one of the
> four documented ones still does not [run]"*. #52 rewrote that ⚠ three hundred lines
> away and the pointer kept paraphrasing the old text — **the exact restate-in-two-
> places failure this item was closed for, committed by the closure.** Deferring to
> another section does not help if the pointer restates what it points at; a paraphrase
> is a copy. It was also wrong about the count: 2b is a documented check, so five run,
> not four. Corrected 2026-08-05.

<details><summary>The finding as recorded</summary>
`19_SAFETY` §During a session reasons explicitly about the loop's composition and
names the module and driver scans. `GuardSupervisor.ScanOnceAsync` →
`FlGuardEvaluate` → `EvaluateImpl` runs drivers, **services** and the **static
pre-scan** as well. That omits the only tier ever measured firing on real
anti-cheat (services) and the only one touching the filesystem (check 4) — whose
cost this session raised by deepening the walk. The paragraph that decides what
the loop costs is missing the expensive half.

</details>

**5. ✅ A fourth statement of the gate's composition lived in the shipped data — closed 2026-08-05.**
`rules/detection-rules.json`'s own `$comment` enumerated the checks that run,
omitting 2b and omitting `services` from the signals that catch the seed — in the
one copy that ships to users. Three doc-side variants were reconciled that
session; this one was not, and `rules-validate`'s doc/data cross-check reads the
blocklist table, not the comment.

> **Closed the way §S23-4 closed the same class: by removing the restatement, not
> by correcting it.** The comment now states only facts about *this data* — that
> both per-title arrays are empty, and where the composition is documented — and
> `rules-validate` fails when any `$comment` in the document enumerates checks
> again. Proven red by putting the old sentence back.
>
> **The first version of that rule could not fire, and that is the part worth
> keeping.** It was scoped to `anticheat.$comment`; the text it exists to catch
> lives in the **top-level** `$comment`. A check pointed at the wrong object —
> this file's signature defect, committed inside the fix for it. Its canary
> reported green twice before the cause was found: once because a backtick inside
> a double-quoted PowerShell needle silently mangled the search string so the
> mutation never applied at all (the harness, for the sixth time on this project),
> and once for real. The rule now walks **every** `$comment` in the document and
> **fails if it finds none**, because a walk that reports clean having looked
> nowhere is the same defect one layer up.

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
>
> **✅ Closed 2026-09-06, by making there be one block.** `legal/ACCURACY.md` is the single
> statement of what the software measures, dated; `README.md` and `legal/DISCLAIMER.md` §4
> embed it verbatim between markers, and `tools/accuracy-check.ps1` — a `build.ps1` gate with
> a four-case self-test, both directions — fails when any copy differs from the source, when
> a copy has no markers, or when the source is empty. What the gate does NOT do is decide
> whether the block is true; that stays with whoever changes the Overlay or the capture host,
> and the block's own comment says so. The block was rewritten to 2026-09-06 truth in the same
> PR (the previous §4 note still said frame generation and ray tracing had no hook).

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
> #### ◐ The EXECUTABLE half is wired, 2026-08-05. The store-id half is BLOCKED, and named.
>
> `CheckBlockedExecutable` runs inside `EvaluateImpl`, between the module scan and
> the static pre-scan, so checks 1, 2, 2b, **3 (exe)** and 4 now run. It needed a
> new seam — `Sources::ImageFileName` — because `ImageDirectory` deliberately
> resolves the *install root* and throws the file name away, which is the one fact
> check 3 matches on.
>
> **Unresolvable identity refuses** (`kProcessUnreadable`), per the owner decision
> and `19_SAFETY`'s "must read UNKNOWN, never clean": `kFailed`, `kIncomplete`, an
> empty name and a null seam all take the same path. The conversion to narrow is
> `WC_ERR_INVALID_CHARS` with no default character, so a name that cannot be
> represented exactly **fails** rather than becoming a string with `?` in it —
> §S21's ANSI defect was exactly a silent lossy conversion.
>
> **The list ships empty**, so nothing is refused today. Which titles to list stays
> the owner's product decision; what changed is that populating it would now *do*
> something.
>
> **The store-id half cannot be called, for three independent reasons**, and this
> is a limitation rather than an oversight:
>
> 1. **No producer.** Nothing parses Steam `.acf`, GOG `.info` or Epic `.item` —
>    the platform metadata extractors were never built, so `store_id` is null for
>    every title (`15_ROADMAP`).
> 2. **No channel, by design.** `FlGuardEvaluate` takes a pid and nothing else, and
>    `fl_guard_abi.h` says so deliberately: *"no way to hand in evidence — the guard
>    collects its own."* A caller-supplied store id is a caller asserting a safety
>    fact, which §S3 forbids in as many words.
> 3. **"Unknown refuses" applied to it is a gate that cannot pass.** If the guard
>    can never resolve a store id and an unresolved one refuses, every title on
>    every machine refuses.
>
> So `MatchesBlockedStoreId` stays implemented, tested and uncalled. **Do not
> "fix" (2) by widening the ABI.** The route is the metadata extractors plus a
> guard-side resolver, which is its own PR and its own fail-closed matrix row.
>
> **🅓 The store-id half is deferred to P4, rationale written 2026-09-06.** Its only
> in-policy route is the platform metadata extractors (Steam/GOG/Epic/itch — P4's library
> import) feeding a guard-side resolver, and a resolver with no extractor behind it would
> refuse every title (an unresolved id refuses, by the matrix). The executable half is
> wired and proven red; the data for both halves is empty by decision. Cost of waiting:
> none — no title is blocked by store id today because none is listed.
>
> Proven red: disarming the matcher makes the guard **allow** a blocked
> executable, and the test asserts `kBlockedExecutable` specifically rather than
> "it refused" — which is indistinguishable from the four refusals the guard
> already makes.

> **Check 4 is now implemented** (`fl_prescan.cpp`, inside `EvaluateImpl`), so
> ~~checks 1, 2, 2b and 4 run. **Check 3 remains unwired** and this item stays
> open on that.~~
>
> > **Superseded by the block above, which sits forty lines higher in this same
> > section.** For part of 2026-08-05 §S14 said both "the executable half is
> > wired" and "check 3 remains unwired", and a reader greping for the status
> > found whichever copy came first. Struck rather than deleted: two statements
> > of one status inside one section is the drift shape this file exists to
> > record, reintroduced inside the recorder. **`EvaluateImpl` runs five checks:
> > 1, 2, 2b, 3 (executable half) and 4.**
>
> The parser now reads both per-title arrays in their real object shape, so the
> data can be written before the wiring lands — it used to read them as bare
> strings, and the first entry ever added would have refused the whole rules
> file (§S17).

The gate is not currently weakened — checks 1, 2, 2b, 3 (exe) and 4 run, and
every family in the seed is caught by a module, driver, service or directory
signal — but a documented check that does nothing will read as "this title is
not a known online title" to the next person who trusts it. That still applies
to the store-id half, and to the executable half for as long as its list ships
empty.

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

### S6 ✅ · The 30 s scan window is the weakest part of the most important behavior — **the in-process half built 2026-09-06 (P1 item 1)**

> **Built, as the deferral below said it would be: one detour, both jobs.** `kernelbase.dll!LoadLibraryExW`
> is patched from `InitThread` after the present hooks; the body calls the original first, skips
> data-file / resource mappings, reads the mapped module's own file name into a stack buffer and compares
> its base name (a) against `FL_HOOK_INVENTORY` + `FL_RUNTIME_CENSUS` — a match bumps a 15-bit wake count
> and signals the watchdog's auto-reset event, so the lazy installers run **within milliseconds** rather
> than on the next 1 Hz tick — and (b) against the compiled floor's MODULE families (prefix, case-
> insensitive, the generated table the guard itself carries; zero allocation) — a match CASes the family's
> 1-based index into `earlyStopFamily`, and the present path (`MayObserve`) or the watchdog, whichever is
> first, calls `StopObserving(FL_STATUS_STOPPED_BLOCKLISTED)`. It supplements the poll and replaces
> nothing; it installs nothing inline (§H2). **Measured**, real Overlay in a real target (ctest `fl_guard`):
> a late `sl.interposer.dll` hooked **94 ms** after it appeared (bound 500 ms, which the tick alone cannot
> meet more than half the time); a decoy copied to `EasyAntiCheat_fl_fixture.dll` and loaded 2.5 s in
> stopped the Overlay with `earlyStopFamily` equal to the index the test computed from the same generated
> table, `unhookRequested` still 0, and `writeIndex` frozen thereafter. The CaptureHost reads both words
> (`Consume/EarlyStop.cs`), ends the session as `WriterStoppedBlocklisted` (exit 5, the safety-stop code)
> and names the family from the staged rules file.
>
> **What it does not do, stated so the disclosure stays honest:** the fragment tier and the signer tier
> are not in the detour, so a module the floor knows only that way waits for the host's 30 s scan; the
> Disclaimer's "within 30 seconds" is unchanged. **Owed:** the real-game leg of §H2 — a title that loads
> D3D12 or Streamline lazily, hooked through the wake rather than the tick — which the next captures
> report via the `loader detour:` line (`woke the watchdog N time(s)`).

Now disclosed honestly in the Disclaimer and README. The open question ~~is~~ was whether
to shrink it.

> **"Already installs" was false, and it changed how this item read.** Corrected
> 2026-08-04. `src/native/FrameLedger.Overlay/` contains two files and
> `dllmain.cpp` exports exactly one function; there is no `MH_CreateHook` outside
> `fl-probe-hookprofile` anywhere in the tree. So §S6 is **not** "the cheapest
> available improvement" — it is downstream of the entire P1 hook layer, and a
> reader was being told a mechanism sat there unused. `17_HOOK_ENGINE` §DLL entry
> words the same thing correctly, as *specification*. The sentence below is
> retained in its original form because the plan it describes is still the plan.
>
> > **Every factual clause in the paragraph above is now false** (2026-08-05,
> > after #40–#44), and it is corrected rather than deleted because the *reasoning*
> > it carried is what the next reader needs. `FrameLedger.Overlay/src/` holds
> > **one** file; `dllmain.cpp` exports **four** functions (`FlGetLayoutVersion`,
> > `FlGetBuildId`, `FlGetStatus`, `FlRequestUnhook`); and there are **three**
> > `MH_CreateHook` calls in it — slots 8, 13 and 22 on the shared `dxgi.dll`
> > class vtable.
> >
> > **What that changes for §S6, stated narrowly.** The premise of the correction
> > — "downstream of the entire P1 hook layer" — is now only partly true: MinHook
> > is initialised and a hook layer exists to hang a `LoadLibrary` hook on, so the
> > *distance* to §S6 is shorter than it was. It does **not** follow that §S6 is
> > now cheap or that it is scheduled. `17_HOOK_ENGINE` §DLL entry still specifies
> > a **deferred** install (§H2), the poll it would supplement still has no
> > production driver (nothing writes `guardTicks`), and no phase owns this item.
> > Its disposition — work, or deferral-with-rationale — is an open owner
> > decision, listed as such in §S24.

The Overlay is specified to install a `LoadLibrary` hook for lazily
loaded graphics DLLs (`17_HOOK_ENGINE` §DLL entry); the same hook could raise the
control-block flag the moment a blocklisted module name loads, turning a 30 s
poll into near-immediate detection. This is the cheapest available improvement to
the behavior the product treats as most critical.

**Needs:** confirmation that a name comparison against a small fixed table is
acceptable in that hook under the no-allocation rule (it should be), and a
decision on whether it supplements or replaces the poll. Supplements — the poll
also catches modules loaded before we hooked.

> #### 🅓 Deferred to P1, rationale written 2026-09-06 (owner: "theo bảng")
>
> The mechanism is a `LoadLibrary` hook inside the Overlay raising the control-block flag
> the moment a blocklisted module name loads, supplementing the 30 s poll. It is specified
> (`17_HOOK_ENGINE` §DLL entry, deferred install per §H2) and it needs the P1 hook layer's
> `LoadLibrary` detour to hang on — the same detour lazily loaded graphics DLLs need — so
> building it in P0 would mean building that detour twice. The cost of waiting is the one
> the Disclaimer already discloses: up to 30 s between a mid-session load and the stop.
> Not closed: the name comparison in that hook must be against the compiled floor
> families and allocation-free, and it supplements the poll rather than replacing it.

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

> **The deferred path is built and exercised in a fixture (2026-09-06, P1 item 1 — §S6).** The detour
> signals an auto-reset event the watchdog waits on with its 1 s timeout; it never calls MinHook.
> ctest `fl_guard` `[loader]` loads `sl.interposer.dll` into a presenting harness 2.3 s after start and
> sees the hook family within 94 ms, wake count ≥ 1, no fault, no stop. **Still owed, unchanged:** the
> real-game leg — a title that pulls Streamline or D3D12 in lazily, hooked through the wake — which the
> `loader detour:` report line will show as a non-zero wake count with the family present from the first
> bucket.

### H5 ✅ · Proxy swapchains defeat the dummy-vtable assumption — **the DLSS / DLSS-G question closed 2026-09-06 (owner)**

> **CLOSED 2026-09-06, 12:17, on Cronos: The New Dawn demo with frame generation ON (`spike-notes` §9).**
> The second Streamline 2.8.0 title, UE: `Native FPS: 86.8 -> Displayed FPS: 172.72 (x1.99 FG)`,
> `unseen=0 over samples=10063`, `frame generation: DlssG — the NVIDIA driver agrees`, `upscaler: Dlss
> (driver-reported)`, RT Yes. **So the pacer that bypasses the patched `Present` bodies is Dying Light:
> The Beast's integration, not SDK 2.8.0's** — 2.8.0 on UE presents its generated frames through the body
> the inline patches cover, exactly as 2.7.x and 2.10.x do. Every measured title now prints the rule-6 trio
> or a counted `none`, on NVIDIA through one of three producers: the hooked bodies (Cyberpunk, Hell Is Us,
> Expedition 33, Wukong, Rune Factory, Onimusha, Cronos), DXGI's own counter where the hook cannot count
> (DL:TB), or a withheld `N/A` only when that counter was never read. Identity comes from the tags (DLSS-G)
> and the hooks, and from the driver's per-process word where no hook can see it (the NGX-direct titles),
> with the negative measured. **What stays unlocalised and is not needed for any number the report
> prints:** which body DL:TB's pacer reaches. The owner closed the item on this reading; the sub-items
> below are its history.

> ### CASE 3 HAS A CANDIDATE TITLE — Dying Light: The Beast, Streamline 2.8.0, DLSS Frame Generation ON, 2026-09-05
>
> **The numbers.** Five 58 s captures at DLSS with DLSS Frame Generation on (the owner's
> setting, confirmed after the runs): `presents = tokens = batches` on every one —
> 4706 / 4258 / 4560 / 4311 / 3441 — so the report printed `FPS: 80.41 / 72.73 / 77.69 /
> 73.64 / 58.87` and **`frame generation: none`**, with `sl.dlss_g.dll` and `nvngx_dlssg.dll`
> (310.6) in the census. The fifth ran on the #116 build, 2026-09-05 00:53. **Owner decision
> the same night: a separate session and a separate PR own this; nothing below is started
> until the discriminator has a reading.**
> **The same title with FSR frame generation on** read presents 8255 against 4129 tokens
> (×2, the loader row, §H11 R6). **Cyberpunk 2077 on Streamline 2.7.1 with the same DLSS-G
> generation (310.8), same GPU,** read presents 13706 against 3439 tokens (×3.99) on 2026-09-03.
>
> **What that means, stated as the two readings the record cannot separate.** Either (a)
> Streamline 2.8's DLSS-G presents its generated frames on a path the class-vtable hook on
> `Present`/`Present1` does not see — case 3 of this entry, realised, with the application
> frames still counted by `slGetNewFrameToken` and the generated ones simply absent — or (b)
> the title was not generating in those runs despite the setting. **The discriminator is the
> owner's counter reading**: an in-game or Steam frame counter near 2× our `FPS:` line is (a);
> one near our line is (b). It is pre-committed here so the next run answers it rather than
> being read into whichever reading is more comfortable.
>
> **Why this is a rule-6 exposure and not a curiosity.** `none` there is computed —
> `presents == application frames` — and under reading (a) that equality holds *because*
> the generated presents are missing, so the report states the absence of frame generation on
> a title that is generating. The consumer cannot tell this from a real off. Until the
> discriminator runs, a `none` on a Streamline 2.8 title with DLSS-G in the census is a
> **count**, not a verdict, and `03_METRICS` §FG has no wording for that case yet. This is the
> §H5 outcome the entry was written to fear, on a newer Streamline than the one that measured
> it not happening; whether the fix is a wording, a census-version gate (the interposer's file
> version is ours to read — it is a module in our process, not game memory), or a second
> present sink is the owner's decision after the reading. **What is NOT in doubt:** the
> Streamline tag row (`slSetTagForFrame`, #115) is unrelated — this is about presents.
>
> ### THE READING, 2026-09-05 — reading (a), and what landed on it
>
> The owner, verbatim: *"Trong game là DLSS X 4, steam đã hiện dlss, frame ghi nhận được
> native/frame gốc chưa frame gen."* — the title is at DLSS ×4 (multi-frame generation), the Steam
> overlay shows DLSS, and the frames this hook records are the native ones, before frame
> generation. That is reading **(a)**: case 3 realised on Streamline 2.8.0. The numeric counter
> value was not part of the reading and is the first thing the session below writes down.
>
> **The record already corroborated it, in a field nobody had read for this purpose.** The RT
> census on the DLSS ×4 runs reads **97.1 % of all presents RT-active**; the same title with FSR
> frame generation ×2 reads **54.6 %** — generated presents carry no ray-tracing work, so at ×2
> half of them are RT-silent. A window in which every present carries RT work is a window that
> holds no generated presents. Both readings of the count predicted that; only the owner's
> reading says which.
>
> **What landed (PR-A1, C# only, nothing on the present path):**
>
> - The capture host takes an **out-of-process module snapshot** beside every guard scan —
>   `Process.Modules`, the loader's list through documented PSAPI, the same read the guard
>   already performs on every target — and the file's version resource on the path it names.
>   The report prints one `module:` line per census-named module (version, path). The census
>   cannot carry a version without a layout change; this can, at no cost in the hook.
> - `MeasuredFacts` **withholds `none`** when `sl.dlss_g.dll` is in the census beside an
>   interposer whose file version is ≥ 2.8.0 or could not be read, and the report prints
>   `Presented FPS` with a qualifier: the number counts APPLICATION frames; whether frames were
>   generated, and the Displayed rate, are unknown. **The key is the Streamline plugin, not
>   `nvngx_dlssg.dll`:** every validated `none` on this machine — Cyberpunk off / ×3 / ×4 at 2.7.1
>   (census `0x5793D`), Expedition 33 and Lies of P with frame generation off — has the NGX
>   module loaded and the plugin bit CLEAR; every DL:TB run (`0x25F0F` / `0x25F4F`) has it SET.
>   The gate leaves each validated result byte-identical, and eight consumer cases pin that.
> - **Cost, pre-committed:** DL:TB at DLSS with frame generation **off** also reads withheld if
>   the plugin is a startup-time load on this title. Leg 0 below measures which.
>
> **The session, pre-committed before it runs.** One owner session, one launch per leg, the
> capture host as built plus **PresentMon 2.5.1 console, elevated, concurrently** (it is retired
> as a shipped rung — §S31 row P2 — and is still the one instrument on this box that counts
> presents from the kernel's side; on Cyberpunk it counted every generated present at the same
> rate as this hook), plus the title's own FPS counter and the Steam overlay counter read by
> eye mid-capture and written down **as numbers**. Read off our report: `FPS:` / `Presented FPS`,
> `FG counts` (`presents=`, `tokens=`, `streams=`), the `module:` lines. Read off PresentMon:
> rows over span, the number of distinct `SwapChainAddress` values, `PresentRuntime` and
> `PresentMode`.
>
> | Leg | Title / setting | What it settles |
> |---|---|---|
> | 0 | DL:TB, DLSS, DLSS FG **off** | whether `sl.dlss_g.dll` is in the census with FG off (per-setting vs startup); ours ≈ PresentMon ≈ counter ≈ 1× |
> | 1 | DL:TB, DLSS, DLSS FG **×4** | the discriminating leg |
> | 2 | Cyberpunk 2077, DLSS, MFG ×4 (Streamline 2.7.1) | control on the same build: ours ≈ PresentMon ≈ 4× tokens, `x3.99 FG` printed, `module: sl.interposer.dll 2.7.1.0`, plugin bit clear |
>
> **Leg 1, the counter half, READ 2026-09-05** (owner's run on #118, `dyinglightbeast-141045`):
> DLSS FG ×4, our line `Presented FPS: 76.7` with `none` withheld exactly as built, the title's own
> counter **~300** — 3.9× ours. Reading (a) now has its number. The same day's `-105050` run read
> 93.24 on the same shape. **Still owed from this table:** the PresentMon leg beside it (which
> decides P1 against P3, i.e. whether the generated presents are kernel-visible at all), Leg 0
> (whether `sl.dlss_g.dll` is a startup load), and Leg 2's control. Also the same day, Cyberpunk
> at DLSS + RR + MFG ×3 read `62.86 → 187.49 (×2.98 FG)`, `frame generation: Active (technology not
> identified)` — the count is right and the technology is unnamed, because `kFeatureDLSS_G` is
> never evaluated through the export this build hooks; and the four UE titles at DLSS read
> `upscaler: N/A` (Lies of P, Hell Is Us, Expedition 33, Wukong: NGX-direct super-resolution, the
> Streamline interposer loaded and idle for it). **These three shapes — hidden presents on SL 2.8,
> an unnamed DLSS-G, and an unnamed NGX-direct DLSS — are one question with one candidate answer
> that is not a hook:** `NvAPI_NGX_GetNGXOverrideState` (NVAPI, MIT, vendored; R570+) takes a
> PROCESS ID and returns the driver's own feedback bits per feature — SR / RR / FG each with
> `DLL_LOADED`, `CREATED`, `EVALUATE`, `FG_MODE`, `FG_MULTI_FRAME` — plus `scalingRatio`,
> `performanceMode` (the quality mode), `renderPreset`, `frameGenerationCount` and
> `frameGenerationMode`. Out of process, from the driver's bookkeeping, whatever route the title
> took to NGX. **Unmeasured, and measured before anything is built on it:** whether the bits are
> populated for a process with no NVIDIA-app override configured (the API's name says "override"),
> and whether `frameGenerationCount` is the title's multiplier or only an override target.
> `fl-probe-nvapi --ngx-state <pid>` prints the raw words and every decoded bit; the owner runs it
> against Lies of P (DLSS, no FG), Hell Is Us (DLSS + FG ×4), Dying Light: The Beast (DLSS FG ×4)
> and Cyberpunk (MFG), each while the title runs — no capture, no injection, no consent needed.
> **The decision it opens is the owner's:** a driver-reported fact is neither a hooked argument
> (rule 4) nor a static hint (`05_DETECTION`); if the probe answers, `03_METRICS` needs a
> *driver-reported* rung with its own wording ("reported by the NVIDIA driver, not counted"), and
> the rule-6 pair on the hidden-presents shape could then read *application frames counted ×
> multiplier reported* — labelled as such, never as a measured Displayed.
>
> **MEASURED 2026-09-06 morning, three titles, driver 616.64 (`spike-notes` §9): the bits ARE populated
> without an override, for super resolution only.**
>
> | Title (override?) | SR feedback | RR feedback | FG feedback | ratio / mode / preset / fgCount |
> |---|---|---|---|---|
> | Hell Is Us (none), DLSS + **DLSS-G ×4 running** | `0x80605` INITIALIZED DLL_EXISTS **CREATED EVALUATE** ERR_NOT_FOUND | INITIALIZED DLL_EXISTS ERR_NOT_FOUND | `0x5` INITIALIZED DLL_EXISTS — **no CREATED / EVALUATE / FG_MODE** | 0 / 0 / 0 / 0 |
> | Expedition 33 (NVIDIA-app override ON), FG off | `0x8067F` … ENABLED DLL_LOADED DLL_SELECTED PRESET PERF_MODE **CREATED EVALUATE** ERR_NOT_FOUND | INITIALIZED DLL_EXISTS ERR_NOT_FOUND | `0x1F` INITIALIZED ENABLED DLL_EXISTS DLL_LOADED DLL_SELECTED | 0 / **2** / **11** / 0 |
> | Lies of P (none), DLSS, no FG | `0x605` INITIALIZED DLL_EXISTS **CREATED EVALUATE** | INITIALIZED ERR_FAILED ERR_NOT_FOUND | INITIALIZED ERR_FAILED ERR_NOT_FOUND | 0 / 0 / 0 / 0 |
>
> Four readings. **(1) `CREATED | EVALUATE` on the SR word is set on both no-override titles** — the
> driver keeps per-process bookkeeping of the NGX super-resolution feature whatever route the title took,
> which is exactly the identity `upscaler: N/A` lacks on the NGX-direct titles (all three print it). **(2)
> The FG word does NOT reflect DLSS-G activity:** Hell Is Us was generating at ×4 through Streamline
> (`83.21 → 332.82 (×4 FG)`, `DlssG`, the same session) and its FG word says INITIALIZED | DLL_EXISTS and
> nothing more — the word tracks the override machinery's DLL, not the plugin's evaluations. Frame
> generation stays with the tags and the count. **(3) `frameGenerationCount` is 0 on every title,
> including the one at ×4 — it is the override target the header names, never the title's multiplier**,
> so the "application frames counted × multiplier reported" pair above has no producer and is dropped.
> **(4) `scalingRatio` is 0 everywhere, override included; `performanceMode` / `renderPreset` are
> populated only under the override (E33: 2 / 11) and are then the OVERRIDE's values, not the title's** —
> so this route gives an identity and nothing about the render size or quality. `ERR_NOT_FOUND` sits
> beside CREATED | EVALUATE on two titles and is absent on the third; the header does not say what it
> refers to (a DRS profile, most likely), and it is not read as anything.
>
> **What a driver-reported rung may therefore claim — the owner's decision, not yet built:** on a
> session where every hook says `upscaler: N/A`, `Dlss (driver-reported: the NVIDIA driver reports an
> NGX super-resolution feature created and evaluated in this process — not counted by this hook)`. It
> may name the vendor and the feature; it may not state a quality, a ratio, a render size, a frame
> generation mode or a multiplier. On a hooked-identity session (Cyberpunk, DL:TB, Onimusha) the word
> is a consistency check the report prints beside the hooked name. **Owed before it is built — the
> negative:** the probe on Lies of P or Hell Is Us with DLSS switched OFF in the menu, expected
> `CREATED` clear; a rung that has only ever seen the positive would be the census's mistake again.
> Plumbing when decided: the probe already builds against `fl_nvapi`; the capture host would call it
> beside each module snapshot (out of process, no consent needed) or, if it goes in-process, through
> `Infrastructure` only (CLAUDE.md: no P/Invoke elsewhere).
>
> **BUILT 2026-09-06, on the owner's decision ("nối probe để bắt đầu").** The capture host spawns
> `fl-probe-nvapi --ngx-state <pid>` beside every module snapshot (the probe prints one machine line,
> `NGXSTATE status=… sr=0x… …`, and is staged beside the host by the project file), merges the readings
> (the last answered word is the state; a change between readings is printed, never averaged) and
> prints `NVIDIA driver NGX state:` in the runtime block. `03_METRICS` §Upscaling carries the rung and
> its limits. **Pre-committed, read off the next run:**
>
> | Row | Title / setting | Expected | Else |
> |---|---|---|---|
> | **N1** | Lies of P, Hell Is Us, Expedition 33 at DLSS | `upscaler: Dlss (driver-reported: …)` | a `CREATED \| EVALUATE` word beside `N/A` is a parser or plumbing defect — localise |
> | **N2** | Cyberpunk, DL:TB, Onimusha (hooked `Dlss`) | `upscaler: Dlss — the NVIDIA driver agrees …` | a disagreement WARNING is printed as such; it is not resolved by preferring either |
> | **N3** | **the negative, still owed:** Lies of P with DLSS switched OFF in the menu | `CREATED` clear → `upscaler: N/A (…) — the NVIDIA driver reports no NGX super-resolution feature created …` | `CREATED \| EVALUATE` still set with DLSS off → **the rung is withdrawn** (the bit is a session-lifetime latch, not a state) and the line goes back to N/A |
> | **N4** | a title at FSR / XeSS with no NGX feature | `Fsr3` / `N/A` with the driver's negative beside it | `CREATED` set on a title with no DLSS in its menu → withdraw |
>
> Lies of P has no DLSS Frame Generation (owner, 2026-09-06), which is why its FG word reads
> `ERR_FAILED ERR_NOT_FOUND`: nothing to load.
>
> **LANDED 2026-09-06, 10:39–11:36 (`spike-notes` §9): N1, N2 and N3 as pre-committed, N4 in a shape the
> row did not foresee, and one correction.** N3 first, because it decides: the standalone probe on Lies of
> P with upscaling OFF read `SR 0x5` — `CREATED` clear — and with DLSS on `0x605`; **the rung stands.** N1:
> Lies of P, Expedition 33, Hell Is Us and the new Cronos: The New Dawn demo all print
> `upscaler: Dlss (driver-reported: …)` where they printed `N/A`. N2: Onimusha and DL:TB print
> `upscaler: Dlss — the NVIDIA driver agrees`. N4 arrived as DL:TB at **FSR upscaling + DLSS Frame
> Generation**: `upscaler: FSR (3.1 or 4 …)`, `2560x1440 -> 2560x1440`, `SR 0x205` — CREATED without
> EVALUATE, so the driver's word correctly does NOT claim DLSS beside FSR (the rung needs both bits).
> **The correction:** the FG word is NOT blind to DLSS-G. It read `CREATED | EVALUATE` on Hell Is Us,
> Onimusha and DL:TB at ×4, and *"the words CHANGED between readings"* on all three — the morning's
> `INITIALIZED | DLL_EXISTS` on Hell Is Us was a first reading taken before the feature came up.
> `FG_MODE` / `FG_MULTI_FRAME` stay clear and `fgCount` is 0 at ×4, so it names the feature and never the
> multiplier: printed beside `frame generation:` as agreement, promoted to `DlssG (driver-reported)` only
> where the count is active and no tag named the technology. Also on this run: Cronos is a **second
> Streamline 2.8.0 title** (UE, FG off, `sl.dlss_g.dll` absent from its census — per-setting there — `FPS
> 105.01`, `none` with DXGI agreeing); its FG-on capture would say whether the 2.8.0 pacer bypass is the
> SDK's or Techland's. And the FSR + DLSS-G session's factor was refused (bucket 8 of 8 at 1.96 against
> 2.85 — the guard working) while the qualifier said *"no evaluation was observed"* beside 4,415 tokens; the
> qualifier now says the factor was counted and refused, and that DXGI counted 8,174 presents this hook
> never saw so the Displayed rate is above the printed one.
>
> **The last row, 12:17 — Cronos with frame generation ON: `86.8 → 172.72 (×1.99 FG)`, `unseen=0`, `DlssG`
> with the driver agreeing.** The 2.8.0 pacer bypass is Techland's, not the SDK's. Closed above.
>
> ### THE STREAMLINE DOCS, READ 2026-09-05 (v2.8.0 and main), AND WHAT THEY CHANGED
>
> The owner asked for the vendor's own guides before any rewrite. Three things in them decide this item.
>
> 1. **DLSS-G presents through the "SL pacer"** (DLSS-G guide §14.1): an asynchronous presentation
>    mechanism, with *"specialized hardware to delay the image after Present() has been called"* and a
>    note that `MsBetweenPresents` in third-party tools *"indicates when the internal Present() call has
>    happened"*. So the generated frames ARE presented by internal `Present()` calls on 2.7.1 — which is
>    what this hook counts on Cyberpunk — and whether 2.8's pacer on Blackwell still reaches the body we
>    patch is exactly what Leg 1's PresentMon half measures. The docs do not say; they say FrameView 16.1
>    is the recommended tool because it reads `MsBetweenDisplayChange`, i.e. the display side.
> 2. **`slDLSSGGetState` is the documented way to get the presented count** (§14.0: `actualFPS = myFPS *
>    state.numFramesActuallyPresented`) — and it is **refused here**, with the reason in `03_METRICS` §FG:
>    the header defines the field as *"number of frames presented since the last `slDLSSGGetState` call"*,
>    so every call RESETS the plugin's counter, and a title that reads it for its own FPS display would
>    read our calls' remainder. It is also marked *"NOT thread safe"*. A measurement that alters the
>    host's measurement is not observing.
> 3. **§5.0 "Tag all required resources": DLSS-G requires the HUD-less and UI buffers to be tagged every
>    frame** through `slSetTag` / `slSetTagForFrame` / `slEvaluateFeature`'s inputs — the three routes this
>    build already hooks and read ONE tag type from. **Built the same day:** every tag's type is recorded
>    (`FlWriterState.slTagCensus`, per route), a HUD-less or UI tag drained by a present marks it
>    `FL_FG_DLSS_G`, and the consumer names `DlssG` beside an active count. Identity, not a count: the
>    count still decides `none`, so a title tagging the inputs with the feature off prints `none` with the
>    inputs noted, and the withheld shape keeps its N/A.
>
> **Pre-committed for the next run, per title:** Cyberpunk at DLSS + MFG → `frame generation: DlssG`,
> tag census `global=[depth, mvec, hudless, ui-color-alpha, …]` (2.7.1 tags globally; the scaling input
> arrives locally). Hell Is Us / Expedition 33 / Wukong at DLSS + FG ×4 → `DlssG` **if** the UE Streamline
> plugin tags through the exports (their `UpscalerParams = 0` says no scaling-input tag arrived, which is
> consistent with NGX-direct super-resolution and says nothing about the DLSS-G inputs); if the census
> reads `global=[-] frame=[-] local=[-]` on those titles, they tag through neither export and the identity
> stays `Active (technology not identified)` honestly. DL:TB at DLSS FG ×4 → the census line says whether
> the title feeds DLSS-G through a route this build sees; the qualifier then says *"FEEDING frame
> generation"* beside the withheld count. Lies of P at DLSS (no Streamline tokens at all) → unchanged:
> the super-resolution identity on NGX-direct titles is what the NVAPI probe above is for.
>
> **LANDED 2026-09-05 evening, on the owner's run on #121 (`spike-notes` §9):** `frame generation: DlssG` on
> Hell Is Us (×4, twice), Expedition 33 (×1.97), Wukong (×4), Rune Factory (×3.98) and Cyberpunk — every
> Streamline title tags the DLSS-G inputs through a route this build hooks (the UE plugin and Cyberpunk 2.7.1
> globally, DL:TB 2.8 through `slSetTagForFrame`), so the identity is measured six of six and *"technology
> not identified"* is gone from all of them. DL:TB's withheld line now says *"FEEDING frame generation"*
> beside the count it cannot make; its census also shows a `scaling-in` tag on the frame route with
> `UpscalerParams` still 0 — the tag carries no size on this title, which closes 7a's last question without
> the layout bump it was thought to need. Cyberpunk's factor was refused on that run (bucket 1 at 3.0 against
> 1.67 overall, `tokens/batch 2.97`): a mixed session, refused as designed rather than averaged. **What is
> left on NVIDIA is one thing: `upscaler: N/A` on the NGX-direct titles** — the probe above.
>
> **BUILT 2026-09-05, late — A1.7 promoted from "not in A1" to the in-process half of Leg 1.** The owner
> read that Streamline ≥ 2.8 "needs `sl.interposer.dll` and `sl.common.dll` loaded before an injection
> succeeds". Checked against the source, not the claim: the interposer's DXGI proxy
> (`source/core/sl.interposer/d3d12/dxgiSwapchain.cpp`) is **byte-for-byte the same file at v2.7.32 and
> v2.8.0** — plugins' before-hooks receive `m_base` and may `skip` the base call, exactly as before — and
> 2.8's `sl.common` gains `resourceTaggingForFrame.cpp` (the `slSetTagForFrame` route #121 already hooks).
> Injection into DL:TB already succeeds (every capture there has `streams=1`, a full `runtime census`, a
> tag census), so "loaded before injection" is a launcher's problem, not this hook's. What 2.8 does NOT
> show is where its closed `sl.dlss_g` pacer presents. So the Overlay now asks DXGI itself: on every hooked
> present it reads `IDXGISwapChain::GetLastPresentCount` on the chain it just saw (rule 4: the same object
> `FindOrAdd` already calls `GetDesc` on) and publishes two words on the 1 Hz watchdog —
> `dxgiPresentSamples` (hooked presents that read it) and `dxgiPresentsUnseen` (presents DXGI counted
> between two hooked ones, beyond the one seen). The report prints
> `DXGI present counter: unseen=M over samples=N (r unseen per hooked present)` and the withheld qualifier
> carries the reading. The harness's own presents are the negative control (`fl_guard` `[fg]`:
> `unseen == 0`, `samples >= drained`); the positive cannot be fixtured — nothing in this repo presents
> through a body the inline patches miss — which is why the rows below are written before the run.
> Cost: one virtual call per hooked present, measured by `hook-harness --probe-frames` (number in the PR
> body); `--probe-cost` is the empty-detour floor and does not include it.
>
> Leg 1's in-process rows, read off the new line on a DL:TB DLSS FG ×4 capture (r = unseen / samples):
>
> | Row | Reading | Meaning | Action |
> |---|---|---|---|
> | **P1-DXGI** | r ≈ 3 (×4) — `unseen ≈ 3 × samples` | the pacer presents on the SAME chain through a body the inline patches do not cover; the generated presents ARE DXGI presents | the next PR promotes `Displayed = hooked + unseen`, labelled **DXGI-counted** (a count DXGI made, not this hook), `presents/batch` from it, and `none` withdraws by count; the pacer's entry is then localised (a second `CDXGISwapChain` class, or a path that bypasses `Present`'s body) at leisure |
> | **P3-DXGI** | r ≈ 0 | DXGI counted nothing this hook did not: the generated frames are displayed below DXGI (driver pacing / flip metering) or on an object this hook never saw | the withheld wording stays; no in-process sink exists; only PresentMon (kernel) and the NVAPI corroboration remain, and if PresentMon also reads 1× the frames never were kernel presents |
> | **P5-DXGI** | 0 < r < 2.5, or r ≠ 0 with `streams > 1` | a mixed path or a second chain | stop and localise; the number is printed, never averaged into a factor |
> | **P2/P4** | r ≈ 0 with ours ≈ 4× tokens, or the count itself moved | as the kernel rows say: not generating, or the title changed | the kernel rows above decide |
>
> **LANDED 2026-09-05 night, on the owner's run on #124 (`spike-notes` §9): row P1-DXGI, twice.** DL:TB at
> DLSS FG ×4 read `unseen=12204 over samples=4136 (2.95)` at 21:56 and `unseen=12741 over samples=4392
> (2.90)` at 22:06 — DXGI counted 3.9× the presents this hook did, on the SAME chain, which is the owner's
> ~300 against ours ~75 from the afternoon, now measured in-process. The two controls read 0.00: Hell Is Us
> (Streamline **2.7.3**, `79.89 → 308.14 (×3.86)` through the patched bodies, 18,207 samples) and the
> owner's new title, the **Onimusha: Way of the Sword demo on Streamline 2.10.3** (`60.04 → 220.33
> (×3.67)`, `DlssG`, `upscaler: Dlss`, 12,914 samples, 0 unseen). So **2.8.0's pacer is the odd one** —
> 2.7.x before it and 2.10.x after it present the generated frames through `Present`'s body — and the
> "`sl.interposer.dll` + `sl.common.dll` must be loaded on ≥ 2.8" lead is closed by 2.10.3, which injects,
> tags on the frame route and counts exactly like 2.7. **The pre-committed action is built:** the record
> carries `dxgiUnseen` (@52, `FL_MEASURED_DXGI_PRESENTS`), the window counts `Displayed = hooked +
> unseen` and prints *"Displayed is DXGI-COUNTED"* on its own line, `presents/batch` takes the same
> numerator, and `none` withdraws on DL:TB by count (`03_METRICS` §Displayed may be DXGI-COUNTED). Expected
> on DL:TB's next capture: the trio at ≈ ×3.9 with the label, no withheld line, `frame generation: DlssG`.
> Which body the 2.8.0 pacer reaches stays unlocalised, as the row said.
>
> **LANDED 2026-09-06 morning, on the owner's run on #125 (`spike-notes` §9): the promotion, and Leg 0.**
> DL:TB at DLSS FG ×4 (07:31): `Native FPS: 70.52 -> Displayed FPS: 282.08 (x4 FG) over 16516 present(s)`,
> *"Displayed is DXGI-COUNTED: 12387 of those present(s)"*, `unseen=12483 over samples=4202 (2.97)`,
> `frame generation: DlssG`, uniform across every bucket, no withheld line — the trio rule 6 asks for, on
> the title that could not print it. **Leg 0, twice (07:45, 07:55): frame generation OFF**, census still
> `0x25F0F` — `sl.dlss_g.dll` is a **startup-time load**, the cost paragraph's fear measured true —
> presents = tokens, `unseen=0 over samples=3659` and `4519`, tag census `0x23600` (depth, mvec,
> scaling-in, scaling-out, other: **no hudless / UI tag with FG off**, so the DLSS-G identity is
> per-setting on this title too), and the report withheld `none` exactly as pre-committed. **The gate is
> narrowed rather than withdrawn:** DXGI's counter is the discriminator it lacked — the 2.8.0 pacer's
> presents are DXGI presents on this chain (P1-DXGI), so a counted 1.0 beside a READ counter with zero
> unseen is `none`, printed with DXGI's agreement; withheld only when the counter was not read. Control:
> Expedition 33 FG off (Streamline 2.7.30) reads `FPS: 85.86`, `none` by count, tag census `0x0` — the UE
> plugin sends no tags at all with FG off.
>
> Leg 1's rows (ours = `presents / span`; PM = PresentMon rows / span; C = the counter; ≈ = within 5 %):
>
> | Row | Reading | Meaning | Action |
> |---|---|---|---|
> | **P1** | PM ≈ 4× ours, C ≈ PM | generated presents reach the kernel on a path **outside** the `CDXGISwapChain::Present/Present1` bodies the Overlay inline-patches — case 3 realised | gate stays; a second present sink is costed (candidates below) and the route is the owner's; PresentMon's `SwapChainAddress` count says whether it is a second chain (2 addresses against our `streams=1`) or the same chain by another entry (1) |
> | **P2** | PM ≈ ours ≈ C ≈ 1× | the title was not generating despite the setting | **withdraw the gate** (keep the `module:` lines); `none` was right; case 3 stays unrealised; the owner checks the setting |
> | **P3** | PM ≈ ours ≈ 1×, C ≈ 4× | frames displayed without a kernel-visible present per frame (driver pacing / flip metering), or the counter reports a target rather than a measurement | keep the wording ("Displayed unknown"); no in-process or kernel sink would help; only a vendor read could corroborate |
> | **P4** | ours ≈ 4× tokens on the rerun | the five earlier runs were not generating, or the title changed — compare the `module:` versions and the exe fingerprint against the 00:53 run | withdraw the gate; localise what changed |
> | **P5** | PM ≈ 2×, or two `SwapChainAddress` values with ours `streams=1` | a second chain or a mixed path | stop and localise before deciding |
> | **P6** | elevation refused / a leg unrun | nothing measured | not a result — P2 is not inferred from silence |
>
> Leg 0's own row: if `sl.dlss_g.dll` is **absent** with FG off, the census bit is per-setting on
> this title and the gate is sharper than its cost paragraph feared (consider dropping the version
> conjunct); if present, the interim cost stands as written. **Landed 2026-09-06: PRESENT, twice** —
> and the cost stood for one morning, because the DXGI counter (above) narrowed the gate the same day.
>
> **Second-present-sink candidates for P1 / P5, one line each, none chosen:** MinHook inline
> patches on the `gdi32.dll` D3DKMT thunks (`D3DKMTPresent`, `D3DKMTPresentMultiPlaneOverlay3`,
> `D3DKMTSubmitPresentToHwQueue`) — in-process, no elevation, argument reads only, but
> semi-documented structs that move between Windows builds and more patched bytes on the most
> anti-cheat-sensitive path; an ETW `Microsoft-Windows-DxgKrnl` consumer in the capture host
> (what PresentMon reads) — exact per-pid kernel count, costs **elevation**, which is §G's Tier-2
> question re-entering; an `IDXGIFactory2::CreateSwapChain*` hook (`17_HOOK_ENGINE` ⏳) — sees a
> second chain at creation only, i.e. launch mode (§S1); in-hook `IDXGISwapChain::GetLastPresentCount`
> sampling — a same-chain localiser only, and a hot-path virtual call with two reserved words
> behind it. NVAPI has no present count outside present barrier; `NvAPI_NGX_GetNGXOverrideState`
> (R570, by PID) reports the driver's DLSS override state and multi-frame target, which is
> corroboration rather than a count — an optional `fl-probe-nvapi --ngx-state <pid>` reading on
> Legs 2 then 1, never a gate input.

> ### NARROWED, NOT CLOSED — five real-title captures, 2026-08-15
>
> **What is now measured.** Cyberpunk 2077, SL 2.7.1, RTX 5080, D3D12, 2560×1440, DLSS
> Balanced, Ray Reconstruction on. Five 40 s captures at four frame-generation settings,
> Overlay payload hash-verified against the just-built DLL before each run:
> **off → `presents/batch` 1.000 · ×2 → 2.000 · ×4 → 4.000** (×4 three times independently).
> Every run: one identified swapchain, one segment, 0 gaps, 0 dropped.
>
> **What that settles.** The presents a multi-frame-generation configuration adds are
> **visible to a hook on `dxgi.dll`'s shared class vtable**. The ratio tracks the configured
> MULTIPLIER, not merely the on/off state — which a two-point sweep could not have shown,
> and which is why §S30's oracle paragraph pre-committed three points. `fg_factor`
> structurally 1.0 on every Streamline title, the outcome this entry exists to fear, does
> **not** occur here.
>
> **What it does NOT settle, and the distinctions are load-bearing:**
>
> - **Case 3 is narrowed, not answered.** Nothing went missing *in aggregate*, but these
>   runs cannot separate "the interposer forwards through the chain we already see" from
>   "the interposer presents on its own chain that our shared-vtable patch also catches".
>   One identified stream in every run is what makes the second reading unlikely rather than
>   excluded.
> - **Case 2 is made WORSE by this result, not resolved by it.** If three of every four
>   presents at ×4 originate in the vendor's swapchain, then ~75% of records carry
>   `syncInterval` / `presentFlags` that **no application call produced** — and `dllmain.cpp`
>   sets `FL_MEASURED_PRESENT_ARGS` on every one of them unconditionally, as "the one thing a
>   DXGI present hook always has". Nothing has compared observed present args against what
>   the title passed. Any consumer of those two fields inherits this.
> - **`presents/batch` is a PROXY and its denominator is unverified.** A batch is "a present
>   that drained a Streamline evaluation". It equals an application frame here only because
>   Ray Reconstruction is evaluated once per application frame on this title, which no
>   independent oracle has confirmed — `fl-baseline-probe` at the three settings and the
>   game's own frame counter are both pre-committed in §S30 and both **unrun**.
> - **Scope.** One title, one SL build, one GPU, D3D12, RR **on**, MFG only. Says nothing
>   about RR-off, about other SL 2.x builds, about SL 1.x titles (refused before any hook is
>   installed), or about XeFG and FSR3-FG.
>
> **A separate finding from the same runs, which belongs to HANDOFF item 3 rather than
> here:** `slEvaluateFeature(kFeatureDLSS_G)` is **never called** — 0 across ~14,000 batches.
> `UNDECODED` is also 0, and that zero is now meaningful rather than merely observed: the
> injected `--hold-presenting-upscaled-unknown` case asserts the bucket reads non-zero for an
> id outside the decoded set, proven red. So a vendored `kFeatureDLSS_G` constant not matching
> the runtime id is excluded, and the honest statement is that the id does not reach the
> hooked export. **Where frame generation IS driven from was not measured** — an absence at
> one export of one module does not locate a mechanism, and the NGX tier
> (`NVSDK_NGX_D3D12_EvaluateFeature`, seven exporting modules) is hooked nowhere.

**Partly answered, and the news is better than expected.** `hook-harness
--probe-proxy` builds a real forwarding `IDXGISwapChain` wrapper — what
`sl.interposer` and ReShade hand the application — and presents through it with
our hook on the **real** vtable. The hook still fires: the proxy forwards via
`real_->Present(...)`, an ordinary virtual dispatch, so patching the real vtable
catches it one layer down. A proxy having its own vtable does *not* by itself
make us miss the present.

> **Case 3 is now probed, bounded, and blocked on a LICENCE rather than on
> hardware** (2026-08-05, `fl-probe-interposer`, `spike-notes.md` §5). Loading a
> real `sl.interposer.dll` — Cyberpunk 2077 and Black Myth: Wukong — into our own
> process shows it forwards to `dxgi.dll` and leaves both the factory and the
> swapchain vtable untouched **until `slInit()` runs**, which the probe proves by
> enumerating its own modules and finding no `sl.*` plugin mapped. So the
> comparison is currently measuring passthrough, and the probe reports
> **inconclusive** rather than a verdict.
>
> Recorded because the first version of that probe did **not**: it printed "the
> vtable is THE SAME — a hook DOES catch Streamline presents" for both titles,
> which would have closed §H5 case 3 on a measurement of nothing. The tell was in
> its own output — an interposing Streamline cannot leave the *factory* vtable
> unwrapped, since wrapping the factory is how it reaches the swapchain.
>
> ~~Reaching the wrapped path needs `slInit`'s `sl::Preferences`, i.e. vendor ABI,
> i.e. the question `legal/THIRD_PARTY_NOTICES.md` answers for Intel IGCL and
> nobody has asked for NVIDIA.~~ **Unblocked by #64 and MEASURED 2026-08-14.**
> Streamline is MIT and `sl::Preferences` is vendored, so `fl-probe-interposer` now
> calls `slInit` → `D3D12CreateDevice` *through the interposer* → `slSetD3DDevice`,
> every entry point resolved with `GetProcAddress`. **The premise holds:** with
> `slInit` returning `eOk`, the interposer hands back a swapchain whose vtable is
> inside `sl.interposer.dll` while `dxgi.dll`'s own route yields `dxgi.dll`'s.
> Reproduced on Alan Wake 2 (SL 2.7.0) and Cyberpunk 2077 (SL 2.7.1), RTX 5080.
> **The risk is also narrower than the entry
> implies:** NGX-direct titles never wrap the swapchain, so the vtable premise is
> only in question for Streamline-shimmed ones.
>
> > **What that does and does not settle, because the difference is the whole
> > metric.** It settles that the game's swapchain is **not** an instance of the
> > class whose shared vtable we patch. It does **not** settle whether we miss the
> > present: §H5's own `--probe-proxy` result stands — a *forwarding* proxy calls
> > `real_->Present(...)`, an ordinary virtual dispatch, caught one layer down.
> > **Different class ≠ missed present.** Deciding which needs presents driven
> > through this proxy with our hook installed. No feature plugin reported loaded in
> > these runs, so the DLSS-G question — whether GENERATED presents reach the same
> > vtable — is untouched.
> >
> > **A second finding, which cost a crash to get.** The Witcher 3 ships
> > `sl.interposer.dll` **1.5.6**, a different API generation: it exports
> > `slGetHooks`, `slIsFeatureEnabled` and `slSetFeatureConstants`, and exports
> > **neither** `slSetD3DDevice` nor `slIsFeatureLoaded`. `slInit` exists in both
> > with a **different `sl::Preferences` layout**, so calling it with the vendored
> > 2.x struct access-violates (`0xC0000005`). The probe now version-guards on the
> > SL2-only exports and skips with a reason.
> >
> > **That generalises past the probe and touches the hook inventory.**
> > `docs/vendor-exports.json` records **one copy per module name**, so its
> > `sl.interposer.dll` is one machine's 2.7.4 and says nothing about a 1.5.6 a title
> > may ship. `hookinventory-check` Pass A would pass `slEvaluateFeature` against
> > such a title — the **name** exists in both generations — while the **signature**
> > differs, so a detour typed with the 2.x `PFun_slEvaluateFeature` would read 1.x
> > arguments. Today's hook reads only `feature`, the first argument, and is
> > probably unharmed. **Item 2b's plan to walk `inputs`/`numInputs` is not**, and
> > needs a version guard of its own before it dereferences anything.
>
> What the probe *did* settle is the premise underneath everything else, and it
> is now ctest `fl_vtable_identity_control`: two independently created swapchains
> share one vtable, and a different interface does not — both directions, so a
> regression in either fails the build.

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

### H11 ◐ · XeFG and FSR3-FG identity — **AMD built 2026-09-04 through the ffx-api leaves, complete 2026-09-06; Intel closed by licence; FSR 3.0 host route built 2026-09-05**

> **2026-09-06: the AMD half is complete on its oracle** — Lies of P at FSR 3.1 + FSR FG on the #129 build
> prints `Fsr3` / `FsrFg` / `59.77 → 119.55 (×2 FG)` / `2560x1440 -> 2560x1440` with `presents/frame=2.00`,
> `frames/upscale-drained=1.00`, `unseen=0` and the NVIDIA driver's word saying nothing was created
> (`spike-notes` §9); the owner called it done. **Intel, re-checked the same day: the licence has not
> changed** (`18_GPU_VENDOR_APIS` §Intel), so nothing in `libxess*.dll` may be hooked or declared, and the
> signature-free thunk stays refused. **What is left in policy for an XeFG session, proposed and not yet
> built — identity BY ELIMINATION, labelled as such:** frame generation is COUNTED by the Streamline token
> (UE titles request it whatever the FG vendor — measured on Rune Factory's compiled-in FSR) or by DXGI's
> counter; DLSS-G is EXCLUDED by the NVIDIA driver's FG word (no feature created) and by the absence of the
> HUD-less / UI tags; FSR-FG through a shipped DLL is EXCLUDED by the ffx census (no FRAMEGENERATION dispatch
> at any hooked module); the frame-generation runtime the census names is `libxess_fg.dll`. The line would
> read `Active (technology not identified) — by elimination among the runtimes loaded: not DLSS-G (the
> NVIDIA driver reports no NGX frame-generation feature), no FSR dispatch reached a hooked module, and the
> frame-generation runtime present is libxess_fg.dll; a frame generator compiled into the executable would
> read the same`. Never `XeFg` as a measured identity; never a render extent. Intel's README says XeSS-FG
> runs on non-Intel GPUs with SM 6.4, so DL:TB / Hell Is Us / Cronos / Expedition 33 with XeSS FG on are
> the sessions that measure it, and the pre-commitment is the last clause: the wording must survive the
> one static title (Rune Factory at FSR FG) reading the same way.
>
> **BUILT 2026-09-06 (owner: "build cả hai"), with the executable-file scan beside it.** `FgModePrinted`
> carries the elimination sentence on every rung-3 line; `ExecutableMarkerScan` reads the exe on disk once
> after the session and the clear-census qualifier says MAY when a frame-generation-capable SDK string is in
> the file. Expected on an XeFG session (DL:TB / Hell Is Us / Cronos / Expedition 33 with XeSS FG on):
> the trio or a Presented line from the token or DXGI count, and `frame generation: Active (technology not
> identified) — by elimination …: not DLSS-G (…), no FSR frame-generation dispatch …; the frame-generation
> runtime(s) loaded: libxess_fg.dll …`. Expected on Rune Factory at FSR FG: the same shape with *"no
> frame-generation runtime module is loaded at all; the executable itself carries ffxFsr3, …"*. Neither
> prints a vendor as measured.
>
> **LANDED 2026-09-06, 13:45–14:02 (`spike-notes` §9).** **Rune Factory at FSR + FSR FG** reads exactly the
> pre-committed line: `30.05 → 60.05 (×2 FG)` from the Streamline token, `unseen=0` (the compiled-in FSR 3
> presents through the patched body), `frame generation: Active (technology not identified) — by elimination
> …: not DLSS-G (…); no FSR frame-generation dispatch …; the frame-generation runtime(s) loaded:
> nvngx_dlssg.dll; the executable itself carries ffxFsr3, ffxFrameInterpolation — …` — 7b at its ceiling, the
> Steam overlay showing FG where the report shows the count. **Hell Is Us at XeSS + XeSS-FG** (the owner:
> the Steam overlay could not show FG there): `Presented FPS: 186.47`, **`tokens=0`** — this plugin version
> requests no Streamline token at XeSS, where it did at DLSS — so no count and no factor; `unseen=0`, so
> XeFG presents its generated frames through the patched body and the 186 IS the Displayed rate; the driver's
> words clear; the file carries `xefgSwapChain ×13`. The FG line read the bare *"N/A (a hook ran …)"* and the
> upscaler N/A named "a vendor this build does not decode" — both now carry their witnesses: the N/A prints
> the same exclusion clauses (*the frame-generation runtime(s) loaded: nvngx_dlssg.dll, libxess_fg.dll, …;
> the executable itself carries xefgSwapChain*), and the upscaler line says *libxess.dll is loaded, and XeSS
> cannot be hooked or declared under Intel's licence … N/A here by policy, not by ignorance* (7c item 4's
> string change, done). **Cronos at XeSS** (no FG option): `FPS: 110.02`, `none` by count with DXGI agreeing
> — this plugin requests the token at XeSS. **The ceiling for XeFG on a title that requests no token:**
> Displayed measured, Native unknown, vendor by elimination — there is no in-policy application-frame
> producer for XeFG (the token is the plugin's choice; the XeFG API cannot be hooked; the ffx census is
> silent by construction), and the report says so rather than inventing one.
>

> **BUILT, 2026-09-04, the AMD half — and the deferral's own rule was kept: header first, decode
> second, never from observation.** `HANDOFF` item 7c. The FidelityFX headers cleared the
> checklist and were vendored at tag **`v2.3.0`** (the tree is MIT by exception, 845 of 845 files;
> `18_GPU_VENDOR_APIS` §AMD carries why 2.3.0 and not 1.1.4), and the Overlay now hooks `ffxDispatch`
> on the three **leaves** — `amd_fidelityfx_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll`,
> `amd_fidelityfx_framegeneration_dx12.dll` — and never on `amd_fidelityfx_loader_dx12.dll`, which
> forwards to the effect DLLs through their own exports (all four modules export only the five
> ffx-api names, so that is the only route there is) and would double every count. What it reads
> is the descriptor's head type and, once matched, `renderSize` and `frameID`; what it publishes is
> identity (`FSR3` from the 1.1.x monolith, `FSR_UNVERSIONED` from the 2.x upscaler DLL that hosts
> FSR 3.1 and FSR 4 behind one type), `renderW/H`, `fgMode = FSR_FG` on a present that drained a
> generated batch, and `fgEvaluations` from PREPARE's `frameID` counted on a new index only. The
> K = 1 / K = 4 fixtures run on both vendor shapes with a forwarding loader decoy in the chain.
>
> **The owner's run, pre-committed before it happens** (one launch per leg; read the
> `ffx dispatch census` line and the trio):
>
> | Row | Leg / reading | Verdict |
> |---|---|---|
> | **A1** | Lies of P, FSR + FG: `upscaler: Fsr3`, `frame generation: FsrFg`, `presents/frame ≈ 2.0`, `frames/upscale-drained ≈ 1.00`, `fg-dispatch/frame ≈ 1.0` | accept |
> | **A2** | Lies of P, FSR, FG off: `presents/frame ≤ 1.05` → `none`, no `FsrFg` record, frames ≈ upscale-drained | accept |
> | **A3** | Hell Is Us, FSR (+ FG if offered): `FSR (3.1 or 4 …)` printed; if Streamline still issues tokens, `fgEvaluations` is tokens and `frames/upscale-drained ≈ 1.00` corroborates across vendors; if tokens are 0 the latch hands the count to AMD — record which | accept, record |
> | **R1** | A title that never prepares (count from UPSCALE dispatches): FG on still reads K, off reads 1.00 | accept, note here |
> | **R2** | `presents/frame ≥ 1.5` with zero `FsrFg` records: the title's `frameGenerationCallback` bypasses the export → `Active (technology not identified)` | honest; follow-up, no code change |
> | **R3** | `presents/frame ≈ 0.5·K` or below 1: a second hook in the chain — check `hooks installed`, the census, whether a loader row crept in | defect, block |
> | **R4** | `frames/upscale-drained ≈ 2.00` with FG off: two upscale contexts per frame, or a doubled count on a title that never prepares | investigate before publishing |
> | **R5** | UE5 with both vendors loaded and tokens ≠ prepares per frame: Streamline wins by the latch; the census line prints the disagreement | record; revisit precedence |
>

> **RUN, 2026-09-04 evening — thirteen captures across nine titles, one launch each (`E:\frameledger-runs\2026-09-04\`).
> Rows A1–A3 accepted; one row the table did not have was found, and it reversed a design decision the same day.**
>
> | Title / setting | Shape | `upscaler` | `frame generation` | Trio / FPS | `frames/upscale-drained` |
> |---|---|---|---|---|---|
> | **Lies of P**, FSR + FG (A1) | monolith | `Fsr3`, render 854×480 | `FsrFg` | `118.98 → 238.02 (×2 FG)` | **1.00**, fg-dispatch/frame 1.00 |
> | **Lies of P**, FSR, FG off (A2) | monolith | `Fsr3` | **`none`** by count (3506 frames / 3504 presents) | `FPS: 59.56` | 1.00 |
> | **Hell Is Us**, FSR + FG (A3) | UE5, two leaves, no loader | `FSR (3.1 or 4 …)`, 1707×961 | `FsrFg` | `85.7 → 171.35 (×2)` | 1.00, fg-dispatch/frame 1.00 |
> | **Hell Is Us**, FSR, FG off | same | same | `none` | `FPS: 104.18` | 1.00 |
> | **Expedition 33**, FSR upscale + **DLSS frame generation ×3** | UE5, two leaves | `FSR (3.1 or 4 …)`, 1707×961 | `Active (technology not identified)` — no FRAMEGENERATION dispatch, no Streamline token; the count came from the UPSCALE dispatches | `67.51 → 200.66 (×2.97 FG)` | 1.00 (fg-dispatch/frame 0.00) |
> | **Expedition 33**, FSR, FG off | same | same | `none` | `FPS: 85.12` | 1.00 |
> | **Cyberpunk 2077**, FSR 3 + FG | FSR 3.0 host DLLs for upscaling, the **1.1.x monolith for frame generation** | `N/A` (upscale is through the FSR 3.0 host DLLs, unhooked on this build; **the host row landed 2026-09-05 — rows B1–B4 below**) — render 1506×847 from PREPARE | `FsrFg` (4261 generated batches) | `72.93 → 145.76 (×2)` | — (no UPSCALE dispatch; count = Streamline tokens 4266) |
> | **Dying Light: The Beast**, FSR + **FSR FG** | loader + two leaves | `N/A` | `N/A` — **zero dispatches at any leaf export** | `Presented 146.54`, WARNING | census: no FSR identity, no FSR_FG |
> | **KCD2**, FSR (×2 runs) | loader + upscaler leaf | `N/A` | `N/A` — zero leaf dispatches | `Presented 58.8`, cannot-include shape (no FG runtime) | — |
> | **Black Myth: Wukong**, FSR | monolith + loader 2.1.0 | `N/A` | `none` (Streamline tokens = presents) — zero leaf dispatches | `FPS: 41.11` | — |
> | **Rune Factory**, **FSR FG** (×2 runs) | **statically linked**: `amd_fidelityfx_dx12.dll` is on disk and NOT in the census | `N/A` | `Active (technology not identified)`, count from Streamline tokens | `59.99 → 119.95 (×2 FG)` | — |
>
> **What it settles.** A1–A3 land exactly as pre-committed, on both vendor shapes the leaf rows
> reach (the 1.1.x monolith; the UE5 pair with no loader), with the two per-frame counts agreeing to
> the frame every time and `none` reached by count on every FG-off leg. **The Expedition 33 FG leg is
> the mixed-vendor case the table did not name:** FSR upscaling with NVIDIA's ×3 frame generation on
> top — no FRAMEGENERATION dispatch, no Streamline token (UE5 drives DLSS-G through NGX), so the
> UPSCALE count carried the application frames and the report said *active, technology not
> identified* at ×2.97 against the title's own ×3. Honest and correct. **Cyberpunk at FSR 3 splits
> across two AMD generations:** the FSR 3.0 host DLLs upscale (the deferred route, so `upscaler: N/A`)
> while frame generation goes through the 1.1.x monolith, which the row sees — `FsrFg` at ×2.
>
> **What it reversed — row R6, which the table lacked: the SDK 2.x LOADER is the game's entry, and
> the leaves stay silent behind it.** Dying Light: The Beast (FSR + FSR FG, which neither Steam's
> counter nor DLSS Swapper could see either), KCD2 (FSR) and Wukong (FSR) produced **zero**
> dispatches at any leaf export while the leaves sat in the census. The morning's argument — five
> identical export tables, so the loader can only reach a leaf through the leaf's export — was
> wrong about the loader: the MIT source shows an *external provider object* and the signed loader
> evidently calls it, not `ffxDispatch`. **So the loader is a row** (the PR after this run), the
> stub forwards through the leaves' direct entry to model what was measured, and the K = 1 control
> with all four modules hooked is what would show a loader that re-entered an export (2.0). Owed:
> the three loader titles again, against **R6-accept** = identity + count as the leaf rows give,
> `frames/upscale-drained ≈ 1.00`; **R6-double** = `presents/frame ≈ 0.5·K` → the loader re-enters
> an export on that version and the leaf rows must yield to it.
>
> **And 7b's title exists after all: Rune Factory: Guardians of Azuma links FSR 3 frame generation
> statically.** The 1.1.x monolith ships beside the executable and is never loaded — the census has
> no AMD module at all — yet FSR FG is on and the Streamline token count still measured ×2.00
> against the title's own setting, so the report printed the trio with *technology not identified*.
> That is better than the ceiling §Scope decisions predicted (Presented FPS + qualifier): on a UE5
> title Streamline's token is there even when the upscaler and FG are AMD's, so the application-frame
> count survives static linking. The identity does not, and cannot.
>
> **ROW R6 RUN, 2026-09-04 night, on #111 (one launch each):**
>
> | Title / setting | `upscaler` | `frame generation` | Trio | `frames/upscale-drained` | Verdict |
> |---|---|---|---|---|---|
> | **Dying Light: The Beast**, FSR + FSR FG | `FSR (3.1 or 4 …)`, 1505×847 | `FsrFg` | `70.49 → 140.9 (×2 FG)` | **1.01** (fg-dispatch/frame 0.99) | **R6-accept** — the loader row carries the title the leaf rows could not, at 1×, no double count |
> | **KCD2**, FSR | `FSR (3.1 or 4 …)`, 1505×847 | `none` by count (3523 / 3523) | `FPS: 59.97` | **1.00** | **R6-accept** |
> | **Black Myth: Wukong**, FSR (×2 runs; **the title has no FSR frame-generation option**) | `N/A` | `none` (Streamline tokens = presents) | `FPS: 39.15 / 90.64` | — (no dispatch at the monolith OR the loader) | **not R6 at all** — Rune Factory's shape, see below |
>
> Two of the three loader titles are closed on the first rerun, and the K = 1 control the fixtures
> run is confirmed by a real title: with all four modules hooked, DL:TB's loader route counts
> once. **Wukong is a different shape**: `amd_fidelityfx_dx12.dll` is *loaded* (it is in the census)
> and *never dispatched to*, with the loader silent too, while FSR upscaling is on and Streamline's
> tokens equal the presents. That is Rune Factory's shape with one difference — the monolith is in
> the process — and the reading that fits every number is **FSR upscaling compiled into the UE
> plugin (static, as Rune Factory's) with the 1.1.x monolith loaded for FSR frame generation and
> idle while FG is off.** ~~The leg that tests it is Wukong with FSR frame generation ON~~ —
> **the owner reports the title offers no FSR frame generation at all**, so the monolith is loaded
> for nothing this hook can see (a plugin that ships it and never dispatches to it), and **Wukong
> joins Rune Factory in §Scope decisions' static row**: FSR upscaling compiled into the UE plugin,
> identity out of reach by name, the application-frame count carried by Streamline's token. Nothing
> is owed on it.
>
> ~~**Still deferred, with the costing written:** the FSR 3.0 host route — `ffx_fsr3_x64.dll!ffxFsr3ContextDispatchUpscale`
> / `ffxFsr3DispatchFrameGeneration`, named exports, no struct decode for identity — needs ten
> host headers from tag `v1.1.4` (`main` dropped `ffx_fsr3.h`), whose closure reaches
> `<mutex>`/`<shared_mutex>` through `ffx_types.h`, for exactly one installed title (Cyberpunk 2077,
> whose default here is DLSS). Its own PR, with Cyberpunk switched to FSR 3 as the oracle.~~
>
> ### THE FSR 3.0 HOST ROUTE IS BUILT, 2026-09-05 — and the costing above named the wrong tag
>
> **`fsr3-v3.0.4`, not `v1.1.4`.** Read off the upstream tags: Cyberpunk's `ffx_fsr3_x64.dll`
> exports twelve `ffxFsr3*` names, which is exactly `fsr3-v3.0.4`'s declaration list; `v1.1.4`
> declares fifteen (adds `ContextGetGpuMemoryUsage`, `ContextDispatchFrameGenerationPrepare`,
> `GetEffectVersion`) and its `FfxFsr3DispatchUpscaleDescription` appends `upscaleSize`, `flags`
> and `frameID` **after `renderSize`** — a detour typed against it would read the wrong bytes off
> the module the title ships. The prefix through `renderSize` is identical across 3.0.3 / 3.0.4 /
> 1.1.4 and is the only field read; `dllmain.cpp` pins its offset to a literal. And **`<mutex>` is
> not in the closure at all** at this tag — `ffx_types.h` includes `<stdint.h>` only; the mutex block
> exists only from 1.1.x. Ten files, no `ffx_message.h`, vendored at
> `src/native/third_party/fidelityfx-fsr3/` (root `LICENSE.txt` = MIT verbatim, inline grants;
> `license-check.ps1` §2e).
>
> **One row, the same family.** `ffx_fsr3_x64.dll!ffxFsr3ContextDispatchUpscale` feeds the ffx-api
> drain word: identity `FSR3` as a fact (the 3.0 host hosts nothing else), `renderSize` as the
> extent, and the UPSCALE count when neither Streamline nor a PREPARE has ever spoken. Published by
> `PublishFfxFamilyIfWhole`, which now waits for a loaded host as it waits for a loaded leaf.
> `ffxFsr3DispatchFrameGeneration` is **not** a row: on the one title the monolith generates
> (4261 batches) and the count is Streamline's, and the host's FG export fires per *generated*
> batch. **Reversal, pre-committed:** it becomes a row only on a title reading
> `Active (technology not identified)` with `FL_CENSUS_FFX_FSR3` set and no monolith in the census.
> Fixtures: the `ffx_fsr3_x64.dll` stub (Pass C's second positive control), harness topologies
> `fsr3host` and `fsr3host+mono`, and a `[ffx]` injected case at K = 1 (the double-count control
> for the row) and K ∈ {1, 4} beside the monolith.
>
> **Acceptance, pre-committed — Cyberpunk 2077 (runbook title 3), one launch per leg:**
>
> | Row | Leg / reading | Verdict |
> |---|---|---|
> | **B1** | FSR 3 + FSR FG: `upscaler: Fsr3`; `render 1506x847 -> 2560x1440` with **one** distinct extent (the host's `renderSize` and the monolith's PREPARE agree); `frame generation: FsrFg`; `≈73 → ≈146 (×2 FG)` from tokens; `frames/upscale-drained = 1.00`, `fg-dispatch/frame = 1.00`; `module: ffx_fsr3_x64.dll (no version resource)` | accept |
> | **B1-off** | FSR 3, FG off: `Fsr3`, `none` by count, `frames/upscale-drained = 1.00` | accept |
>
> **B1 and B1-off LANDED, 2026-09-05 (owner's run on #119, `cyberpunk2077-105954` and `-135728`):**
> FSR 3 + FG → `upscaler: Fsr3`, `render 1506x847 upscaler=Fsr3 on 4332 record(s)` (one tuple),
> `frame generation: FsrFg`, `74.31 → 148.56 (×2 FG)`, `frames/upscale-drained = 1.01`,
> `fg-dispatch/frame = 1.00`, `module: ffx_fsr3_x64.dll (no version resource)`; FSR 3 with FG off →
> `Fsr3`, `none` by count (4919 = 4919), `frames/upscale-drained = 1.00`,
> `render -> output: 1506x847 -> 2560x1440 = 1.7x (59% render scale)`. B2–B4 did not fire. **One
> defect the run exposed, in the consumer:** the FG-on leg printed `render -> output: N/A` two lines
> under its own raw block — `UpscaleExtent` keyed its window on the params bit, which under frame
> generation rides on one present in N, so `ClaimedSuffixStart`'s clean-suffix rule found no
> window; it now keys on the identity bit (per-record once live) and counts every record inside
> that carries both sizes. Every FG-on capture since 2026-08-16 had been reading `N/A` there; the
> earlier "not computed yet" wording had hidden it.
> | **B2** | `upscale-drained = 0` with `ffx_fsr3_x64.dll` in the census and FSR on — the game calls `ffx_fsr3upscaler_x64.dll` directly and the facade is silent | add the upscaler-DLL row (`ffxFsr3UpscalerContextDispatch`, ten `FfxResource`s, its own offset pin) **instead** — never both |
> | **B3** | `frames/upscale-drained ≈ 2.00` | two upscale contexts per frame or a re-entered dispatch — investigate before publishing (mirrors R4) |
> | **B4** | `SETTINGS MOVED: 2 distinct extents` | the host and the PREPARE disagree on the input size; print both, do not average |
>
> *The banner as it stood on the morning of 2026-09-04:* **The deferral has a next step, and it is `HANDOFF` item 7c.** The Streamline
> work built every tool this needs — module-scoped inventory rows, types-only vendoring with a
> consumer in the same commit, `hookinventory-check` Pass A–D, the K = 1 / K = 4 harness pair,
> and a per-frame count validated against a title's own menu — and Lies of P (FSR 3.1 FG on,
> `amd_fidelityfx_dx12.dll` alone) is the title that shows what this deferral costs today:
> `upscaler: N/A`, `frame generation: N/A`, a WARNING, no factor. The first move is the one this
> entry names and nobody had made: **run the licence checklist on the FidelityFX and XeSS SDK
> headers.** ~~Both are published under MIT~~ — **run 2026-09-04, and the sentence struck was
> wrong about Intel.** FidelityFX at tag `v1.1.4` is MIT root-and-inline and is clear to vendor
> types-only (as is `fsr3-v3.0.4`, the tag the host route actually vendored on 2026-09-05); the XeSS SDK is the Intel Simplified Software License (binary-only grant,
> no-reverse-engineering, termination) and its headers say they may not be copied without
> Intel's permission, so step 3 binds and XeSS stays where NGX is. `18_GPU_VENDOR_APIS` §Vendor
> SDKs carries both decisions with the evidence. **So 7c is AMD only.** The one thing that
> looked left for Intel — a signature-free call counter on a named export — was refused the
> same day (§Scope decisions, the Intel row): it still patches Intel's code in the game process.

HANDOFF item 3 asks for this to be *deferred with a written rationale rather than
guessed*, and this is that rationale. The deferral is the decision; it is not a
placeholder.

**What is measured** (`docs/vendor-exports.json`, dev machine, 2026-08-05):

| Module | Exports that would identify frame generation |
|---|---|
| `libxess_fg.dll` | `xefgSwapChainD3D12InitFromSwapChain`, `…GetSwapChainPtr`, `…SetEnabled` — 28 of its 31 exports are `xefgSwapChain*` |
| `ffx_fsr3_x64.dll` | `ffxFsr3SkipPresent` |
| **`amd_fidelityfx_framegeneration_dx12.dll` (3.1.5, in three installed titles)** | **five generic `ffx*` entry points and nothing else** |

**Why the third row is the blocker.** On the newer AMD module the symbol name carries
no identity at all: which *feature* a call configures is decided by a **field inside a
struct passed as an argument**, and we have no headers for that struct. Reading it
would mean hand-declaring a vendor ABI from observation — which is exactly what
`#71` proved fatal in the Streamline case, where a module kept the symbol NAME and
changed the SIGNATURE and a wrong argument list yields *a wrong answer rather than a
crash*, the failure class `17_HOOK_ENGINE` calls the highest false-confidence risk in
the spike. A guessed struct layout is the same defect with a different vendor's name
on it.

**Whether the headers may be vendored is not the obstacle** — `18_GPU_VENDOR_APIS`
§Checklist has to rule on FidelityFX and XeSS-FG licences before either could be
consumed, and nobody has run that checklist for them. So there are two gates here,
and the licence one has not even been opened.

**What the deferral costs, stated so it is not rediscovered as a surprise.** An XeFG
or FSR3-FG title reports `fgMode = FL_FG_UNKNOWN` and `FL_MEASURED_FG` set — *"a hook
ran and could not identify what it saw"*, which is the honest state and is already
what `dllmain.cpp` publishes. It does **not** report `NONE`, so nothing is aggregated
as a negative. The cost is coverage, not correctness.

**What would lift it**, in order: run `18_GPU_VENDOR_APIS` §Checklist on the FidelityFX
and XeSS-FG SDK licences; if either clears, vendor the header **with its consumer in
the same commit** (the rule §2b's `sl_dlss.h` landed under); and only then decode the
struct — never before, and never from observation.

**Related and NOT the same question.** Both vendors also take over the present
(§H5 case 3, and item 3's own entry), which is a separate hazard: identity is what
this item defers, and whether generated presents reach our vtable is what §H5 tracks.

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
| M1 ✅ | ~~Can PresentMon 2.x `FrameType` see **driver-level** frame generation (AMD AFMF)?~~ | **Closed by DECISION 2026-08-27, not by measurement, and the distinction matters.** PresentMon is dropped, so this question has no subject. What was learned before it went: `FrameType` reports events a vendor chose to emit, and NVIDIA's DLSS-G emits none (§S31 row P2) — so the "capability inversion" this row hoped for was never demonstrated for any vendor. If a Tier-2 mechanism is ever chosen (§G), ask this of **that** tool rather than reviving this row |
| M2 ✅ | ~~Does the pinned console binary still exist, run unelevated, and emit the 2.x column set?~~ | **Answered, then closed 2026-08-27.** It exists (2.5.1, 956,768 bytes, SHA256 `9BEC…A191`, **no VERSIONINFO at all**); it does **not** run unelevated (exit 6 — the account is in neither Administrators nor Performance Log Users, and the running `PresentMonSharedService` does not help because the console starts its own session); and the 2.x column set **is measured** — 24 columns, `spike-notes` §11. The parser ate three real CSVs and resolved `FrameType` correctly against a first column literally named `Application`. Closed because the binary is dropped, and kept in full because every one of those facts cost a measurement |
| M5 ✅ | **Do LHM GPU sensors work unelevated, without PawnIO?** **YES — measured 2026-09-03, row R1 of the pre-committed table below**, NVIDIA RTX 5080 / 616.56 / LHM 0.9.6: eight fields unelevated (core and memory temperature, load, VRAM used, core and memory clock, package power, fan), the identical eight in the elevated control, PawnIO never opened. The three user-facing sentences stand. Numbers in `spike-notes` §10. *(The row as it stood while open follows, unchanged.)* This decides whether the default unelevated Agent has temperatures at all, and therefore how ADR-9 reads to users. **SEVERITY PROMOTED 2026-08-28, and the promotion is the point.** It used to decide how a design note read. It now decides whether a **user-facing claim is true**: the two-rung ladder says a Tier-2 session records duration and hardware telemetry, and that sentence is heading for `README`, the consent dialog and `legal/EULA.md`. If LHM needs elevation for GPU sensors, then the DEFAULT Agent's Tier-2 session is **duration only** and every one of those documents is wrong. Until it is measured, all three are worded *"whatever hardware telemetry this machine can provide"* — honest under either outcome, at the cost of vagueness. `18_GPU_VENDOR_APIS` §P0(b) already specifies the test and the dev box can run it |
| M6 ✅ | ~~Elevation-free Tier 2~~ | **Closed by decision 2026-08-27.** Half was answered on 2026-08-20 in the direction nobody expected: `PresentMonSharedService` is installed and `Running` as LocalSystem and the console **still** fails, because it starts its own trace session. The other half — whether `Performance Log Users` suffices without admin — stays unmeasured and now has no subject. **The elevation question does not disappear with the tool**: ETW trace sessions are restricted by Windows, not by PresentMon, so whatever §G chooses inherits it. Re-ask it there |
| M7 | `18_GPU_VENDOR_APIS` §Runtime policy says telemetry is never read from the game process, but `17_HOOK_ENGINE` reads per-process VRAM and Reflex latency there. Reconcile the wording — the rule means "no vendor SDK polling loops in the game", not "no measurement in the game" |
| M8 | The `GpuSample` type has no latency field, yet L3 is credited with Reflex/PC latency. Latency is per-frame and arrives via the ring, not the 1 Hz sample. Fix the layering description |
| M9 ✅ | **Closed 2026-08-02 by a decision.** The old file/module detection does not exist in this repository and the owner confirmed there is no copy elsewhere, so **"build a minimal static-hint detector as the measurement baseline" is now P0 item 3** (`15_ROADMAP`). It needs no guard and no injection. Until it exists, ADR-7's headline claim stays out of the README rather than being asserted unmeasured |
| M10 | PDH `\GPU Engine(*)\Utilization Percentage` summed across engines does not reproduce the Task Manager figure the doc invokes. Decide what we actually report and label it accordingly |

#### §M5 — the decision table, pre-committed 2026-09-03 BEFORE the first run

The §S30 / §S31 discipline: write down what each outcome means before the instrument
produces one, so the result cannot be read into the conclusion that is most convenient.
The instrument is `FrameLedger.CaptureHost probe-lhm`, run **unelevated first**, then once
elevated as a control — the control exists to separate *"needs elevation"* from *"this
driver / this LHM version has no such sensor on an RTX 5080"*, which read identically from
the unelevated run alone. The five fields the row is decided on are the ones `README`'s
Tier-2 sentence would name if it named any: **core temperature, load, power, adapter VRAM
used, core clock.**

| Row | Unelevated, no PawnIO, `Computer { IsGpuEnabled }` only | Consequence |
|---|---|---|
| **R1** | All five fields non-null | The three documents stand and may name the fields instead of saying *"whatever"*. `18_GPU_VENDOR_APIS` L2 row → ✓. §M5 ✅ |
| **R2** | A non-empty subset non-null | The three documents name exactly that subset. If the consent sentence changes, `OperatorDisclosure.Version` /2 → /3. §M5 ✅ with the subset recorded |
| **R3** | LHM enumerates a GPU and every sensor reads null | The default unelevated Agent's Tier-2 session is **duration + reason only**. `README:67`, `legal/EULA.md:15` and `OperatorDisclosure.cs` narrow accordingly, /3. §M5 🔴 until those edits land in the same PR |
| **R4** | `Open()` / `Accept()` throws, or a poll never returns | As R3, and the *"throws or hangs twice ⇒ disabled for the session"* rule (`18_GPU_VENDOR_APIS` §Runtime policy) is exercised by a real fault rather than a fixture. Record separately whether the failure names PawnIO, because that would mean the GPU path is not user-mode after all |

Two things this table does NOT decide, stated so the run is not read as deciding them:
which of the fields AMD or Intel expose (§R5/§R6, untestable here), and whether the CPU
and board sensors need PawnIO (they do, by LHM's own design — `--cpu` is deliberately not
an option of the probe, so the run cannot accidentally measure that instead).

**RESULT, 2026-09-03 — R1, both runs.** Unelevated: all five deciding fields plus memory
temperature, memory clock and fan; through the port, `Capabilities` reads the same eight
and `faults=0`. Elevated control, launched through UAC: the same eight fields, the same
verdict, values within idle drift. Hotspot temperature is the one field this vendor did
**not** expose — there is no `GPU Hot Spot` sensor in the tree at all, which is a property
of this driver / GPU / library combination and not of elevation. One defect the raw tree
caught before the verdict: the first draft of `SensorMap` mapped `D3D Shared Memory Used`
(system RAM) to adapter VRAM by a fragment rule; fixed and pinned by a test in the same
PR, which is what printing the raw tree ahead of the interpretation is for.
Consequences taken: the three sentences stand (`README`'s row may now name the fields and
does), `OperatorDisclosure.Version` stays /2, `18_GPU_VENDOR_APIS` L2 NVIDIA column filled,
AMD / Intel untouched.

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
| **Tier-2 mechanism** *(was: PresentMon distribution)* | **By what mechanism does a shipped build reach a no-injection MEASUREMENT?** Nothing names one. Since 2026-08-28 the ladder is two rungs and Tier 2 is explicitly not a measurement — duration, sensors and the reason — so this row is no longer about degraded fidelity but about whether that capability returns at all. **It now owns TWO casualties, not one:** the frame-generation oracle §S31 spent four attempts looking for, and `14_TESTING`'s Tier-1 accuracy cross-check, struck the same day because it required a second instrument measuring the same frames. **One mechanism would restore both**, which is the strongest argument for costing it. Constraints on any answer: `README`'s capture-tier table and its **safety** row *"always a way out"*, `legal/DISCLAIMER.md` §73 and `legal/EULA.md` §33; ETW needs elevation whatever the tool; and `15_ROADMAP` has no v2 fallback either | `README`, `legal/DISCLAIMER.md`, `legal/EULA.md`, `04_CAPTURE`, `14_TESTING`, `15_ROADMAP` |

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

   > #### 🅓 §R5 and §R6 deferred 2026-08-05, by owner decision, with this rationale
   >
   > **The AMD/Intel half of the capability matrix and the AFMF question are not
   > deferred for cost — they are unreachable on the only machine that exists.**
   > `spike-notes.md` §Environment records it: one adapter, an RTX 5080, and a `KF`
   > CPU with no iGPU. There is no measurement to take. Deferring is the only
   > honest option; the alternative is a matrix filled from documentation, which is
   > exactly what §M3/§M4 were run first to avoid.
   >
   > **The NVIDIA half is not deferred** and remains P0 item 8: L1 (DXGI + PDH),
   > L2 (LibreHardwareMonitor — including §M5, whether GPU sensors work unelevated
   > without PawnIO, which decides whether the default Agent has temperatures at
   > all) and L3 (NVAPI). None of it exists in code today.
   >
   > **What the deferral costs.** The matrix drives what the UI advertises as
   > available, so on AMD and Intel hardware v1 ships advertising capabilities
   > nobody has verified. The mitigation is the marker, not a guess: **`untested`,
   > never `?`, and never a checkmark inferred from a vendor's documentation.**
   >
   > **And the matrix as written cannot record this deferral**, which is its own
   > defect and is why the note lives here as well as there: its columns are
   > `L1 | L2 | L3` with no vendor axis, so "measured on NVIDIA, untested on
   > AMD/Intel" is not expressible in it. Whoever fills it for item 8 has to add
   > that axis first, or the distinction this deferral rests on is lost in the
   > artifact that consumes it.
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
    | `12_BUILD:136`, `13_CI_CD:11` | the managed **struct-mirror** test | ~~Absent. Nothing under `tests/` references `FlFrameRecord`.~~ **✅ Built in #47** — `ShmLayout.cs` + `ShmLayoutMirrorTests`, driven by `fl-layout-dump`'s JSON rather than a transcribed table, asserting blittability as well as offsets, and walking the field list in both directions. `build.ps1`'s gate now reads the run's `.trx` and fails when the class did not execute, so **deleting the test is red too** — it used to `Test-Path` a source file |
    | `13_CI_CD:21`, `12_BUILD:121`, `CHANGELOG:9` | `.github/workflows/release.yml` | Absent. `CHANGELOG`'s header instructs an author to write for a consumer that does not exist |
    | `12_BUILD` §Local quality gate | gates omitted from the list | ~~**Three documents give three different counts, which is the finding.** `build.ps1` has **15** `Write-Step` calls as of 2026-08-05 (14 before this PR's `changelog-check`); `12_BUILD` lists 10; this row said 13.~~ **Corrected 2026-08-06: `build.ps1` has 17 `Write-Step` calls** (`package-closure` added two), and `12_BUILD`'s list is rewritten to 16 numbered entries — the difference is that `Invoke-Native`'s three steps run under one heading there. `13_CI_CD` no longer restates the list at all; it points at `12_BUILD`, because restating it in two places is what let one of them rot. **Nothing still derives the list from the script**, so the drift is slowed rather than stopped, and this row remains open for that reason. **And it drifted again, exactly as predicted — re-counted 2026-08-28.** `hookinventory-check` had reached `build.ps1` and never reached the list; `frametype-oracle` had reached both and then been deleted with PresentMon, so the two errors happened to cancel and the *totals* matched while the *membership* did not — the same coincidence §S24's own recount was caught by. The list is rebuilt to **17 entries against 17 `Write-Step` calls, in script order** (re-deriving the order caught a second defect: two adjacent entries had been transposed relative to the script). **The row still does not close.** Aligning by hand is what was done in 2026-08-06 and again here; it lasts until the next gate lands. The fix is a gate that reads the `Write-Step` names out of `build.ps1` and fails when the list disagrees — until it exists, re-count rather than trusting the number |

    The struct-mirror row is the one that matters. `CLAUDE.md` §Struct mirroring
    makes that test the mechanism protecting the shared-memory ABI, and a doc that
    says a gate runs is worse than a doc that says it is missing — the second
    prompts someone to write it. **`build.ps1`'s skip-loudly discipline is the
    right answer here**: a gate that is not written should be named and skipped,
    not omitted.

    ~~Not urgent — the ring is P1 and there is nothing to mirror yet.~~ **That
    stopped being true on 2026-08-05.** `fl_shm.h` defines four structs with
    39 pinned offsets, `fl_ring.h` implements the protocol, and the Overlay
    writes real records into a real mapping — so there is now a live ABI with
    **nothing binding it to a managed reader**, which is the state the gate
    exists to prevent rather than the state that made it unnecessary. The count
    also grew: `build.ps1:324-329` enumerates **nine** files describing the
    mechanism in the present tense, and the two strongest sites are `fl_shm.h`
    (normative) and `fl-layout-dump/CMakeLists.txt` (a build file, which reads as
    a wiring fact rather than as prose). All nine were marked pending on
    2026-08-05; §S24 schedules the mirror itself.

    Recorded so it is found by reading, not by trusting.
