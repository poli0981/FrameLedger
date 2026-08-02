# 10 — Logging, diagnostics, bug reports

## Serilog configuration

- Sinks: rolling file per process — `logs/ui-.log`, `logs/agent-.log` (`rollingInterval: Day`, `retainedFileCountLimit: 7`, `fileSizeLimitBytes: 10 MB`, `rollOnFileSizeLimit: true`). Console sink in DEBUG builds.
- Minimum level `Information` (`Debug` toggle in Settings → applies live via `LoggingLevelSwitch`).
- Enrichers: process name, version, `SessionGuid` and `GamePid` scoped properties during capture (`LogContext.PushProperty`).
- Template: `[{Timestamp:HH:mm:ss.fff} {Level:u3}] {SourceContext} {Message:lj} {Properties:j}{NewLine}{Exception}`.
- **Never log:** full user paths outside `%LOCALAPPDATA%\FrameLedger` (redact to `<user>`), machine name, any exe arguments of games. A `RedactingEnricher` enforces the path rule.
- Capture hot path logs nothing per-frame; counters summarized at finalize.

## Crash handling (the app's own crashes)

- Hook `AppDomain.CurrentDomain.UnhandledException`, `DispatcherUnhandledException`, `TaskScheduler.UnobservedTaskException` (both processes).
- On fatal: Serilog `Fatal` with full exception → write minidump via `MiniDumpWriteDump` (CsWin32, `MiniDumpWithIndirectlyReferencedMemory | WithThreadInfo`) to `crashdumps/` (keep last 5) → UI shows crash dialog offering the bug-report flow → exit code 1.
- Agent crash mid-session → `.partial` recovery path (04_CAPTURE) on next start finalizes an `interrupted` session.

## In-app log viewer (Logs screen)

Tails the active files (shared read), level filter, text search, pause autoscroll, "Open logs folder", "Export bug bundle". Reads at most last 2 MB per file into the view.

## Bug report flow (FR-13)

1. Entry points: Help → Report a bug, crash dialog, Logs screen button.
2. **Bundle builder** creates `FrameLedger-bugreport-YYYYMMDD-HHmm.zip` in a user-chosen location:
   - `logs/` (last 7 days, redacted copies)
   - `sysinfo.json` (app + agent + **overlay build id**, OS build, CPU/GPU name, driver version, telemetry source, Vulkan layer state, PawnIO present?, PresentMon version, elevation state, locale)
   - `overlay-<pid>-*.log` for the last hooked sessions, plus hook fault details and the ring's dropped/fault counters — these are what make injection bugs diagnosable at all
   - `settings.json` (sanitized — no paths)
   - optional checkbox: last session metadata + aggregates JSON (never raw blobs by default)
   - crash dumps included only when the user ticks the checkbox (size warning shown)
3. **Preview step:** the dialog lists every file included and lets the user open the zip before continuing. Nothing is ever sent automatically.
4. "Open GitHub issue" → launches browser to
   `{{REPO_URL}}/issues/new?template=bug_report.yml&title=[Bug]%20&labels=bug&app_version=…&os=…`
   (short fields only — GitHub URLs cannot carry logs) with on-screen instruction: *"Drag the zip file into the issue description."* Clipboard fallback copies the environment summary as Markdown.
5. `bug_report.yml` issue form (in `.github/ISSUE_TEMPLATE/`) fields: description, steps, expected/actual, app version (prefilled), OS (prefilled), attachments note.

## Diagnostics extras

- `Tools → Agent status…` shows the `HelloAck` capability set (tier availability, telemetry source, overlay build id, Vulkan layer) + last 20 Agent log lines.
- **Never include a game's own logs, saves, or config files** in a bug bundle, even when a crash looks game-related. We ship our logs only.
- `--diag` CLI flag on the App prints environment + capability report to stdout (support requests).
