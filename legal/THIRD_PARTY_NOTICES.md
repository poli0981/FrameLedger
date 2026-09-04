# FrameLedger — Third-Party Notices

FrameLedger is licensed under **GPL-3.0-only**. It includes or depends on the third-party components below, each under its own license. All listed licenses are compatible with distribution alongside/within a GPL-3.0 application. Full license texts must be shipped in `legal/licenses/` in release packages. Populating that directory is a P4 task, driven by a license-gathering script and enforced at build time by `tools/license-check` (`docs/12_BUILD.md` §Local quality gate).

> ⚠ **Bundling audit — last checked 2026-08-05.** Two rows below claimed material
> this repository does not contain, and the licence gate could not have caught
> either: `tools/license-check.ps1` keys its check on the directory a component
> *would* occupy, so it fires on **vendored-without-a-licence** and never on
> **claimed-vendored-but-absent**. A gate whose verdict is decided before it
> looks, inside the file the EULA incorporates by reference — the same shape as
> the privacy policy disclosing a network request that did not exist.
>
> - **NVIDIA NVAPI SDK** said *"Yes — headers and import library vendored,
>   Verified 2026-08-02"*. `src/native/third_party/` holds `CMakeLists.txt` and
>   `vulkan-headers` and nothing else; the only `nvapi` path in the tree is the
>   licence copy. The **licence** verification was real and is kept; the
>   **bundling** claim was not.
> - **Intel PresentMon** said *"Bundled as a pinned native binary; SHA-256
>   verified at build"*. `assets/` does not exist.
>
> Both rows now say **Not yet**, and `license-check.ps1` gained a bidirectional
> check so the claim and the filesystem cannot drift again in either direction.
> Nothing here was a licence violation: shipping a notice for material we do not
> distribute is over-disclosure, which is its own defect in a document a user
> relies on.

## Bundled / linked components

| Component | Use | License | Notes |
|---|---|---|---|
| **MinHook** | Inline function hooking in `FrameLedger.Overlay` | BSD-2-Clause | Vendored and built from source, pinned by commit. Copyright notice must ship in `legal/licenses/` |
*(The **Intel PresentMon** row was removed on 2026-08-27. It is not bundled, not fetched, not redistributed and no longer used at all — §S31 retired it as a measurement oracle and the owner then dropped it outright — so a notice for it would be over-disclosure, which this document treats as a defect in the same way an omission is. `tools/license-check.ps1`'s matching claim was removed in the same commit, because a bidirectional check whose subject no longer exists cannot go green honestly: it would be asserting agreement about nothing.)*
| Vulkan headers / loader interfaces | `FrameLedger.VkLayer` implicit layer | Apache-2.0 | Khronos headers; layer implemented against the documented loader–layer interface |
| LibreHardwareMonitorLib | GPU sensors (all vendors) + CPU/motherboard sensors (optional, elevated) | MPL-2.0 | See §GPU telemetry below. Consumed unmodified |
| WPF UI (`Wpf.Ui`, lepoco) | Fluent UI theme/controls/navigation | MIT | © lepo.co, Leszek Pomianowski and contributors. License copy must ship with the app (MIT requirement) |
| Fluent UI System Icons | Icon font bundled inside WPF UI | MIT | © Microsoft. Segoe Fluent Icons is **not** bundled (its EULA forbids redistribution) and must not be used |
| CommunityToolkit.Mvvm | MVVM framework | MIT | |
| ScottPlot 5 | Charts | MIT | |
| Serilog (+ file sink) | Logging | Apache-2.0 | |
| Velopack | Installer/updater | MIT | |
| Microsoft.Data.Sqlite / SQLitePCLraw | Database | MIT / Apache-2.0 | SQLite itself: public domain |
| Dapper | Data access | Apache-2.0 | |
| CsWin32 (build-time) | Win32 interop source generator | MIT | Build-time only |
| NVIDIA NVAPI SDK | NVIDIA GPU telemetry + Reflex latency | MIT | **Vendored 2026-08-05** — nine headers + `amd64/nvapi64.lib`, at `src/native/third_party/nvapi/`. See §GPU telemetry below |
| H.NotifyIcon.Wpf | Tray icon | MIT | |
| Roslynator, Meziantou.Analyzer, VS Threading Analyzers (build-time) | Static analysis | Apache-2.0 / MIT | Build-time only |

## GPU telemetry — what is bundled and what is not

Telemetry is layered so no proprietary vendor licence is ever required (`docs/18_GPU_VENDOR_APIS.md`).

| Component | How we use it | Licence | Bundled? |
|---|---|---|---|
| **NVIDIA NVAPI SDK** (headers, interface definitions, `nvapi64.lib`) | Planned: NVIDIA-only telemetry, throttle reasons, Reflex/PC latency. **No code links it yet** — the `fl_nvapi` target exists and nothing consumes it | **MIT** — <https://github.com/NVIDIA/nvapi> @ `cd6918f6` (2026-06-24) | **Vendored 2026-08-05** at `src/native/third_party/nvapi/`: nine headers (`nvapi.h` and its include closure, plus `nvapi_interface.h`), `License.txt`, and `amd64/nvapi64.lib` — **x64 only**, `x86/nvapi.lib` deliberately not taken. **Verified 2026-08-02 and re-verified on the vendored copy:** `License.txt` opens "nvapi.lib and nvapi64.lib are licensed under the following terms" + `SPDX-License-Identifier: MIT`, so the grant names the import libraries explicitly and the binary is covered rather than only the headers. Every vendored header retains its own SPDX MIT block, which `license-check.ps1` asserts file by file |
| `nvapi64.dll` (runtime implementation) | Loaded at runtime from the user's system | Part of the NVIDIA graphics driver | No — never redistributed by us |
| **LibreHardwareMonitorLib** | GPU sensors for **all vendors** (AMD, Intel, NVIDIA) + CPU/board sensors when elevated | **MPL-2.0** | Yes, as an unmodified NuGet package |
| **DXGI + Windows performance counters (PDH)** | Vendor-neutral baseline: utilisation, VRAM, adapter identity | Windows OS APIs | n/a |
| **NVIDIA Streamline headers** (`sl.h`, its eight-file include closure, and `sl_dlss.h`) | Declarations only — `sl::Feature`, the `kFeature*` ids and `PFun_slEvaluateFeature` — so the Overlay's upscaler hook has the vendor's own signature instead of a hand-written guess. **Types and `decltype` only: never linked**, or `sl.interposer.dll` would become a load-time dependency and the Overlay would fail to load in every game that ships no Streamline | **MIT** — <https://github.com/NVIDIA-RTX/Streamline>, `main`, 2026-08-09 | **Vendored 2026-08-09** at `src/native/third_party/streamline/`: `license.txt` and ten headers. **`sl_nvperf.h` is excluded** — the same `license.txt` carries a second, proprietary "NSight Perf SDK License, Version 2023.3" block naming `sl_nvperf.h` and `sl_nvperf.dll`. **All of `external/` is excluded** — upstream's `external/ngx-sdk/` is the RTX SDKs Licence. `license-check.ps1` asserts both exclusions and the grant text |
| **AMD FidelityFX SDK headers** (`ffx_api.h`, `ffx_api_types.h`, `ffx_upscale.h`, `ffx_framegeneration.h`, `ffx_framegeneration_api_types.h`) | Declarations only — `ffxApiHeader::type`, the `FFX_API_*_DESC_TYPE_*` values, `ffxDispatchDescUpscale::renderSize`, the frame-generation `Prepare` descriptors' `frameID` and `PfnFfxDispatch` — so the Overlay's AMD hook decodes the dispatch descriptor by the vendor's own layout instead of a guess. **Types only: never linked**, and the Overlay defines none of the names the header declares `dllexport` (`hookinventory-check` Pass C reads its export table) | **MIT, by exception** — <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK> tag `v2.3.0` (`60f4ea81`, 2026-06-24). Upstream's `docs/license.md` is a binary-only default licence followed by an MIT exception list; on 2026-09-04 that list named all 845 files in the tree, these five among them, and each header carries the grant in its own banner | **Vendored 2026-09-04** at `src/native/third_party/fidelityfx/`: `license.md` verbatim (exception list included) and five headers in upstream's `Kits/FidelityFX/…` layout. **No `signedbin/`, no `.lib`, no source, no `dx12/` backend headers** (no consumer yet). `license-check.ps1` §2d asserts every vendored path is on the exception list and every header carries the grant |
| NVIDIA **NGX** / **Streamline**, AMD **FidelityFX**, Intel **XeSS** **runtimes** (the DLLs themselves) | Not loaded, not linked, not redistributed. We observe the calls a *game* makes to the copies it ships (`docs/17_HOOK_ENGINE.md`). Unchanged by the row above, which vendors **headers** and no runtime | — | **No vendor upscaler code is distributed by FrameLedger** |

### GPL-3.0 compatibility

- **NVAPI SDK — MIT, confirmed for the binary too.** The question worth asking was whether the MIT grant covered only the headers, leaving `nvapi64.lib` under the NVIDIA SDK agreement. It does not: the import libraries are tracked files in the MIT-licensed repository and `License.txt` names them as the subject of the grant. MIT is one-way compatible with GPL-3.0: the material may be included here and conveyed as part of a GPL-3.0 work, provided the copyright and permission notice are retained. The runtime `nvapi64.dll` is a component of the user's installed graphics driver and is not distributed by us; its use falls under GPLv3 §1's **System Library** provision.
- **LibreHardwareMonitorLib — MPL-2.0.** MPL-2.0 is explicitly designed to be combinable with GPL ("Secondary Licenses", MPL-2.0 §1.12 and §3.3). We consume the package **unmodified**; if any LHM source file is ever modified, that file remains under MPL-2.0 and the modification must be published. Upstream source: <https://github.com/LibreHardwareMonitor/LibreHardwareMonitor>.
  - [x] **Verified 2026-08-02 (LHM 0.9.6, repo commit `3d331e33`).** No source file applies the MPL-2.0 **Exhibit B** notice: a repository-wide code search returns exactly one hit, the `LICENSE` file itself, where Exhibit B appears only as part of the standard MPL-2.0 template. Every file we depend on (`Computer.cs`, `Sensor.cs`, `Gpu/NvidiaGpu.cs`, `Gpu/AmdGpu.cs`) carries the permissive **Exhibit A** notice, and the shipped `.nupkg` contains no Exhibit B notice in any entry. §3.3 Secondary Licenses applies → GPL-3.0 compatible. Re-check on every version bump; `docs/spike-notes.md` §0 has the method.
  - ⚠ **The package ships no licence file** — `<license>` is an SPDX expression. MPL-2.0 §3.1 therefore makes shipping the text our obligation, not a courtesy. `legal/licenses/mpl-2.0.txt` is committed.

### Vendor SDKs deliberately rejected

**Intel Graphics Control Library (IGCL)** — rejected on licence grounds. It is distributed under the Intel Software License Agreement, which permits use and redistribution **"solely for use on Intel platforms"**, imposes a broad indemnification obligation, and adds its own termination trigger. Each of these is a further restriction on downstream recipients that GPL-3.0 §10 forbids and that §7's list of permitted additional terms does not cover. The headers are therefore **not vendored**, and re-declaring the API by hand is not treated as an acceptable workaround. Intel GPU telemetry is provided by the vendor-neutral baseline plus LibreHardwareMonitor instead.

**AMD ADLX / ADL** — not used. LibreHardwareMonitor already covers AMD sensors under a GPL-compatible licence, so there is no reason to take on an additional vendor licence.

**Intel XeSS SDK** (`inc/xess/*.h`, `inc/xess_fg/*.h`, `inc/xell/*.h`, and the `libxess*.dll` binaries) — rejected on licence grounds, 2026-09-04. The repository's `LICENSE.txt` is the Intel Simplified Software License (October 2022): a binary-form-only grant with a no-reverse-engineering clause and a termination clause, and the headers themselves state they may not be used, copied or distributed without Intel's written permission. As with NGX, the headers are neither vendored nor re-declared; the runtime is observed by module name only (`docs/18_GPU_VENDOR_APIS.md` §Vendor SDKs we deliberately do not use).

*(The **AMD FidelityFX SDK** headers cleared the checklist on 2026-09-04 and were vendored the same day — the row is in the table above, with the tag and the file list, exactly as Streamline's is. The tag is `v2.3.0` rather than the `v1.1.4` this paragraph first named: installed titles already ship SDK 2.x effect DLLs whose descriptors `v1.1.4` does not declare, and the 2.x tree is MIT by the same mechanism, for every file in it.)*

Any proposal to add a vendor SDK must first pass the checklist in `docs/18_GPU_VENDOR_APIS.md` §Checklist, and the decision — including rejections and their reasons — must be recorded there.

## Interoperates with (not bundled)

| Component | Relationship | License / terms |
|---|---|---|
| **PawnIO** | Optional kernel driver required by LibreHardwareMonitor for CPU/motherboard sensors; installed separately by the user from <https://pawnio.eu/> | Its own license and signing; not distributed by FrameLedger |
| Windows ETW, DXGI, Event Log | OS facilities | Microsoft Windows license |
| Steam / GOG Galaxy / Epic Games Launcher / itch.io | Local manifest files read for library import | Respective platform terms; read-only local access |

## Trademarks

All product names, logos, and brands are property of their respective owners and are used for identification purposes only (see `DISCLAIMER.md` §4).

## Attribution requirements checklist (release gate)

- [ ] `legal/licenses/` contains full texts: MIT (per-project copies, **including `nvapi-MIT.txt`**), BSD-2-Clause (MinHook), MPL-2.0, Apache-2.0
- [ ] About → Third-party tab lists this table with versions filled from `Directory.Packages.props`
- ~~PresentMon license + copyright shipped beside the bundled binary~~ — **removed 2026-08-27 with the dependency.** Nothing Intel-authored is distributed by this project
- [ ] MPL-2.0 source-availability note points to upstream LibreHardwareMonitor repository
- [x] LHM checked for MPL-2.0 Exhibit B on any depended-upon file — clear as of 0.9.6 / commit `3d331e33`, 2026-08-02
- [x] **AMD FidelityFX headers are vendored** (2026-09-04) — five headers, MIT **by exception**: `license-check.ps1` §2d asserts each vendored path against the exception list inside the vendored `license.md` and each header's own banner, file by file. No binary, no source
- [x] **NVAPI is vendored** (2026-08-05) and all nine headers carry their `SPDX-License-Identifier: MIT` blocks unmodified — asserted file by file, not sampled. `license-check.ps1` enforces that this line and the table agree with the filesystem, **in both directions**: vendoring the material while the table still said "Not yet" failed the build, which is how this row came to be flipped
- [ ] No Intel IGCL or AMD ADLX material anywhere in the tree (CI grep, see `docs/13_CI_CD.md`)
