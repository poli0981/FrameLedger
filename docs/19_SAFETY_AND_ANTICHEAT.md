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

Implemented in `FrameLedger.Injector` and re-checked by the Agent. Runs **before every injection**, and again periodically during a hooked session.

### Pre-injection checks (all must pass)

1. **Target module scan** — enumerate loaded modules of the target process (`EnumProcessModulesEx`, read-only handle: `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`). Match against the blocklist below by filename.
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
4. **Multiplayer heuristic** — if the pre-launch file scan finds an anti-cheat SDK shipped alongside the game (e.g. EOS anti-cheat binaries, `EasyAntiCheat/` directory) even when not currently loaded → refuse and explain.

Any check failing ⇒ **injection is refused**. The UI shows which check fired and offers Tier-2 (ETW) capture instead, which requires no injection — but does require an elevated Agent, so the offer must state that plainly and fall through to Tier 3 rather than appearing to succeed and recording nothing (`04_CAPTURE` §Frame source abstraction).

> There is no override. No hidden setting, no config-file flag, no CLI switch, no "advanced users" escape hatch. If a user disagrees with a specific entry, the path is a GitHub issue against the rules file, reviewed in public — not a local bypass.

### Blocklist seed (`anticheat.modules`)

Matched case-insensitively on module filename, prefix match where noted:

| Family | Signals |
|---|---|
| Easy Anti-Cheat | `EasyAntiCheat*.dll`, `EasyAntiCheat_EOS*.dll`, `EasyAntiCheat/` dir, EAC service |
| BattlEye | `BEClient*.dll`, `BEService*`, `BattlEye/` dir |
| Riot Vanguard | `vgk.sys` driver loaded (machine-wide refusal), `vgc` service |
| Denuvo Anti-Cheat | `denuvo*`, anti-tamper + AC variants |
| Activision Ricochet | associated driver/service present |
| nProtect GameGuard | `GameGuard*`, `npgg*`, `GameMon*` |
| Xigncode3 | `xhunter*`, `x3.xem` |
| mhyprot / anti-cheat drivers of gacha titles | `mhyprot*.sys` |
| Valve VAC | `steamservice`-loaded VAC modules — VAC titles are treated as **online** (refuse) |
| FACEIT / ESEA | `faceit*`, `esea*` drivers or services |
| PunkBuster | `pb*.dll`, `PnkBstr*` |

The list is data, versioned in `detection-rules.json`, expandable without a release. Unknown-but-suspicious modules (filename containing `anticheat`, `antitamper`, `guard`, `protect` + unsigned-by-known-vendor) produce a **warn-and-refuse** with a "report this to us" link rather than silently allowing.

### During a session

Re-run the module scan every 30 s. Anti-cheat loading *after* injection (some titles load it late, or the user launched a multiplayer mode from a single-player menu) ⇒ **clean unhook on detection**, session finalized as `exit_status = unhooked_safety`, prominent UI notice. This is the single most important runtime behavior in the whole capture layer.

**Be honest about the window.** A 30 s poll means anti-cheat can be loaded for up to 30 s before we react — the unhook is immediate *once detected*, not immediate in absolute terms. Consent and disclaimer wording must say "within 30 seconds", never "immediately" (`legal/DISCLAIMER.md` §2). Whether to shrink the window, or to detect the load directly via the `LoadLibrary` hook the Overlay already installs for lazily-loaded graphics DLLs, is `20_OPEN_QUESTIONS` §S6 — the hook exists and is currently unused for this purpose, which is the cheapest available improvement to the most important behavior in the product.

### Elevated / protected targets

If `OpenProcess` with the needed rights fails (protected process, higher integrity), do **not** escalate creatively. Report "cannot attach" and offer Tier-2. Never attempt to acquire privileges beyond running the Agent elevated at the user's explicit request.

## User-facing consent

Enabling hooking is a **per-game** action, gated by a one-time dialog per game that states, in plain language:

- what gets injected and why (measuring the real render resolution, upscaler, frame generation and ray tracing state — which passive measurement cannot do accurately),
- that anti-cheat systems may flag or ban accounts, and that FrameLedger refuses to inject where it detects one but **cannot guarantee it knows every anti-cheat**,
- that the user is responsible for the terms of service of the games they play,
- that Tier-2 (no injection) capture is available and is the default for anything the guard is unsure about.

Consent is stored per game (`games.hook_consent_at`). Wording lives in `.resx` and is reviewed with the same care as the legal documents.

The default for every newly added game is **hooking off, Tier-2 on**. Nothing is ever injected because the user merely added a game.

## Crash & stability safety

- Two crashes of the same game within 60 s of injection ⇒ hooking auto-disabled for that game, UI explains, Tier-2 takes over. Recorded in `games.hook_autodisabled_reason`.
- The Overlay DLL self-disables after 3 faults in hook bodies (`17_HOOK_ENGINE` §Fault policy) and reports it.
- Every hooked session writes a breadcrumb file before injection; if the game process dies before the first frame record arrives, the next run starts in "cautious mode" (hooks installed, overlay drawing disabled) to isolate whether rendering or capture caused it.

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
