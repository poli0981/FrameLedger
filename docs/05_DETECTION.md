# 05 — Detection

Detection now has **two clearly separated tiers**, and conflating them was the root cause of the accuracy problem this rewrite addresses.

| Tier | Source | Answers | Confidence |
|---|---|---|---|
| **Runtime facts** | API calls we hooked (`17_HOOK_ENGINE`) | What the game is *actually doing*: upscaler + quality, render/output resolution, FG, RT, present mode, HDR | **Measured** |
| **Static hints** | Files on disk, PE metadata, store manifests | What the game *is* and what it *could* support: engine, version, publisher, store, capability | Inference |

**Rule: a static hint may never set a runtime fact.** `nvngx_dlss.dll` existing on disk means the game ships DLSS. It does not mean DLSS is on. The old design blurred these and produced the field errors that motivated the rewrite. Static hints populate library metadata and *capability chips*; they never write `sessions.upscaler`, `fg_mode`, or the RT flags.

## Runtime facts (Tier 1 only)

Derived in the Agent from the record stream; the mapping is in `03_METRICS`. Summary of what each hook establishes:

| Fact | Established by |
|---|---|
| Graphics API + swapchain format, buffer count, swap effect, flags | `CreateSwapChain*` / present hooks |
| Present mode (flip / blt / independent flip) | swapchain desc + `presentFlags` + `syncInterval` |
| HDR output | `SetColorSpace1` |
| Upscaler identity + quality preset | NGX / Streamline / FFX / XeSS create+evaluate calls |
| Render resolution vs output resolution (incl. mid-session changes) | upscaler parameter reads + `ResizeBuffers` |
| Frame Generation mode + factor | NGX/SL/FFX FG feature evaluation, present-count delta, cadence |
| Ray Tracing active | AS builds + `DispatchRays` (both, to catch inline RayQuery) |
| Ray Reconstruction | NGX `RayReconstruction` feature evaluated |
| Reflex on + PC latency | NVAPI Reflex hooks |
| Per-process VRAM | `QueryVideoMemoryInfo` |
| PSO compilation events | pipeline-creation hooks |

Tier-2 sessions have `upscaler = unknown`, resolutions `N/A`, RT `N/A`, and an FG mode only if PresentMon's `FrameType` or cadence resolves it.

## Static hints — rules engine

Rules are **data, not code**: `rules/detection-rules.json`, bundled and updatable from the repo (raw GitHub URL, ETag-cached; manual "Update detection rules" in Tools + weekly auto-check). Schema:

```json
{
  "schemaVersion": 2,
  "rulesVersion": "2026.07.2",
  "engines":    [ { "id": "unity", "name": "Unity", "signals": [...], "version": {...} } ],
  "platforms":  [ ... ],
  "capabilities": [ ... ],
  "anticheat":  { "modules": [...], "drivers": [...], "blockedExecutables": [...], "blockedStoreIds": [...] }
}
```

The `anticheat` block is the same file that feeds the hard guard in `19_SAFETY` — shipping it as updatable data is what lets a newly-protected game be blocked without waiting for an app release. **Rules updates that touch the `anticheat` block are treated as security updates:** applied on next check regardless of the user's auto-update preference for other rules.

Signal types evaluated by `RuleEvaluator` (Domain): `file_exists`, `dir_exists`, `sibling_glob`, `pe_company_contains`, `pe_product_contains`, `strings_contains` (bounded 8 MB scan), `manifest_field`. Combine with `all` / `any`. Version extractors: `pe_file_version`, `pe_product_version_regex`, `manifest_field`.

`tools/rules-validate` checks schema and runs rules against fixture trees in CI.

### Trust and staleness of the rules feed

`20_OPEN_QUESTIONS` §S4. The file is fetched over HTTPS from a raw GitHub URL.
Three rules, because a gate whose data can silently go stale is a gate with an
expiry date nobody sees:

- **The Agent reads rules from exactly one place:** its own
  `%LOCALAPPDATA%\FrameLedger\rules\`. The source is not a parameter and cannot
  be redirected over the pipe (`07_IPC` §The pipe is not a trust boundary).
- **A fetched file replaces the local copy only if it validates.** Same
  structural checks `tools/rules-validate.ps1` runs, including the non-empty
  `anticheat` requirement. A malformed, truncated or empty-blocklist download is
  discarded and the **last valid copy is kept** — never cleared, never partially
  applied.
- **Staleness warns; it never disables.** Past N days without a successful
  check, the UI says so plainly. It must **never** be wired to relax or disable
  the blocklist: "the rules are old" is an argument for more caution, not less,
  and an expiry that weakens a gate is an override with a timer on it.

Signing the feed is **not** decided. HTTPS authenticates the host, not the
content, and a signature would authenticate the content. It is deferred rather
than dismissed: the app ships a seed blocklist that validates locally, so a
compromised feed can be *rejected* by the rules above but not *proven genuine*.
Recorded as a residual risk, not as a solved problem.

### Engine signatures

| Engine | Signals | Version |
|---|---|---|
| Unity | `UnityPlayer.dll` sibling **or** `<Exe>_Data/` dir | FileVersion of `UnityPlayer.dll` |
| Unreal 4/5 | exe matches `*-Win64-Shipping.exe` **or** `*/Content/Paks/*.pak` | ProductVersion regex `\+\+UE(4\|5)\+Release-(\d+\.\d+)` |
| Godot | `.pck` sibling **or** `strings_contains("Godot Engine v")` | strings regex `Godot Engine v(\d+\.\d+[\.\d]*)` |
| GameMaker | `data.win` | `N/A` |
| RPG Maker MV/MZ | `nw.dll` + `www/` or `package.json` | `rpg_core.js` / `rmmz_core.js` header |
| RPG Maker XP/VX/VXAce | `RGSS10*`/`RGSS20*`/`RGSS30*.dll` | dll name → XP/VX/VXAce |
| Ren'Py | `renpy/` dir **or** `*.rpa` | `renpy/__init__` strings / `log.txt` first line |
| CryEngine | `CrySystem.dll` | FileVersion |
| Source | `gameinfo.txt` + `bin/engine.dll` | `N/A` |
| Unknown | fallback | — |

Order matters (first match wins). Engine is user-overridable.

### Platform signatures & metadata

| Platform | Signals | Metadata |
|---|---|---|
| Steam | `steam_api64.dll`/`steam_api.dll` sibling, or path contains `steamapps\common` | nearest `steamapps/appmanifest_<id>.acf` (walk up): `appid`, `name`, `buildid` → version |
| GOG | `goggame-<id>.info` sibling, or `Galaxy64.dll` | `.info` JSON: `gameId`, `name`, `version` |
| Epic | `EOSSDK-Win64-Shipping.dll`, or path under an Epic install | `%ProgramData%\Epic\EpicGamesLauncher\Data\Manifests\*.item` matched by `InstallLocation` |
| itch.io | `.itch\receipt.json.gz` | receipt JSON: title, id |
| None/Manual | fallback | PE VersionInfo |

**Auto-import (FR-1.2):** Steam via `libraryfolders.vdf` → all `appmanifest_*.acf`; GOG via `HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\*`; Epic via `Manifests\*.item`; itch by receipt scan. Import presents a review checklist; nothing is launched, nothing is hooked on import.

**Publisher/version order:** store manifest → PE `CompanyName`/`ProductVersion` → *(opt-in)* Steam `appdetails` lookup, cached 7 days.

## Capability hints (explicitly labelled as such)

Files shipped with a game tell us what it *supports*. These populate a **"Supports"** row in the game header — visually distinct from the measured per-session chips, and never mixed with them.

- DLSS SR `nvngx_dlss.dll` · DLSS-G `nvngx_dlssg.dll` · **Ray Reconstruction `nvngx_dlssd.dll`**
- Streamline `sl.interposer.dll`, `sl.dlss_g.dll`, `sl.reflex.dll`
- FSR `ffx_fsr2_*.dll`, `ffx_frameinterpolation_*.dll`, `amd_fidelityfx_*.dll`, `ffx_api*.dll`
- XeSS `libxess.dll`, XeFG `libxess_fg.dll`
- DXR-capable: `d3d12.dll` usage + RT-capable GPU (capability only — says nothing about the game)

The UI wording is deliberate: **"Supports DLSS-G"** (capability, from files) versus **"Frame Generation: DLSS-G ×1.9"** (measured, from this session). Users conflating these is exactly the confusion the old design created.

## Anti-cheat pre-scan (static)

Before a game is ever launched with hooking enabled, the static scan checks for anti-cheat SDKs shipped alongside it (`EasyAntiCheat/` directory, BattlEye binaries, EOS anti-cheat components, etc.). A hit **disables the hooking toggle for that game entirely** in the UI, with an explanation — the user cannot enable it, so the guard never even has to fire at launch. Prevention beats interception.

**Implemented in the native guard, not here** (`fl_prescan.cpp`, exposed as `FlStaticPreScan`). It uses the same `MatchName` and the same rules file as the injection guard, matching directory names against the `anticheat.directories` group and file names against `anticheat.files`. Nothing managed matches a blocklist (§S15 item 1).

Three things this section previously implied that are not true, and are worth stating:

- **The UI answer is advisory.** It decides whether the toggle is *offered*; it does not gate injection. The same scan runs a second time inside the guard's chokepoint against a directory derived from the target's own pid, so "prevention beats interception" is a convenience, not the enforcement.
- **A hit disables the toggle; "could not scan" must not.** The scan is tri-state. `Reason::kPreScanFailed` — directory absent, unlistable, past a bound, or behind a reparse point — is *neither* a hit nor a pass. It must surface as "could not verify", distinct from "anti-cheat found": disabling the toggle on it would be a false refusal with no appeal, and clearing it would be a fail-open.
- **The token list is thin.** `directories` and `files` carry three tokens today. Widening needs verified names; a guessed token fails closed by never firing, which is a silent hole (`19_SAFETY` §Blocklist seed).

## Caching & privacy constraints

- Detection results cached per game; refreshed when exe timestamp/size changes or `rulesVersion` changes.
- Strings scans bounded to 8 MB, local only.
- Module enumeration uses read-only handles (`PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`); never a handle with write access outside the injection call itself.
- No game memory is read for detection purposes — all runtime facts come from arguments to APIs we hooked (CLAUDE.md rule 4).
