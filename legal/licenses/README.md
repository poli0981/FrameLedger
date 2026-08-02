# Third-party licence texts

Full licence texts for everything FrameLedger bundles or links. Populated at P4
by a licence-gathering script; `tools/license-check.ps1` fails the build if a
vendored component is present here without its licence copy.

Required before the first release
(`legal/THIRD_PARTY_NOTICES.md` §Attribution requirements checklist):

| File | Component |
|---|---|
| `minhook-BSD-2-Clause.txt` | MinHook — vendored and built from source |
| `nvapi-MIT.txt` | NVIDIA NVAPI SDK — headers + import library vendored |
| `wpfui-MIT.txt` | WPF UI (lepoco) — MIT requires the notice to ship |
| `presentmon-MIT.txt` | Intel PresentMon — ships beside the bundled binary |
| `librehardwaremonitor-MPL-2.0.txt` | LibreHardwareMonitorLib, consumed unmodified |
| `apache-2.0.txt` | Serilog, Dapper, the analyzers, Vulkan headers |
| `mit.txt` | Shared text for the MIT-licensed NuGet packages |

Two of these are release-blocking questions, not paperwork
(`docs/20_OPEN_QUESTIONS.md` §M3, §M4):

- **NVAPI** — confirm the exact vendored artifact is MIT, *including*
  `nvapi64.lib`. The import library may not be covered by the same grant as the
  headers. If it is not, Reflex / PC latency has no alternative source.
- **LibreHardwareMonitor** — confirm no depended-upon file carries MPL-2.0
  **Exhibit B** ("Incompatible With Secondary Licenses"), which would remove the
  GPL-compatibility route and leave no sensor layer for any vendor.

Answer both before writing NVAPI or LHM code, not after.
