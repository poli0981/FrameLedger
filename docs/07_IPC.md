# 07 — IPC

Three channels, each chosen for its constraints:

| Channel | Between | Transport | Why |
|---|---|---|---|
| **A. Frame ring** | Overlay DLL → Agent | Shared memory, lock-free SPSC | Hot path: no syscall allowed |
| **B. Control block** | Agent ↔ Overlay DLL | Same shared memory, atomics | Tiny, infrequent, must work even if the game is unresponsive |
| **C. Command pipe** | UI ↔ Agent | Named pipe, JSON | Rich, versioned, human-debuggable |

## A + B — shared memory (`Local\FrameLedger.Ring.<pid>`)

Created by the Overlay on init, opened by the Agent. Name is deliberately plain and identifiable (`19_SAFETY` — no obfuscated object names).

Layout — **four regions, each exactly one 64-byte cache line before the ring**:

```
[0x0000] FlShmHandshake  64 B  write-once by the Overlay at init
[0x0040] FlWriterState   64 B  Overlay-written (writeIndex touched every present)
[0x0080] FlControlBlock  64 B  Agent-written
[0x00C0] FlFrameRecord[capacity]   (64 B each, power-of-two capacity)
```

The header is split in two on purpose. The immutable handshake and the
per-present writer state have completely different access patterns, and keeping
the Agent's control line separate from both is what makes the no-false-sharing
claim true rather than aspirational: the Overlay writes line 2 every frame while
the Agent writes line 3 every second, and neither invalidates the other's line.

```cpp
struct alignas(64) FlShmHandshake {  // exactly 64 B, no padding holes
    uint32_t layoutVersion;      // @0  must equal FL_SHM_LAYOUT_VERSION on both sides
    uint32_t recordSize;         // @4  64; belt-and-braces against struct drift
    uint32_t capacity;           // @8  power of two
    uint32_t pid;                // @12
    char     buildId[32];        // @16 native DLL build id
    uint64_t qpcEpoch;           // @48 session time base, shared with sensor samples
    uint64_t adapterLuid;        // @56 which GPU the swapchain is on (multi-GPU selection)
};

struct alignas(64) FlWriterState {   // Overlay-written
    uint64_t writeIndex;         // @0  monotonic; release-store via std::atomic_ref
    uint32_t status;             // @8  init|ready|self_disabled|unhooked
    uint32_t apiMask;            // @12 which graphics APIs got hooked
    uint32_t faultCount;         // @16 hook faults so far (17_HOOK_ENGINE §Fault policy)
    uint32_t vramBudgetMb;       // @20 IDXGIAdapter3 Budget, refreshed at 1 Hz
    uint32_t reserved[10];       // @24..63 must be zero; reserved for additive fields
};

struct alignas(64) FlControlBlock {  // Agent-written
    uint32_t pauseRequested;     // @0
    uint32_t unhookRequested;    // @4  19_SAFETY: set when the guard fires mid-session
    uint32_t overlayEnabled;     // @8  in-game overlay draw toggle (v1.1)
    uint32_t guardTicks;         // @12 completed guard evaluations, NOT a timer
    uint32_t reserved[12];       // @16..63 must be zero
};

static_assert(sizeof(FlShmHandshake) == 64);
static_assert(sizeof(FlWriterState)  == 64);
static_assert(sizeof(FlControlBlock) == 64);
static_assert(FL_SHM_RING_OFFSET == 0xC0 && FL_SHM_RING_OFFSET % 64 == 0);
```

The cross-process fields are declared as plain integers and accessed through
`std::atomic_ref` rather than as `std::atomic<T>` members. `std::atomic<T>` has
no guaranteed layout, cannot be memcpy'd, and cannot be mirrored by a C#
`[StructLayout(LayoutKind.Sequential)]` struct — and CLAUDE.md §Struct mirroring
requires exactly that mirror, with a test asserting every field offset on both
sides. `std::atomic_ref<uint32_t>` requires 4-byte alignment and
`std::atomic_ref<uint64_t>` 8-byte, which every offset above satisfies.

**Offset asserts apply to all three header structs, not just the record.** The
Agent reads `layoutVersion`, `recordSize`, `buildId`, `writeIndex` and
`faultCount` across the process boundary; a silent drift in any of them is the
same class of bug as record drift.

> Why this is spelled out in such detail: an earlier revision of this document
> declared a single 88-byte `FlShmHeader` while mapping `FlControlBlock` to
> `0x0040`. In code, `unhookRequested` would have aliased `faultCount` — so the
> safety stop would fire on any hook fault, and the fault counter would be
> clobbered by the Agent's heartbeat. Header layout in this file is normative
> and must be checked with `offsetof`, not read as illustration.

**Security:** the mapping is created with a DACL granting access only to the **current user's SID**; `Local\` namespace keeps it session-scoped. No `Global\` objects (would need admin and would be visible across sessions for no benefit).

**Protocol rules**

- **Writer (game) — seqlock per record.** `seq` lives at offset 56 *inside* the
  record, so "fill 64 bytes" would overwrite the very field guarding the write.
  The payload write must cover bytes `[0,56)` and `[60,64)` and **never touch
  `seq`**:

  ```cpp
  FlFrameRecord* slot = &ring[idx & (capacity - 1)];
  std::atomic_ref<uint32_t> seq{slot->seq};
  const uint32_t s = seq.load(std::memory_order_relaxed);  // sole writer of seq
  seq.store(s + 1, std::memory_order_relaxed);             // odd = writing
  std::atomic_thread_fence(std::memory_order_release);
  /* write payload — bytes [0,56) and [60,64), never slot->seq */
  std::atomic_thread_fence(std::memory_order_release);
  seq.store(s + 2, std::memory_order_relaxed);             // even = complete
  writeIndex.store(idx + 1, std::memory_order_release);    // via atomic_ref
  ```

  `seq` is **monotonic per slot and never reset**, so a full lap of the ring
  always changes it — otherwise a reader stalled for exactly one lap would
  validate a completely different frame as unchanged. Both fences compile to
  nothing on x86-64, so this costs zero against the ≤ 1 µs budget.

- **Reader (Agent).** `writeIndex.load(acquire)`; for each slot, load `seq`
  (acquire), skip if odd, copy, `atomic_thread_fence(acquire)`, then re-load
  `seq` and accept only if unchanged.

- **The ring carries FRAMES, and `DXGI_PRESENT_TEST` is not one.** The writer drops
  occlusion probes before allocating a slot, so `writeIndex` counts frames and no
  consumer has to remember to filter. Decided 2026-08-05; before that the question
  was assigned to nobody here and `03_METRICS` was silent about it.

  > A probe runs the presentation test and submits nothing — measured, 500 of them
  > leave `GetLastPresentCount` at 0 while 37 real presents move it by 37. An
  > application issues them while minimised or fully occluded, so a backgrounded
  > game emits a steady stream of non-frames: **142 of them reached the ring in one
  > 2-second canary run** against a writer without the filter. `03_METRICS` derives
  > Displayed FPS from `count(F_disp)/D` and frame times from consecutive `qpc`, so
  > recording them makes a minimised game report a frame rate it is not rendering.
  >
  > The filter sits **after** the safety checks, so a probe-only process still
  > evaluates the stop rather than going unsupervised because it stopped drawing.

- **A skipped torn record is a data gap, not a missing frame.** Dropping it
  silently makes the two surrounding frame times merge into one double-length
  interval — i.e. it *manufactures a stutter* in the metric the whole product
  exists to report. The drain must emit an explicit gap marker at that index;
  `03_METRICS` excludes gap-adjacent intervals from frame-time statistics and
  counts them toward the session's data-quality warnings.

- **Dropped records are computed by the Agent, not the writer.** The writer has
  no reader index and therefore cannot know whether the slot it is about to
  overwrite was ever consumed — the field was unimplementable as originally
  specified. The Agent computes it from the write index it already tracks:
  `if (writeIndex - readIndex > capacity) dropped += (writeIndex - readIndex) - capacity`,
  then resumes at `writeIndex - capacity`. This keeps the hot path free of an
  extra atomic and puts the accounting where the information actually exists.
- Version handshake: Agent compares `layoutVersion` + `recordSize` + `buildId` against its own. **Mismatch → refuse to attach**, tell the user to restart the game (this happens when the app updates while a game is running).

  > **Implemented 2026-08-05** as `FrameLedger.Shared.ShmHandshakeValidator`, a pure
  > function so every refusal is drivable without a live target. The Agent's own build
  > id comes from `FlGuardBuildId` on the guard DLL — **not** from the Overlay, whose
  > `FlGetBuildId` the Agent cannot reach without `LoadLibraryW`-ing the payload into
  > itself and creating a ring under its own pid (§S23-1).
  >
  > Three things the check does that "compare three fields" does not imply:
  > `layoutVersion == 0` is **`Incomplete`, not a mismatch**, because the Overlay
  > publishes that field last behind a release fence, so zero means "not ready yet" and
  > the Agent should retry rather than tell the user to restart; the version is checked
  > **before** the fields it vouches for, since a record size read under a layout we do
  > not know names the wrong cause; and an **absent** id on either side refuses rather
  > than matching — with neither side carrying one, `"" == ""` is true and the gate
  > would permit attaching to anything, which is the shape it shipped in.
- **`guardTicks` counts completed guard evaluations, not seconds.** It was
  specified as "Agent bumps every second", and a timer-driven tick is the wrong
  signal: it attests that the Agent *process* is alive, while the guard loop can
  be dead — a swallowed exception, or blocked in a service query, or stalled on
  one unreadable process in the §S16 scan set. A consumer reading "supervised"
  would then keep observing *because* the thing supervising it had stopped. The
  Agent increments it at exactly one site, after `Evaluate` returns a verdict.
  A refusal counts too, and also sets `unhookRequested`, so no consumer has to
  infer a verdict from a counter.
- **Supervision loss means stop observing.** If `guardTicks` has not advanced
  within **65 seconds** (`FL_GUARD_TICK_DEADLINE_MS`, `fl_shm.h`), the capture
  side stops recording.

  > **Evaluated OFF the present path, by a watchdog thread** (`17_HOOK_ENGINE`
  > §The watchdog thread). For one day it was evaluated only inside the present
  > hook, which made it unreachable in a hung or alt-tabbed game — the exact
  > scenario it exists for, and the one `fl_shm.h` warns about in capitals over
  > the constant. `20_OPEN_QUESTIONS` §S25 records the measurement.
  >
  > **The safety stop (`unhookRequested`) is checked in BOTH places**, and the
  > asymmetry is deliberate: this rule tolerates a second of latency, while the
  > safety stop is required within one frame, which only the present path can
  > deliver while frames exist.

  > **This sentence had no number until 2026-08-05.** One grep over `docs`, `src`
  > and `tools` for "deadline" returned exactly one line — this one, the sentence
  > that depends on it. A rule with no value is not implementable, and this had
  > been read as delivered.
  >
  > **65 s is two missed scans.** The guard re-scan runs every 30 s, so a ~35 s
  > deadline would stop a session on a single late tick — and a tick is late
  > whenever the machine is busy, which during a benchmark is always. Two
  > consecutive misses is a signal; one is noise. The cost is that the worst-case
  > window in which an unsupervised hooked process keeps observing doubles, from
  > the 30 s `legal/DISCLAIMER.md` discloses to 65 s, and that document now says
  > 65 rather than being left to imply 30.

  This used to read "the
  Overlay keeps writing (harmless)" — which described an *unsupervised hooked
  process* as harmless, and the 30 s re-scan `19_SAFETY` calls the most
  important runtime behaviour is exactly what has stopped in that state.
  One field cannot mean opposite things in two hosts, so the Overlay and the
  Vulkan layer share this rule.

  > The two are not equally exposed and the asymmetry is worth stating rather
  > than hiding: the Overlay entered a process that passed a full pre-injection
  > guard, the layer never did. That argues for the layer being *stricter*, not
  > for the Overlay being laxer.

  "Never advanced" and "stopped advancing" are the same state. The clock starts
  when the mapping is published, so a capture side that is never adopted by an
  Agent is inert from the beginning rather than enjoying a grace window.
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
| `SetWatchlist` | UI→A | `{ entries: [{ gameId, exePath }] }` — full replace; UI re-sends on connect. **Carries no hooking state** (see §The pipe is not a trust boundary) |
| `LaunchGame` | UI→A | `{ gameId }` → suspended-launch + inject path (`04_CAPTURE` §Launch mode) |
| `SetHookEnabled` | UI→A | `{ gameId, enabled }` — Agent re-runs the static AC pre-scan and may reply `Refused`. **The Agent stamps the consent timestamp itself** |
| `PauseCapture` / `ResumeCapture` | UI→A | global |
| `StopSession` | UI→A | `{ sessionGuid }` — graceful unhook + finalize |
| `UpdateRules` | UI→A | **no payload** — a trigger, not a source. The Agent re-reads only its own `%LOCALAPPDATA%\FrameLedger\rules\` copy |
| `Shutdown` | UI→A | graceful stop |
| `SessionStarted` | A→UI | `{ sessionGuid, gameId, pid, tier, startedAt }` |
| `SessionProgress` | A→UI | 1 Hz: `{ elapsedS, nativeFps5s, displayedFps5s, fgFactor?, fgMode, upscaler, upscalerQuality, renderW/H, outputW/H, rtActive, gpuTempC?, cpuTempC?, vramProcMb, latencyUs? }` |
| `SessionCompleted` | A→UI | `{ sessionGuid, sessionId, exitStatus, tier }` — UI loads the full row from SQLite |
| `CaptureRefused` | A→UI | `{ gameId, reason, signal }` — **guard fired**; UI shows the plain-language explanation and the Tier-2 offer |
| `CaptureDegraded` | A→UI | `{ sessionGuid, from, to, reason }` — Tier 1 → Tier 2 mid-flight, or overlay self-disabled |
| `SafetyUnhook` | A→UI | `{ sessionGuid, signal }` — anti-cheat appeared mid-session; prominent UI notice |
| `CaptureError` | A→UI | `{ code, message }` — `InjectFailed`, `RingVersionMismatch`, `EtwAccessDenied`, `PresentMonMissing`, `TelemetryUnavailable`, `DbWriteFailed` |
| `Ping`/`Pong` | both | 15 s keepalive |

## The pipe is not a trust boundary

The ACL restricts the pipe to the current user, and that is worth having — but
it is not a guarantee, because **everything the user runs is also the user**.
The gate must therefore survive a hostile client on this pipe, and three
messages were written as though it would not have to. All three are corrected
above; the reasoning is recorded here so they are not reintroduced as
conveniences.

**`UpdateRules { path }` was a documented override of the hard gate (§S3).** It
let any client hand the Agent an arbitrary filesystem path to load detection
*and anti-cheat* rules from — i.e. replace the blocklist with an empty one.
That is precisely the config-file flag `19_SAFETY` §The anti-cheat guard says
does not exist. It is now a bare trigger; the rules **source** is not a
parameter.

**`SetHookEnabled` took `consentAt` from the client.** The per-game informed
consent that FR-2.1 and `19_SAFETY` §User-facing consent make the basis of every
injection was therefore client-asserted, and a forged timestamp would have made
a game look consented-to that the user never saw a dialog for. The Agent stamps
`games.hook_consent_at` from its own clock when it accepts the enable, or
refuses. A client can *request*; it cannot *attest*.

**`SetWatchlist` carried `hookEnabled` and re-sent it on every connect.** The
adjacent `SetHookEnabled` re-runs the static anti-cheat pre-scan and may reply
`Refused`; `SetWatchlist` did not, while setting the same state in bulk. That
asymmetry is a bypass — and a self-healing one, because the UI re-sends the full
list on reconnect, so a value forced once would be re-asserted forever.
`SetWatchlist` now carries identity only. Enabling hooking has exactly one
message, and that message always re-scans.

> The general rule: **no inbound message may assert a safety fact.** It may ask
> for a state change, and the Agent then establishes the fact itself. Any new
> message that carries a verdict, a clearance, a consent record or a rules
> source is the same bug in a new costume.

## Client behavior (UI)

- Connect with 250 ms × 8 backoff; on failure start the Agent, retry; then show an Agent status banner with Repair.
- Treat the pipe as unreliable: library, history and charts must all work with the Agent offline. Only live status degrades.
- SQLite is the source of truth for anything persisted; pipe events are refresh signals (except `SessionProgress`, which is live-only by design).
- `CaptureRefused` and `SafetyUnhook` are **never** collapsed into a generic error toast — they get dedicated, explanatory UI (`08_UI` §Notifications).

## Versioning

`protocol` bumps only on breaking changes; additive fields are always allowed and unknown fields must be ignored (tested on both sides). `FL_SHM_LAYOUT_VERSION` bumps whenever `FlFrameRecord` or the header changes — and because the DLL lives inside a running game, the Agent must handle mismatch gracefully rather than assuming lockstep.
