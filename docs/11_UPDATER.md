# 11 — Updater

Velopack, feeding from GitHub Releases of `{{REPO_URL}}`. Stable channel only in v1.

## Flow

- **Startup silent check** (if enabled): `UpdateManager.CheckForUpdatesAsync()` 5 s after UI idle. Offline/any failure → log `Information`, no dialog (NFR-10). Update found → non-blocking toast "Update vX.Y.Z available" → Downloads in background → "Restart to update" button (never auto-restart; the user may be mid-capture — if a capture is active, defer the prompt until session end).
- **Manual check** (Help → Check for updates): progress dialog; all failures produce the mapped dialog below.
- **Never apply an update while a game is hooked** (FR-12): the Overlay DLL on disk must not change under a running game, and a mid-session swap would invalidate the ring layout handshake. If a capture is active, defer the prompt and the apply until the session ends.
- Apply: `WaitExitThenApplyUpdates` on restart. Agent is stopped via `Shutdown` before applying and the scheduled task action path is re-validated after update (Velopack keeps a stable `current` path, but verify in P4 and re-register task if the action path changed).
- Release notes (GitHub release body, Markdown) rendered in the update dialog.

## Error mapping (FR-12)

| Condition | Dialog (resx key) | Extra behavior |
|---|---|---|
| HTTP 404 | `Update_Err404` — "No release feed found. The repository may have moved." | Link to releases page |
| HTTP 403 / 429 | `Update_Err_RateLimited` — "GitHub rate limit reached. Try again in {n} minutes." | Parse `Retry-After` / `X-RateLimit-Reset` when present; send `If-None-Match` ETags on checks to conserve the anonymous quota |
| HTTP 5xx | `Update_Err_Server` — "GitHub is having trouble. Try again later." | |
| Timeout / DNS / no network | `Update_Err_Offline` | Silent on auto-check |
| Package hash mismatch | `Update_Err_Corrupt` | Auto-retry once, then dialog |
| Unknown | `Update_Err_Unknown` + exception logged | "Report a bug" shortcut |

Detection-rules updates share this client, but the **`anticheat` block is fetched and applied on its own schedule regardless of the user's rules-update preference** (`05_DETECTION` FR-7.3) — a user who turned off rules updates must still receive new anti-cheat entries.

All update HTTP goes through one `GitHubHttpClient` (also used by rules updates) with: UA `FrameLedger/{version}`, 10 s timeout, ETag cache in `settings`, single retry with jitter for transient failures.

## Unsigned releases

Binaries are not code-signed (project policy). Consequences and mitigations, documented in README and the update dialog footer:
- SmartScreen warning on first run of a new version — expected; SHA-256 checksums (`SHA256SUMS.txt`) published with every release; CI prints them into release notes.
- Velopack delta packages reduce download size; full package fallback automatic.
- Never bypass or suppress OS warnings programmatically.

## Versioning

SemVer `MAJOR.MINOR.PATCH`. Tag `vX.Y.Z` triggers the release workflow (13_CI_CD). `MAJOR` bumps for DB schema or IPC protocol breaks; migrations must cover every released `MAJOR-1` version.
