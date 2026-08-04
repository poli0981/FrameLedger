# P0 spike notes

P0's named deliverable (`15_ROADMAP` §P0). Where measured results go, not
predictions. (It began empty by design and is now the longest record here.)

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

### ✅ Done — 2026-08-03

- **Title:** Lies of P (Steam, Unreal, x64, single-player, no anti-cheat).
  Injected into `LOP-Win64-Shipping.exe`, the presenting process — `LOP.exe` at
  the install root is a shim and was left alone.
- **`/MT` DLL loaded without incident: yes.** `FlGuardEvaluate` → `Allow`;
  `FlGuardedInject` → `Allow`; `FrameLedger.Overlay.dll` present in the target on
  re-enumeration (143 → 144 modules), mapped at `0x7FFA60340000`, 820 KB.
  Afterwards: alive, responding, **103.6 s of CPU over 6 s wall-clock** across
  180 threads — still rendering, not stalled.
- **It also shut down cleanly with the Overlay still mapped.** `WM_CLOSE`, and
  both `LOP-Win64-Shipping.exe` and the `LOP.exe` shim exited on their own; no
  hang, no crash dialog, no orphan. Worth stating because process exit runs
  `DllMain(DLL_PROCESS_DETACH)` on a loader-locked thread, which is the one
  lifecycle path a `/MT` static-CRT DLL is most likely to fail on, and it is not
  exercised by `hook-harness` — the harness is killed, not closed. **Nothing is
  hooked yet**, so this clears the loader, not the unhook path; P1's fault
  policy and unhook still need the same check once `Present` is hooked.
- Reached through the shipped C ABI (`FrameLedger.Guard.dll`), so there was no
  path that skipped the gate. There is no injector CLI and there will not be —
  §S9 closed that as a design decision, not as a gap.

**The three refusals that came first are the valuable part.** Every one was a
real defect, and none would have surfaced without a real machine and a real
title:

| Attempt | Verdict | What it actually was |
|---|---|---|
| Deadly Heart Gambit, no rules installed | `RulesUnreadable` | Correct. A machine that has never run the product refuses by default. |
| Same, rules installed | `BlockedService` `EasyAntiCheat_EOS` | **Defect.** A Stopped/Manual service installed by an unrelated EOS game made the guard refuse *every process on the machine*, `explorer.exe` included. Fixed: present now means running. |
| Same, from the launching shell | `SuspiciousUnsigned` `FrameLedger.Guard.dll` | **Defect, open.** Our own DLL trips our own `guard` name fragment. See below. |
| Same, from a non-ancestor process | `Allow` → injection refused | Correct, and now legible: **Deadly Heart Gambit is x86**, and the Overlay is x64-only. This fills `14_TESTING`'s manual-matrix row for a 32-bit title. |

### ✅ Closed — the guard refused itself in launch mode *(fixed 2026-08-04)*

Isolated cleanly on the same title, same machine, same rules:

| Evaluating process | Verdict |
|---|---|
| An **ancestor** of the game, with `FrameLedger.Guard.dll` loaded | `SuspiciousUnsigned` |
| Not an ancestor | `Allow` |

§S16 walks the game's ancestors up to the first platform launcher. The name
`FrameLedger.Guard.dll` contains `guard`, which is one of the heuristic's
`nameFragments`, and the signer half is not wired — so an unchecked signature is
untrusted by definition and the pair refuses.

**In launch mode the Agent *is* the parent** (`04_CAPTURE` §Launch mode) **and
hosts that DLL**, so every launch-mode injection would refuse. Attach mode is
unaffected, which is why the run above succeeded. Not fixed here; recorded as its
own item.

Note the project also ships **unsigned** (CLAUDE.md rule 9 and the pinned-stack
Packaging row — *not* `12_BUILD`, which this line used to cite and which contains
no signing text at all), so wiring the signer check would not rescue our own
binaries. Whatever the fix is, it cannot be "sign it".

#### ✅ Fixed and re-measured on three real titles — 2026-08-04

The arrangement was reproduced end to end rather than argued: a stand-in for the
Agent, built into `FrameLedger.Agent`'s own output directory **beside a real
`FrameLedger.Guard.dll`**, loads that DLL and then **launches the game**. So our
binary is the game's ancestor and carries the module whose name trips the guard's
own `guard` fragment — the 2026-08-03 shape, with a real title on the other end.

Evaluate only. `FlGuardedInject` was never called; nothing was injected into any
game.

| Title | Store / engine | Pre-fix (`bb0da0a`) | Post-fix |
|---|---|---|---|
| Deadly Heart Gambit | Steam / Unity, x86 | `SuspiciousUnsigned` — `FrameLedger.Guard.dll` | **`Allow`** |
| Lies of P | Steam / Unreal, x64 | `SuspiciousUnsigned` — `FrameLedger.Guard.dll` | **`Allow`** |
| Alan Wake 2 | Epic / Northlight | `SuspiciousUnsigned` — `FrameLedger.Guard.dll` | **`Allow`** |

Two things about the harness that are worth keeping, because both produced a
confident wrong answer first:

- **The evaluator must not be in the target's ancestor chain.** The first attempt
  used `Start-Process`, so the evaluating process — which had P/Invoked into the
  guard and therefore carried `FrameLedger.Guard.dll` itself — was the game's
  grandparent. Post-fix still refused, and it looked like the fix did not work.
  It did; the refusal was about the evaluator, whose image is in
  `C:\Program Files\PowerShell`, correctly *not* ours. The script now walks the
  chain and asserts the evaluator is absent from it before reporting.
- **Launching the stand-in through WMI put `WmiPrvSE.exe` in the scan set**,
  which an unelevated guard cannot open — a correct `ProcessUnreadable` about
  something irrelevant. Orphaning it via `cmd /c start` terminates the ancestor
  walk at the stand-in instead.

An earlier iteration also stopped at `PreScanFailed` because the target was
`cmd.exe` and check 4 then tried to list `System32`. That is check 4 answering
correctly about the wrong directory — noted because it is the shape of a green
that means nothing.

> **Decided 2026-08-03 — identity by install root.** A
> four-lens panel, three refuters and a completeness critic. All three refuters
> broke the panel's first answer. What survived: suppress the fuzzy fragment tier
> for any scan-set process whose image resolves inside our own install directory,
> never for the target, ancestor walk left intact. `GetCurrentProcessId()` was
> rejected because the defect is a property of the **binary** and
> `FrameLedger.App` carries it too; the platform-launcher-style boundary was
> rejected because everything above the Agent is an undefined category, so
> truncating there is "could not look" recorded as clean. Full reasoning and the
> rejected options in §S18.
>
> **The panel also measured that this is worth more than it looked.** §S18 is the
> sole blocker of the entire **Vulkan Tier-1 path**: the layer is gated by
> `FRAMELEDGER_ENABLE_VK_LAYER=1`, which only the launching process can set, and
> §S1 does not apply there because the Vulkan path performs no injection at all.

#### ◐ The same tier matches benign system DLLs — measured twice, and the second time changed the conclusion

**First measurement (2026-08-03), unelevated, Windows 11 26300:** 331 processes,
0 access-denied, 4 modules matching the `protect` fragment, none anti-cheat.
Recorded as "a game that loads `mskeyprotect.dll` is refused today, in the mode
that ships", and used to justify wiring the signer half next.

**Re-measured 2026-08-04 on the same machine, and neither half of that survived:**
290 processes, 0 inaccessible, **3 hits**.

| Process | Module | Signature type | `O=` | Suppressed by a file-choice signer check? |
|---|---|---|---|---|
| `WidgetService` | `mskeyprotect.dll` | **Catalog** | Microsoft Corporation | **No — no embedded signature to read** |
| `ProtonVPN.Client` | `System.Security.Cryptography.ProtectedData.dll` | Authenticode | Microsoft Corporation | Yes |
| `Malwarebytes` | `Malwarebytes.Protection.Interop.dll` | Authenticode | **Malwarebytes Inc** | **No — publisher absent from `trustedSigners`** |

Two findings, both of which change what gets built:

- **The proposed fix addresses one case of three.** The signer table in §1 above
  was taken with `Get-AuthenticodeSignature`, which consults the CatRoot
  catalogs — so it reports `O=` for files that carry no embedded signature at
  all. `mskeyprotect.dll`, `kernel32.dll` and `nvapi64.dll` are all
  `SignatureType=Catalog`. A native `WinVerifyTrust` with `WTD_CHOICE_FILE`,
  which is what "wire `IsTrustedSigner`" means in every prior note, recovers
  nothing for them; `IsTrustedSigner(nullptr)` is false by contract and the
  refusal survives. Catalog verification needs `CryptCATAdminAcquireContext2` /
  `CalcHashFromFileHandle` / `EnumCatalogFromHash` + `WTD_CHOICE_CATALOG`.
  **A measurement taken with a convenient tool does not license a design built on
  a different one.**
- **"Refused today, in attach mode" is not supported.** All three hits are
  desktop processes — a widget host, a VPN client, an AV service. None can enter
  a game's scan set: `EnumerateScanSetImpl` stops the ancestor walk at the first
  `IsPlatformLauncher` match, and that list includes `explorer.exe`,
  `services.exe` and `svchost.exe`. Three real titles (§8) were scanned with no
  fragment hit at all.

The defensible claim: **the `protect` fragment matches a benign, widely-loaded
Microsoft system DLL, and has not been shown to match inside any game's scan
set.** A game process loading DPAPI/CNG for save encryption or a launcher token
would trip it — plausible, unmeasured, and worth fixing; not a live incident.

§S19(b) is therefore **deferred with a written rationale** rather than scheduled
next. Before any design is fixed, `fl-probe-signer` should answer three things on
this machine, in the shape `fl-probe-guard` established: whether
`WinVerifyTrust(WTD_CHOICE_FILE)` recovers `O=` from a catalog-signed system
binary; what one full `Evaluate()` costs with catalog verification across a real
scan set, against the 30 s re-scan; and whether `WTD_REVOKE_NONE` +
`WTD_CACHE_ONLY_URL_RETRIEVAL` emits network traffic — the default
`WTD_REVOKE_WHOLECHAIN` performs CRL/OCSP fetches, which breaks **NFR-10
offline-first** as well as CLAUDE.md rule 8, from inside the hard gate.

Also measured, and unchanged: **`gameguard` can never fire.** The match is a
case-insensitive substring and `guard` is a substring of `gameguard`, so the
shorter token always wins first. A shipped rule incapable of firing
independently, inside the safety gate.

Neither is fixed by deleting a fragment — that is a detection removal in a hard
gate, and this tier is the only coverage for the families the seed admits it has
no data for (Ricochet, VAC).

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

### ✅ The static half, measured on three real titles (2026-08-03)

First run of the detector against real installs. **It failed on three of four
cases, and the failures were the point** — both were real defects that only a
real layout could expose.

| Title | Store / engine | On disk (scanned by hand) | Detector, after the fixes |
|---|---|---|---|
| Deadly Heart Gambit | Steam / Unity | `UnityPlayer.dll`, `steam_api.dll` | `unity` **2022.3.32.13119501**, `steam` |
| Lies of P | Steam / Unreal | `nvngx_dlss.dll`, `amd_fidelityfx_dx12.dll`, `steam_api64.dll` | `unreal`, `steam`, **dlss + fsr** |
| Alan Wake 2 | Epic / *(Northlight)* | `EOSSDK-Win64-Shipping.dll`, 6 NGX/Streamline DLLs | `epic`, **dlss + dlss_g + dlss_rr + streamline** |

Alan Wake 2 reports **no engine**, correctly: Remedy's Northlight is not in the
rules table, and "no rule matched" is a real answer rather than a failure. Lies
of P reports no engine *version* because its `LOP-Win64-Shipping.exe` carries no
`ProductVersion` for the regex to read — also honest.

**None of the three carries anti-cheat**, so all three are legitimate candidates
for the first real injection (item 2).

#### 🔴 Defect A — a depth cap that made the detector useless

`GameFileProbe` capped the walk at depth 4. Measured real depths: **Deadly Heart
Gambit 6, Alan Wake 2 5, Lies of P 9.** Every real game exceeded it.

Worse than the cap was what incompleteness *meant*: an unfinished walk marked
`FileExists`, `SiblingGlob` and `DirExists` all uncollected, so **every**
file-based signal became `Unknown`, the engine walk stopped at its first rule,
and nothing was ever identified. Failing safe is right; failing safe on every
input is not working.

Fixed by separating the two questions. A file the walk **listed** is a file that
is there, however early it stopped afterwards — only *absence* is in doubt. So a
hit stays `Match` regardless, and only a miss becomes `Unknown` when the listing
did not finish. Caps raised to depth 16 / 200,000 entries, with the entry count
as the real bound.

#### 🔴 Defect B — the pre-scan was looking in the wrong directory

**This one is a hole in a hard gate, not a detection miss.**

Both the probe and `ImageDirectoryImpl` derived "the game directory" by stripping
the filename from the executable's path. Unreal puts the exe at
`<root>\<Project>\Binaries\Win64\` — measured on Lies of P, that directory holds
**seven files**, none of which could ever have been an anti-cheat SDK, because
`EasyAntiCheat/` sits at the install root three levels up.

So for exactly the layout most likely to carry EAC, **check 4 scanned a folder
that could not contain what it was looking for** and returned clean. It also cost
Lies of P its DLSS capability, which is how the defect was noticed at all.

Fixed by `ResolveInstallRoot`: walk up from the executable to a hardcoded
platform boundary (`steamapps\common\<X>`, `GOG Galaxy\Games\<X>`,
`Epic Games\<X>`) and scan from there. Hardcoded for the same reason
`IsPlatformLauncher` is — a data-driven boundary would let a rules update move
where the hard gate looks.

**When no boundary is recognised the executable's own directory is kept.** Alan
Wake 2 is installed at `D:\another\epic\AlanWake2` with no store marker in the
path; walking up blindly would reach a folder of unrelated games, and refusing a
title because a *sibling* ships anti-cheat is a false refusal with no appeal.
That residual is stated rather than hidden: a nested executable outside a
recognised store layout is still scanned from its own directory.

#### The entry point must not change the answer — and now it does not

Unreal titles conventionally ship **two** executables: a shim at the install root
and the real shipping binary nested under `<Project>\Binaries\Win64\`. Lies of P
has `LOP.exe` and `LOP-Win64-Shipping.exe`.

That matters because a user adds one of them to the watchlist, and the guard is
handed whichever process it is handed. Before this resolution existed the two
disagreed:

| Entered via | Before | After |
|---|---|---|
| `LOP.exe` (root shim) | engine **undetermined**, capabilities **none** | `unreal`, `steam`, `dlss + fsr` |
| `LOP-Win64-Shipping.exe` | `unreal`, `steam`, **`fsr` only** | `unreal`, `steam`, `dlss + fsr` |

Both now resolve to the same install root and produce identical results, which is
asserted in both languages rather than left as an observation.

### Still unmeasured

The runtime table above is empty on purpose. The static half is now real and has
been run on three real installs; the per-title runtime rows still need a
**hooked** session.

**That is no longer item 2.** Item 2 closed on 2026-08-03 — the Overlay loads
into a real title and the process survives it (§7). But nothing is hooked yet:
`Present` interception, the ring writer and the fault policy are P1 by
construction. So these rows are blocked on P1, not on finding a game.

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

## 12 · The gate's own inputs — §S21 and §S20 *(2026-08-04)*

Analysis and the decisions live in `20_OPEN_QUESTIONS` §S21 and §S20. What
belongs here is what was **run**.

### ✅ §S21 · The override, demonstrated end to end on two real binaries

Same input to both — a crafted twelve-line rules file naming the three families
`IsCompleteEnoughToGate` required, with values that match nothing, plus a real
`EasyAntiCheat_x64.dll` loaded in the target:

| Binary | Verdict |
|---|---|
| `bb0da0a`, the tree before the fix | **`Allow`** |
| The fix branch | **`BlockedModule`**, family "Easy Anti-Cheat" |

No admin, no write to our install directory, nothing left on disk. This is the
one measurement to keep from the whole item: the hard gate could be turned off by
an inherited environment variable and a text file.

### ✅ §S21 · The ANSI path, on the real Win32 calls (system ACP 1252)

A profile component the system code page cannot spell exists on disk and
`CreateFileW` opens it, while `CreateFileA` fails `ERROR_INVALID_NAME (123)`.
Under an ASCII profile the same file opens both ways — which is exactly why a dev
box cannot see this, and why it would have shipped.

**A measurement of my own that was wrong first.** The initial round-trip used
`[Text.Encoding]::Default`, which in .NET Core is **always UTF-8** and not the
process ANSI code page, so it reported no loss. Re-run against the real ACP, it
lost the characters. *Measuring the wrong API is indistinguishable from measuring
a working one.*

### ✅ §S21 · What the first floor actually covered

Measured against the shipped seed, the hand-written three-family floor held
**4 of 22 values, 2 of 5 groups, 0 of 5 name fragments** — so it closed "a crafted
file allows everything" and left "a crafted file removes most of the blocklist"
open. Recorded because the write-up read as though it bounded more than it did.
The floor is now generated from `rules/detection-rules.json` at build time and
carries all of it.

Two residuals measured rather than asserted:

- The known-folder path **is** still user-relocatable — the `User Shell Folders`
  key under `HKCU` is FullControl for the user. That is a persistent change
  affecting every application, not a per-launch variable, so the fix is a
  narrowing and is written down as one.
- Moving the layer to `SHGetKnownFolderPath` added `ole32` and `SHELL32` to its
  imports. Resident in **62%** and **61%** of inspectable processes respectively,
  so the layer takes on no meaningful new load-time surface.

### ✅ §S20 · `MoveFileEx` vs `ReplaceFileW` against a live reader

`MoveFileExW(MOVEFILE_REPLACE_EXISTING)` returns **`ERROR_ACCESS_DENIED`** when
the destination is open by a reader holding the guard's exact share mode
(`READ|WRITE|DELETE`). `ReplaceFileW` succeeds. **This repository had prescribed
the failing one** — in a source comment and in the ledger — as guidance for
whoever built the seeder. A prescription nobody ran is a guess in the imperative
mood.

### ✅ §S20 · `rulesVersion` does not track the blocklist

The first seeder design replaced the installed file when the packaged seed was
strictly newer. Measured against this repository's own history of that file:
`31825cf` and `ba5355e` both changed the `anticheat` block **without** touching
`rulesVersion`, and `a4a2c63` — the only commit that bumped it — changed the block
**not at all**. The rule would have delivered none of the two changes it existed
for. The seeder records a hash of what it installed instead.

### ✅ §S20 · The seed half, on the machine's real rules location

Remove the file → the guard answers `RulesUnreadable`. Run the Agent → the guard
reads its rules and reaches check 1. Four Agent branches exercised against the
real location, with the machine's own file backed up and restored by hash:
already-current, installed-from-nothing, idempotent second run, corrupt file
replaced.

The feed half is untouched, so a rules edit still reaches no installed machine
until a release.

### 🔴 The canary harness lied three times before it caught anything

Kept because this file's rules ask for dead ends, and because this one produced
three confident greens in one session:

1. `ctest`/Catch2 with `-c "*name*"` **selects nothing and exits 0**, so every
   "the canary passed" was vacuous.
2. The rewritten check matched only Catch2's **failure** summary, so a genuinely
   green run parsed as "the filter selected nothing".
3. `build.ps1`'s throw escaped the harness and **left a canary edit on disk** —
   the next build then failed on an unrelated `/W4 /WX` unused-parameter warning,
   which was nearly diagnosed as the canary working.

Now: tag filters, an assertion that assertions actually ran, and `try`/`finally`
around the build. A canary that cannot go red is the defect it was written to
catch, one level up.

---

## Exit criteria

`15_ROADMAP` §P0. Nothing in P1 starts until all are met.

- [ ] A throwaway build records a real session from a real offline game
      reporting **correct** upscaler, quality preset, render → output
      resolution, FG factor and RT state — verified against the game's own
      settings menu.
- [x] **Moved to the end of P1** (§R4, decided 2026-08-02) — ~~measured game FPS
      impact under 0.5%~~. As written it imported P2's drain, aggregate and
      recorder paths; the harness-level per-present cost stayed in P0 and is
      measured in §3 (8.4 ns).
- [ ] Every S-series item in `20_OPEN_QUESTIONS.md` resolved, or explicitly
      deferred with a written rationale.
- [x] M3 and M4 answered **before** any NVAPI or LHM code is written — both
      CLEAR, §0 above.

> ~~Note (§R4): … Decide before starting, not halfway through.~~ **Decided
> 2026-08-02** — the FPS-impact criterion moved to the end of P1, and the
> harness-level per-present cost stayed in P0 as item 2. This file went on asking
> for a decision that two other documents had already recorded.
