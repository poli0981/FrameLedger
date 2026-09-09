# 04 — Capture orchestration (Agent side)

The Agent decides *whether* to capture, *how* (tier), starts it, drains data, and finalizes sessions. The in-process mechanics live in `17_HOOK_ENGINE`.

## Frame source abstraction (retained from the ETW design — and now with ONE implementation, which is the point)

~~```csharp
public interface IFrameSource : IAsyncDisposable
{
    CaptureTier Tier { get; }
    IAsyncEnumerable<FrameEvent> StreamAsync(CaptureTarget target, CancellationToken ct);
}
```~~

> **Never written, and replaced rather than built — 2026-09-09 (P2 PR-C).** No `IFrameSource`,
> `FrameEvent` or `IAsyncEnumerable` stream ever existed in a `.cs` file; what ran every hooked
> session since 2026-08-06 is a *loop over a sink*, and that is the shape promoted into the
> shipped assemblies:
>
> ```csharp
> public interface ICaptureSink : IDisposable           // Application.Capture — what the loop needs from the ring
> {
>     FlWriterState WriterState { get; }   FlShmHandshake Handshake { get; }
>     long TotalDropped { get; }           long TotalGaps { get; }
>     DrainResult Drain(Span<FlFrameRecord> into, IList<ulong> gapIndices);
>     void PublishGuardResult(uint completedEvaluations, bool unhookRequested);
>     void SetPaused(bool paused);         void RequestLogFlush();
> }
> public sealed class CaptureSession(...)              // the loop: gate → inject → attach → drain, until something ends it
> {
>     Task<CaptureOutcome> RunAsync(normalisedExePath, observed, payloadPath, ct);          // attach mode
>     Task<CaptureOutcome> RunLaunchedAsync(normalisedExePath, observed, payloadPath, args, ct); // launch mode
> }
> ```
>
> `CaptureSession` takes ports, not delegates, so the Agent's composition root supplies the
> adapters: `IRingAttacher` (`Infrastructure.Capture.ShmRingAttacher` over `ShmRingReader.TryAttach`
> with `FlGuardBuildId`), `ITargetLivenessSource` (`HeldProcessLivenessSource`, §S29(e)),
> `ITargetResolver` (`TargetResolver`, path only — never a pid), `IProcessLauncher`
> (`ProcessLauncher`, launch mode), `IRuntimeModuleSnapshot`, `INgxDriverProbe`,
> `IExecutableMarkerScan`. `ShmRingReader` itself gains no interface and no second consumer
> (`NoSecondRingReaderTests`). A **pure move**: the loop's ordering rules, its tests and the
> capture host's report are byte-for-byte what they were; the host is now a thin shell that
> composes the same objects. What the table below used to call `HookedFrameSource` is this.
> `MockFrameSource` / `FL_MOCK=1` remains specified and unwritten (CLAUDE.md §Dev mode); when it
> exists it is a second `ICaptureSink`, not a second loop.

| Implementation | Tier | Notes |
|---|---|---|
| ~~`HookedFrameSource`~~ `CaptureSession` + `ShmCaptureSink` | 1 | Injects (or relies on the Vulkan layer), attaches to the ring, drains at 10 Hz under the 30 s re-scan |
| ~~`EtwFrameSource`~~ | — | **Deleted from the design, 2026-08-28.** Tier 2 is not a frame source: it produces no frames. PresentMon was dropped (§S31 row P2), no mechanism replaced it (§G), and a port with no adapter on a tier that measures nothing is a row that only invites someone to implement it. The interface keeps its shape for `MockFrameSource` and for whatever Vulkan needs |
| `MockFrameSource` | dev | Synthetic frames incl. simulated FG/upscaler/RT records; `FL_MOCK=1` — unwritten; would be a second `ICaptureSink` |

Tier selection per launch:

```
hookingEnabledForGame && guardPasses && injectionSucceeds   → Tier 1
otherwise                                                    → Tier 2
```

**Two tiers, and the second one is not a measurement.** Owner decision 2026-08-28,
and it is the honest shape once PresentMon was dropped (§S31 row P2, then §G):
there is no no-injection *measurement* mode, so there is nothing for a middle rung
to hold. What was Tier 3 is now Tier 2.

| Tier | Mechanism | Yields |
|---|---|---|
| **1** | Injected hooks, or the Vulkan layer | Everything: frame times, FPS and lows, render/output resolution, upscaler + preset, FG, RT/PT evidence, per-process VRAM, PSO stutter, Reflex latency |
| **2** | None | **Session duration, whatever hardware telemetry this machine can provide, and the REASON there is nothing else.** Every measured field reads `N/A` (FR-4.9) |

**Frame times are Tier-1-only now, and that is a reduction in what this product
promises.** It is stated here, in `README` §Capture tiers and in `03_METRICS`
§Accuracy budget rather than in a footnote, because the previous ladder promised
frame times without injection and a reader who remembers that will otherwise assume
it still holds.

**The reason is the payload of Tier 2**, not a consolation attached to it. A session
that records duration and sensors while saying nothing about *why* it measured
nothing is indistinguishable from a broken capture. `sessions.capture_notes` and the
`CaptureRefused` / `CaptureDegraded` messages carry it: guard refused (with which
signal fired), hook failed, hooking not enabled for this game, or a 32-bit target the
x64 Overlay structurally cannot enter.

> **NO CAPTURE TIER NEEDS AN ELEVATED AGENT ANY MORE, and that closes something.**
> The elevation requirement came from ETW trace sessions, and this ladder has no ETW
> rung. `19_SAFETY`, `08_UI` §First-run and the Disclaimer previously had to explain
> that an unelevated Agent could not reach the fallback; there is nothing to explain.
> Elevation stays **optional** and unlocks exactly what ADR-9 always said: CPU/board
> temperatures (LHM + PawnIO) and attaching to games that themselves run elevated.
>
> ~~Whether a documented one-time "add me to Performance Log Users" setup step, or the
> PresentMon Service, can restore genuinely elevation-free Tier 2~~ — moot: the
> question was about reaching a tier that no longer exists. The measurements that
> killed it are in `spike-notes` §11 and `20_OPEN_QUESTIONS` §M2/§M6, which is where
> they belong; this file no longer restates them.

The chosen tier is recorded on the session and surfaced in the UI. A Tier-1 attempt
that fails **degrades to Tier 2 for the session without interrupting the user's
game**, and raises a one-time notification explaining why — users must not lose data
fidelity without knowing, and under this ladder the loss is total rather than partial,
which makes the notification more important than it was, not less.

## Process watcher

- 1 Hz snapshot via `CreateToolhelp32Snapshot` (CsWin32): pid, ppid, exe path (`QueryFullProcessImageName`).
- Watchlist match on normalized full path (`GetFinalPathNameByHandle` — junctions/symlinks), filename fallback with a stale-path warning badge.
- Process tree assembled from ppid chains; the **capture target** is the descendant that actually presents. In launch mode we know it; in attach mode we wait for the first ring handshake. ~~or (Tier 2) elect the PID with the most presents in the first 10 s~~ — there is no Tier-2 present stream to elect from. Re-elect if the presenting PID dies while the tree lives (level-transition relaunches).

## Launch mode vs attach mode

**Launch mode (preferred).** User starts the game from FrameLedger (or FrameLedger is set as the launch wrapper): `CreateProcess(CREATE_SUSPENDED)` → guard → inject → `ResumeThread`. Catches swapchain creation and upscaler init, which attach mode can miss entirely — a game that creates its DLSS feature during startup will otherwise report `upscaler = unknown` for the whole session.

> **Built 2026-09-06 (P1 item 2) as "inject late", which is the only shape §S1 left possible, and the
> sentence above is kept as the intent it does not quite describe.** A `CREATE_SUSPENDED` target has
> loaded nothing and the module scan *fails* against it (`ERROR_PARTIAL_COPY`, §S1 measured), so the guard
> cannot run before the loader has. The built order is **start → the guard WAITS → inject**: the host
> starts the consented executable (`ProcessLauncher`: working directory beside the exe,
> `FRAMELEDGER_ENABLE_VK_LAYER=1` in its environment for the Vulkan layer, the handle held from birth so
> the pid cannot recycle), then asks the guard through its new waiting entry, `FlGuardedInjectWhenReady`,
> which polls the loader every 50 ms — through the module seam, matching **no** blocklist — until
> `dxgi.dll` with `d3d11.dll` or `d3d12.dll`, or `opengl32.dll`, or `vulkan-1.dll` is mapped, and *then*
> runs every check and injects exactly as attach mode does. The poll decides **when** the full scan runs,
> never whether it passes. **Vulkan wins over a D3D or OpenGL runtime beside it, and since 2026-09-10 over
> one mapped a poll ahead of it:** a static D3D/OpenGL import is in the module list from the first
> instruction while a Vulkan title's loader arrives milliseconds later, so the guard reads once more, one
> interval later, before it commits to the injecting branch (measured on the harness's own `--vulkan` mode
> the moment PR-D's warmer host reached the poll 20 ms sooner; `fl_guard` pins it in both directions). A target that exits first, or maps no runtime inside the budget (60 s in the
> host), answers `LaunchTargetExited` / `LaunchNoPresentationRuntime` with nothing injected; a launcher
> that spawns the real game and quits lands on the first, and re-electing the descendant is the Agent's
> (P2, §Process watcher). **The host never terminates what it launched**: any refusal after the start
> leaves the title running unhooked, which is Tier 2.
>
> **What it measures about itself, which is the input §S1 deferred on.** The report prints the wait
> (`launch: the guard injected N ms after the process was started`) and `FlWriterState.dxgiPresentsBeforeHook`
> — DXGI's own count, read at the first hooked present on the first hooked chain, of presents completed
> before the hook was in. **0 means nothing ran unhooked on that chain**; N is the early-init cost, per
> title, from the title itself. On `hook-harness` (device at startup, presenting every 8 ms) the real-seam
> fixture injects within one poll and reports the ~1 s of presents the fixture ran before asking — a
> measurement of the fixture, not a bound on a title. What launch mode can *never* see is anything before
> its own injection, and a swapchain created before the runtime-mapped poll fired is exactly that; the
> number says how much.
>
> The CaptureHost's verb is `launch --exe <path> [--args "<game's own arguments>"] [--seconds <n>]`; the
> consent record, the gate and the payload are the ones `capture` uses, reached one process later.

**Attach mode.** Game launched from Steam/GOG/Epic normally; watcher sees it, guard runs, inject. Feature hooks install late, so early-init facts may be missed; the Overlay compensates by re-reading state on the first `EvaluateFeature` call it does observe, and the session is flagged `late_attach = true` so the UI can note that startup-time settings may be incomplete.

Steam users can also set FrameLedger as a launch option wrapper; documented in the UI rather than automated (never modify a user's Steam config for them).

## The guard

The guard is **native** (`20_OPEN_QUESTIONS` §S13(a)) and the Agent reaches it through `IAntiCheatGuard`, a **thin P/Invoke facade over the single implementation** — never a second one. Two blocklist matchers that can disagree is a fail-open by construction: the day they diverge, one is wrong and nothing says which. Nothing managed parses rules or matches a blocklist, and a test asserts it (§S15 item 1).

The facade exposes exactly two operations, and neither hands out a clearance:

- `GuardedInjectAsync(pid, payload)` — runs every pre-injection check and, only on a pass, injects. There is no overload that skips the checks and no way to supply evidence; the guard collects its own, so a caller can ask but only the guard answers (§S13(b)).
- `EvaluateAsync(pid)` — the same checks with no injection, for the 30 s in-session re-scan. It cannot be used to pre-authorise anything: it takes no payload and returns no token, so acting on a pass means calling `GuardedInjectAsync`, which re-collects.

Its result is authoritative: no code path may inject without a passing check, and there is no override. Failures produce a structured `CaptureRefused { reason, signal }` surfaced to the UI with plain-language text; the reason codes mirror `fl::guard::Reason` and a test proves the two have not drifted.

What the Agent checks **before** asking the guard is the thing the native side structurally cannot see: **per-game consent** (CLAUDE.md rule 1). Consent is a record of something a human did, so `HookedCaptureGate` refuses an unconsented or un-enabled game without the guard ever being called.

> **Where it lives, corrected 2026-08-06.** This sentence said "lives in SQLite", and was the
> only line in the tree that said where consent lives — while no database, no `games` table
> and no consent writer existed in any `.cs` file (§S27). The port is
> `Application.Consent.IGameConsentStore`; ~~**SQLite is P2's adapter for it** and is still
> unwritten, and `06_DATA_MODEL` declines to guess `0001_init.sql` before its consumers exist.
> The only adapter today is a file-backed store inside the unshipped `FrameLedger.CaptureHost`,
> whose record dies with the build tree and is **not** a migration source.~~ **Written 2026-09-09
> (P2 PR-B): `Infrastructure.Persistence.SqliteGameConsentStore` over the `games` table of
> `0001_init.sql`, with the file store deleted rather than migrated.** The unshipped host now opens
> its OWN `ledger.db` beside its binary (HANDOFF §P2 decision D5), so the Agent stays the sole owner
> of `%LOCALAPPDATA%\FrameLedger`; the Agent's console verb (PR-F, D4) and the UI's dialog (P3) are
> the producers that stamp from the Agent's clock, which is the property the paragraph below says a
> file could not uphold.
>
> **A file cannot uphold the Agent-stamp property, and saying so is the honest position.**
> `19_SAFETY` §User-facing consent requires the timestamp to be *"stamped by the Agent, never
> supplied by a client"*, and `07_IPC` §The pipe is not a trust boundary makes the Agent's own
> clock the attestation. A file on disk is by construction supplied by whoever can write it.
> What stands in for the property until SQLite exists is narrower and is stated rather than
> implied: the store belongs to a binary `12_BUILD` does not publish, its record carries a
> **disclosure provenance whose default means no disclosure was shown**, and every anti-cheat
> check still runs afterwards — this is the opt-in half, never the anti-cheat half.
>
> **The request the gate evaluates has exactly one producer**, `HookRequest.FromConsent`.
> That is not organisation: the type was a `record` with `init` members, so
> `new HookRequest { HookEnabled = true, ConsentedAt = DateTimeOffset.UtcNow }` satisfied the
> gate from any call site — the synthesis §S27 named and rejected. It is now get-only behind a
> private constructor, so the expression does not compile.

## Ring draining

- Map `Local\FrameLedger.Ring.<pid>`; validate layout version + build id against our own. Mismatch (app updated while game running) → refuse to attach, tell the user to restart the game.

  > **"Our own" needed a source, and this document never named one** — which is the
  > gap §S23-1 was raised for: the check was specified in two documents and could not
  > run in either direction. It is `FlGuardBuildId`, exported by the guard DLL the
  > Agent already loads by absolute path from our install directory. **Not** the
  > Overlay's `FlGetBuildId`: reaching that would mean `LoadLibraryW`-ing the payload
  > into the Agent, which starts its init thread and creates a ring under the Agent's
  > own pid.
  >
  > Implemented as `FrameLedger.Shared.ShmHandshakeValidator`; `07_IPC` §Protocol
  > rules carries the five refusals and why `layoutVersion == 0` means *retry*
  > rather than *restart the game*.
- Drain every 100 ms: read `writeIndex` (acquire), copy new records, validate `seq` before/after each (skip torn), advance the read index. Protocol in `07_IPC` §Protocol rules.
- **Each drain tick also samples whether the target owns the foreground window**, and the session carries the pair `(drainTicks, foregroundTicks)`. Frame generation stops while a title is unfocused, so a window spanning an alt-tab averages two configurations — measured 2026-08-16 on Cyberpunk 2077, where that produced an achieved `presents / batch` of 1.84 against a title configured for ×2, an 8% error with no diagnostic anywhere.

  > **Out of process, not in the hook, and §S30 suggested the opposite.** That entry says "focus loss is observable in-process"; it is, and doing it there would cost a syscall on the present path or a second cached flag for no gain. `GetForegroundWindow` + `GetWindowThreadProcessId` at the existing 10 Hz tick costs nothing, reads nothing belonging to the target, and needs no record byte.
  >
  > **The PAIR, because zero is not a finding.** A process owning no top-level window at all — `hook-harness` presents to a composition swapchain and has none — is unfocused on every tick of every run. Reporting that as focus loss would fire on every integration run and teach the reader to ignore the line, which is the could-not-look/looked-and-found-nothing collision `FlRtTier` and `upscalerQuality` already exist to avoid.
  >
  > **It is attribution, never the guard.** What refuses a mixed window is `FgWindow.BatchRefusal`, computed from the records alone, so a caller that never wires focus still cannot publish an averaged ratio. Focus is what lets the report name the cause.
- **Dropped records are computed here, not read from the header.** The Overlay has no reader index and cannot know whether a slot it overwrites was consumed. The Agent owns the read index, so it owns the accounting: when `writeIndex - readIndex > capacity`, add the excess to the session's data-quality counter and resume at `writeIndex - capacity`. A non-zero value means the Agent stalled for over ~16 s — log it, surface it as a session warning, never silently accept it.

  > **That sentence is true only because the read index is seeded from the writer at
  > attach**, and neither this document nor `07_IPC` said so until 2026-08-05.
  > Seeded at 0 instead, a drain attaching to a ring at `writeIndex` 1,000,000
  > reports 999,992 drops on its first pass — so the warning would fire on every
  > attach to a game that had been running a while, and its magnitude would be set
  > by the game's uptime rather than by anything the Agent did. Records already
  > published at attach are counted separately as `RecordsBeforeAttach`; that is not
  > a stall and must not raise this warning.
- **A torn record is a gap, not a skipped frame.** Silently dropping it merges two frame times into one double-length interval, i.e. fabricates a stutter. Record an explicit gap at that index; `03_METRICS` excludes gap-adjacent intervals from frame-time statistics.
- Records go into an in-memory buffer (`ArrayPool<FlFrameRecord>` segments). Every 60 s, a crash-safety flush writes raw buffers to `%LOCALAPPDATA%\FrameLedger\tmp\<sessionGuid>.partial`.

## Threading model

> **Written 2026-09-09 (P2 PR-C), because `20_OPEN_QUESTIONS` §G listed it as unspecified and the
> loop had just become shipped code.** Four kinds of thread, one owner per shared object.

| Thread | Cadence | Owns | Touches |
|---|---|---|---|
| **Session loop** (`CaptureSession.RunAsync`, one async loop per session) | `Task.Delay(100 ms)` drain ticks; the 30 s guard scan is awaited inline | The ring reader, for the life of the session — drain, `PublishGuardResult`, `SetPaused`, `RequestLogFlush` — and the record buffer it fills | Samples foreground per tick, drains the telemetry queue per tick (PR-D), writes `.partial` itself (PR-D) |
| **Telemetry** (`fl-telemetry`, `TelemetryPoller`, one per session) | 1 Hz, never faster than 500 ms | Its own `ConcurrentQueue` of `TelemetrySample` | Reads the composite source only; L2 has a thread of its own inside the library, and the composite is read from this thread and no other |
| **Watcher** (PR-F, `PeriodicTimer(1 s)`) | 1 Hz process snapshot | A `Channel<WatcherEvent>` with the orchestrator as single consumer | Never the ring, never the database |
| **Finalize** (PR-D) | Once, on the session loop's task | The one SQLite connection, behind a `SemaphoreSlim(1)` | `ApplicationStopping` cancels the loop; finalize gets a grace window, then the `.partial` stays for recovery |

Rules the table encodes: **nothing but the session loop touches `ShmRingReader`** — no lock is
added to the reader, because there is no second party; a diagnostic wanting a look at the ring
is a second consumer on a single-consumer ring and is refused by review and by
`NoSecondRingReaderTests`. The ring holds ~16 s at a game's present rate, so a 100 ms cadence
has two orders of magnitude of headroom, and `TotalDropped > 0` stays a session warning rather
than a tuning knob. There is no UI thread in P2; the pipe reader (P3) joins as one more
`Channel` producer, not as a reader of anything above.

## Telemetry poller

1 Hz on its own thread: `CompositeTelemetrySource.TryRead` (`18_GPU_VENDOR_APIS` — DXGI/PDH baseline, LibreHardwareMonitor sensors, NVAPI extras on NVIDIA) + optional CPU sample when the Agent is elevated and PawnIO is present. Never called from the game process, never faster than 500 ms. Samples carry the session QPC epoch for timeline alignment.

## Session recorder — state machine

```
Idle → Detected(pid) → Guarded → Injecting → Capturing → Finalizing → Saved
                          │           │           ├→ Unhooked(safety)   → Saved (exit_status=unhooked_safety)
                          │           └→ Degraded(Tier 2)               → Capturing
                          └→ Refused → Degraded(Tier 2) or Idle
                                                  └→ Interrupted (agent restart mid-session) → recovered from .partial
```

**Finalizing:**
1. Signal the Overlay to stop writing (control flag), drain the ring one last time.
2. Build **segments** from resolution/upscaler changes (`03_METRICS` §Upscaling).
3. Compute all aggregates.
4. Compress raw series (Deflate) — **the full `frame_blobs` set in `06_DATA_MODEL`**, not a subset: frametimes `float32[]`, frame flags `byte[]`, RT flags `byte[]`, render/output resolution `uint16[]` (two pairs per frame, only when either varies), dispatch-rays volume `uint32[]`, PSO counts `uint16[]`, per-process VRAM `uint32[]`, Reflex latency `uint32[]`, and the sensor series `float32[]`. Anything skipped here becomes a CSV column the exporter cannot fill (`03_METRICS` §Export schema) — Reflex latency was previously omitted from this step while `06_DATA_MODEL` declared a column for it.
5. Write session row + blobs in one SQLite transaction; delete the `.partial`.

**Vulkan sessions do not pass through `Injecting`.** The layer is loaded by the
Vulkan loader when the Agent launches an opted-in game, so the path is
`Detected(pid) → Guarded → Capturing`, skipping injection entirely.

> **Built 2026-09-06 (P1 item 3), through launch mode.** The guard's waiting entry classifies the
> launched target by what it mapped: `vulkan-1.dll` with no D3D or OpenGL runtime runs the **full** guard
> and, on a pass, injects nothing — `TargetIsVulkanLayered`, which the loop reads as "attach to the
> layer's ring" with launch mode's budget (the ring appears at the title's first `vkCreateDevice`). The
> CaptureHost's `launch` gives the process the layer's environment (`VkLayerLaunchEnvironment`:
> `VK_ADD_IMPLICIT_LAYER_PATH`, the enable variable, the enable-list line for the session); a D3D title
> ignores all of it. One ring per process, the first creator owns it (`fl_shm_host.h`). Attach mode on a
> running Vulkan title stays Tier 2: only a launch can set the loader's environment. This is
stated because the state machine above, read literally, meant the 30 s guard
re-scan — which `19_SAFETY` calls the most important runtime behaviour in the
capture layer — was not specified to run for Vulkan at all. It runs for **every
Tier-1 session**, and `Unhooked(safety)` is reachable from a layered session as
well; for Vulkan it means the layer goes passthrough rather than removing hooks,
because a layer cannot leave a running game's loader chain.

**Discard rule:** duration < min session length (default 30 s) → discard silently (log only).

> **Built 2026-09-10 (P2 PR-D): `Application.Recording`.** The state machine above is the intent; what
> runs is one class per box. `SessionRecorder` owns a session end to end — the row's identity and time
> base (`session_guid`, `qpc_epoch`/`qpc_frequency` from `TimeProvider.GetTimestamp`, which is QPC), the
> `games` and `hardware_snapshots` rows, the telemetry poller for the session, the `.partial` from before
> the first record to after the last, the loop, the classification, the finalize, the crash policy. The
> loop (`CaptureSession`) owns `Guarded → Injecting → Capturing` and reports them as ONE outcome, so what
> the recorder observes and writes as breadcrumbs is `started` (the file exists) → `attached` (the ring is
> ours; Tier 1 from here) → `ended <reason>` → `finalizing <exit_status>`; a loop that never attached is a
> Tier-2 row with the reason in `capture_notes` (`tier2: attach=…; guard=<reason>/<family>/<signal>`).
> Everything the recorder does runs on the loop's task through `ICaptureObserver`: after every drain it
> drains the poller's queue and, on the flush interval, appends to the `.partial` — the loop stays the
> ring's only reader (§Threading model). **Finalizing as built:** step 1 is the loop's own last drain;
> steps 2–3 are `SessionAggregator` over Domain's calculators and `FgLadder` (the identity and withhold
> rules, moved out of the host's report so the row and the report read one implementation); step 4 is the
> FULL blob set through `ISeriesCodec` — every column of `frame_blobs` that any record claimed, `latency_us`
> included, and one `sensor_blobs` series per field any sample carried, aligned to `t_ms` with −1 where a
> tick had no reading; step 5 is `ISessionRepository.InsertFinalizedAsync` (one transaction) then the
> retention sweep, and the `.partial` is deleted only on `Saved` or `Discarded`. `Interrupted` is
> `PartialRecovery`'s alone: at startup every pending file becomes an `interrupted` row from its valid
> prefix, or is dropped for one of three stated reasons (already stored, too short, unreadable). The
> unshipped host lowers the discard threshold to 5 s for bounded operator captures; the Agent keeps 30 s.

## Crash & exit classification

- Normal: presenting PID exit code 0.
- `crashed`: nonzero exit code, **or** an Application Error (1000) / WER (1001) event log record naming the exe within `[start, end + 30 s]`.
- `unhooked_safety`: the guard fired mid-session.
- `degraded`: Overlay self-disabled after faults, or ring layout mismatch mid-session.
- `interrupted`: Agent died mid-session; recovered from `.partial` on next start.

Crash-within-60s-of-injection happening twice for the same game ⇒ **hooking auto-disabled for that game** and the reason stored on the `games` row (`19_SAFETY` §Crash safety). The UI explains it and offers a manual re-enable after the user has, e.g., updated their GPU driver.

> **Built 2026-09-10 (P2 PR-D).** `ExitStatusMapper` is the table above as one function of what the
> session saw: our own safety stop is `unhooked_safety` whatever the process did next; the capture side
> stopping on its own (self-disabled, blocklisted mid-session, supervision lost, hooks never installed) is
> `degraded`; a non-zero exit code — read from the held handle, so it is the target's own — or an
> Application Error (1000) / WER (1001) event naming the executable inside `[start, end + 30 s]`
> (`EventLogCrashSource`, read-only, unprivileged, false when the log cannot be read) is `crashed`; a
> target still running when a bounded capture ends is `normal`. `capture_notes` keeps the fine reason, the
> exit code and the witness (`end=TargetExited; exit_code=-1073741819; crash_event=application_log`).
> `CrashAutoDisablePolicy` counts only crashes inside the 60 s window after the attach — `hook_crash_count`
> is that count and nothing else — and disables on the second; it never re-enables.

## Live progress

`SessionProgress` at 1 Hz to the UI (`07_IPC`): rolling 5 s Native FPS, Displayed FPS, FG factor, current render→output resolution, upscaler + quality, RT active flag, GPU/CPU temp, per-process VRAM, elapsed. Suppressed when no UI client is connected. This is what makes the Dashboard live card genuinely useful — it is showing *measured* settings, not guesses.

## Overhead rules (NFR-1)

| Where | Budget |
|---|---|
| Game process, per present | ≤ 1 µs; no syscall, no allocation, no lock, no logging (`17_HOOK_ENGINE`) |
| Game process, resident | ≤ 8 MB (DLL + ring) |
| Agent, during capture | ≤ 1% of one core average, ≤ 150 MB working set |
| Agent, idle | ~0% (1 Hz process poll only) |
| Measured game FPS impact | ≤ 0.5% vs uninstrumented, verified per release (`14_TESTING` §Hook overhead) |

Ring sizing: at 500 fps a 100 ms drain consumes 50 of 8192 slots (~164× headroom), and the ring holds ≈ 16 s of frames. The number that matters to an implementer is that **second figure** — the Agent can stall for many seconds (a GC pause, a scheduler hiccup, a debugger break) without dropping a record. That is why a non-zero drop count means something went genuinely wrong and must surface as a session warning, never be silently accepted.
