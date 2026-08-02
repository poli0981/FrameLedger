# P0 spike notes

P0's named deliverable (`15_ROADMAP` §P0). **Empty by design** — this file is
where measured results go, not predictions.

Rules:

- Record what was **observed**, with the machine, driver version, game and SDK
  version it was observed on. A result without its context is not reusable.
- Record failures and dead ends too. "We tried X, it deadlocked, here is why" is
  worth more than silence, and prevents the next person retrying it.
- When an entry answers an item in `20_OPEN_QUESTIONS.md`, fix the owning doc,
  delete the question, and note both here.
- Nothing in P1 starts until the exit criteria at the bottom pass.

---

## Environment

Recorded 2026-08-02. Toolchain rows are **verified** — the native layer builds
and its tests pass on this machine. Everything below the toolchain is context
for the measurements that have not been taken yet.

| | |
|---|---|
| CPU | Intel Core i7-14700KF |
| GPU | NVIDIA GeForce RTX 5080, driver 32.0.16.1088 |
| RAM | 32 GB DDR5 |
| Motherboard | Gigabyte Z790M AORUS ELITE AX |
| Display | MSI MAG 274QF X24 — 2560×1440, 240 Hz |
| OS | Windows 11 Pro Insider Preview, build 26300.9032 |
| .NET SDK | 10.0.302 (pinned by `global.json`) — ✅ verified |
| MSVC | 19.51.36252, toolset 14.51.36231, VS 2026 Insiders — ✅ verified |
| Windows SDK | 10.0.22621.0 and 10.0.26100.0 installed — ✅ matches the TFM |
| CMake / Ninja | CMake 4.4.0, Ninja from the VS CMake component — ✅ verified |
| Vulkan SDK | not installed — needed before `FrameLedger.VkLayer` has real content |
| Launchers installed | Steam, Epic, itch.io |

**Coverage gaps to state plainly rather than discover later:**

- **No AMD or Intel GPU.** The `18_GPU_VENDOR_APIS` §Capability matrix cannot be
  filled for those vendors here, and that matrix drives what the UI advertises
  as available. Mark them "untested", never `?`. AFMF (§9) is likewise
  unreachable — it is an AMD driver-side feature.
- **No integrated GPU** (the `KF` suffix means no iGPU) and exactly one adapter.
  Multi-GPU adapter-LUID selection — the `adapterLuid` field in the shm
  handshake, and the PDH instance filtering in `18_GPU_VENDOR_APIS` §L1 — cannot
  be exercised on this machine.
- **GOG Galaxy is not installed,** so the GOG detection path in `05_DETECTION`
  §Platform signatures has no local fixture. Steam, Epic and itch do.
- **240 Hz display.** Useful to know when reading the ring-sizing argument in
  `04_CAPTURE`: 8192 records is ~16 s at 500 fps, so ~34 s at this refresh rate.
  Hook-overhead runs should uncap the frame rate rather than sit at 240.

---

## 0 · Licence checks *(no hardware needed — do these first)*

Answers `20_OPEN_QUESTIONS` §M3, §M4. Both can invalidate a whole telemetry
layer, and neither needs a GPU.

### M4 · LibreHardwareMonitor MPL-2.0 Exhibit B

- Pinned version:
- Searched for "Incompatible With Secondary Licenses":
- **Result:**
- Consequence if present: L2 is unusable for AMD, Intel *and* NVIDIA; IGCL and
  ADLX are already rejected, so there is no sensor layer left. Escalate
  immediately rather than working around it.

### M3 · NVAPI licensing and the import library

- Artifact and tag:
- Licence covering the **headers**:
- Licence covering **`nvapi64.lib`** (may differ from the headers):
- SPDX blocks intact:
- **Result:**
- Consequence if not MIT: Reflex / PC latency has no alternative source. FR-4.6,
  the latency tab (FR-5.2) and `sessions.latency_*` become permanently `N/A`.
  Do **not** fall back to ordinal resolution — `12_BUILD` and
  `18_GPU_VENDOR_APIS` both reject it.

---

## 1 · Guard *(moved to first — `20_OPEN_QUESTIONS` §R1)*

- Module enumeration on a suspended process (§S1) — what is actually visible:
- WOW64 / `LIST_MODULES_ALL` behaviour (§S7):
- Fail-closed behaviour on each error path:
- Driver enumeration for machine-wide blockers:
- **Decision on launch-mode injection timing:**

## 2 · Vulkan layer passthrough *(moved earlier — §R2)*

- `enable_environment` behaviour with the loader:
- Confirmed passthrough for non-enabled processes:
- **Blast-radius check:** layer registered, unrelated Vulkan app run, nothing of
  ours loaded/executed:

## 3 · Hook viability on `hook-harness` only

**Already settled — the shared-memory layout.** ✅ `fl_shm.h` compiles under
MSVC 19.51 with `/W4 /WX`, every `static_assert` holds, and `fl-layout-dump`
output is **byte-identical between MSVC and gcc** across all four structs and
39 field offsets. All four regions are exactly 64 bytes with no implicit
padding, and the mapping is 512 KiB + 192 B of header at the default capacity.
This is what the corrected `07_IPC` layout was worth checking twice: the
earlier design put `FlControlBlock` at an offset that overlapped the header.

Still open:

- Vtable indices observed (§H4):
- CFG / `__fastfail` behaviour with MinHook trampolines (§H1) — **not yet
  exercised: `/guard:cf` is deliberately not applied to the Overlay target
  until this is answered**:
- `-D_HAS_EXCEPTIONS=0` + `<atomic>` compiles (§H3) — same, the define is not
  applied yet:
- Deferred hook install from the `LoadLibrary` hook, no deadlock (§H2):
- Per-present cost, hooked vs unhooked (QPC around the call site):

## 4 · Vendor symbol reality check

Resolve the **actual** exported names; `17_HOOK_ENGINE` records conventions, not
verified facts. A wrong name degrades silently to `unknown`, which reads as
"working, no upscaler detected" — the highest false-confidence risk in the spike.
Record the SDK version each name came from.

| SDK | Version shipped by the title | Symbols found |
|---|---|---|
| NGX | | |
| Streamline | | |
| FidelityFX (`ffx_api` vs legacy) | | |
| XeSS | | |

## 5 · Proxy swapchains *(§H5)*

- Title tested (must be a real Streamline/DLSS-G title):
- Does our dummy-vtable patch actually intercept its presents:
- **If not, the vtable-swap strategy does not survive P1 as designed.**

## 6 · RT detection

- `DispatchRays` counting:
- `BuildRaytracingAccelerationStructure` on an **inline `RayQuery`** title — the
  specific regression the old design got wrong:
- Recorded-vs-executed semantics and counter concurrency (§H6):

## 7 · First real injection *(requires the guard, §1)*

- Title:
- `/MT` DLL loaded without incident:

## 8 · The accuracy question — why this rewrite exists

≥ 3 real offline titles. Verify against each game's own settings menu.

| Title | Upscaler | Quality | Render → output | FG active | RT | Matches menu? |
|---|---|---|---|---|---|---|
| | | | | | | |

- **Baseline problem (§M9):** `15_ROADMAP` requires comparing against the old
  file/module-based detection, but no prior implementation exists in this
  repository. Baseline used instead:
- **Quantified improvement (belongs in the README):**

## 9 · Frame generation

- Rung 1 (API / `fgEvaluations`) vs Tier-2 ETW `FrameType`:
- Can PresentMon 2.x `FrameType` see driver-level FG / AFMF (§M1):
- AFMF on this machine: **untested — RTX 5080, AMD driver-side feature**

## 10 · Telemetry layering

- L1 baseline vendor-neutral:
- `D3DKMT` perf-data probe on Win 10 22H2 **and** Win 11 — stable enough to keep:
- L2: LHM fields returned per vendor (fill `18_GPU_VENDOR_APIS` §Capability
  matrix; use explicit "untested", never `?`, for absent hardware):
- L2: **GPU sensors unelevated, without PawnIO** (§M5) — decides whether the
  default unelevated Agent has temperatures at all:
- L3: Reflex latency, throttle reasons, per-domain utilisation:

## 11 · PresentMon / Tier 2 *(§M2, §M6)*

- Console binary obtained, version, pinned hash:
- Runs unelevated:
- 2.x column set over stdout:
- Performance Log Users sufficient without admin:

---

## Exit criteria

`15_ROADMAP` §P0. Nothing in P1 starts until all are met.

- [ ] A throwaway build records a real session from a real offline game
      reporting **correct** upscaler, quality preset, render → output
      resolution, FG factor and RT state — verified against the game's own
      settings menu.
- [ ] Measured game FPS impact ≤ 0.5%.
- [ ] Every S-series item in `20_OPEN_QUESTIONS.md` resolved, or explicitly
      deferred with a written rationale.
- [ ] M3 and M4 answered **before** any NVAPI or LHM code is written.

> Note (`20_OPEN_QUESTIONS` §R4): the first two criteria import P2 work — a real
> session needs the drain, aggregate and recorder paths. Either scope a
> throwaway drain harness into P0 or move the FPS-impact criterion to the end of
> P1. Decide before starting, not halfway through.
