# 07 — IPC

Three channels, each chosen for its constraints:

| Channel | Between | Transport | Why |
|---|---|---|---|
| **A. Frame ring** | Overlay DLL → Agent | Shared memory, lock-free SPSC | Hot path: no syscall allowed |
| **B. Control block** | Agent ↔ Overlay DLL | Same shared memory, atomics | Tiny, infrequent, must work even if the game is unresponsive |
| **C. Command pipe** | UI ↔ Agent | Named pipe, JSON | Rich, versioned, human-debuggable |

## A + B — shared memory (`Local\FrameLedger.Ring.<pid>`)

Created by the Overlay on init, opened by the Agent. Name is deliberately plain and identifiable (`19_SAFETY` — no obfuscated object names).

Layout:

```
[0x0000] FlShmHeader     (one cache line, written by Overlay)
[0x0040] FlControlBlock  (one cache line, written by Agent)
[0x0080] FlFrameRecord[capacity]   (64 B each, power-of-two capacity)
```

```cpp
struct alignas(64) FlShmHeader {
    uint32_t layoutVersion;      // must equal FL_SHM_LAYOUT_VERSION on both sides
    uint32_t recordSize;         // 64; belt-and-braces against struct drift
    uint32_t capacity;           // power of two
    uint32_t pid;
    char     buildId[32];        // native DLL build id
    std::atomic<uint64_t> writeIndex;
    std::atomic<uint32_t> status;        // init|ready|self_disabled|unhooked
    std::atomic<uint32_t> apiMask;       // which graphics APIs got hooked
    std::atomic<uint32_t> droppedRecords;
    std::atomic<uint32_t> faultCount;
    uint64_t qpcEpoch;           // session time base, shared with sensor samples
    uint64_t adapterLuid;        // which GPU the swapchain is on (multi-GPU selection)
};

struct alignas(64) FlControlBlock {
    std::atomic<uint32_t> pauseRequested;
    std::atomic<uint32_t> unhookRequested;   // 19_SAFETY: set when the guard fires mid-session
    std::atomic<uint32_t> overlayEnabled;    // in-game overlay draw toggle (v1.1)
    std::atomic<uint32_t> agentHeartbeat;    // Agent bumps every second
};
```

**Security:** the mapping is created with a DACL granting access only to the **current user's SID**; `Local\` namespace keeps it session-scoped. No `Global\` objects (would need admin and would be visible across sessions for no benefit).

**Protocol rules**

- Writer (game): seqlock per record — bump `seq` odd, fill 64 bytes, bump `seq` even, `writeIndex.store(release)`. Overwrite-oldest on wrap; increment `droppedRecords` when overwriting unread data.
- Reader (Agent): `writeIndex.load(acquire)`, copy, validate `seq` unchanged and even, skip torn records. Drain every 100 ms.
- Version handshake: Agent compares `layoutVersion` + `recordSize` + `buildId` against its own. **Mismatch → refuse to attach**, tell the user to restart the game (this happens when the app updates while a game is running).
- Heartbeat: if `agentHeartbeat` stops advancing for 60 s, the Overlay keeps writing (harmless) but stops any overlay drawing and flushes its native log — the Agent may have crashed.
- If `unhookRequested` is set, the Overlay disables hooks within one frame and sets `status = unhooked`. This path must be the fastest, most-tested code in the DLL: it is the safety stop.

## C — command pipe (`\\.\pipe\FrameLedger.v2`)

Unchanged in spirit from v1, bumped to `v2` for the new message set.

- Message mode, single server (Agent), max 2 clients (UI + future CLI), `PIPE_REJECT_REMOTE_CLIENTS`.
- ACL: current interactive user's SID + Administrators. Reject clients whose token user differs.
- Framing: 4-byte LE length + UTF-8 JSON, max 1 MB. `System.Text.Json` source-generated contexts in `FrameLedger.Shared`.
- Envelope: `{ "type": …, "id": …, "payload": … }`; `Ack` correlates by `id`; events have no `id`.

### Messages

| Type | Dir | Payload / notes |
|---|---|---|
| `Hello` | UI→A | `{ appVersion, protocol: 2 }` → `HelloAck { agentVersion, protocol, elevated, overlayBuildId, vulkanLayerRegistered, telemetrySource /* composite, e.g. l1+lhm+nvapi */, cpuTempAvailable, etwAvailable }` |
| `GetStatus` | UI→A | → `StatusAck { state, activeSession?, tier? }` |
| `SetWatchlist` | UI→A | `{ entries: [{ gameId, exePath, hookEnabled }] }` — full replace; UI re-sends on connect |
| `LaunchGame` | UI→A | `{ gameId }` → suspended-launch + inject path (`04_CAPTURE` §Launch mode) |
| `SetHookEnabled` | UI→A | `{ gameId, enabled, consentAt }` — Agent re-runs the static AC pre-scan and may reply `Refused` |
| `PauseCapture` / `ResumeCapture` | UI→A | global |
| `StopSession` | UI→A | `{ sessionGuid }` — graceful unhook + finalize |
| `UpdateRules` | UI→A | `{ path }` — hot-reload detection + anticheat rules |
| `Shutdown` | UI→A | graceful stop |
| `SessionStarted` | A→UI | `{ sessionGuid, gameId, pid, tier, startedAt }` |
| `SessionProgress` | A→UI | 1 Hz: `{ elapsedS, nativeFps5s, displayedFps5s, fgFactor?, fgMode, upscaler, upscalerQuality, renderW/H, outputW/H, rtActive, gpuTempC?, cpuTempC?, vramProcMb, latencyUs? }` |
| `SessionCompleted` | A→UI | `{ sessionGuid, sessionId, exitStatus, tier }` — UI loads the full row from SQLite |
| `CaptureRefused` | A→UI | `{ gameId, reason, signal }` — **guard fired**; UI shows the plain-language explanation and the Tier-2 offer |
| `CaptureDegraded` | A→UI | `{ sessionGuid, from, to, reason }` — Tier 1 → Tier 2 mid-flight, or overlay self-disabled |
| `SafetyUnhook` | A→UI | `{ sessionGuid, signal }` — anti-cheat appeared mid-session; prominent UI notice |
| `CaptureError` | A→UI | `{ code, message }` — `InjectFailed`, `RingVersionMismatch`, `EtwAccessDenied`, `PresentMonMissing`, `TelemetryUnavailable`, `DbWriteFailed` |
| `Ping`/`Pong` | both | 15 s keepalive |

## Client behavior (UI)

- Connect with 250 ms × 8 backoff; on failure start the Agent, retry; then show an Agent status banner with Repair.
- Treat the pipe as unreliable: library, history and charts must all work with the Agent offline. Only live status degrades.
- SQLite is the source of truth for anything persisted; pipe events are refresh signals (except `SessionProgress`, which is live-only by design).
- `CaptureRefused` and `SafetyUnhook` are **never** collapsed into a generic error toast — they get dedicated, explanatory UI (`08_UI` §Notifications).

## Versioning

`protocol` bumps only on breaking changes; additive fields are always allowed and unknown fields must be ignored (tested on both sides). `FL_SHM_LAYOUT_VERSION` bumps whenever `FlFrameRecord` or the header changes — and because the DLL lives inside a running game, the Agent must handle mismatch gracefully rather than assuming lockstep.
