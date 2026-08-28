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

### ◐ §S19(b) · the signer half, measured before it is designed — `fl-probe-signer` (2026-08-27)

`ctest fl_signer_probe`, unelevated, dev box. §S19(b) asked for three answers before
any design is fixed; these are they. The analysis and the decision rows live in
`20_OPEN_QUESTIONS` §S19(b) — what belongs here is what was **run**.

**Q1 · which route recovers an organisation.** Offline flags throughout
(`WTD_REVOKE_NONE | WTD_CACHE_ONLY_URL_RETRIEVAL`):

| Subject | embedded | catalog | `O=` |
|---|---|---|---|
| `System.Security.Cryptography.ProtectedData.dll` (NuGet **6.0.0**) | **`ERROR_SUCCESS`** 3.61 ms | no catalog | `Microsoft Corporation` |
| `mskeyprotect.dll` | **`TRUST_E_NOSIGNATURE`** 0.23 ms | **`ERROR_SUCCESS`** 3.62 ms | `Microsoft Corporation` |
| `kernel32.dll` | `ERROR_SUCCESS` 4.69 ms | `ERROR_SUCCESS` 3.48 ms | `Microsoft Corporation` |

Full subject on every one: `C=US, S=Washington, L=Redmond, O=Microsoft Corporation,
CN=Microsoft Windows` — so `signerField: "O"` holds here, on a subject class it had
never been measured against.

**Two results that cut opposite ways.** The CI blocker is embedded-signed and verifies
offline, so the embedded half alone would clear that refusal. `mskeyprotect.dll` — the
module §S19(b) was *written about* — does not, and needs `CryptCATAdmin*`. The entry
predicted exactly that and it is now measured rather than argued.

**`kernel32.dll` carries BOTH**, which the probe was written assuming it would not.
"Catalog-signed" is not inferable from a file being a system binary; it is a per-file
measurement, which is why the probe reports both routes for every subject.

**And the module is not what §S19(b) says it is.** It calls the blocker *"a .NET
shared-framework assembly"*. `Microsoft.NETCore.App` 10.0.11 does not contain it. It
is a NuGet package assembly reached transitively as `Microsoft.NET.Test.Sdk` →
`System.Configuration.ConfigurationManager` → `System.Security.Cryptography.ProtectedData`,
staged next to every test binary. **Dropping the reference means dropping the test
SDK**, so the cheapest-looking route out of the CI refusal is closed.

**Q2 · cost.** Cold 3.45 ms; warm mean of 20 = **3.54 ms**. There is no amortisation:
the second verification of the same file costs what the first did. Comfortable against
the 30 s re-scan **because the scan set is small** — three real titles produced no
fragment hit at all — and it stops being comfortable on a title that trips the fuzzy
tier repeatedly.

**Q3 · 🔴 `cryptnet.dll` is newly loaded UNDER THE OFFLINE FLAGS.** Census bracketing
the offline arm alone: `cryptnet.dll` appears. Nothing further appears once the
default `WTD_REVOKE_WHOLECHAIN` calls run, so this is not the default arm leaking into
the measurement.

> **The first version of this probe could not have told those apart.** It censused
> once at the top and once at the bottom with both arms in between, so the module
> appeared and the delta could not attribute it — a census spanning both arms of the
> comparison it exists to discriminate. Fixed before the run recorded above. Same
> shape as §S30's *"two numbers agreeing can be one number read twice"*, one layer
> down.

**Mapped is not transmitted, and this probe cannot close the gap.** `cryptnet.dll`
being loaded is the module that *would* make a request, not evidence that one was
made; there is no packet counter here. **The discriminating run is the owner's:
adapters disabled, same three subjects, compare verdicts.** A verdict that changes
offline retires the route, and §S19(b)'s row G2 says so before the run.

**Canary.** The probe's own unsigned executable returns `TRUST_E_NOSIGNATURE` and
yields no organisation — so a green result discriminates rather than merely running.
`fl-baseline-probe` was retired by exactly this class of test.

**Not measured here:** the CI leg (a different machine stages a different copy), and
the adapters-disabled leg. Both have pre-committed rows. An unrun leg is unrun.

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

### ✅ The Overlay is a real capture side now — and what it does NOT measure (2026-08-05)

Recorded here retrospectively: PRs #40–#44 changed 8 files, all under `src/native/`,
and touched no documentation, so none of the below was written down at the time.

| | |
|---|---|
| Hooks installed | **three**, on the shared `dxgi.dll` class vtable: `Present` (8), `ResizeBuffers` (13), `Present1` (22) |
| Acquisition | throwaway WARP D3D11 device + `CreateSwapChainForComposition`, vtable read, dummy released — no HWND, runs headless |
| Ring | `Local\FrameLedger.Ring.<pid>`, DACL = current user's SID only, `ERROR_ALREADY_EXISTS` refuses |
| ctests | **15**, incl. four `[inject][shm]` cases that inject the **real** Overlay into `hook-harness` cross-process |

**The `api` field, and a Windows API that does not do what the docs imply.**
`IDXGISwapChain::GetDevice` on a **D3D12** swapchain returns the **device**, not the
command queue — even though `CreateSwapChainForComposition` is *given* the queue. DXGI
resolves the queue to its owning device before storing it. The obvious implementation
(query the result for `ID3D12CommandQueue`) made every record from a real D3D12 target
come back `FL_API_UNKNOWN`. Both queries are kept: a DXGI that *did* hand back the queue
would otherwise regress to `UNKNOWN` silently.

**What the Overlay measures today is 8 of 23 record fields**, and the other 15 are
reported as *not measured* rather than defaulted:

| Written | Left at "not measured" |
|---|---|
| `qpc`, `frameIndex`, `presentFlags`, `syncInterval`, `api`, `swapchainId`, `outputW/H` | `renderW/H`, `upscaler`, `upscalerQuality`, `fgMode`, `fgEvaluations`, `rtFlags`, `dispatchRaysVolume`, `maxTraceRecursionDepth`, `psoCreatedThisFrame`, `vramUsedBytes`, `reflexLatencyUs`, `hdr` |

`measuredMask = FL_MEASURED_OUTPUT_RES` and `rtFlags = FL_RT_NOT_MEASURED` on every
record. Without those two bytes a present-only writer would assert "no upscaler, no
frame generation, no ray tracing" as *measured fact* ~118 times a second — `fg_factor`
1.0 (rule 6) and a definite RT `No` (rule 7) about a title nobody looked at.

**Two fields have a producer and no consumer, and one has neither:**
`FlWriterState::vramBudgetMb` is never written (no `QueryVideoMemoryInfo` hook);
`FlControlBlock::overlayEnabled` is never read; and `FlControlBlock::pauseRequested` is
never written by anything, while its only reader ~~is unreachable on any frame where
`guardTicks` changed~~.

> **Half of that last clause was fixed five lines below it, in the same PR, and this
> sentence was left in the present tense.** The reader is no longer unreachable — #46
> moved the deadline onto the watchdog thread, which removed the early return that
> jumped the pause check. What *is* still true is the first half: nothing writes
> `pauseRequested`. `ShmRingReader.SetPaused` is the intended writer and it has **no
> caller and no test at all** — three symbol sites in the whole tree, none of them a
> test — so the round trip has never been driven end to end even though both halves
> exist and the harness that would drive it is already staged.

**Two gaps found the same day by tracing call paths, and one is still open.**

- ✅ **Both runtime stops were unreachable in a non-presenting process**, and
  `pauseRequested` was unreachable on any frame where `guardTicks` changed — one root
  cause, `MayObserve()` having exactly one caller. Measured in both directions:
  `unhookRequested` against a live `--hold` target left `status` at `READY` through 10 s
  of polling, and a paused session leaked **12 records across 12 guard ticks, exactly one
  per tick** (`writeIndex` 9 → 21). Closed by the watchdog thread (§S25).
- 🔴 **The three-fault self-disable still has no test at all.** `NoteFault` discarded
  `MH_DisableHook`'s return and set no `g_observing`; that is fixed, without a regression
  net. The blocker is the vehicle — a fault seam compiled into a DLL that ships into
  games is rejected on sight, and the `VirtualProtectEx` alternative cannot locate the
  target's view base. Both approaches and their failure modes are written into
  `src/native/tests/CMakeLists.txt`.
- The 65-second value **in its real configuration** is also unverified: the stop is proven
  to fire, but a suite that runs on every build cannot wait 65 s, so what is tested is the
  mechanism, not the number.

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

### ✅ Answered 2026-08-05 — `docs/vendor-exports.json`

Measured across **34 distinct modules in 162 files** from installed titles by
`tools/vendor-exports.ps1` (dumpbin over files on disk; nothing is loaded). The
map is committed rather than transcribed here, because a table copied by hand is
a second source that drifts from the first.

**The finding this item existed to produce** — the NGX parameter surface splits
into two hook classes, and only one half is an exported function:

| Module | Parameter **accessors** | Parameter-object **factories** |
|---|---|---|
| `sl.common.dll` (Streamline) | **yes**, all 16 | yes |
| `nvngx.dll` / driver-store `_nvngx.dll` | **no** | yes (D3D11/12/VK/CUDA) |
| `nvngx_dlss.dll`, `_dlssg`, `_dlssd` | no | no |

Streamline-shimmed titles (9 of those installed here) expose
`NVSDK_NGX_Parameter_SetI/SetUI` as ordinary exports. NGX-direct titles do not:
the accessors are function pointers inside the object the factories return, so
there is no symbol to hook. `17_HOOK_ENGINE` §The NGX parameter surface carries
the decision and its CLAUDE.md rule-4 justification.

**Not gated.** Nothing cross-checks `17_HOOK_ENGINE`'s symbol table against the
JSON, so they can drift — that gate belongs with the PR that adds the feature
hooks, where a failing row would mean something. The map is also one machine on
one driver version, and the JSON says so in its own `$comment`.

## 5 · Proxy swapchains *(§H5)*

### ✅ The shared-vtable premise, proven both directions — ctest `fl_vtable_identity_control`

`fl-probe-interposer` (2026-08-05). Two composition swapchains created by
independent routes through the real `dxgi.dll` report **one vtable**
(`0x…842BC688`, stable across runs), and a different interface reports a
different one. The second half is what makes the first readable: a comparison
that has never been shown to detect a *difference* carries no information when it
reports "same".

This is the property `17_HOOK_ENGINE` §`swapchainId` and the whole vtable-hook
design rest on, and it is now a ctest rather than a one-off measurement — if it
ever stops holding, the build fails instead of a game discovering it.

### ◐ The interposer question is NOT answered, and the first run of the probe answered it wrongly

Measured against **Cyberpunk 2077** (`sl.interposer.dll` 2.7.1) and **Black Myth:
Wukong** (2.7.4), loaded into our own process — no game running, no injection, no
guard.

| | |
|---|---|
| The interposer loads standalone and exports `CreateDXGIFactory2` | its address differs from `dxgi.dll`'s, so we are genuinely on its code path |
| Swapchain vtable through the interposer | **identical** to the real one |
| **Factory** vtable through the interposer | **also identical** |
| `sl.*` plugins mapped afterwards | **none** — only `sl.interposer.dll` itself |

**That last row is why the first three mean nothing, and the probe was changed to
say so.** As first written it printed *"VERDICT: THE SAME — a vtable-slot present
hook DOES catch presents made through the Streamline interposer"* for both
titles. It was a confident wrong answer. Streamline forwards straight to
`dxgi.dll` until `slInit()` has run and a feature is loaded, so what was measured
is that **passthrough is passthrough**. The tell was in the output the whole
time: a genuinely interposing Streamline cannot leave the *factory* vtable
unwrapped, because wrapping the factory is how it reaches the swapchain.

The probe now enumerates its own loaded modules and refuses to render a verdict
when no plugin is mapped, exiting **2 = inconclusive**. Same discipline as the
guard's tri-state collectors: "could not look" must not read as "looked and it
was clean".

**What answering it actually needs, stated rather than left to be rediscovered:**
`slInit()`, whose `sl::Preferences` argument is vendor ABI — i.e. the licence
question `legal/THIRD_PARTY_NOTICES.md` settles for Intel IGCL ("re-declaring the
API by hand is explicitly NOT an approved workaround") and which is unanswered
for NVIDIA. So §H5 case 3 is **blocked on a licence decision, not on hardware**,
which is a different and more tractable blocker than "needs a real DLSS-G
session". The alternative route is unchanged: observe a real title once the
Overlay has a present hook.

**What is bounded now:** the risk is confined to titles that go through the
Streamline interposer. NGX-direct titles call `nvngx*.dll` and never wrap the
swapchain, so the vtable premise is not in question for them.

## 6 · RT detection

### ✅ The pre-flight, before a single hook was written — 2026-08-20

`hook-harness --probe-d3d12-vtable` (ctest `fl_d3d12_vtable_indices`) and
`--probe-dxr` (ctest `fl_dxr_probe`), on this machine: Windows 11 Insider 29648,
RTX 5080, MSVC 14.51.

**Adapters, and all three can ray-trace.**

| Adapter | D3D12 | `RaytracingTier` |
|---|---|---|
| NVIDIA GeForce RTX 5080 | yes | **12** (`TIER_1_1`) |
| Microsoft Basic Render Driver | yes | **12** |
| WARP (software) | yes | **12** |

So `HANDOFF` item 4's *"check first whether WARP on the CI runners supports DXR"*
is answered **yes on this machine's WARP**, which means a DXR fixture is not
condemned to a GPU box. CI still has to answer for its own `d3d10warp.dll`; the
tier is a property of that binary, and `fl_dxr_probe` prints it on every run.

**Slot indices, proved by behaviour on BOTH list types.** Slot 72 is
`BuildRaytracingAccelerationStructure` and slot 76 is `DispatchRays`: patched,
called by name through the interface, detour ran, slots restored and the restore
asserted. 60 consecutive runs of each probe, zero failures.

### 🔴 `Reset()` moves `DispatchRays` into the vendor driver, and the first version of the hook never fired

**THE FIRST THREE ANSWERS WERE MEASURED ON THE WRONG OBJECT, and two of them were
wrong because of it.** The probe originally read the vtable off a **freshly
created** command list and compared vtable ARRAYS. A game's list is Reset every
frame, and the Overlay patches the FUNCTION a slot points at rather than the slot
— so both choices measured something no hook depends on. Corrected here rather
than quietly re-run: the wrong answers are the useful part.

| Question | measured on a **reset** list, comparing **functions** |
|---|---|
| two DIRECT lists, one device | **same functions** — one inline patch covers every list. Their vtable ARRAYS differ (per-object after Reset), which is why a *slot* patch would have to be repeated and a *function* patch does not |
| a command allocator (the control) | different, as it must be |
| DIRECT vs COMPUTE | **same functions** — slot 72 `D3D12Core.dll` in both, slot 76 `nvwgf2umx.dll` in both. One detour per method covers both list types |
| a WARP list vs a hardware list | **DIFFERENT** — slot 76 is `nvwgf2umx.dll` on the RTX 5080 and `D3D12Core.dll` on WARP |
| a fresh list vs a reset list | **DIFFERENT** — slot 76 moves from `D3D12Core.dll` to `nvwgf2umx.dll`; slot 72 stays in `D3D12Core.dll` |

**The failure this produced, before the fix.** The Overlay read its targets off an
unreset throwaway list, so it hooked `D3D12Core.dll`'s `DispatchRays` — a function
no title on this GPU ever calls, because the first `Reset` hands the method to the
driver. The hook installed, published `FL_HOOK_RT_DISPATCH`, and never fired. The
injected fixture caught it exactly: `withAsBuild = 60`, **`withDispatch = 0`**,
`hooks = RT_DISPATCH | RT_AS_BUILD` — a mask bit with nothing behind it, which is
the honesty failure the whole entitlement machinery exists to prevent.

**Why one hook worked and the other did not, which is what made it hard to read.**
`BuildRaytracingAccelerationStructure` stays in `D3D12Core.dll` across the swap, so
the AS-build hook fired from the first run. A pair where one half works reads as a
bug in the other half's *detour*, not in the *acquisition* both share.

**The fix is one call**: put the throwaway list through the same lifecycle a game's
list goes through — `Close()`, `Reset()` — before reading its vtable. `--probe-dxr`
Q5 now prints the module on each side of the Reset, so a runtime that stops
swapping, or starts swapping the other slot, says so rather than being discovered
by a silent zero on a real title.

**And Q4 stops being permissive.** With the correction, a WARP list and a hardware
list resolve `DispatchRays` to *different* functions, so a throwaway-**device**
acquisition would silently miss every call on an NVIDIA GPU. The Overlay takes its
targets off a list created on the **game's own device** — which was already the
design, for the unrelated reason that this machine lost WARP's D3D12 path to an
Insider build for a fortnight (§Traps), and now has a second reason that is about
correctness rather than availability.

**Bundles are not a further case**, and this is a documented API constraint rather
than something measured here: `D3D12_COMMAND_LIST_TYPE_BUNDLE` does not permit
`DispatchRays`. Stated as an inherited claim so the next reader knows which of
these lines came from a run.

### ✅ Both hooks, proved by injection — 2026-08-20

`ctest fl_guard`, the case *"the injected Overlay records ray-tracing work, and an
AS-build-only title is not a negative"*. Two harness modes that differ by **one
recorded call**, sharing their acceleration structure, state object, swapchain and
loop, so any difference in the record is attributable to that call:

| Mode | records | `FL_MEASURED_RT` | `AsBuildObserved` | `DispatchObserved` | `dispatchRaysVolume` |
|---|---|---|---|---|---|
| `--hold-presenting-dxr` | 60 | every one | 60 | ≥ 8 | exact multiple of 64×32×1 |
| `--hold-presenting-rayquery` | 60 | every one | 60 | **0** | **0** |

The rayquery arm is what makes `03_METRICS:226` falsifiable instead of a sentence:
a writer with only the `DispatchRays` hook sees **nothing** there, and its silence
is indistinguishable from a real negative — `rtTier` is 12, the mask bit is set,
evidence is zero, and the `No` branch fires about a title that ray-traces every
frame. **It does not run a RayQuery shader**, and does not need to: the claim under
test is "AS-build catches a title `DispatchRays` misses", and the absence of a
dispatch is what tests it.

`DispatchRays` also needs a **bound raytracing state object** to be RECORDED at
all: measured 2026-08-20, it access-violates inside D3D12Core with no state object
set, with a well-formed shader table and with a zeroed one alike. That is why the
fixture carries a 1.2 KB DXIL raygen library (`dxr_raygen.hlsl`, compiled by hand
and checked in with its command line).

### ✅ Both hooks against four real titles' own settings menus — 2026-08-20

§8 carries the five captures in full. What belongs here is the RT half:

- **`Yes` twice, `No` twice, every verdict agreeing with the game's own menu.** Cyberpunk with
  path tracing on and Wukong at RT High both read `Yes`; Rune Factory, whose menu has no
  ray-tracing option, and Cyberpunk with every RT option switched off both read **`No`** — the
  branch that had never been reachable in this project.
- **`rt_frame_pct` reads 25.0% at ×4 on two titles**, which is the falsifier `03_METRICS`
  §RT/PT/RR pre-committed. It did not fire.
- **`faults = 0` on every run**, with both detours on the render thread of titles running at up
  to ~950 presents/second.
- **The dispatch extent is exact.** Alan Wake 2: `4,913,280` rays per RT-active present, and
  `3 × 1706 × 960` is `4,913,280` — three rays per render pixel, against a render resolution
  the menu states outright.

### Still open in §6
- The same measurements on an **AMD or Intel** GPU. Every vendor-specific result
  above is one driver's: `nvwgf2umx.dll` takes `DispatchRays` and leaves
  `BuildRaytracingAccelerationStructure` alone, and nothing says another UMD splits
  them the same way — or leaves either in `D3D12Core`:
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

~~The defensible claim: **the `protect` fragment matches a benign, widely-loaded
Microsoft system DLL, and has not been shown to match inside any game's scan
set.** A game process loading DPAPI/CNG for save encryption or a launcher token
would trip it — plausible, unmeasured, and worth fixing; not a live incident.~~

> #### 🔴 MEASURED IN A REAL SCAN SET BY CI, 2026-08-05 — and this file is where that belongs
>
> The sentence above is superseded. CI, running the drain integration test:
>
> ```
> the guard refused our own harness: SuspiciousUnsigned unknown
> System.Security.Cryptography.ProtectedData.dll
> ```
>
> **The mechanism is the one the paragraph above ruled out.** §S16 puts the target's
> *ancestors* in the scan set; the integration test spawns `hook-harness`, so the .NET
> test host is its parent — which is the launch-mode arrangement, where the Agent is
> the game's parent. A .NET host that loads that assembly poisons its own scan set and
> the injection it is attempting is refused. **A gate that cannot pass.**
>
> Attach mode is unaffected: a normally-launched game's ancestor chain terminates at a
> platform launcher one hop above it, so no .NET host of ours is in the set.
>
> **It passed on the dev box**, because the two hosts load different module sets — the
> second time in one PR that this machine was not the configuration that mattered, the
> first being the rules file. This entry is recorded here rather than only in
> `20_OPEN_QUESTIONS` because §Purpose of this file makes measurements this file's job,
> and #51 changed only that other document.
>
> **Live consequence:** the drain tests are `Category=Integration` and CI runs
> `./build.ps1 check -SkipIntegration`, so the only end-to-end proof of the capture
> path does not run on the machine that gates merges.

§S19(b) is therefore **deferred with a written rationale** rather than scheduled
next. Before any design is fixed, `fl-probe-signer` should answer three things on
this machine, in the shape `fl-probe-guard` established: whether
`WinVerifyTrust(WTD_CHOICE_FILE)` recovers `O=` from a catalog-signed system
binary; what one full `Evaluate()` costs with catalog verification across a real
scan set, against the 30 s re-scan; and whether `WTD_REVOKE_NONE` +
`WTD_CACHE_ONLY_URL_RETRIEVAL` emits network traffic — the default
`WTD_REVOKE_WHOLECHAIN` performs CRL/OCSP fetches, which breaks **NFR-10
offline-first** as well as CLAUDE.md rule 8, from inside the hard gate.

> **THE PROBE NOW EXISTS AND HAS ANSWERED ALL THREE, 2026-08-27 — see the
> §S19(b) subsection in §1 above.** This paragraph is kept because the QUESTIONS it
> fixes are what the probe was built to, and because a reader arriving here needs to
> know the shape was decided before the answers existed. Two of the three came back
> the way it predicted; the third — whether the offline flags avoid network I/O —
> came back with `cryptnet.dll` LOADED, which is neither a pass nor a failure and is
> why the discriminating run is the owner's.

Also measured: **`gameguard` could never fire.** The match is a case-insensitive
substring and `guard` is a substring of `gameguard`, so the shorter token always
won first. A shipped rule incapable of firing independently, inside the safety
gate.

> **Closed 2026-08-05 by removing it, and the reason it was safe to remove is not
> the reason that applies to the fragment below.** These two findings sat in one
> paragraph and the paragraph's conclusion — *"neither is fixed by deleting a
> fragment"* — was true of one and false of the other.
>
> - **`protect`**: deleting it IS a detection removal. This tier is the only
>   coverage for families the seed admits it has no data for (Ricochet, VAC), and
>   three refuters rejected deletion. Unchanged, still open as §S19(b).
> - **`gameguard`**: deleting it removes **zero** coverage, by construction. That
>   is what subsumption means — every module name `gameguard` could match,
>   `guard` matches too, and matches first. Measured separately: nProtect
>   GameGuard also has its own **named module family** in the same file
>   (`GameGuard`, `npgg`, `GameMon`, exact-prefix), so the fragment was redundant
>   twice over rather than once.
>
> `rules-validate` now fails when any fragment contains another, so the list
> cannot regrow a rule that cannot fire. Note that removing it does trip
> `rules-publish`'s removal check — correctly. That gate exists to make a
> blocklist removal reviewable, and this is one.

### The guard against a RUNNING title, 2026-08-15

The precondition every verification run below depends on, and it had never been
measured against a live process. Everything earlier in this file evaluated the
guard in **launch-mode arrangement** — our binary as the game's ancestor,
evaluate-only, nothing injected — or scanned files on disk. Neither answers the
question the capture path actually asks: *does the guard allow this title while it
is playing?*

**Alan Wake 2, pid 40424, running, `FlGuardEvaluate` → `Allow`.** Reason 0, empty
family, empty signal. Rules resolved from
`C:\Users\Anon\AppData\Local\FrameLedger\rules\detection-rules.json`, 24 reason
codes.

**Evaluate only. Nothing was injected**, and no consent record exists for this
title — `FlGuardEvaluate` is the read-only half of the ABI and takes no injection
rights.

Why it needed doing at all: the installed-corpus scan (§Environment) is a **file**
scan, and the guard scans **loaded modules of the live process** plus the target's
ancestors (§S16). A title clean on disk can still load an anti-cheat module at
runtime, and Alan Wake 2 is launched by the Epic client, which is in the scan set.
Had this refused, every per-title row in §8 would have been undischargeable and the
upscaler/FG/RT work would have had no oracle to verify against.

**One title, one moment.** This is not a statement about Cyberpunk 2077, about
Alan Wake 2 on another machine, or about the same machine after a launcher update.
It is the one measurement that was missing before the feature hooks had anywhere
to prove themselves.

## 8 · The accuracy question — why this rewrite exists

≥ 3 real offline titles. Verify against each game's own settings menu.

| Title | Upscaler | Quality | Render → output | FG active | RT | Matches menu? |
|---|---|---|---|---|---|---|
| **Cyberpunk 2077** (2026-08-15) | ❌ `Unknown` | ⬜ `0xFF` | ✅ **1485×835 → 2560×1440** | ⬜ not measured | ⬜ not measured | **1 of 5** |
| **Cyberpunk 2077** (2026-08-15, later, after §S30) | ✅ `Dlss` | ⬜ `0xFF` | ✅ **1485×835 → 2560×1440** | ◐ on, factor not measurable on this route | ⬜ not measured | **2 of 5** |
| **Cyberpunk 2077** (2026-08-20, PT on · RR on · MFG ×4 · Balanced) | ✅ `Dlss` | ⬜ `0xFF` | ✅ **1485×835 → 2560×1440** | ◐ on, `presents/batch` = 4.00 as a PROXY | ✅ **`Yes`** | **3 of 5** |
| **Alan Wake 2** (2026-08-20, RR on · PT ultra · FG 4X *set but reportedly not applying*) | ✅ `Dlss` | ⬜ `0xFF` | ⬜ no local tag; **1706×960 corroborated arithmetically, see below** | ❓ `presents/batch` = 1.00 — unexplained | ✅ **`Yes`** | **2 of 5** |
| **Black Myth: Wukong** (2026-08-20, RT High · FG ×4 · Balanced) | ⬜ `Unknown` — *coverage short, not the title's* | ⬜ | ⬜ no local tag | ◐ `presents / RT-active` = **4.0000** as a PROXY | ✅ **`Yes`** | **2 of 5** |
| **Rune Factory: Guardians of Azuma** (2026-08-20, no RT option · FG ×6 via NVIDIA App · DLAA) | ⬜ `Unknown` | ⬜ | ⬜ no local tag | ⬜ nothing observed | ✅ **`No`** | **1 of 5** |
| **Cyberpunk 2077** (2026-08-20, **all RT off** · RR off · MFG ×4 · Balanced) | ✅ `Dlss` | ⬜ `0xFF` | ✅ **1485×835 → 2560×1440** | ◐ `presents/batch` = 4.00 as a PROXY | ✅ **`No`** | **3 of 5** |

> ### ✅ FIVE CAPTURES ACROSS FOUR TITLES — 2026-08-20, and the RT tri-state is complete
>
> Every run: 40 s, one identified swapchain, one segment, **0 gaps, 0 dropped**, foreground on
> all 369 drain ticks, `status=Ready`, `layoutVersion=3`, `rtTier=12`, `apiMask=0x4` (D3D12),
> and **`faults=0`** — including a title that pushed **37,823 presents through the hook in 39
> seconds** (~950/s) without a drop. All six hook families installed on every title. The
> Overlay payload was hash-verified against the just-built DLL before each run.
>
> | Run | records | batches | Streamline ids seen | asBuild / dispatch | `rt_frame_pct` | Displayed FPS |
> |---|---|---|---|---|---|---|
> | Cyberpunk, PT on | 10,603 | 2,578 | `DLSS_RR` 2578 | 2,561 / 2,567 | 24.2% (**25.0%** of the claiming window) | 264.93 |
> | Alan Wake 2 | 900 | 875 | `DLSS_RR` 875 | 869 / 871 | 96.8% | 22.5 |
> | Wukong | 5,956 | **0** | **none at all** | 1,437 / 1,441 | 24.2% (**25.0%**) | 148.89 |
> | Rune Factory | 37,823 | **0** | **none at all** | **0 / 0** | **0.0%** | 946.44 |
> | Cyberpunk, all RT off | 17,456 | 4,242 | **`kFeatureDLSS` 4242** | **0 / 0** | **0.0%** | 436.28 |
>
> **1 · The tri-state is complete, and `No` is correct twice for two different reasons.**
> `Yes` on Cyberpunk (path tracing) and Wukong (RT High); `No` on Rune Factory, whose settings
> menu has no ray-tracing option at all, **and** on Cyberpunk with every RT option switched off
> — a title that *can* ray-trace and is not doing so, which is the harder and more valuable
> case. Both negatives satisfy all three conjuncts `03_METRICS` requires: `rtTier` 12,
> `RtAsBuild` installed, and zero evidence across 36,575 and 16,871 claiming records.
>
> **2 · `slEvaluateFeature` IS NOT A GENERAL ROUTE, and this generalises HANDOFF item 3 from
> one title to four.** Two of the four titles never call it *at all* — Wukong with both
> `sl.interposer.dll` and `sl.dlss_g.dll` loaded and DLSS-G demonstrably running, and Rune
> Factory likewise. The other two call it only for Ray Reconstruction. **`kFeatureDLSS_G` is
> zero on every run**, so item 3's counter has nothing to count on any of them.
>
> **3 · §S30's closure survived a test in the REVERSE direction, which it never had.** It
> concluded that Ray Reconstruction *replaces* the super-resolution pass rather than running
> beside it, from one configuration. Turn RR off on the same title, same machine, one setting
> changed: the census flips from `DLSS_RR` 2578 / `DLSS` 0 to **`DLSS` 4242 / `DLSS_RR` 0**.
> This is also the **first observation of `kFeatureDLSS` anywhere in this project**, and the
> local-tag extent arrives on that evaluation exactly as it did on the RR one — the same
> `1485×835`.
>
> **4 · A SECOND FG proxy, and it covers what the first cannot.** On Wukong there are no
> Streamline batches, so `presents/batch` is unreadable — and `presents ÷ RT-active presents`
> reads **5,764 / 1,441 = 4.0000 exactly** against a title configured for ×4. It carries the
> *same* unverified premise as `presents/batch` (that the work is recorded once per application
> frame), so it is a proxy and not a producer. What it adds is coverage: the two are readable
> on disjoint sets of titles, and on the one run where both were available they agreed.
>
> **5 · The #87 falsifier did not fire, on two independent titles.** It pre-committed that
> `rt_frame_pct` must read ≈25% at ×4 and not ≈100%. Cyberpunk: 25.0%. Wukong: 25.0% — and
> Wukong reached it through the RT evidence alone, with no Streamline batches involved.
>
> **6 · Alan Wake 2 is the outlier and is NOT explained.** `presents/batch` = 1.00 and
> `rt_frame_pct` = 96.8%, i.e. every present carried an application frame's work, against a
> menu set to FG 4X. Two readings fit every number equally: generated presents not reaching the
> vtable we patch (§H5 case 3), or frame generation simply not running. **The operator reported
> that this title would not apply its settings**, which is evidence for the second and is why
> it is not written up as §H5 case 3. The discriminating run — the same scene with FG off — was
> not taken.
>
> **7 · One arithmetic result worth keeping on its own.** Alan Wake 2's dispatch volume is
> `4,279,466,880 / 871 = 4,913,280` rays per RT-active present, and `3 × 1706 × 960 =
> 4,913,280` **exactly** — three rays per pixel of the render resolution the game's own menu
> states. The params hook reported nothing on that title, so the render resolution was
> confirmed by a completely different route from the one meant to measure it.
>
> **8 · `FL_MEASURED_UPSCALER_PARAMS` produced nothing on three of the four titles.** Only
> Cyberpunk tags scaling inputs locally. §2b's local-tag route is narrower than that entry
> assumed, and the honest failure mode held: the writer published *nothing* rather than
> something wrong.

**The first row, and it is one row.** Read the legend before the marks: ✅ measured and
correct against the title's own settings; ❌ measured and **wrong**; ⬜ honestly absent — no
producer, or the title does not expose it on the route we take.

Conditions: Cyberpunk 2077 in combat, 40 s bounded capture, **10,169 presents, 0 gaps, 0
dropped**, 254.82 displayed FPS, `apiMask` = D3D12, hooks `Present | UpscalerIdentity |
UpscalerParams`. Consent granted by the operator; the guard evaluated `Allow`. A second run
minutes earlier gave 11,108 presents with the same shape.

**Render → output is the one that lands, and it lands exactly.** `UserSettings.json` records
`DLSS = Balanced` at `2560x1440`; Balanced is 0.58, so 2560 × 0.58 = 1484.8 and
1440 × 0.58 = 835.2. The writer said **1485×835**. This is the first of exit criterion 1's
five values measured correctly from a real game.

> ### The second row, and §S30's answer: Ray Reconstruction was doing the upscaling
>
> **Three further 40 s captures, same title, same settings** (`DLSS = Balanced`,
> `DLSS_D = True`, `DLSS_MultiFrameGeneration = x4`, `ReflexMode = Enabled`, 2560×1440),
> with the id census the previous row's defect motivated. Conditions: 10,092–10,443
> presents, 0 gaps, 0 dropped, ~260 displayed FPS, `apiMask` = D3D12, `rtTier` = **12**
> (`TIER_1_2`), hooks `Present | UpscalerIdentity | UpscalerParams | FgEvaluations`.
>
> **The census, and it is the whole of §S30:** of 2,523 batches, `kFeatureDLSS_RR` = 2,523,
> `kFeatureDLSS` = **0**, `kFeatureNIS` = 0, `kFeatureDLSS_G` = **0**, undecoded = 0. With
> Ray Reconstruction on, the title evaluates RR **instead of** super-resolution, not beside
> it. The decode had no arm for that and reported `UNKNOWN`. What settles it rather than
> suggesting it: `renderW/H` are published only on a frame that drained an evaluation, and
> they are 1485×835 — so the scaling-input tag arrives ON the RR evaluation. The evaluation
> that upscales is the one the decode was already looking at.
>
> **`presents/batch = 4.000` on three independent runs** (10,176/2,544 · 10,276/2,569 ·
> 10,092/2,523), against the title's own `x4`. Two things follow, and the second is the one
> that changes the plan:
>
> - **DLSS-G's GENERATED presents reach the vtable we patch.** A factor of exactly 4 cannot
>   be produced by a writer that only sees application frames. §H5's fear — `fg_factor`
>   structurally 1.0 on every Streamline title — does **not** hold for the present path.
> - **`slEvaluateFeature(kFeatureDLSS_G)` is never called.** Zero, in ~7,600 batches across
>   three runs, while frame generation is demonstrably active. On Streamline 2.x DLSS-G is
>   driven through the interposer's swapchain proxy, not through the feature-evaluation
>   entry point — so **HANDOFF item 3's premise is wrong for this route**, and the counter
>   built for it is correct and has nothing to count. `presents/batch` is a working proxy
>   only because RR happens to be evaluated once per application frame.
>
> **Still unmeasured, and it is one setting away:** the same capture with multi-frame
> generation OFF. If `presents/batch` falls to ~1 the 4.000 is proven to be FG rather than a
> property of how this title evaluates RR, and §H5 closes on two points instead of one.
> Until then the ×4 reading has one configuration behind it.

**Quality `0xFF` is a measurement of the TITLE, not a gap in the code.** Cyberpunk sets its
preset out of band through `slDLSSSetOptions` and never chains `sl::DLSSOptions` into
`slEvaluateFeature`'s `inputs`. The only route that would reach it is the one
`docs/HANDOFF.md` §2b refuses on five grounds. So **the `DLSSOptions` half of the inputs walk
has a ZERO hit rate on this title** — the unknown §2b flagged as its largest, now measured
once. `0xFF` is the defined "a hook ran and could not tell", and it is true.

**Upscaler `Unknown` is WRONG, and is filed as §S30 rather than reported as a result.**
Cyberpunk is running DLSS. Every one of the 2,461 params-carrying presents decoded to
`UNKNOWN`. Honest — it is never `NONE` — and not the answer the exit criterion asks for.

**One number fell out that this run was not designed to take.** The params bit appeared on
2,461 of 10,169 presents (24.2%), i.e. **10,169 / 2,461 = 4.13**; the earlier run gave
11,108 / 2,696 = **4.12** independently. `UserSettings.json` says
`DLSS_MultiFrameGeneration = x4`. That is `fg_factor` matching its oracle to two significant
figures **before the frame-generation producer exists** — strong support for counting
evaluations directly (the 2026-08-14 owner ruling) rather than deriving generated frames from
a multiplier. **It is not a measurement of `fgEvaluations`**: it uses the params bit as a
proxy for "an SL feature evaluated during this present", and item 3's producer must reproduce
it directly before the number may be published.

**Two operational facts, learned by hitting them.** The lazy feature-hook install costs the
opening **~1.15 s** of every session — 292 of 10,169 records lack the `Upscaler` bit, and 288
of 11,108 in the other run, both matching the 1 Hz watchdog. And **a game launch yields ONE
capture**: when the host detaches, the Overlay stops receiving guard ticks and self-unhooks at
the 65 s deadline exactly as `19_SAFETY` specifies, so the next attach correctly reports
`SupervisionLost` rather than a live session. The Overlay does not re-arm.

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
into a real title and the process survives it (§7).

> **"Nothing is hooked yet" was true when written and is not now** (corrected
> 2026-08-05). `Present` interception, the ring writer and the fault policy all
> landed in #40–#44, ahead of the P1 label they were filed under. What blocks the
> rows above is therefore no longer the *present* path — it is that **no upscaler,
> FG or RT hook exists** (P0 items 4, 6 and 7), and that ~~**nothing reads the ring**:
> `src/FrameLedger.Shared` holds a `.csproj` and no `.cs` files, so the Overlay's
> output is observable only from inside `guard_test.cpp`~~. Both are prerequisites of
> filling this table, and neither is "finding a game".
>
> > **The reader half is done, corrected later the same day.** `ShmLayout.cs` (#47),
> > `ShmHandshakeValidator` (#49), `ShmRingReader` (#50) and a closed write-read
> > integration test (#51) all exist, and the Overlay's output is readable from managed
> > code. This passage was itself written to correct an earlier stale claim and went
> > stale within hours, in the same file — which is why the strike is left visible.
> >
> > **What still blocks this table is one thing plus one caveat.** The one thing: no
> > upscaler, FG or RT hook exists. The caveat: the reader has **no production caller**,
> > so something must drive it against a real title — and §S27 forbids an injecting
> > entry point on a shipped binary until a consent record exists, so that driver is its
> > own piece of work rather than a command-line flag.

## 9 · Frame generation

- **Rung 1 (API / `fgEvaluations`) vs Tier-2 ETW `FrameType`: NOT RUN, and rung 1 does not
  produce a number to compare.** Measured 2026-08-15 on Cyberpunk 2077 (SL 2.7.1):
  `slEvaluateFeature(kFeatureDLSS_G)` is **never called** — 0 across ~14,000 Streamline
  batches at four frame-generation settings — so `fgEvaluations` is 0 on every record and
  there is nothing for ETW to be compared against. The comparison `15_ROADMAP` item 7 asks
  for is blocked on a PRODUCER, not on tooling.
- **What DID move with the setting is `presents / batch`, and it is a PROXY.** Five 40 s
  captures, one title, one GPU, D3D12, Ray Reconstruction on: **off → 1.000, ×2 → 2.000,
  ×4 → 4.000** (×4 three times independently, one identified swapchain and 0 gaps/0 dropped
  in every run). Application frame rate falls 85.3 → 70.9 → 63.1 as the multiplier rises,
  which is the direction frame-generation overhead predicts, and 70.9 × 2.06 = 146
  reproduces the ×2 displayed figure. So the extra presents are real, they are visible to
  our present hook, and their count tracks the configured multiplier.
  **A batch is "a present that drained a Streamline evaluation", NOT "an application
  frame"** — the two coincide here only because Ray Reconstruction happens to be evaluated
  once per application frame on this title, and no independent oracle has confirmed that.
- **THE APPLICATION-FRAME PREMISE IS STILL NOT MEASURED — a draft of this bullet said it was,
  and was corrected before it landed, 2026-08-16.** Steam's overlay read `DLSS 162 | FPS 81`
  during a ×2 capture whose own figures imply 161.7 and 80.8. That is **one** agreement, not
  two: `presents/batch` was 2.0000 exactly and 162/81 is 2.0000 exactly, so both residuals are
  forced to −0.182%. The surviving comparison is circular (the span was derived from
  `Displayed FPS`, so `presents/span` restates it), and the rival reading — that the overlay's
  `FPS` field is *displayed ÷ 2* rather than an application-frame count — predicts the identical
  number at ×2. Steam's overlay is also **not** an independent instrument: `17_HOOK_ENGINE`
  §Coexistence records that it hooks D3D presentation in-process too.
  **The discriminating run is ×4** (application frames ⇒ ≈65, fixed halving ⇒ ≈130) plus an
  FG-off leg where a genuine counter converges with displayed and a halving does not, comparing
  RATIOS — our `presents/batch` against the overlay's `DLSS/FPS` — so neither side needs a span.
  §S30 carries the full correction.

  > **GENERALISED FROM ONE TITLE TO FOUR — 2026-08-20, §8.** The bullet above rests on
  > Cyberpunk. Four titles, five captures: **`kFeatureDLSS_G` is zero on every one**, and two of
  > the four never call `slEvaluateFeature` *at all* — Black Myth: Wukong with both
  > `sl.interposer.dll` and `sl.dlss_g.dll` loaded and DLSS-G demonstrably running, and Rune
  > Factory: Guardians of Azuma. So this is not "DLSS-G avoids that export"; on half the titles
  > measured, **nothing** goes through it. `slEvaluateFeature` is a route some titles use for
  > some features, not the Streamline entry point.
  >
  > **`presents / RT-active presents` is a second proxy and covers the gap.** Wukong has no
  > batches at all, so `presents/batch` is unreadable — and `5,764 / 1,441 = 4.0000` exactly,
  > against a title configured for ×4. Same unverified premise, disjoint coverage; on the one
  > run where both proxies were readable they agreed. Neither is a producer, and §S31's
  > measurement is still what decides whether either may be published.

  **`fl-baseline-probe` was run on 2026-08-16 and is RETIRED as an FG-engagement oracle by
  its own pre-committed falsifier**: against the running title with FG on at ×2 it reports
  all seven capabilities `loaded`, including `dlss_g` **and** `xefg` — two mutually exclusive
  frame-generation implementations — so `loaded` means "mapped", not "engaged", and one run
  settled it without needing the FG-off leg. Its real job, the static/loaded baseline for
  item 4, is unaffected. **The game's own frame counter is the only remaining candidate**
  and is still unrun.
- Can PresentMon 2.x `FrameType` see driver-level FG / AFMF (§M1):
- AFMF on this machine: **untested — RTX 5080, AMD driver-side feature**

## 10 · Telemetry layering

- L1 baseline vendor-neutral:
- `D3DKMT` perf-data probe on Win 10 22H2 **and** Win 11 — stable enough to keep:
- L2: LHM fields returned per vendor (fill `18_GPU_VENDOR_APIS` §Capability
  matrix; use explicit "untested", never `?`, for absent hardware):
- L2: **GPU sensors unelevated, without PawnIO** (§M5) — decides whether the
  default unelevated Agent has temperatures at all:
- L3: Reflex latency, throttle reasons, per-domain utilisation: **not yet** — but L3
  now initialises and is proven to degrade cleanly. Measured 2026-08-05 by
  `ctest fl_nvapi_probe` after vendoring: `NvAPI_Initialize` → `NVAPI_OK`, driver
  **610.88** (branch `r610_85`), **1** physical GPU, "NVIDIA GeForce RTX 5080".
  The absent-driver path is exercised by CI, where the probe prints
  `BRANCH: DEGRADED` and exits 0 — `nvapi64.lib` is a static stub library reaching
  `nvapi64.dll` through `nvapi_QueryInterface` at first call, so it is **not** a
  load-time dependency and a machine with no NVIDIA driver still loads the binary.
  The fields in this bullet need `NvapiTelemetrySource`, which does not exist.
- L3 · **a doc error the vendoring found, which reading vendor documentation would
  not have**: `18_GPU_VENDOR_APIS` §L3's table named `NvAPI_GPU_GetMemoryInfo`. The
  vendored headers mark it `__nvapi_deprecated_function` ("deprecated in release
  520 — use `NvAPI_GPU_GetMemoryInfoEx`"), so under `/W4 /WX` a call to it fails the
  native build. Table corrected. This is the class `17_HOOK_ENGINE` calls the highest
  false-confidence risk in the spike, in a document rather than in code.

## 11 · PresentMon / Tier 2 *(§M2, §M6)*

### ◐ Measured 2026-08-20 — the binary is here, and it will not run for us

- **Console binary obtained, version, pinned hash: ✅**
  `C:\Program Files\Intel\PresentMon\PresentMonConsoleApplication\PresentMon-2.5.1-x64.exe`,
  **956,768 bytes**, SHA256
  `9BEC3083069F58F911E6A512F4806DB51A27BD096103087BC1D05EF54C80A191`.
  Version **2.5.1**, read from its own `--help` banner.

  > **It carries NO VERSIONINFO at all** — `FileVersion`, `ProductVersion` and
  > `CompanyName` are all empty. So "pin the version" has to mean **pin the hash
  > and the filename**; there is nothing in the file for a `versioninfo-check`-style
  > gate to read. Worth stating because this repository requires exactly that
  > metadata of everything it builds (`19_SAFETY`, `tools/versioninfo-check.ps1`),
  > and a bundled third-party binary that lacks it is a packaging fact, not a
  > detail.

- **Runs unelevated: 🔴 NO, on this machine.** Measured against
  `hook-harness --hold-presenting`, current user `DESKTOP-NUHVIDP\Anon`:

  ```
  error: failed to start trace session: access denied.
         PresentMon requires either administrative privileges or to be run by a
         user in the "Performance Log Users" user group.
  ```

  Exit code **6**, no CSV written. The account is in `BUILTIN\Users`,
  `docker-users` and `Authenticated Users` — **not** an administrator, and **not**
  in `Performance Log Users` (`S-1-5-32-559`), so the refusal is exactly what it
  says rather than something subtler.

- **Performance Log Users sufficient without admin: ❓ UNMEASURED**, and it stays
  that way here. Adding an account to a local group is a system settings change and
  it needs administrative rights of its own; it is the owner's to do, not a session's.

- **The shared service is running and it does NOT help the console.**
  `PresentMonSharedService` is `Running` / `Automatic`, `StartName = LocalSystem`,
  from `C:\Program Files\Intel\PresentMonSharedService\PresentMonService.exe`. The
  console binary still starts its **own** trace session and still fails. So §M6's
  "or the PresentMon Service" half is not answered by the service merely being
  installed — the console does not talk to it. Whether `PresentMon.exe` (the
  Application, a GUI) does is unmeasured and would not produce a CSV anyway.

- **2.x column set over stdout: ❓ UNMEASURED.** No session, no CSV, no header. This
  is the input `tools/frametype-oracle.ps1` parses, so **that parser has never seen
  a real PresentMon CSV** — it resolves columns by name and refuses loudly on
  anything it does not recognise, and its decision table is the only thing standing
  behind it. Said plainly rather than left for the next reader to assume.

### 🔴 `--track_frame_type` is a BETA option that needs the VENDOR to cooperate

This is the finding, and it lands on a pre-committed oracle. `PresentMon 2.5.1
--help`, verbatim, under **Beta Options**:

> `--track_frame_type`  Track the type of each displayed frame; **requires
> application and/or driver instrumentation using Intel-PresentMon provider.**

So `FrameType` is **not** a general ETW classification of any present. It is a
report of events the application or the graphics driver chose to emit through
Intel's provider. `03_METRICS` rung 2 says *"PresentMon 2.x `FrameType` column
reports generated frames directly"* and §S30 called it *"a mechanism that divides
by nothing"* — both are true of the mechanism and neither is a promise that the
mechanism is **available** on an NVIDIA DLSS-G title. Whether NVIDIA's driver
instruments that provider is the make-or-break question and is unmeasured.

**The falsifier is written before the run** (§S31): if a DLSS-G capture comes back
with no `FrameType` column, or with every row spelled `Application` while frame
generation was demonstrably on, PresentMon is retired as the application-frame
oracle for NVIDIA frame generation **in that same row** — exactly as
`fl-baseline-probe` was, rather than being re-run until it answers.

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

## 13 · Can the guard go RED against real anti-cheat? *(2026-08-04)*

Every real-machine verdict recorded before this section was `Allow` or an
*uncertainty* refusal. The only blocklist positive ever seen — `EasyAntiCheat_EOS`
as an installed-but-stopped service — was classified as a defect and deliberately
narrowed away. So nothing had ever shown that the module, driver, service or
directory tiers fire against a running commercial anti-cheat, and a gate that has
never been observed to refuse carries the same amount of information as one that
never refuses.

Measured against four real titles, unelevated. **`FlGuardEvaluate` and
`FlStaticPreScan` only — nothing was injected anywhere.**

### ✅ It goes red — through the SERVICE tier, and machine-wide

With Goose Goose Duck (Easy Anti-Cheat, Unity) actually running:

| Target | Verdict |
|---|---|
| `Goose Goose Duck` (pid) | `BlockedService`, family `Easy Anti-Cheat`, signal `EasyAntiCheat_EOS` |
| `GGDLauncher` | same |
| `EasyAntiCheat_EOS` | same |
| **a freshly spawned `hook-harness`, completely unrelated** | **same** |

State at the moment of measurement: service `EasyAntiCheat_EOS` **Running**,
driver `EasyAntiCheat_EOSSys` **Running**.

**The refusal is machine-wide, and that is worth stating plainly rather than
discovering.** Checks 2 and 2b do not depend on the target, so while any EAC
title is running FrameLedger refuses to inject into *anything* — a user with a
party game idling in the background cannot capture an unrelated single-player
title, and the signal names a game they are not playing. That is the correct
fail-closed posture (a live anti-cheat driver is a machine-wide hazard, which is
exactly what §S1's driver check exists for), but the user-facing text has to
explain it or it reads as a bug.

### 🔴 Check 1 structurally cannot see an EAC-protected game

Measured with the guard's own rights and flags
(`PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`, `LIST_MODULES_ALL`):

| Process | Result |
|---|---|
| `Goose Goose Duck` | `OpenProcess` succeeds, **`EnumProcessModulesEx` fails with `ERROR_ACCESS_DENIED (5)`** |
| `EasyAntiCheat_EOS` | `OpenProcess` fails with `5` |
| `GGDLauncher` | 62 modules readable, **none** matching any blocklist prefix or fuzzy fragment |

So the module tier's `EasyAntiCheat` entries can never fire on the process that
matters: the guard gets `kFailed` → `kProcessUnreadable`, which is a refusal for
"could not look", not for "found EAC". The handle opening while the enumeration
is denied is worth remembering — a probe that only checked `OpenProcess` would
have concluded the process was readable.

**The consequence for §S16:** the readable member of this title's tree is the
launcher, and the launcher carries nothing. The service check is doing all the
work.

### 🔴 Two whole families were missing, and one is kernel-level

| | |
|---|---|
| `ACE-BASE.sys`, `ACE-ADVT.sys` | Installed under `System32\drivers` by Neverness To Everness (Anti-Cheat Expert, **kernel level**). Matched **nothing** — the family was absent from `19_SAFETY`'s table and from the data |
| service `AntiCheatExpert Protection` | Registered. Matched nothing |
| `EasyAntiCheat_EOSSys` | Running as a **kernel driver**. Matched nothing in `drivers`; the refusal came from `services` instead |
| `NTEGlobal/driver/PGameProtectDriver_X64.sys` | A kernel driver shipped **inside the game install**. Matched nothing. Its name contains `protect`, but the fuzzy tier is modules-only and never looks at check 4's file and directory groups |

All four are now in the data and in `19_SAFETY` §Blocklist seed, with the
unmeasured siblings marked as such.

**Valve VAC remains absent, and CS2 measures `Allow`.** VAC is neither a
machine-wide driver nor a service; it is modules loaded into the game process,
which — per the previous section — is exactly what cannot be read. The route
`19_SAFETY` reserves for it is `blockedStoreIds` — check 3's **store-id half**,
which cannot be called (§S14). #52 wired the *executable* half; that changes
nothing here, because a renamed executable defeats a name list and the store-id
route is exactly why `19_SAFETY` reserves it. Recorded rather than papered over:
**a real VAC title is allowed by this guard today.**

### 🔴 The data addition alone changed nothing, and that is the sharper finding

Adding `PGameProtectDriver_X64.sys` made check 4 refuse when the scan started at
`NTEGlobal` — and still return `Allow` from the **install root**, which is where
check 4 actually runs (`ImageDirectory` → `ResolveInstallRoot`).

`kMaxPreScanDepth` was 2. Measured with a control tree carrying a known
blocklisted file at increasing depth:

| File at | Old cap | New cap |
|---|---|---|
| depth 0 | refuse | refuse |
| depth 1 | refuse | refuse |
| depth 2 | **Allow** | refuse |
| depth 3 | Allow | **Allow** |

The driver sits at depth 2. So the blocklist row was coverage on paper — a row
that cannot be reached from where the scan begins.

**The cap was measured before it moved, because it is a REFUSAL and not a
truncation:** overrunning `kMaxPreScanEntries = 4096` does not scan less, it
refuses the title. Across 67 installed titles the worst case is **506** entries
at the old reach and **729** at the new one — 18% of budget, 5.6x headroom.

### ✅ Proven on the whole corpus, both directions

The deeper pre-scan run against all 67 installed titles:

| Verdict | Count |
|---|---|
| `Allow` | 65 |
| `AntiCheatDirectory` (Goose Goose Duck) | 1 |
| `AntiCheatFile` (Neverness To Everness) | 1 |
| `PreScanFailed` | **0** |

Exactly the two anti-cheat titles refuse, nothing else does, and the deeper walk
produced no false refusal on any real install.

### What is still unmeasured

- Whether the module tier fires on **any** real anti-cheat. It could not be
  reached here: the one title whose modules are readable carries none.
- Whether the driver tier fires. `ACE-*` and `EasyAntiCheat*` are now in
  `drivers`, but both were **Stopped** during the measurement window that would
  have exercised it, and the EAC session was caught by the service check first.
  The driver rows are therefore data-complete and **behaviourally untested**.
- VAC, as above.

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
