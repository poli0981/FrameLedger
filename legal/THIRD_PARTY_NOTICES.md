# FrameLedger — Third-Party Notices

FrameLedger is licensed under **GPL-3.0-only**. It includes or depends on the third-party components below, each under its own license. All listed licenses are compatible with distribution alongside/within a GPL-3.0 application. Full license texts must be shipped in `legal/licenses/` in release packages ({{TODO: generate at P4 with a license-gathering script}}).

## Bundled / linked components

| Component | Use | License | Notes |
|---|---|---|---|
| **MinHook** | Inline function hooking in `FrameLedger.Overlay` | BSD-2-Clause | Vendored and built from source, pinned by commit. Copyright notice must ship in `legal/licenses/` |
| Intel PresentMon (console binary) | Tier-2 fallback frame timing (ETW) | MIT | Bundled as a pinned native binary; SHA-256 verified at build. © Intel Corporation |
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
| NVIDIA NVAPI SDK | NVIDIA GPU telemetry + Reflex latency | MIT | Headers + import library vendored — see §GPU telemetry below |
| H.NotifyIcon.Wpf | Tray icon | MIT | |
| Roslynator, Meziantou.Analyzer, VS Threading Analyzers (build-time) | Static analysis | Apache-2.0 / MIT | Build-time only |

## GPU telemetry — what is bundled and what is not

Telemetry is layered so no proprietary vendor licence is ever required (`docs/18_GPU_VENDOR_APIS.md`).

| Component | How we use it | Licence | Bundled? |
|---|---|---|---|
| **NVIDIA NVAPI SDK** (headers, interface definitions, `nvapi64.lib`) | Linked normally; NVIDIA-only telemetry, throttle reasons, Reflex/PC latency | **MIT** — <https://github.com/NVIDIA/nvapi> | **Yes** — headers and import library vendored. MIT copyright notice ships in `legal/licenses/nvapi-MIT.txt` |
| `nvapi64.dll` (runtime implementation) | Loaded at runtime from the user's system | Part of the NVIDIA graphics driver | No — never redistributed by us |
| **LibreHardwareMonitorLib** | GPU sensors for **all vendors** (AMD, Intel, NVIDIA) + CPU/board sensors when elevated | **MPL-2.0** | Yes, as an unmodified NuGet package |
| **DXGI + Windows performance counters (PDH)** | Vendor-neutral baseline: utilisation, VRAM, adapter identity | Windows OS APIs | n/a |
| NVIDIA **NGX** / **Streamline**, AMD **FidelityFX**, Intel **XeSS** runtimes | Not loaded, not linked, not redistributed. We observe the calls a *game* makes to the copies it ships (`docs/17_HOOK_ENGINE.md`) | — | **No vendor upscaler code is distributed by FrameLedger** |

### GPL-3.0 compatibility

- **NVAPI SDK — MIT.** MIT is one-way compatible with GPL-3.0: the SDK material may be included in this repository and conveyed as part of a GPL-3.0 work, provided the MIT copyright and permission notice are retained. The runtime `nvapi64.dll` is a component of the user's installed graphics driver and is not distributed by us; its use falls under GPLv3 §1's **System Library** provision.
- **LibreHardwareMonitorLib — MPL-2.0.** MPL-2.0 is explicitly designed to be combinable with GPL ("Secondary Licenses", MPL-2.0 §1.12 and §3.3). We consume the package **unmodified**; if any LHM source file is ever modified, that file remains under MPL-2.0 and the modification must be published. Upstream source: <https://github.com/LibreHardwareMonitor/LibreHardwareMonitor>.
  - [ ] **P0 check:** confirm the pinned LHM version does not carry MPL-2.0 **Exhibit B** ("Incompatible With Secondary Licenses") on any file we depend on — that notice would remove the GPL-compatibility route.

### Vendor SDKs deliberately rejected

**Intel Graphics Control Library (IGCL)** — rejected on licence grounds. It is distributed under the Intel Software License Agreement, which permits use and redistribution **"solely for use on Intel platforms"**, imposes a broad indemnification obligation, and adds its own termination trigger. Each of these is a further restriction on downstream recipients that GPL-3.0 §10 forbids and that §7's list of permitted additional terms does not cover. The headers are therefore **not vendored**, and re-declaring the API by hand is not treated as an acceptable workaround. Intel GPU telemetry is provided by the vendor-neutral baseline plus LibreHardwareMonitor instead.

**AMD ADLX / ADL** — not used. LibreHardwareMonitor already covers AMD sensors under a GPL-compatible licence, so there is no reason to take on an additional vendor licence.

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
- [ ] PresentMon license + copyright shipped beside the bundled binary
- [ ] MPL-2.0 source-availability note points to upstream LibreHardwareMonitor repository
- [ ] LHM checked for MPL-2.0 Exhibit B on any depended-upon file (P0)
- [ ] Vendored NVAPI headers still carry their `SPDX-License-Identifier: MIT` blocks unmodified
- [ ] No Intel IGCL or AMD ADLX material anywhere in the tree (CI grep, see `docs/13_CI_CD.md`)
