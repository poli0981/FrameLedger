# 15 — Roadmap

The hook rewrite front-loads risk: almost everything uncertain is in P0/P1. That is deliberate — a hook layer that doesn't work is not a feature you discover in week four.

> **Read `docs/20_OPEN_QUESTIONS.md` alongside this file.** It holds the audit
> findings that P0 exists to answer. The §R resequencing proposals are now folded
> into the order below rather than sitting as a proposal: the guard is item 0,
> the Vulkan passthrough test is item 1, and the two licence checks — which
> needed no hardware and could each have invalidated an entire telemetry layer —
> were run first and came back clear.

## P0 — Spike · *gates everything*

Findings written to `docs/spike-notes.md`. Nothing in P1 starts until the exit criteria pass.

> **Status as of 2026-08-20.** Items **2, 3, 5 and 6 done**; **4 partly**; 0 and 1 partly.
> Written 2026-08-27, because the block below is dated 2026-08-06 and eleven PRs landed
> after it — the upscaler identity and params hooks, the frame-generation counter, and both
> ray-tracing hooks — while this file said none of them had. **What moved:** item 6 is done
> and item 4 is no longer untouched. **What did not:** item 7 is blocked on §S31, item 8 has
> no code at all, and exit criterion 1 still reads **3 of 5** at its best (`spike-notes` §8).
>
> The 08-06 block below is kept and is now wrong in one specific clause — it says the writer
> *"records `measuredMask = FL_MEASURED_OUTPUT_RES | FL_MEASURED_PRESENT_ARGS` and nothing
> else"*. It records upscaler identity, upscaler params, frame-generation counts and ray
> tracing as well. Struck in place rather than deleted, because *how* this file went stale is
> the thing it keeps having to record about itself.
>
> **Status as of 2026-08-06.** Items **2, 3 and 5 done**; 0 and 1 partly. **The
> capture path that P2 owns on paper now exists, unshipped**, and is what makes the
> remaining P0 work purely about hooks: `FrameLedger.CaptureHost` drives
> `HookedCaptureGate` → `FlGuardedInject` → `ShmRingReader` → drain with
> `GuardSupervisor` beside it, so `guardTicks` advances from a non-test binary for
> the first time. It moves **no roadmap item** — items 4, 6 and 7 still need feature
> hooks, and the writer still measures nothing but output resolution and its own
> present arguments — which is why this line still reads the same below.
>
> **Status as of 2026-08-05.** Items **2, 3 and 5 done**; 0 and 1 partly. The
> safety work that had to precede the first injection — the guard, its matrix,
> the chokepoint, the layer's gates — is in, and the first real injection has
> happened. Everything still open needs either **feature hooks that do not exist
> yet** (4, 6, 7 — a throwaway build, per the exit criteria, not all of P1),
> absent hardware (8, and the AMD/Intel half of the capability matrix), or is P1
> by construction (the layer's presentation hooks). Item 0's residual is check 3's
> **store-id half** — the executable half was wired in #52, and that half is blocked
> by design rather than pending (§S14).
>
> **The Overlay stopped being a scaffold on 2026-08-05, and this file was the last
> to hear about it.** Five PRs (#40–#44) landed the SPSC ring, the mapping and
> handshake, the DXGI present hook, the safety stop and supervision loss, and
> per-swapchain `api` resolution — and changed **8 files, all under `src/native/`,
> with no documentation edit anywhere.** For a day this section, `20_OPEN_QUESTIONS`
> §S6, `spike-notes.md` §8 and `legal/DISCLAIMER.md` all described a DLL with no
> hooks. Recorded rather than quietly corrected, because it is the same failure the
> 2026-08-04 handoff audit found and the reason a stale ledger makes the next
> session over-scope.
>
> > **Then #46–#52 landed the same day and this file was last to hear again** — the
> > watchdog thread and both runtime stops (#46), the C# struct mirror and a
> > `.trx`-backed gate (#47), the occlusion-probe filter (#48), `FlGuardBuildId` and
> > the handshake validator (#49), `ShmRingReader` (#50), the closed write-read
> > integration test and CI's `-SkipIntegration` (#51), and check 3's call site
> > (#52). Seven more PRs, no `CHANGELOG.md` entry for any of them and no line here.
> > Corrected 2026-08-05; `ci.yml` now fails a pull request that touches `src/`
> > without touching the changelog, because the prose asking people to remember is
> > what had already been tried and had already failed once.
>
> **What that does and does not move.** It does *not* advance items 4, 6 or 7: the
> Overlay records `qpc`, `frameIndex`, `presentFlags`, `syncInterval`, `api`,
> `swapchainId` and `outputW/H`, and sets `measuredMask = FL_MEASURED_OUTPUT_RES`
> with `rtFlags = FL_RT_NOT_MEASURED` on every record — i.e. it says in the data
> that it has measured no upscaler, no frame generation and no ray tracing. What it
> moves is the *prerequisite*: there is now a writer for those fields to be written
> by. ~~**There is still no reader** — `src/FrameLedger.Shared` holds a `.csproj` and
> no `.cs` files, and no managed code maps the shared memory — so the present hook's
> output is observable only from inside `guard_test.cpp`.~~
>
> > **False since #47/#49/#50/#51, struck rather than deleted.** `ShmLayout.cs`
> > mirrors all four structs against `fl-layout-dump`'s JSON, `ShmHandshakeValidator`
> > is the refuse-to-attach comparison, `ShmRingReader` maps the section and drains
> > it with gap and drop accounting, and `ShmDrainIntegrationTests` closes the whole
> > write-read loop against `hook-harness` — real guard, real injection, real
> > Overlay, real reader. **Two qualifications that matter for planning:** that test
> > is `Category=Integration` and CI runs `-SkipIntegration` (§S19(b)), so the loop is
> > proven on a dev box and not in the merge gate; and ~~the reader has **no production
> > caller**, so nothing drives it outside the suite~~. The item-4/6/7 status in the
> > paragraph above is, by contrast, still accurate.
>
> > **The reader has a production caller as of 2026-08-06**, and it is deliberately not
> > in a shipped binary. `FrameLedger.CaptureHost` drives `HookedCaptureGate` →
> > `FlGuardedInject` → `ShmRingReader.TryAttach` → a 10 Hz drain with
> > `GuardSupervisor.ScanOnceAsync` and `PublishGuardResult` beside it — **the first
> > production advance of `FlControlBlock.guardTicks`**, which is the sending half of the
> > 30 s re-scan `19_SAFETY` calls the most important runtime behaviour in the capture
> > layer and `README` already promises users. Both ends of that field had existed and
> > been tested since #46/#50; only the loop was missing, and a missing loop reads as a
> > missing subsystem.
> >
> > It changes nothing about items 4, 6 or 7: the writer still records
> > `measuredMask = FL_MEASURED_OUTPUT_RES | FL_MEASURED_PRESENT_ARGS` and nothing else,
> > and the consumer built on top of it reports `N/A` for upscaler, frame generation and
> > ray tracing because that is what the data says. What it moves is the **exit
> > criterion's other half**: there is now a path from a consent record to a drained
> > session, so the throwaway build criterion 1 asks for needs feature hooks and nothing
> > else.
> >
> > `12_BUILD` publishes `FrameLedger.App` and `FrameLedger.Agent` and neither references
> > it, which `tools/package-closure-check.ps1` now enforces rather than assumes — §S27
> > is closed on exactly that basis.
>
> Work that P1 owns on paper and that landed here: the ring writer, the present
> hook and the fault policy. The unhook path is partial (`MH_DisableHook`, not the
> compare-and-restore body §H7 specifies).
>
> **Three safety items moved 2026-08-04.**
>
> - **§S18 ✅** — the guard no longer refuses itself, so launch mode is no longer
>   blocked by it. Measured on three real titles.
> - **§S21 ✅** — a **local override of the hard gate**: the rules path came from
>   an inherited `LOCALAPPDATA` and the completeness check never read the values,
>   so a crafted twelve-line file made the guard allow everything. Its first fix
>   shipped too narrow — the compiled-in floor held 4 of the seed's 22 values —
>   and was rebuilt the same day as a table **generated** from the shipped
>   blocklist. That also closed §S19(d)'s runtime half.
> - **§S20 ◐, seed half done** — the Agent now installs the rules file. Until
>   then the guard answered `RulesUnreadable` on any machine that had not
>   hand-installed one, which is what the first real injection hit. **The feed
>   half is open**, so FR-7.3's independent anti-cheat schedule is still unmet and
>   a rules edit reaches no installed machine until a release.
>
> **Be precise about what unblocking §S18 does and does not buy**, because the
> §S18 entry oversold it and this line used to repeat that. It removes a
> *blocker*; it delivers no capability. Vulkan Tier 1 additionally needs
> `vkQueuePresentKHR`, which is P1 and not started — the layer would now load and
> observe nothing. Launch-mode *injection* additionally needs §S1 and §S13(c),
> which are open owner decisions whose deciding input (a title that loads a
> presentation runtime lazily) this machine does not have. §S19 is re-measured
> and deferred: the heuristic tier matches benign system DLLs, but has ~~**not**
> been shown to match inside any game's scan set~~ **now been shown to, by CI**
> (#51): `SuspiciousUnsigned unknown System.Security.Cryptography.ProtectedData.dll`,
> the guard refusing our own harness because §S16 puts the target's ancestors in the
> scan set and a .NET test host is one. **A gate that cannot pass**, in launch mode.
> The deferral stands — the fix is a `CryptCATAdmin*` PR doing network I/O inside
> the hard gate, against NFR-10 — but the evidence changed, and the consequence is
> live: the drain tests are skipped in CI until it is fixed.

0. **The guard.** Module + driver enumeration, blocklist matching, fail-closed behaviour on every error path. **Moved from item 8**: items 1 and 6 below inject into real games, and CLAUDE.md rule 2 plus P1's own "it ships before the first real injection, not after" both forbid that ordering. **◐ Every documented check now has a call site; one half of one check does not.** `EvaluateImpl` runs checks 1, 2, 2b, 3 (executable half) and 4 — five, not four, because 2b (`services`) is the only tier ever measured firing on real anti-cheat and every count that says "four" omits it. The guard is built (`FrameLedger.Injector`, native per §S13(a)), owns the chokepoint, and its fail-closed matrix is Catch2-driven. The injection primitive landed after it, in that order. Reached from managed code through one P/Invoke facade — never a second matcher (§S15). Evidence: `spike-notes.md` §1; §S7, §S8, §S16 closed.

   > **This line said ✅ DONE, and it was wrong in a way worth recording.**
   > `19_SAFETY` specifies four pre-injection checks. **Check 4** (the static
   > pre-scan) had no implementation at all — its two reason codes were
   > declared, named and mirrored into the managed enum while nothing produced
   > either, so three artifacts agreed on a behaviour no code had. **Check 3**
   > (per-title lists) is worse than §S14 recorded: its matchers have no call
   > site, so it is *unwired*, not merely unpopulated.
   >
   > Check 4 is now implemented and runs inside the chokepoint. ~~**Check 3 is
   > still unwired**, so this item stays ◐ until it is, and the status will not
   > read ✅ again on the strength of "most of it works".~~
   >
   > > **Superseded by #52, 2026-08-05.** `CheckBlockedExecutable` runs inside
   > > `EvaluateImpl`, between the module scan and the pre-scan, with its own
   > > `Sources::ImageFileName` seam and its own fail-closed matrix row. Both
   > > directions are proven and the test asserts `kBlockedExecutable`
   > > *specifically*, because "it refuses" is indistinguishable from the four
   > > refusals the guard already makes. The sentence above is struck rather than
   > > deleted: it is the third place that had to be corrected for one PR, and the
   > > count is the point.
   > >
   > > **This item stays ◐, on a different and narrower reason.** Check 3's
   > > **store-id half** has no call site and cannot be given one: nothing produces
   > > a `store_id` — the platform metadata extractors are unbuilt, which is the
   > > *same* gap item 3 shipped with — `FlGuardEvaluate` takes a pid and nothing
   > > else *by design*, and "unknown refuses" applied to it would refuse every
   > > title on every machine. **One unbuilt component sits under two open P0 items
   > > and is budgeted in neither**; cost it once, against both.
   > >
   > > Whether item 0 may read ✅ with a documented sub-check permanently
   > > uncallable is an owner decision, not a coding one. And unchanged either way:
   > > `blockedExecutables` **ships empty**, so check 3 refuses nothing today. What
   > > #52 changed is that populating it would now do something.
1. **Vulkan layer passthrough.** Minimal implicit layer registered under `HKCU`, with the opt-in checks that keep it passthrough for non-enabled processes. **Moved from item 7**: a passthrough bug loads FrameLedger into every Vulkan process on the machine, which is the highest blast radius in the spike. **◐ Gates done, interception not started.** `enable_environment` measured against loader 1.4.357, blast radius verified, in-layer blocklist self-scan built and proven both directions. `vkQueuePresentKHR` is **not** hooked — that is P1, and §S2's in-layer supervision check lands with it.
2. **Hook viability.** `hook-harness` (D3D11 + D3D12) + MinHook: dummy-device vtable probe, verify present indices at runtime, install/uninstall cleanly, measure per-present cost. Confirm `/MT` DLL loads into a real (offline, non-AC) game without incident. **✅ DONE.** Vtable indices proved by behaviour (§H4), unhook proved not to clobber a later hooker (§H7), per-present cost measured at 8.4 ns against a 1,000 ns budget — and **the `/MT` DLL is now loaded into a real title**: Lies of P, attach mode, guard passing, module verified present by re-enumeration, game still rendering afterwards (`spike-notes.md` §7).

   > **Attach mode only when this ran.** Launch mode was blocked by §S18 — the
   > Agent is the game's parent and hosted `FrameLedger.Guard.dll`, whose name
   > trips the guard's own `guard` fragment. Found by this run, decided
   > 2026-08-03 and **implemented 2026-08-04**: the fuzzy tier is suppressed for a
   > scan-set process whose directory is ours, never for the target, and the Agent
   > is now the sole host of the DLL.
   >
   > **The fix is measured on three real titles** in the launch-mode arrangement
   > — our binary as the game's ancestor, carrying the guard DLL — and every one
   > goes `SuspiciousUnsigned` → `Allow`: Deadly Heart Gambit, Lies of P, Alan
   > Wake 2 (`spike-notes.md` §7). **Evaluate only; nothing was injected.**
   >
   > **Launch mode is still not working, and that is a different sentence.** The
   > guard now passes, which is all §S18 was about. Launch-mode *injection* also
   > needs §S1 and §S13(c), both open owner decisions, and there is no Agent
   > capture path to drive it — `Program.cs` gained a rules seeder the same day
   > (§S20) but still builds no host, no watcher and no injector control. This row
   > therefore stays "attach mode" for the *injection* claim; what is upgraded is
   > the guard's verdict, which is the only thing that was measured.
   >
   > Three refusals came before the success, and each was a real defect: a
   > machine-wide `EasyAntiCheat_EOS` service that refused every process on the
   > machine, a failed injection that reported `Allow`, and §S18. None would have
   > surfaced without a real machine and a real title.
3. **The accuracy baseline.** Build a **minimal static-hint detector** — passive file/module scanning, no injection — as the thing item 4 measures against. Added to P0 scope 2026-08-02 (`20_OPEN_QUESTIONS` §M9): the "old detection" this roadmap assumed as a baseline does not exist in this repository, and without it the comparison below cannot be made and ADR-7's founding claim is unfalsifiable. It needs no guard and no injection, so it can be built at any point before item 4. **✅ DONE — both halves, and run on real installs.** The module half is `fl-baseline-probe` (ctest `fl_baseline_probe`, proven in both directions, reusing the guard's own measured enumerator). The inference half is `Domain.Detection.RuleEvaluator` over `Infrastructure.Detection.GameFileProbe`, with a fixture corpus and both an over-match and an under-match canary. Measured against **three real titles** — Deadly Heart Gambit, Lies of P, Alan Wake 2 (`spike-notes.md` §8).

   > **This line said "the rule evaluator is not built" until 2026-08-03**, after
   > the PR that built it. Recording the drift rather than quietly correcting it:
   > a roadmap that lags its own repository is the same defect as a doc that
   > describes an unimplemented check as live, which is what started this phase.
   >
   > **What did *not* land, and what each costs.** Platform metadata extractors
   > (Steam `.acf`, GOG `.info`, Epic `.item`) — every platform rule is
   > `sibling_glob`/`path_contains`, so identification is unaffected but
   > **`store_id` is null for every title**. No SQLite, so nothing is persisted
   > and `field_provenance` is decided-but-unimplemented (P2). The engine and
   > platform fixture families are **P4 library metadata shipped early** under
   > P0's name.
   >
   > First contact with real installs failed on three of four cases and both
   > causes were real: a depth cap of 4 against measured depths of 6, 5 and 9,
   > and an install-root resolution that scanned `Binaries\Win64\` for files that
   > live at the root. Both failed *safe* — and failing safe on every input is
   > not working.

   > **A finding from building it, recorded before the README is drafted:** the
   > baseline can answer **none** of item 4's four runtime questions (upscaler
   > identity, quality preset, render→output resolution, FG activity). A loaded
   > `nvngx_dlss.dll` means the title *can* use DLSS, not that it is on. So
   > "quantify the improvement" below cannot honestly be a percentage; the
   > defensible claim is **"the baseline cannot answer four of these five
   > questions at all"**. `spike-notes.md` §8 carries the reasoning.
4. **The accuracy question — the reason this rewrite exists.** On the dev machine (RTX 5080), verify against ≥ 3 real offline titles that hooks recover: NGX/Streamline feature identity, render vs output resolution, quality preset, and DLSS-G activity. Compare against what the item-3 baseline reports. **Quantify the improvement** — this number justifies the whole trade-off and belongs in the README.

   > **◐ The HOOKED half is measured; the COMPARISON is not, and the difference is not
   > paperwork.** Five captures across four real titles landed 2026-08-20 and every verdict
   > agrees with the game's own settings menu (`spike-notes` §8's table, empty until then).
   > **Two things keep this ◐ rather than ✅:**
   >
   > - **The baseline side cannot be run.** This item's text says *"compare against what the
   >   item-3 baseline reports"*, and `StaticGameDetector` has **no runnable vehicle** — every
   >   construction of it is in `tests/`. So the comparison has one arm. `HANDOFF` item 5
   >   records this as uncosted; this file had never said it at all.
   > - **The README sentence does not exist**, and it is the artifact this item is for. It is
   >   **not a percentage** — item 3 and `spike-notes` §8 already record that the baseline
   >   cannot answer four of the five questions *at all*, which is the defensible claim.
   >
   > An item whose deliverable does not exist may not read ✅ on the strength of the
   > measurements that were supposed to feed it.
5. **Vendor SDK reality check. ✅ DONE.** Measured across 34 distinct modules in 162 files from installed titles; `tools/vendor-exports.ps1` regenerates and `docs/vendor-exports.json` is committed. `17_HOOK_ENGINE` §Upscaling is corrected and its caveat discharged. **It needed no feature hooks** — it reads files with `dumpbin` — so the status header above was wrong to group it with 4/6/7. The finding: `sl.common.dll` exports the NGX parameter **accessors**, while the NGX core exports only the **factories**, so NGX-direct titles need a different hook class (`17_HOOK_ENGINE` §The NGX parameter surface splits into two hook classes).
6. **RT detection.** Harness + a real DXR title: `DispatchRays` counting *and* `BuildRaytracingAccelerationStructure`; verify the AS-build path catches an inline-RayQuery title that dispatch counting misses. **✅ DONE 2026-08-20.** Both detours are on `ID3D12GraphicsCommandList4`, and the discriminating claim is **proved rather than asserted**: `ctest fl_guard`'s two harness arms differ by one recorded call, and `--hold-presenting-rayquery` reports `AsBuildObserved = 60` against `DispatchObserved = **0**` — a writer with only the dispatch hook sees nothing there and its silence is indistinguishable from a real negative (`spike-notes` §6). Four real titles agree with their own settings menus, `Yes` twice and `No` twice. **Carry the trap forward:** a command list's first `Reset()` swaps in a per-object vtable in which the driver has taken `DispatchRays` over, so the first version of this hook installed, published its family bit and never fired — `HANDOFF` §item 4.
7. **Frame Generation ground truth.** ~~Compare rung 1 (FG feature evaluations per present) against Tier-2 ETW `FrameType` on a DLSS-G title.~~ **BLOCKED ON A PRODUCER, not on tooling, measured 2026-08-15:** `slEvaluateFeature(kFeatureDLSS_G)` is never called by Cyberpunk 2077 (0 across ~14,000 Streamline batches at four FG settings), so rung 1 emits nothing for ETW to be compared against. What DID track the setting is `presents / batch` — 1.000 / 2.000 / 4.000 against off / ×2 / ×4 — but a batch is not an application frame and that premise has no independent oracle. `docs/HANDOFF.md` item 3 carries the candidate producers and the decision nobody has taken; `spike-notes` §9 carries the numbers. ~~rung 2 (`GetFrameStatistics` present delta)~~ was removed as structurally impossible, not merely unreliable (`03_METRICS` §Frame Generation). Driver-level FG (AFMF) is undetectable at Tier 1 in v1; whether PresentMon 2.x `FrameType` sees it at Tier 2 is `20_OPEN_QUESTIONS` §M1 — and is untestable on this dev machine, which has no AMD GPU.
8. **Telemetry layering.** Fill the `18_GPU_VENDOR_APIS` capability matrix on real hardware:
   - L1 baseline (DXGI + PDH counters) working vendor-neutrally; decide whether the `D3DKMT` perf-data probe is stable enough on Win 10 **and** Win 11 to keep. **🅓 Deferred to Win 11 only, 2026-08-05** — one machine, and it is Win 11. Win 10 22H2 stays a supported floor and is explicitly unmeasured; the deferral holds only for as long as the probe stays non-load-bearing, which `18_GPU_VENDOR_APIS` §L1 now states as the condition rather than as advice.
   - L2: which fields LibreHardwareMonitor actually returns per vendor, and **whether GPU sensors work unelevated without PawnIO** — this decides whether the default unelevated Agent has temperatures.
   - L3: NVAPI linked from vendored MIT headers; Reflex latency, throttle reasons, per-domain utilisation.
   - ~~**Licence confirmations:** LHM free of MPL-2.0 Exhibit B; NVAPI SPDX blocks intact~~ — **done before P0 began, both clear** (`spike-notes.md` §0). `tools/license-check` is in place and proven to fail on a planted violation.

*(The former items 7 "Vulkan layer" and 8 "Guard prototype" are now items 1 and 0. §R1/§R2 are folded in, not pending.)*

**Exit criteria:** a throwaway build records a real session from a real offline game reporting *correct* upscaler, quality preset, render→output resolution, FG factor and RT state — verified against the game's own settings menu.

> **The FPS-impact criterion moves to the end of P1** (`20_OPEN_QUESTIONS` §R4,
> decided 2026-08-02). "Records a real session" with an Agent CPU/RSS budget
> silently imported the drain, aggregate and recorder paths that P2 delivers, so
> as written P0 could not exit without building most of P2. The measurement
> itself is unchanged (`14_TESTING` §Hook overhead ≤ 0.5%); only its gate moves,
> to the point where a real Overlay and a real drain exist to measure.
>
> The **harness-level** per-present cost (≤ 1 µs, hooked vs unhooked, uncapped)
> stays in P0 as item 2 — it needs no game, no Agent and no drain.

## P1 — Native core (1.5 weeks)
`FrameLedger.Overlay` proper: hook installation for D3D11/12/9/OGL, feature hooks, ring writer, fault policy, unhook path, native logging. `FrameLedger.Injector` with launch + attach modes. Struct mirror + Catch2 tests. Vulkan layer to parity — `vkQueuePresentKHR` plus the in-layer supervision check §S2 gates on it.

> **The guard already shipped, in P0.** This line used to read "the guard,
> complete and fully tested — it ships before the first real injection, not
> after", which was the correct ordering stated in the wrong phase: P0 items 1
> and 6 inject into real games. It moved to item 0 and is done. What P1 still
> owes the guard is the launch-mode decision (§S13(c)) and the mid-session
> re-scan wired to a real Overlay.

## P2 — Capture pipeline (1 week)
Agent: watcher, tier selection, injection orchestration, shm drain, telemetry poller (NVAPI first), session recorder, segments, `.partial` recovery, Tier-2 `EtwFrameSource` retained as fallback. SQLite v2 schema + migrations. Domain metric calculators + golden tests. **Milestone: first real hooked session persisted with measured upscaler/RT data.**

## P3 — UI (1 week)
WPF UI shell (`16_WPFUI_SYNTAX`), library, per-game hooking consent flow, Dashboard live card showing *measured* settings, session summary, ScottPlot charts, game detail tabs, Compare with tier guarding, segment ribbon, tri-state chips with override, refusal/degradation/unhook notices as first-class UI.

## P4 — Detection & product polish (1 week)
Static rules engine + `detection-rules.json` (engines, platforms, capabilities, **anticheat blocklist**) + validator + fixtures; store auto-import; capability-vs-measured UI separation; i18n en/vi/ja; Legal Gate; Velopack + updater (with the hooked-session deferral); bug bundle incl. overlay logs; tray + toasts; Settings; DB maintenance; accessibility pass.

## P5 — Ship (3–4 days)
CI/CD with the native build; CodeQL (C# + C++); Dependabot; CHANGELOG; README with the P0 accuracy comparison — **which is not a percentage, and this line used to send its author looking for one**. The decision is recorded under item 3 and in `spike-notes.md` §8: the baseline can answer **none** of item 4's four runtime questions, so the defensible claim is *"the baseline cannot answer four of these five questions at all"*, not *"N% more accurate"*. Overhead + tier cross-validation measurements; release smoke on clean VMs; `v0.1.0-beta.1`.

**Total ≈ 5.5–6.5 weeks full-time** — roughly 1.5–2 weeks more than the ETW design, which is the honest price of the accuracy.

## v1.1 — earned features (only after the data path is stable)

- **In-game overlay** (FR-15): we are already in the process; draw live FPS/frametime/upscaler/RT state. Opt-in per game, off by default. Borderless and fullscreen-flip both handled since we own the swapchain.
- **PSO stutter deep-dive**: attribute compile stalls to specific pipeline creations with a timeline.
- Unknown-game suggestion (Tier 2 only — never suggests hooking).
- Per-segment comparison ("before/after I changed DLSS Quality → Balanced, mid-session").

## v2 backlog

- PresentMon Service + API2 for a richer Tier 2
- **YouTube overlay export**: frametime/FPS graph as transparent PNG sequence for DaVinci Resolve benchmark videos (SkullMute workflow)
- CapFrameX-compatible CSV import/export
- Microsoft Store / Game Pass titles (packaged-app injection constraints)
- AFK segmentation via `GetLastInputInfo`
- GPU frame time via timestamp queries (needs care: injecting queries into the game's command lists is more invasive than anything in v1 — needs its own risk review)
- Beta update channel; ARM64

## Permanently out of scope

Evasion of any kind; game memory access; kernel drivers; hardware control; online/competitive titles. See `19_SAFETY_AND_ANTICHEAT.md` — these are not "later", they are never.
