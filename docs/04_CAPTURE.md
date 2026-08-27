# 04 — Capture orchestration (Agent side)

The Agent decides *whether* to capture, *how* (tier), starts it, drains data, and finalizes sessions. The in-process mechanics live in `17_HOOK_ENGINE`.

## Frame source abstraction (retained from the ETW design, now with two real implementations)

```csharp
public interface IFrameSource : IAsyncDisposable
{
    CaptureTier Tier { get; }
    IAsyncEnumerable<FrameEvent> StreamAsync(CaptureTarget target, CancellationToken ct);
}
```

| Implementation | Tier | Notes |
|---|---|---|
| `HookedFrameSource` | 1 | Injects (or relies on the Vulkan layer), maps the ring, drains at 10 Hz |
| `EtwFrameSource` | 2 | ~~Bundled~~ PresentMon console binary, CSV over stdout — unchanged from the previous design, now a fallback. **Not bundled** (owner decision 2026-08-27): the console binary is a local test tool, and how a shipped build reaches Tier 2 is undecided — `20_OPEN_QUESTIONS` §G. `EtwFrameSource` is unwritten either way, so nothing depends on this today |
| `MockFrameSource` | dev | Synthetic frames incl. simulated FG/upscaler/RT records; `FL_MOCK=1` |

Tier selection per launch:

```
hookingEnabledForGame && guardPasses && injectionSucceeds   → Tier 1
otherwise, if the Agent is elevated (ETW needs it)          → Tier 2
otherwise                                                    → Tier 3 (duration + sensors only)
```

**Tier 2 is not unconditionally available, and the product must stop implying it
is.** Controlling an ETW trace session requires the caller to be in
Administrators or Performance Log Users, and joining the latter itself needs
admin. Intel's PresentMon console binary re-launches itself elevated when run
without rights, which for an Agent driven by a 1 Hz watcher (FR-3.1) means a UAC
prompt per capture — unacceptable — or outright failure on a standard account.

So an unelevated Agent that fails Tier 1 lands on **Tier 3**, not Tier 2. The UI
must say so at the moment it happens and in the Agent-setup step, rather than
offering "capture without injection instead" and then silently recording nothing
but duration. `19_SAFETY` §Pre-injection checks, `08_UI` §First-run flow and the
Disclaimer all state the elevation requirement for the same reason.

Whether a documented one-time "add me to Performance Log Users" setup step, or
the PresentMon Service (`15_ROADMAP` v2 backlog), can restore genuinely
elevation-free Tier 2 is `20_OPEN_QUESTIONS` §M6.

The chosen tier is recorded on the session and surfaced in the UI. A Tier-1 attempt that fails **degrades to Tier 2 for the session without interrupting the user's game**, and raises a one-time notification explaining why (users must not lose data fidelity without knowing).

## Process watcher

- 1 Hz snapshot via `CreateToolhelp32Snapshot` (CsWin32): pid, ppid, exe path (`QueryFullProcessImageName`).
- Watchlist match on normalized full path (`GetFinalPathNameByHandle` — junctions/symlinks), filename fallback with a stale-path warning badge.
- Process tree assembled from ppid chains; the **capture target** is the descendant that actually presents. In launch mode we know it; in attach mode we wait for the first ring handshake, or (Tier 2) elect the PID with the most presents in the first 10 s. Re-elect if the presenting PID dies while the tree lives (level-transition relaunches).

## Launch mode vs attach mode

**Launch mode (preferred).** User starts the game from FrameLedger (or FrameLedger is set as the launch wrapper): `CreateProcess(CREATE_SUSPENDED)` → guard → inject → `ResumeThread`. Catches swapchain creation and upscaler init, which attach mode can miss entirely — a game that creates its DLSS feature during startup will otherwise report `upscaler = unknown` for the whole session.

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
> `Application.Consent.IGameConsentStore`; **SQLite is P2's adapter for it** and is still
> unwritten, and `06_DATA_MODEL` declines to guess `0001_init.sql` before its consumers exist.
> The only adapter today is a file-backed store inside the unshipped `FrameLedger.CaptureHost`,
> whose record dies with the build tree and is **not** a migration source.
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
`Detected(pid) → Guarded → Capturing`, skipping injection entirely. This is
stated because the state machine above, read literally, meant the 30 s guard
re-scan — which `19_SAFETY` calls the most important runtime behaviour in the
capture layer — was not specified to run for Vulkan at all. It runs for **every
Tier-1 session**, and `Unhooked(safety)` is reachable from a layered session as
well; for Vulkan it means the layer goes passthrough rather than removing hooks,
because a layer cannot leave a running game's loader chain.

**Discard rule:** duration < min session length (default 30 s) → discard silently (log only).

## Crash & exit classification

- Normal: presenting PID exit code 0.
- `crashed`: nonzero exit code, **or** an Application Error (1000) / WER (1001) event log record naming the exe within `[start, end + 30 s]`.
- `unhooked_safety`: the guard fired mid-session.
- `degraded`: Overlay self-disabled after faults, or ring layout mismatch mid-session.
- `interrupted`: Agent died mid-session; recovered from `.partial` on next start.

Crash-within-60s-of-injection happening twice for the same game ⇒ **hooking auto-disabled for that game** and the reason stored on the `games` row (`19_SAFETY` §Crash safety). The UI explains it and offers a manual re-enable after the user has, e.g., updated their GPU driver.

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
