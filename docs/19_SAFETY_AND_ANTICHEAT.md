# 19 — Safety & anti-cheat policy

FrameLedger injects a DLL into game processes. That is a legitimate, mainstream technique (RTSS, Special K, OBS Game Capture, ReShade, Steam/Discord overlays all do it) — but it carries one real risk that falls entirely on the user: **an anti-cheat system may flag, block, or ban an account.** This document defines the guard rails that make that risk manageable, and the things this project will deliberately never build.

## Design principle

> A performance tool should be **easy for anti-cheat to see and identify**, and should **refuse to run** where it isn't welcome.

Every decision below follows from that. The failure mode we are engineering against is not "anti-cheat detected us" — that is fine and expected. It is "a user got banned because our tool loaded somewhere it shouldn't have."

## What we will never build (rule 3 in CLAUDE.md)

These are permanently out of scope. A PR implementing any of them is rejected regardless of quality:

- Manual mapping / reflective loading, or any injection that bypasses `LoadLibrary`
- Erasing or corrupting PE headers of the loaded module
- Unlinking the module from the PEB loader lists
- Hiding, renaming, or randomizing the DLL, its exports, or its shared-memory object names
- Thread hiding (`NtSetInformationThread`/`ThreadHideFromDebugger`) or debugger-evasion tricks
- Signature-breaking obfuscation/packing of our own binaries
- Any "stealth mode", "bypass" setting, or documentation explaining how to defeat the guard below
- Reading or writing game memory outside the arguments of APIs we hooked
- Kernel drivers of our own

The DLL ships with its real filename, a populated VERSIONINFO block (`CompanyName`, `ProductName=FrameLedger`, version), and named kernel objects that clearly say `FrameLedger`. Being identifiable is a feature.

## The anti-cheat guard (hard gate)

Implemented in `FrameLedger.Injector` and reached from managed code through a thin P/Invoke facade — one implementation, never two (`20_OPEN_QUESTIONS` §S15). Runs **before every injection**, and again periodically for **every Tier-1 session**, injected or layered.

> "During a hooked session" was the original wording, and it excluded Vulkan by
> accident: those titles are captured by a Khronos layer and nothing is ever
> injected into them. The periodic re-scan is scoped to the *tier*, not to the
> mechanism.

### Pre-injection checks (all must pass)

1. **Target module scan** — enumerate loaded modules of the target process (`EnumProcessModulesEx`, read-only handle: `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`). Match against the blocklist below by filename.

   > **`LIST_MODULES_ALL` is mandatory, not a preference.** Measured 2026-08-02
   > unelevated against a live 32-bit target from this x64 process
   > (`spike-notes.md` §1): `ALL` returns 15 modules, `LIST_MODULES_DEFAULT`
   > returns **7** — and returns them as a *success*. A guard using the default
   > would scan a 32-bit title, see under half its modules, find no anti-cheat
   > among them and report clean.
   >
   > **Every failure of this call means REFUSE.** Two specific cases, both
   > measured: `OpenProcess` returning `ERROR_ACCESS_DENIED (5)` on a protected
   > target, and `EnumProcessModulesEx` returning `ERROR_PARTIAL_COPY (299)`
   > against a suspended target (§S1). Neither is "no modules found". The read
   > handle rights above are confirmed sufficient for every target this call can
   > legitimately reach.
   > **Which processes get scanned: the game's own subtree.** `04_CAPTURE`
   > §The guard writes `Check(pid)` — singular — while `04_CAPTURE` §Process
   > watcher says the capture target is a *descendant* elected from a ppid
   > chain. Neither of the obvious readings is right (`20_OPEN_QUESTIONS` §S16,
   > decided 2026-08-02):
   >
   > - **The presenting process alone is too narrow.** A game's own launcher
   >   routinely initialises anti-cheat and *then* spawns the renderer. Scanning
   >   only the process we inject into would miss exactly that arrangement, and
   >   it is a common one.
   > - **Every ancestor is far too broad.** The ancestor of every Steam title is
   >   `steam.exe`, which loads `steamservice` and VAC modules. Scanning
   >   ancestors without limit would refuse *every Steam game* — not "some false
   >   refusals" but the product not working, which is precisely how a user ends
   >   up hunting for the override that does not exist.
   >
   > So the scanned set is the **injection target, its descendants, and its
   > ancestors up to but excluding the first known platform launcher**
   > (`platforms` in `detection-rules.json`: Steam, GOG, Epic, itch). That is
   > "the game's own tree" — everything the title itself brought with it,
   > nothing belonging to shared platform infrastructure.
   >
   > A hit **anywhere** in that set refuses. A process in the set that cannot be
   > inspected (`ERROR_ACCESS_DENIED`) refuses too — `19_SAFETY` has no "scanned
   > what we could" state.
   >
   > Sibling *services* (BattlEye's `BEService`, EAC's service) are not in any
   > process tree and are deliberately not chased here: check 3's `services`
   > group covers them by name, which is more reliable than tree walking.
   >
   > The runtime re-scan (§During a session) recomputes this set each time
   > rather than caching it: level-transition relaunches re-elect the presenting
   > pid, and a newly spawned helper must be seen.

2. **System driver scan** — enumerate loaded kernel drivers for always-on anti-cheat drivers that gate the whole machine (e.g. Vanguard's `vgk`). Present → refuse for **all** titles while it is running, not just the matching game.

   > 🔴 **`EnumDeviceDrivers` cannot do this unelevated, and it fails *open*.**
   > Measured 2026-08-02 on Windows 11 26300 as a standard user — which is the
   > **default** Agent configuration (ADR-9):
   >
   > ```
   > EnumDeviceDrivers  ok=True  count=258
   > non-null base addresses: 0
   > distinct names recoverable: 1  ->  ntoskrnl.exe
   > ```
   >
   > The call *succeeds*. It reports 258 drivers. It then yields no usable base
   > address for any of them, so `GetDeviceDriverBaseName` recovers one name.
   > A guard built on it would report "no anti-cheat driver present" on a machine
   > running Vanguard — a **fail-open in the hard gate, in the default
   > configuration**. `14_TESTING` already insists an empty *module* list must
   > never read as "clean"; the same rule was never applied to drivers, and this
   > is what it looks like when it is missed.
   >
   > **The defect is purely a function of elevation**, measured both ways by
   > `fl-probe-guard` (`spike-notes.md` §1):
   >
   > | Configuration | `EnumDeviceDrivers` result |
   > |---|---|
   > | **unelevated** (dev machine, the ADR-9 default) | 266 drivers, **0** bases, **0** names |
   > | elevated (CI runner) | 260 drivers, **260** bases, **260** names |
   >
   > So the API is not broken — it is broken *for standard users*, and ADR-9
   > makes standard user the default Agent. Anyone testing this while elevated
   > sees a perfectly working call and concludes the note above is wrong. It is
   > the sharpest illustration in the project of why measurements must state
   > their configuration: the same API, on the same OS, is either fine or
   > catastrophic depending on a token.
   >
   > **Use `NtQuerySystemInformation(SystemModuleInformation)` instead.** Measured
   > on the same unelevated session: `STATUS_SUCCESS`, 258 modules, **258 distinct
   > full driver paths**, real third-party driver names legible. Corroborate with
   > `OpenServiceW`/`QueryServiceStatusEx`, which also distinguishes *absent*
   > (`ERROR_SERVICE_DOES_NOT_EXIST`, 1060) from *denied* — a distinction the
   > guard needs, because denied must fail closed while absent must not.
   >
   > `NtQuerySystemInformation` is a documented-as-unsupported API and its
   > `RTL_PROCESS_MODULE_INFORMATION` layout is version-sensitive; the struct
   > offsets must be asserted, not assumed. Treat a parse failure as *refuse*,
   > never as *clean*. `20_OPEN_QUESTIONS` §S7 tracks the remaining work.
3. **Rules blocklist** — `detection-rules.json` carries `anticheat.blockedExecutables` (exe names) and `anticheat.blockedStoreIds` (Steam appids etc.) for known competitive/online titles, updatable independently of app releases (`05_DETECTION` §Rules updates).

   > ⚠ **This check is UNWIRED, which is worse than empty.** Both arrays are
   > empty, *and* `MatchesBlockedExecutable`/`MatchesBlockedStoreId` have no call
   > site anywhere — `EvaluateImpl` never asks them. Populating the data would
   > therefore change nothing. The gate is not weakened, because checks 1, 2 and
   > 4 run and every family in the seed below is caught by a module, driver,
   > service or directory signal — but the per-title layer this bullet describes
   > does not exist yet, in two independent ways. Which titles to list, and what
   > an *unresolvable* store id means (it must read **unknown**, never *clean*),
   > are decisions tracked as `20_OPEN_QUESTIONS` §S14. Stated here so that
   > "check 3 passed" is not read as "the title is not a known online title".
   >
   > The parser now reads both arrays in their real, documented shape (objects
   > carrying `family`, `match`/`store`+`id`, `values` and `reason`), so the data
   > can be written before the wiring lands. It used to read them as bare
   > strings, which meant the first entry ever added would have refused the whole
   > rules file — and therefore every title on the machine (§S17).
4. **Multiplayer heuristic** — if the pre-launch file scan finds an anti-cheat SDK shipped alongside the game (e.g. EOS anti-cheat binaries, `EasyAntiCheat/` directory) even when not currently loaded → refuse and explain.

   > **Implemented, and stated narrowly.** `fl_prescan.cpp` walks the target's
   > own directory (derived from its pid, never from a caller-supplied path) to
   > depth 2, matching entry names through the **same** `MatchName` as every
   > other check: directory names against the `directories` group, file names
   > against `files`. `Reason::kAntiCheatDirectory` and `kAntiCheatFile` are
   > produced here — until now they were declared, named and mirrored while
   > nothing produced either.
   >
   > **It runs inside the chokepoint**, as the last of the four checks in
   > `EvaluateImpl`. `FlStaticPreScan` also exposes it to the UI for FR-2.2's
   > pre-launch question, but that export is **advisory**: it gates nothing, and
   > a caller who never asks it — or ignores the answer — changes nothing about
   > what injection allows.
   >
   > **Which directory.** The **install root**, resolved by walking up from the
   > executable to a known platform boundary (`steamapps\common\<X>` and
   > friends) — *not* the directory the executable sits in.
   >
   > Measured 2026-08-03 on Lies of P, and the difference is the whole check:
   > Unreal puts the exe at `<root>\<Project>\Binaries\Win64\`, a folder holding
   > **seven files**, none of which could ever have been an anti-cheat SDK —
   > while `EasyAntiCheat/` sits at the root three levels up. For exactly the
   > layout most likely to carry EAC, this check scanned a directory that could
   > not contain what it was looking for, and returned clean.
   >
   > The boundaries are hardcoded, like `IsPlatformLauncher`, because a
   > data-driven boundary lets a rules update move where the hard gate looks.
   > **When no boundary is recognised the executable's own directory is used** —
   > walking up blindly would reach a folder of unrelated games, and refusing a
   > title because a *sibling* ships anti-cheat is a false refusal with no
   > appeal. A nested executable outside a recognised store layout is therefore
   > still scanned narrowly; that residual is stated, not closed.
   >
   > **What it does not cover.** Beyond the above, the residual is an anti-cheat
   > SDK sitting on disk whose service is not installed and whose module is not
   > loaded; checks 1, 2 and 2b already cover the rest. And today the `directories` + `files`
   > groups carry only three tokens between them, so "check 4 ran" is a much
   > narrower statement than "this game ships no anti-cheat". Widening it needs
   > **verified** file and directory names — the seed table below deliberately
   > carries "no data yet" rows rather than guesses, because a wrong token fails
   > closed by never firing, which is a silent hole.
   >
   > **Every uncertainty refuses**, with `Reason::kPreScanFailed` rather than a
   > hit: directory absent or unlistable, either bound exceeded, a name we could
   > not convert, or a reparse point — which is never followed, because a
   > junction can hide an `EasyAntiCheat/` beneath it. **Unmeasured:** how often
   > a legitimate library trips the reparse-point rule or the 4096-entry bound.
   > That needs a real game library, and it is a false-refusal path, so it is
   > recorded rather than assumed benign.

Any check failing ⇒ **injection is refused**. The UI shows which check fired and offers Tier-2 (ETW) capture instead, which requires no injection — but does require an elevated Agent, so the offer must state that plainly and fall through to Tier 3 rather than appearing to succeed and recording nothing (`04_CAPTURE` §Frame source abstraction).

> There is no override. No hidden setting, no config-file flag, no CLI switch, no "advanced users" escape hatch. If a user disagrees with a specific entry, the path is a GitHub issue against the rules file, reviewed in public — not a local bypass.

### Blocklist seed

Matching is case-insensitive. **This table shows the literal tokens the data
carries, not glob patterns** — `rules/detection-rules.schema.json` rejects `*`
in a token outright, so a maintainer copying `EasyAntiCheat*.dll` out of an
earlier version of this table produced an entry that was matched *literally* and
therefore never fired. A silent hole in the blocklist, created by its own
normative documentation. Write tokens here exactly as the data must hold them.

| Family | Group | Match | Tokens |
|---|---|---|---|
| Easy Anti-Cheat | `modules` | prefix | `EasyAntiCheat`, `EasyAntiCheat_EOS` |
| Easy Anti-Cheat | `directories` | name | `EasyAntiCheat` |
| Easy Anti-Cheat | `services` | name | `EasyAntiCheat`, `EasyAntiCheat_EOS` |
| BattlEye | `modules` | prefix | `BEClient`, `BEService` |
| BattlEye | `directories` | name | `BattlEye` |
| Riot Vanguard | `drivers` | exact | `vgk.sys` — **machine-wide refusal** (check 2) |
| Riot Vanguard | `services` | name | `vgc` |
| Denuvo Anti-Cheat | `modules` | prefix | `denuvo` |
| nProtect GameGuard | `modules` | prefix | `GameGuard`, `npgg`, `GameMon` |
| Xigncode3 | `modules` | prefix | `xhunter` |
| Xigncode3 | `files` | name | `x3.xem` |
| mihoyo protect | `drivers` | prefix | `mhyprot` |
| FACEIT | `modules` | prefix | `faceit` |
| ESEA | `modules` | prefix | `esea` |
| PunkBuster | `modules` | prefix | `PnkBstr`, `pbcl`, `pbsv` |
| **Activision Ricochet** | — | — | **No data yet** — driver and service names unconfirmed (`20_OPEN_QUESTIONS` §S5) |
| **Valve VAC** | — | — | **No data yet** — needs `blockedStoreIds`; VAC titles are treated as **online** (refuse) |

The two "no data yet" rows are deliberately kept rather than deleted. An
admitted gap is reviewable; a deleted row is invisible.

> **A prefix must be at least 4 characters and must not shadow a system module.**
> This table used to write PunkBuster as `pb*.dll`; the data correctly narrows
> it to `PnkBstr`/`pbcl`/`pbsv`, and `tools/rules-validate.ps1` now makes the
> narrow form mandatory. A too-short prefix does not fail open — it refuses
> *every* title, which is how a user ends up hunting for the override that
> CLAUDE.md rule 2 says does not exist.

The list is data, versioned in `detection-rules.json`, expandable without a release. Unknown-but-suspicious modules (filename containing `anticheat`, `antitamper`, `guard`, `protect` + unsigned-by-known-vendor) produce a **warn-and-refuse** with a "report this to us" link rather than silently allowing.

**The signer comparison uses the certificate subject's `O=` field, not `CN=`**
(`anticheat.heuristic.signerField`, a schema `const`). Measured 2026-08-02 on
Windows 11 26300: every WHQL-signed binary on the machine — *including the
NVIDIA display driver itself* — carries
`CN='Microsoft Windows Hardware Compatibility Publisher'`, which is not a vendor
name, while `O='Microsoft Corporation'`. Matching on `CN` would therefore make
the entire driver stack read as untrusted, and combined with the `guard` and
`protect` name fragments above that is a live false-refusal path, not a
theoretical one. A signature that is absent, invalid, or simply could not be
checked stays untrusted and refuses — that direction is deliberate.

### During a session

Re-run **both the module scan and the driver scan** every 30 s, for **every
Tier-1 session — injected or layered**.

> **Scoping this to "hooked" sessions was a real gap.** Everything about the
> re-scan was written as "during a hooked session" / "loading *after*
> injection", and `04_CAPTURE` §Session recorder reaches `Capturing` only
> through `Injecting` — a state a Vulkan session never enters, because the
> layer is loaded by the Vulkan loader and nothing is injected. Read literally,
> the most important runtime behaviour in the capture layer was not specified
> to run at all for Vulkan titles. It applies to any session capturing at
> Tier 1.
>
> **The Agent publishes proof it ran**, not proof it is alive. `guardTicks`
> counts *completed evaluations* (`07_IPC` §Protocol rules); a capture side that
> sees it stop advancing stops observing. A timer-driven heartbeat would have
> let the capture side keep going precisely because the guard loop had died. Anti-cheat loading *after* injection (some titles load it late, or the user launched a multiplayer mode from a single-player menu) ⇒ **clean unhook on detection**, session finalized as `exit_status = unhooked_safety`, prominent UI notice. This is the single most important runtime behavior in the whole capture layer.

> **The driver scan is not optional here, and it used to be missing.** This
> section said "the module scan" only. But a machine-wide anti-cheat driver can
> start *after* injection — the user opens Valorant while another title is
> hooked — and check 2 above requires refusal for **all** titles while such a
> driver is running. A module-only re-scan looks inside the hooked game and
> would never see it. The runtime loop must repeat every pre-injection check
> whose subject can change mid-session, not just the one whose subject is the
> game.

**Be honest about the window.** A 30 s poll means anti-cheat can be loaded for up to 30 s before we react — the unhook is immediate *once detected*, not immediate in absolute terms. Consent and disclaimer wording must say "within 30 seconds", never "immediately" (`legal/DISCLAIMER.md` §2). Whether to shrink the window, or to detect the load directly via the `LoadLibrary` hook the Overlay already installs for lazily-loaded graphics DLLs, is `20_OPEN_QUESTIONS` §S6 — the hook exists and is currently unused for this purpose, which is the cheapest available improvement to the most important behavior in the product.

### Elevated / protected targets

If `OpenProcess` with the needed rights fails (protected process, higher integrity), do **not** escalate creatively. Report "cannot attach" and offer Tier-2. Never attempt to acquire privileges beyond running the Agent elevated at the user's explicit request.

## User-facing consent

Enabling hooking is a **per-game** action, gated by a one-time dialog per game that states, in plain language:

- what gets injected and why (measuring the real render resolution, upscaler, frame generation and ray tracing state — which passive measurement cannot do accurately),
- that anti-cheat systems may flag or ban accounts, and that FrameLedger refuses to inject where it detects one but **cannot guarantee it knows every anti-cheat**,
- that the user is responsible for the terms of service of the games they play,
- that Tier-2 (no injection) capture is available and is the default for anything the guard is unsure about.

Consent is stored per game (`games.hook_consent_at`), **stamped by the Agent, never supplied by a client** (`07_IPC` §The pipe is not a trust boundary). Wording lives in `.resx` and is reviewed with the same care as the legal documents.

The default for every newly added game is **hooking off, Tier-2 on**. Nothing is ever injected because the user merely added a game.

### A game already enabled can become blocked later

FR-2.2 disables the toggle for titles that ship anti-cheat, and FR-2.3 refuses at
runtime — but neither says what happens to a game the user enabled *before* it
started matching. A patch adds anti-cheat, or a rules update newly covers it.
Leaving `hook_enabled = 1` in that state is the dangerous reading, and it is the
one the documents used to permit by omission (`20_OPEN_QUESTIONS` §S11).

**The static pre-scan re-runs on every rules update and on every change to the
game's executable** (path, size or mtime). If it now matches:

1. `hook_enabled` is forced to `0`.
2. `hook_blocked_reason` is set — the column already exists (`06_DATA_MODEL`
   §games), so this needs no schema change, and a non-null value already means
   "toggle disabled in the UI".
3. `hook_consent_at` is **preserved**. The user did consent; the block is not a
   withdrawal of consent and must not silently require them to consent again if
   the title is later cleared.
4. The UI shows a persistent notice, not a transient toast — the user needs to
   understand why a game they enabled stopped being captured at Tier 1.

A rules update that *removes* a match does **not** re-enable hooking on its own.
Turning it back on is a user action, because re-enabling silently would mean the
rules feed can switch injection on for a game without anyone looking.

## Crash & stability safety

- Two crashes of the same game within 60 s of injection ⇒ hooking auto-disabled for that game, UI explains, Tier-2 takes over. Recorded in `games.hook_autodisabled_reason`.
- The Overlay DLL self-disables after 3 faults in hook bodies (`17_HOOK_ENGINE` §Fault policy) and reports it.
- Every hooked session writes a breadcrumb file before injection; if the game process dies before the first frame record arrives, the next run is recorded as suspect.

  > **"Cautious mode" is deferred to v1.1 (`20_OPEN_QUESTIONS` §S12).** It was
  > described as "hooks installed, overlay drawing disabled" — but **v1 draws no
  > overlay at all** (FR-15 is v1.1), so as written it disables nothing and is a
  > no-op dressed as a safety measure. In v1 the breadcrumb is still written and
  > still read: a second unexplained death within 60 s of injection triggers the
  > existing auto-disable above, which is the behaviour that actually protects
  > the user. Cautious mode returns with the overlay, when there is something
  > for it to switch off.

## Honest limits to document to users

The Disclaimer states these explicitly, and the UI consent dialog echoes them:

1. The blocklist cannot be complete. New anti-cheat systems appear; a game can add one in a patch.
2. Some anti-tamper (Denuvo) reacts to injection even in single-player titles — usually a crash, not a ban, but it can also mean lost play time.
3. Even a perfectly behaved tool can be flagged by heuristics.
4. FrameLedger's authors cannot restore a banned account. The refusal guard exists to make this unlikely; it is not a warranty.

## Review checklist for any capture-layer PR

- [ ] Does this hook read anything beyond the arguments of the API it hooks? → reject
- [ ] Does this make FrameLedger harder for anti-cheat to identify? → reject
- [ ] Does this add a path to inject without passing the guard? → reject
- [ ] Is the new hook listed in `17_HOOK_ENGINE` §Hook inventory with a stated purpose?
- [ ] Does the hook body allocate, lock, log, or throw? → reject
- [ ] Is there a Tier-2 degradation path if the hook is unavailable?
