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
| `EtwFrameSource` | 2 | Bundled PresentMon console binary, CSV over stdout — unchanged from the previous design, now a fallback |
| `MockFrameSource` | dev | Synthetic frames incl. simulated FG/upscaler/RT records; `FL_MOCK=1` |

Tier selection per launch:

```
hookingEnabledForGame && guardPasses && injectionSucceeds   → Tier 1
otherwise, if ETW available (needs elevation)               → Tier 2
otherwise                                                    → Tier 3 (duration + sensors only)
```

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

`AntiCheatGuard.Check(pid)` runs **before every injection** and every 30 s during a hooked session, exactly as specified in `19_SAFETY_AND_ANTICHEAT.md`. Its result is authoritative: no code path may inject without a passing check, and there is no override. Failures produce a structured `CaptureRefused { reason, signal }` surfaced to the UI with plain-language text.

## Ring draining

- Map `Local\FrameLedger.Ring.<pid>`; validate layout version + build id against our own. Mismatch (app updated while game running) → refuse to attach, tell the user to restart the game.
- Drain every 100 ms: read `writeIndex` (acquire), copy new records, validate `seq` before/after each (skip torn), advance the read index.
- `droppedRecords` from the header is accumulated onto the session's data-quality counter. A non-zero value means the Agent stalled — log it, surface it as a session warning, never silently accept it.
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
4. Compress raw series (Deflate): frametimes `float32[]`, per-frame flags `byte[]`, per-frame render-res `uint16[]` pairs (only when they vary), sensor series `float32[]`.
5. Write session row + blobs in one SQLite transaction; delete the `.partial`.

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
