# FrameLedger — Privacy Policy

**Version:** 2.0-draft · **Effective:** {{RELEASE_DATE}}

**Short version: everything stays on your PC. FrameLedger has no accounts, no telemetry, no analytics, and never uploads your data anywhere.**

## 1. Data the app stores — locally only

Stored in `%LOCALAPPDATA%\FrameLedger` on your device:

- Your game library entries (names, executable paths, cover art, metadata you or the app filled in).
- Performance sessions: frame timing series, computed statistics, hardware sensor series (temperatures, load, memory usage), session duration, crash flags, tags and notes you write.
- A hardware snapshot per session (CPU/GPU model, driver version, RAM size, OS build, display mode) used for the "what changed between sessions" feature.
- App settings, logs (7-day rotation, including logs written by the component loaded into games), and, after a crash of the app itself, crash dump files.
- Which games you enabled code injection for, and when you consented.

This data never leaves your device unless **you** export it or attach it to a bug report yourself. Deleting the app offers deletion of this folder; you can also delete it manually at any time.

## 2. Network connections the app can make

FrameLedger makes **no network connections except the following**, each visible in settings:

| Purpose | Endpoint | When | Data sent |
|---|---|---|---|
| Update check | GitHub Releases API for `poli0981/frameledger` | At startup (can be disabled) and on manual check | Standard HTTP request metadata only (no identifiers beyond your IP as seen by GitHub) |
| Safety list update | Raw file on the project repository | Weekly, and **regardless of your other rules-update settings** | Same as above — this list is what stops FrameLedger injecting into newly-protected games, so it is not optional |
| Update download | GitHub release assets | Only after an update is found | Same as above |
| Detection-rules update | Raw file on the project repository | Weekly check (can be disabled) and manual | Same as above |
| Store metadata lookup | Steam public store API | **Off by default — opt-in** | The Steam AppID of a game you added |

> ⚠ **Accuracy audit, 2026-08-04: the "Safety list update" and "Detection-rules update" rows describe outbound requests the software does not make.** There is no HTTP client anywhere in the product; the anti-cheat blocklist currently ships with the build and is installed locally on first run (`docs/20_OPEN_QUESTIONS.md` §S20 — the seed half is done, the feed half is not). **Over-disclosure is a defect in this document too**: a privacy policy that lists a transmission which never happens is as wrong as one that omits a transmission which does. Either the fetch exists at first release or these rows change.

GitHub's own privacy practices apply to requests it receives: <https://docs.github.com/privacy>. The Steam lookup, if enabled, is governed by Valve's policies.

## 3. Bug reports — always manual

The "Report a bug" feature builds a zip file **on your device**, shows you exactly which files it contains, and lets you inspect them. Nothing is transmitted by the app; **you** decide whether to attach the file to a GitHub issue in your browser. Logs are redacted (user directory paths removed) before bundling. Optional items (crash dumps, last-session metadata) are included only when you tick their checkboxes.

## 4. What the app can technically observe

To do its job, FrameLedger observes: which of *your tracked* executables are running and their process trees; the parameters your games pass to the graphics APIs it intercepts (presentation, upscaling, ray tracing, pipeline creation); video-memory usage reported by the graphics runtime; loaded module names of a game being captured (for detection and for the anti-cheat safety check); and hardware sensor values from your graphics driver's own libraries.

It does **not** read game memory outside those API parameters, and does not read game saves, chat, input content, network traffic, or anything unrelated to performance measurement. It records only for games you added to your library, and injects only into games you individually enabled.

All of this stays on your device. None of it is transmitted anywhere.

## 5. Children

FrameLedger is a technical utility, provides no communication features, and collects no personal information from anyone.

## 6. Changes

Material changes to this policy increment its version; the app will show the updated document for review before continuing.

Contact: <contact@poli0981.dev> · Developer: <https://poli0981.dev/> · Project: <https://github.com/poli0981/frameledger>
