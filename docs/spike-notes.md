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
| Vulkan | SDK 1.4.357.0 at `C:\VulkanSDK\1.4.357.0`, loader instance 1.4.357 — ✅ verified |
| Launchers installed | Steam, Epic, itch.io, GOG Galaxy |

**Coverage gaps to state plainly rather than discover later:**

- **No AMD or Intel GPU.** The `18_GPU_VENDOR_APIS` §Capability matrix cannot be
  filled for those vendors here, and that matrix drives what the UI advertises
  as available. Mark them "untested", never `?`. AFMF (§9) is likewise
  unreachable — it is an AMD driver-side feature.
- **No integrated GPU** (the `KF` suffix means no iGPU) and exactly one adapter.
  Multi-GPU adapter-LUID selection — the `adapterLuid` field in the shm
  handshake, and the PDH instance filtering in `18_GPU_VENDOR_APIS` §L1 — cannot
  be exercised on this machine.
- **240 Hz display.** Useful to know when reading the ring-sizing argument in
  `04_CAPTURE`: 8192 records is ~16 s at 500 fps, so ~34 s at this refresh rate.
  Hook-overhead runs should uncap the frame rate rather than sit at 240.

### ✅ This machine is a realistic multi-overlay test bed — use it

Six machine-wide implicit Vulkan layers are already registered under
`HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers`:

| Layer | Source |
|---|---|
| `VK_LAYER_VALVE_steam_overlay`, `VK_LAYER_VALVE_steam_fossilize` | Steam |
| `VK_LAYER_EOS_Overlay` | Epic Online Services |
| `GalaxyOverlayVkLayer` (+ `_DEBUG`, `_VERBOSE`) | GOG Galaxy |
| `VK_LAYER_OBS_HOOK` | OBS Studio |
| `VK_LAYER_RTSS` | RivaTuner Statistics Server |

**RTSS and the Steam overlay also hook D3D presentation in-process.** That makes
this machine the right place to answer two open questions, rather than
discovering them in a user's bug report:

- **§H7 — unhook clobbering.** `17_HOOK_ENGINE` §Unhooking restores the original
  vtable entry. If RTSS hooked the same slot after us, restoring it silently
  removes *their* hook. Test: hook with RTSS running, unhook, confirm RTSS still
  works. The compare-and-restore-only-if-unchanged fix is testable here today.
- **§H5 — proxy swapchains.** With this many overlays present, the swapchain the
  game presents through may not be the object our dummy-vtable probe patched.

Two more observations from `vulkaninfo` on this machine:

- **`HKCU` implicit layers: none.** All six above needed admin to register.
  `17_HOOK_ENGINE`'s choice of `HKCU` is the less common one and the better one.
- The loader warns that `GalaxyOverlayVkLayer` violates naming policy
  `LLP_LAYER_3`, and that OBS/RTSS declare API 1.3 against a 1.4 application.
  Both are mistakes we should not copy — see `17_HOOK_ENGINE` §Vulkan, where the
  layer name was corrected to `VK_LAYER_FRAMELEDGER_overlay` as a result.

**GOG fixture available:** `A Space for the Unbound - Prologue` at
`D:\another\gog\`, with a `goggame-1125815775.info` sibling — a real fixture for
the `05_DETECTION` §Platform signatures GOG path.

---

## 0 · Licence checks *(no hardware needed — do these first)*

Answers `20_OPEN_QUESTIONS` §M3, §M4. Both can invalidate a whole telemetry
layer, and neither needs a GPU.

### ✅ M4 · LibreHardwareMonitor MPL-2.0 Exhibit B — **CLEAR** (2026-08-02)

- Pinned version: `LibreHardwareMonitorLib` **0.9.6**, nuspec
  `<license type="expression">MPL-2.0</license>`, repo commit `3d331e33`.
- **The LICENSE file is not the evidence.** MPL-2.0's own text *contains*
  Exhibit B as a template (at lines 369–372 of the licence), so grepping the
  licence file finds the string in every MPL-2.0 project ever published and
  proves nothing. What matters is whether a **source file applies** the notice.
- Authenticated code search across the whole repository for
  "Incompatible With Secondary Licenses": **total_count = 1, and the single hit
  is `LICENSE` itself** — the template, not an applied notice.
- Every file we depend on carries **Exhibit A**, the permissive form:
  `Computer.cs`, `Sensor.cs`, `Gpu/NvidiaGpu.cs`, `Gpu/AmdGpu.cs` all begin
  "This Source Code Form is subject to the terms of the Mozilla Public License,
  v. 2.0" with no Exhibit B sentence.
- The shipped 0.9.6 `.nupkg` was unpacked and scanned: **no Exhibit B notice in
  any of its 46 entries.**
- **Result: MPL-2.0 §3.3 Secondary Licenses applies → GPL-3.0 compatible.**
  L2 telemetry is safe to build on.

Two things this turned up that are *not* blockers but are now known:

- **The package ships no licence file at all** (`<license>` is an SPDX
  expression). MPL-2.0 §3.1 therefore makes shipping the text **our**
  obligation — `legal/licenses/mpl-2.0.txt` is now committed, and the
  `THIRD_PARTY_NOTICES` release gate is a real requirement, not boilerplate.
- `Gpu/IntelIntegratedGpu.cs` has **no licence header** — an upstream
  inconsistency, not an Exhibit B problem. The repo-root LICENSE still governs.
  Worth re-checking on any version bump.

### ✅ M3 · NVAPI licensing and the import library — **CLEAR** (2026-08-02)

- Artifact: <https://github.com/NVIDIA/nvapi>, `main`.
- **The import library is in the repository**: `amd64/nvapi64.lib` and
  `x86/nvapi.lib` are tracked files, not a separate SDK download. That was the
  crux of the question — a `.lib` obtained from the NVIDIA SDK installer would
  have come with the SDK's own agreement instead.
- `License.txt` answers it in its first line, unusually explicitly:
  > `nvapi.lib and nvapi64.lib are licensed under the following terms:`
  followed by `SPDX-License-Identifier: MIT` and the standard MIT text. **The
  grant names the import libraries as its subject.**
- Headers carry their own `SPDX-License-Identifier: MIT` blocks
  (`nvapi.h`, `nvapi_lite_common.h`, `nvapi_interface.h` all verified).
- **Result: headers and `nvapi64.lib` are both MIT.** MIT is one-way compatible
  with GPL-3.0; vendoring is fine provided the notice is retained, which
  `tools/license-check.ps1` enforces. `legal/licenses/nvapi-MIT.txt` committed.
- Reflex / PC latency (FR-4.6, the latency tab, `sessions.latency_*`) is
  therefore reachable. Ordinal resolution stays rejected — it was only ever a
  workaround for a constraint that does not exist.

---

## 1 · Guard *(moved to first — `20_OPEN_QUESTIONS` §R1)*

Measured 2026-08-02, Windows 11 Pro Insider Preview 26300.9032, **unelevated**
(the default Agent under ADR-9). Probe: `src/native/tools/fl-probe-guard`,
running as ctest `fl_guard_apis`, so these are re-checked on every build rather
than being a one-off. It opens processes only with
`PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ` — the rights `19_SAFETY`
specifies — and never with `CREATE_THREAD | VM_OPERATION | VM_WRITE`.

### ✅ §S1 · Module enumeration on a suspended process — **it fails, it does not return empty**

```
suspended:          opened=1  enumerated=0  modules=0  enumErr=299 (ERROR_PARTIAL_COPY)
after ResumeThread: opened=1  enumerated=1  modules=9   (kernel32.dll present)
```

S1 predicted `EnumProcessModulesEx` "returns essentially nothing". **Measured, it
is sharper than that, and the difference is the whole point:** the call *fails*
with `ERROR_PARTIAL_COPY (299)`. It does not hand back a plausible empty success.

That is the good version of this news. An empty success is the dangerous shape —
it is exactly what `EnumDeviceDrivers` does, and it is what a guard reads as
"clean". An outright error cannot be mistaken for a clean scan by any caller that
checks the return value. So the guard's obligation here is narrow and explicit:

> **`ERROR_PARTIAL_COPY` means CANNOT DETERMINE, which means REFUSE.** Never
> "no modules found".

The blindness is the suspension and not our handle rights — the same target with
the same rights enumerates normally once resumed. §S1 is confirmed as a real
constraint on launch mode; what to *do* about it is still §S13(c).

### ✅ §S7 · WOW64 — the wrong filter under-reports, and silently

Live 32-bit target (`SysWOW64\cmd.exe`) enumerated from this x64 process:

| Filter | Modules |
|---|---|
| `LIST_MODULES_ALL` | **15** |
| `LIST_MODULES_DEFAULT` | 7 |
| `LIST_MODULES_32BIT` | 9 |
| `LIST_MODULES_64BIT` | 6 |

The default filter returns **less than half** the list, and returns it as a
*success*. A guard using the default would scan a 32-bit title, see 7 modules,
find no anti-cheat among them and report clean. `LIST_MODULES_ALL` is mandatory,
not a preference.

### ✅ Fail-closed inputs — "cannot inspect" is a distinguishable state

- **Protected process:** `OpenProcess` with the guard's own rights returns
  `ERROR_ACCESS_DENIED (5)` for at least one process on this machine. So
  "I could not look" and "I looked and it was clean" are genuinely different
  values, and `19_SAFETY` §Elevated / protected targets is implementable.
- **Absent service:** `OpenServiceW` on a non-existent name returns
  `ERROR_SERVICE_DOES_NOT_EXIST (1060)` **specifically**, not a generic failure —
  which is the discrimination `19_SAFETY` relies on.
- **NOT MEASURED — the DENIED service branch.** A standard user holds
  `SERVICE_QUERY_STATUS` on the stock service set, so `ACCESS_DENIED` is not
  producible against real services here. It has to be driven from a unit-test
  fake. Recorded rather than implied: A5 does **not** cover it.

### ✅ Driver enumeration — the replacement measured against the fail-open

```
EnumDeviceDrivers                 ok=1  count=266  non-null bases=0  recoverable names=0
NtQuerySystemInformation(11)      STATUS_SUCCESS  78,744 bytes
                                  parsed=266  well-formed=266  resolvable-on-disk=257  ntoskrnl=yes
```

`EnumDeviceDrivers` reproduces its documented fail-open exactly: 266 drivers
reported, **zero** usable base addresses, **zero** recoverable names. The `Nt*`
route returns 266 real native paths.

**The fail-open is purely a function of elevation** — established by CI, which
runs elevated and failed this probe's original assertion:

| Configuration | `EnumDeviceDrivers` |
|---|---|
| unelevated (this machine, the ADR-9 default) | 266 drivers, **0** bases, **0** names |
| elevated (GitHub `windows-latest`) | 260 drivers, **260** bases, **260** names |

The API is not broken; it is broken *for standard users*. That is the
configuration ADR-9 makes the default, and it means anyone who tests this while
elevated sees a perfectly working call and concludes the defect is imaginary.

The probe's assertion was originally unconditional — "the `Nt*` route recovers
more identities" — which encoded one machine's configuration as a universal
fact and went red on CI for a correct reason. It is now elevation-aware: it
asserts the fail-open when unelevated, and when elevated says plainly that this
run **cannot** demonstrate it.

**The assertions here check content, not count — deliberately.** "266 distinct
non-empty strings" is *not* discriminating: the earlier two-byte offset bug
produced exactly that, and every string was garbage. So the probe asserts that
`ntoskrnl.exe` is present, that **every** path is a native path (`\SystemRoot\`
or `\??\`), and that 257 of them name files that actually exist on disk.

**Canary, run every build:** the same buffer is re-parsed with the historical
two-byte skew and the validator must reject it —
`well-formed=0, ntoskrnl=NO`, first entry `ystemRoot\system32\ntoskrnl.exe`.
A count check passes that. This one does not.

### Still open

- **§S13(c) — decision on launch-mode injection timing.** The measurement above
  makes the constraint precise but does not make the decision. Deliberately not
  taken here: quantifying the early-init data actually lost needs a title that
  loads a presentation runtime lazily, and the only local fixtures are
  `hook-harness` (creates D3D at startup) and a 2D GOG prologue. A number from
  those would not generalise, which is worse than recording it as unmeasured.

### ✅ Signer field for the unknown-but-suspicious heuristic — **`O=`, not `CN=`**

Measured 2026-08-02, Windows 11 26300, unelevated, via
`Get-AuthenticodeSignature` on real binaries present on this machine.

| Binary | `CN=` | `O=` |
|---|---|---|
| `kernel32.dll` | Microsoft Windows | Microsoft Corporation |
| `win32u.dll` | Microsoft Windows | Microsoft Corporation |
| `nvapi64.dll` | **Microsoft Windows Hardware Compatibility Publisher** | Microsoft Corporation |
| `nvlddmkm.sys` (the display driver) | **Microsoft Windows Hardware Compatibility Publisher** | Microsoft Corporation |
| `nvrla.exe` | NVIDIA Corporation | NVIDIA Corporation |
| `steam.exe` | Valve Corp. | Valve Corp. |

The `trustedSigners` list was a guess marked UNVERIFIED, and the schema's own
comment said the comparison was against **CN**. Measurement says that is the
wrong field: **WHQL re-signing replaces the CN with a Microsoft publisher
string**, so NVIDIA's own kernel driver does not present as NVIDIA. Under a
CN comparison the whole driver stack reads as untrusted, and paired with the
`guard`/`protect` name fragments that is a false-refusal path — the failure
direction that is safe for the user's account but destroys trust in the gate,
which is how someone ends up asking for the override CLAUDE.md rule 2 says does
not exist.

`signerField` is now a schema **`const": "O"`**, so switching it back requires
editing the schema. `"Microsoft Windows"` was dropped from `trustedSigners` — it
is a CN value and matches nothing under `O=`.

**Untested by decision (2026-08-02):** `Intel Corporation` and
`Advanced Micro Devices, Inc.`. No AMD or Intel GPU and no iGPU on this machine
(§Environment), and the owner elected to keep NVIDIA as the v1 focus rather than
chase them.

The WHQL finding above is what makes that cheap: both vendors' driver binaries
are WHQL-signed, so they present `O='Microsoft Corporation'` and are already
suppressed by the first entry. Only their **non-WHQL first-party** binaries
depend on these two strings, and a wrong string there fails **closed** — a
refusal, never a silent allow. Kept in the data, labelled untested, not
presented as measured.

## 2 · Vulkan layer passthrough *(moved earlier — §R2)*

Measured 2026-08-02 against **Vulkan loader 1.4.357** on Windows 11 26300,
unelevated, with `tools/vklayer-blastradius.ps1`. That script is the **only**
place the layer is registered; it registers under `HKCU`, runs `vulkaninfo`, and
unregisters in a `finally` block including on failure. Verified clean afterwards.

### ✅ `enable_environment` works, and it compares the VALUE

| Condition | Result |
|---|---|
| `FRAMELEDGER_ENABLE_VK_LAYER` unset | loader locates the manifest, **never maps the DLL** |
| set to `1` (the manifest's value) | `Insert instance layer` / `Inserted device layer` — mapped |
| set to `0` (non-matching) | **not** enabled — the loader compares the value, not mere existence |

The value comparison is the better of the two possible answers and was not
safe to assume: had the loader merely checked existence, a stray
`FRAMELEDGER_ENABLE_VK_LAYER=0` anywhere in a user's environment would have
enabled us machine-wide.

**Still a loading gate, not a security gate.** Anything running as the user can
set the variable. It shrinks the default blast radius from "every Vulkan process
on the machine" to "processes the Agent launched"; it does not authorise.

### 🔴 Declining `vkNegotiateLoaderLayerInterfaceVersion` CRASHES the host

The single most valuable thing this test found, and it was in code written the
same afternoon. The obvious-looking gate — "if this process is not in the
enable-list, return `VK_ERROR_INITIALIZATION_FAILED` from negotiation so the
loader skips us" — does **not** make loader 1.4.357 skip the layer. It
access-violates the application. Reproduced every time, with and without
`VK_LOADER_DEBUG`:

| Enable-list state (variable set) | `vulkaninfo` exit |
|---|---|
| file absent | `0xC0000005` |
| file present, this process not listed | `0xC0000005` |
| file present, this process listed | ok |

So that design would have crashed **every Vulkan application on the machine
outside our enable-list** — a far larger blast radius than the one §S2 exists to
reduce, and inflicted on applications that have nothing to do with FrameLedger.

**The rule that follows:** the layer always accepts negotiation and always
forwards. The enable-list decides what we *intercept*, never whether we *load*.
Being present and inert is cheap; being absent by erroring out is not something
this loader supports.

### Two false positives in the test itself, both corrected

Recorded because each would have reported a working gate as broken, and both are
the same shape as defects found elsewhere in this project:

1. **Discovery is not loading.** With the variable unset the loader still prints
   `Located json file "...\FrameLedger.VkLayer\VkLayer_..._overlay.json"`. A
   match on `FrameLedger.VkLayer.dll` hit that *path* — the manifest sits in a
   directory of that name.
2. **Availability is not loading.** `vulkaninfo`'s own report lists the layer by
   name, read from the manifest. That is the tool describing the system, not the
   loader mapping anything.

The unambiguous signal is the loader printing `Insert instance layer` /
`Inserted device layer` with our name and DLL path.

### ✅ The in-layer blocklist scan — §S2's second half

The layer scans its own process at init and goes fully passthrough on any hit,
using the same matcher and the same rules file as the injection guard. Verified
by `fl-probe-vklayer` (ctest `fl_vklayer_selfscan`), both directions:

```
[PASS] a clean process is NOT forced inert (so the blocked case below can mean something)
[PASS] the planted module is loaded into this process
[PASS] the self-scan now says STAY INERT - a blocklisted module was found
       after unload the scan says: may observe
```

The planted module is our own DLL copied under a blocklisted name
(`14_TESTING` §Integration tests: "a renamed harmless DLL, not real anti-cheat
software"). Proven red by making the matcher stop matching.

Two things worth keeping:

- **Both directions are asserted.** A self-scan that always answered "stay
  inert" would pass a one-sided test while silently disabling the layer
  everywhere — and would look exactly like a working gate.
- **The probe installs the seed rules when none exist**, then removes them.
  Without that it skipped on any machine that had not run the product, and a
  ctest that always skips is a gate that cannot fail. Installing them is the
  only option: there is deliberately no way to point the layer at a different
  rules file (§S3).

### Not yet done
- **Passthrough under a real Vulkan game**, alongside the six implicit layers
  already resident on this machine (§Environment). `vulkaninfo` proves
  load/no-load; it does not prove a real title still renders correctly with us
  in the chain.

## 3 · Hook viability on `hook-harness` only

**Already settled — the shared-memory layout.** ✅ `fl_shm.h` compiles under
MSVC 19.51 with `/W4 /WX`, every `static_assert` holds, and `fl-layout-dump`
output is **byte-identical between MSVC and gcc** across all four structs and
39 field offsets. All four regions are exactly 64 bytes with no implicit
padding, and the mapping is 512 KiB + 192 B of header at the default capacity.
This is what the corrected `07_IPC` layout was worth checking twice: the
earlier design put `FlControlBlock` at an offset that overlapped the header.

### ✅ H1 · `/guard:cf` with MinHook trampolines — **SAFE** (2026-08-02)

Probe: `src/native/tools/fl-probe-hookprofile`, built with the exact Overlay
flag set and run as ctest `fl_hook_profile`. MinHook v1.3.4, commit
`c3fcafdc`. Result: the indirect call from a `/guard:cf` module into MinHook's
runtime-allocated trampoline **succeeds**; `original=44 hooked=144`, and the
unhook path restores the original behaviour.

**A green probe is only worth what its setup proves, so the setup was verified
separately** rather than assumed — if CFG had not actually been enforcing, the
call would have succeeded for reasons that say nothing about H1:

| Evidence | Value |
|---|---|
| PE DLL characteristics | `Control Flow Guard` present |
| Guard CF function table | 84 entries |
| Guard Flags | `0x10014500` |
| Guarded indirect call sites in `.text` | **114** |
| `GetProcessMitigationPolicy` | `EnableControlFlowGuard = 1` |

Why it works: Windows marks memory allocated `VirtualAlloc(PAGE_EXECUTE_*)` as
a valid call target in the process CFG bitmap by default, so a trampoline is
callable without any `SetProcessValidCallTargets` dance.

⚠ **Measured with CFG strict mode OFF** (the probe reports it explicitly). A
host process that enables strict mode does not auto-validate dynamic code and
could still `__fastfail`. That is rare, and it is **not** catchable by
`FL_HOOK_GUARD` — see §H8. Treat it as a residual risk to watch for on real
titles, not as a solved problem.

### ✅ H3 · `-D_HAS_EXCEPTIONS=0` with `<atomic>` — **SAFE, with a sharp edge**

Compiles and runs under MSVC 19.51 with the full Overlay flag set, real
`fl_shm.h` included. `std::atomic_ref` is **lock-free at both 32- and 64-bit
widths** — the part that actually matters, since a non-lock-free atomic would
take a mutex and violate CLAUDE.md rule 5 in the present hook.

The sharp edge is what the define *does*, which is not "remove failure paths":

```
yvals.h:     #define _THROW(...) (__VA_ARGS__)._Raise()
             #define _RAISE(x)   _MSVC_STL_DOOM_FUNCTION(...)
__msvc_doom_core.hpp:  #define _MSVC_STL_DOOM_FUNCTION(mesg) __fastfail(5)
```

So a would-be exception becomes `__fastfail` — **an immediate, uncatchable
kill of the host game**. In an injected DLL that is arguably worse than an
exception, and SEH cannot intercept it (§H8 again).

It is safe here only because the hot path does not use throwing STL at all:
`<atomic>`, `<cstdint>` and `<type_traits>` contain **zero** throw-sites. That
makes the existing "no STL containers that allocate in hook paths" rule
load-bearing rather than stylistic — and arguably it is now a feature, since
violating it fails loudly in testing instead of propagating quietly.

### ◐ H2 · Hook installation under the loader lock — **safe path verified**

The deferred pattern holds: 20 MinHook enable/disable cycles (each suspending
every other thread to patch) while a worker thread ran **4,510**
`LoadLibrary`/`FreeLibrary` cycles. No deadlock, thread joined cleanly.

**Be precise about what this does not show.** It does not prove the naive
inline install deadlocks. A probe that deadlocks cannot report its own result,
and hanging CI to demonstrate a hazard we have already decided to avoid buys
nothing. So: the safe path is *verified*; the case against installing from
inside the `LoadLibrary` hook rests on the documented mechanism — suspending a
thread that holds the loader lock — and remains an argument, not a measurement.
H2 stays open in `20_OPEN_QUESTIONS` for that reason.

### ✅ H4 · Vtable indices — **all three proved, and CI can prove them too**

`hook-harness --probe-vtable`, ctest `fl_vtable_indices`.

A vtable slot carries no identity, so "check that slot 8 is Present" is not a
question the runtime can answer — which is why the old "verified at runtime
against the dummy object" wording was unimplementable. What *is* answerable is
behaviour: patch the slot, call the method, see whether the detour ran.

| Slot | Claim | Result |
|---|---|---|
| 8 | `IDXGISwapChain::Present` | ✅ detour ran on `Present()` |
| 13 | `IDXGISwapChain::ResizeBuffers` | ✅ detour ran on `ResizeBuffers()` |
| 22 | `IDXGISwapChain1::Present1` | ✅ detour ran on `Present1()` |

Also confirmed: `IDXGISwapChain` and `IDXGISwapChain1` return the **same vtable
pointer** — one concrete object with the table extended past the base
interface — so slot 22 is reachable from either interface pointer. And slot 8
resolves inside a mapped image, with `dxgi.dll` loaded at the expected base.

**The CI half of §H4 is answered too.** The worry was that a hosted runner has
no GPU, no DXGI output and no interactive window station, so index probing
would be dev-machine-only and the test would have to be skipped. Two choices
remove that: **WARP** (software rasteriser, no adapter needed) and
**`CreateSwapChainForComposition`** (no `HWND` at all). Feature level `0xB000`
obtained headless. These run as ordinary ctests.

### ◐ H5 · Proxy swapchains — **less dangerous than assumed, not yet cleared**

`hook-harness --probe-proxy`, ctest `fl_proxy_swapchain`. The harness builds a
real forwarding `IDXGISwapChain` wrapper — what `sl.interposer` and ReShade
hand the application — and presents through it while our hook sits on the
**real** vtable.

```
real vtable  00007FFE3C87C688
proxy vtable 00007FF701716F20   (different, as expected — we never patched it)
proxy saw 1 present;  hook on the REAL vtable saw 1
```

**The hook still fires.** The proxy forwards with `real_->Present(...)`, an
ordinary virtual dispatch through the real vtable, so patching the real vtable
catches it one layer down. The naive fear — "a proxy has its own vtable, so we
miss the present entirely" — does not hold for a forwarding wrapper.

Why this is not yet a clearance for §H5:

1. **A forwarding proxy is the easy case.** DLSS-G does not merely forward; it
   presents *interpolated* frames the application never submitted. Whether
   those reach a real-vtable hook at all is the question that matters for the
   FG counting in `03_METRICS`, and this harness cannot answer it.
2. **We see the post-proxy call.** Parameters the proxy rewrote are what we
   observe, not what the game passed.
3. **A proxy that owns a different swapchain** — rather than wrapping ours —
   would still be invisible.

So: layering does not inherently break the strategy, which is genuinely good
news for the vtable approach. A real Streamline/DLSS-G title is still required
before §H5 closes.

### 🔴 Guard · The documented driver scan is blind unelevated — **fail-open found**

Measured 2026-08-02, Windows 11 26300, standard user (the default Agent
configuration under ADR-9). Independently reproduced twice.

| API | Result unelevated |
|---|---|
| `EnumDeviceDrivers` (what `19_SAFETY` specified) | `ok=True`, 258 drivers, **0 usable base addresses**, **1** recoverable name (`ntoskrnl.exe`) |
| `NtQuerySystemInformation(SystemModuleInformation)` | `STATUS_SUCCESS`, 258 modules, **258 distinct full paths**, real driver names legible |

The first one **succeeds while telling you nothing**. A guard built on it would
report "no anti-cheat driver present" on a machine running Riot Vanguard. That is
a fail-open in the hard gate, in the default configuration — the single most
serious defect found in this project so far, and it was in a shipped doc.

`19_SAFETY` §Pre-injection checks now specifies the `Nt*` route. Caveats that
must not be lost: the API is documented-as-unsupported, and
`RTL_PROCESS_MODULE_INFORMATION`'s layout is version-sensitive — my own quick
probe mis-computed the path offset by two bytes (`INDOWS\system32\...`), which is
exactly the class of error that must fail closed rather than silently match
nothing. Assert the struct offsets; treat any parse failure as *refuse*.

### ✅ H7 · Unhook does not clobber a later hooker — **closed**

`hook-harness --probe-unhook`, ctest `fl_unhook_preserves_foreign`. The foreign
hooker is simulated rather than depending on RTSS being installed, so the test
is deterministic and runs on CI instead of only on this machine.

```
[PASS] the foreign hook chained through ours (it saved our detour as its original)
[PASS] our unhook DECLINED to restore, because the slot is no longer ours
[PASS] the foreign hook is still installed - we did not silently remove it
[PASS] the foreign hook still fires after our unhook
[PASS] with the slot untouched, our unhook DOES restore
[PASS] the slot holds the pristine entry again
```

The mechanism is the interesting part: a later hooker saves **our detour** as
*its* original and chains through it, so an unconditional restore does not
"clean up after ourselves" — it deletes their hook, silently, and their overlay
stops working with no error anywhere. `17_HOOK_ENGINE`'s "cleaner uninstall"
claim was backwards and is corrected.

Both halves are asserted deliberately. A compare-and-restore that never restores
would pass a one-sided test and leave our detour permanently in every process.

### ✅ Per-present cost — **8.4 ns, against a 1,000 ns budget**

`hook-harness --probe-cost`. 20,000 presents × 5 runs, hooked and unhooked runs
**interleaved** so scheduler or thermal drift during the run is not attributed
to the hook, medians compared.

| | ns / present |
|---|---|
| unhooked | 317.9 |
| hooked | 326.3 |
| **delta** | **8.4** (budget 1,000 — NFR-1, `14_TESTING` §Hook overhead item 1) |

**Read this narrowly.** The detour is an atomic increment plus a call through the
saved pointer, i.e. the *floor* for any vtable hook — it bounds the mechanism,
not the product. The Overlay's real per-present cost is `14_TESTING` item 2,
measured on a real game, and that number is not this one.

Not registered as a ctest: a timing threshold on a shared CI runner fails for
reasons that have nothing to do with the code. Run it deliberately.

### Still open in §3

Both remaining items need something this harness cannot synthesise:

- **§H2** — the deferred `LoadLibrary` install pattern is verified against heavy
  loader contention, but the case against the naive inline install still rests
  on the mechanism, not a measurement. Exercising it needs a real game that
  loads D3D12 lazily.
- **§H5** — a forwarding proxy does not defeat a real-vtable hook. DLSS-G
  presenting *interpolated* frames the application never submitted is the case
  that matters for FG counting, and it needs a real Streamline title.

Everything else in §3 is answered: H1, H3, H4, H7 and the per-present cost.

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

### ✅ The baseline exists — `fl-baseline-probe` (2026-08-03)

**§M9's problem is closed.** `15_ROADMAP` required comparing against "the old
file/module-based detection", which does not exist in this repository, so item 4
had no left-hand side and ADR-7's founding claim was unfalsifiable. The baseline
is now a real tool rather than a memory of one.

- **Baseline used instead:** `src/native/tools/fl-baseline-probe`, reading the
  `capabilities` group of `rules/detection-rules.json` (`rulesVersion 2026.08.1`,
  **7 capabilities**: DLSS, DLSS-G, Ray Reconstruction, Streamline, FSR, XeSS,
  XeSS-FG). It answers two questions per capability — is it **on disk** beside
  the game, and is it **loaded** right now.
- It **reuses the guard's module enumerator** (`SystemSources().EnumerateModules`,
  `LIST_MODULES_ALL`, §1) rather than carrying a second walk, so the baseline and
  the product see the same module list — including the same fail-closed
  behaviour. An `INCOMPLETE` or `FAILED` scan is printed as such and must never
  be read as "no capability loaded".
- It reads the capability data in its **own translation unit**. The guard's jsmn
  parser deliberately reads only the `anticheat` subtree, and teaching it a group
  the hard gate does not need would spend the gate's parse budget (§S17) on
  inference data.
- **Proven both directions**, ctest `fl_baseline_probe`: a clean process reports
  nothing loaded, a planted module *is* detected, and the answer flips back after
  unload rather than latching. The planted module is **our own
  `FrameLedger.Guard.dll` copied under a capability name** (`14_TESTING`
  §Integration tests) — no NVIDIA, AMD or Intel binary is shipped, downloaded or
  executed, and none needs to be installed for the test to mean something.
- Proven red twice: a probe that never reports `loaded`, and findings that latch
  across scans.

### 🔴 The comparison item 4 asks for cannot be a percentage

**Stated before any README wording is drafted, because the roadmap's phrasing
("quantify the improvement") invites a number that would have to be invented.**

Item 4 asks the baseline about upscaler identity, quality preset, render vs
output resolution, and DLSS-G activity. The baseline can answer **none of them**:

| Item 4 asks | Baseline answers | Hooks answer |
|---|---|---|
| upscaler identity | *ships / loads* DLSS — a different statement (`05_DETECTION`:10) | measured |
| quality preset | **nothing** | measured |
| render vs output resolution | **nothing** | measured |
| DLSS-G active | *ships / loads* DLSS-G | measured |
| engine · platform · version | detected | not measured by hooks at all |

A loaded `nvngx_dlss.dll` means the title *can* use DLSS this run. It does not
say whether DLSS is on, at what preset, or at what render resolution — and a
title that ships the DLL and has it disabled in the menu is indistinguishable
from one using it.

So the honest form of ADR-7's claim is **"the baseline cannot answer four of
these five questions at all"**, not "N% more accurate". That is both stronger and
checkable. **Owner decision, recorded here rather than assumed.**

- **Quantified improvement (belongs in the README):** *not a percentage — see
  above. Fill the per-title table once an offline title is chosen.*

### Still unmeasured

The table above is **empty on purpose**. The probe's mechanism is proven against
our own process; the per-title rows need a real, offline, anti-cheat-free game,
which is the same thing P0 item 2's remaining half needs. Filling them from
`hook-harness` would produce a number that does not generalise, which is worse
than recording them as unmeasured.

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
