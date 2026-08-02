# 12 — Build & development environment

Mixed toolchain: .NET 10 for managed projects, MSVC for the native layer.

## Prerequisites

- Windows 11 (dev), **.NET 10 SDK**, **Visual Studio 2026** with: .NET desktop development, **Desktop development with C++** (MSVC v143+, Windows 11 SDK ≥ 10.0.22621), C++ ATL not required.
- **Vulkan SDK** (for the layer + validation layers during development).
- CMake ≥ 3.28 (native projects use CMake; consumed by the solution via a build target — see below).
- Optional: PawnIO for CPU-temperature testing; an NVIDIA GPU for the NVAPI/Reflex paths (dev machine: RTX 5080). AMD/Intel telemetry paths need no SDK — if you have such hardware, testing L1/L2 coverage there is valuable.
- **No admin needed for normal development**: `FL_MOCK=1` runs everything with a synthetic source and zero injection.

## Native build

`src/native/` is CMake-based (`CMakePresets.json`, presets `x64-debug`, `x64-release`):

```
cmake --preset x64-release
cmake --build --preset x64-release
```

Targets:
- `FrameLedger.Overlay` → `FrameLedger.Overlay.dll`
- `FrameLedger.VkLayer` → `FrameLedger.VkLayer.dll` + `VkLayer_frameledger_overlay.json`
- `FrameLedger.Injector` → static lib + `FrameLedger.Injector.exe` (thin CLI used by the Agent and for manual testing)
- `FrameLedger.Overlay.Tests` → Catch2 unit tests (ring buffer, record layout, fault filter, seqlock)
- `hook-harness` → the dummy D3D11/D3D12/Vulkan/OpenGL app (`17_HOOK_ENGINE` §Test harness)

Compiler/linker flags (enforced in CMake, not per-target ad hoc): `/std:c++20 /MT /O2 /GS /guard:cf /Qspectre /GR- /W4 /WX`, `-D_HAS_EXCEPTIONS=0` for the Overlay target. Link `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`.

**MinHook** is vendored as a submodule (or FetchContent) and built from source — pinned by commit, recorded in `THIRD_PARTY_NOTICES`.

**NVAPI SDK** (MIT) is vendored under `src/native/third_party/nvapi/` — headers + `nvapi64.lib`, pinned by upstream tag, with the SPDX blocks left untouched and the licence copied to `legal/licenses/nvapi-MIT.txt`. Link it normally; do **not** resolve NVAPI by ordinal (that was a workaround for a licensing constraint that no longer exists, and it breaks across driver versions). Still guard `NvAPI_Initialize` failure as a normal path — plenty of users have no NVIDIA GPU.

**No AMD or Intel GPU SDK is vendored.** Those vendors are covered by LibreHardwareMonitor and the DXGI/PDH baseline (`18_GPU_VENDOR_APIS`). A build that pulls in Intel IGCL or AMD ADLX material is a licensing regression, not a feature — CI greps for it.

**VERSIONINFO is mandatory** on `FrameLedger.Overlay.dll` and `FrameLedger.VkLayer.dll`: real `CompanyName`, `ProductName=FrameLedger`, `FileDescription`, version. Identifiability is a requirement (`19_SAFETY`), and a CI check fails the build if the resource block is missing.

## Managed build & native integration

- **`global.json` pins the SDK band** (`10.0.x`, `rollForward: latestFeature`). Without it `dotnet build` picks the newest installed SDK — on a machine with a .NET 11 preview installed that silently changes analyzer behaviour under `TreatWarningsAsErrors`, and makes local and CI disagree for reasons nobody can see in a diff.
- `Directory.Build.props`:
  - **`TargetFramework` = `net10.0-windows10.0.22621.0`** and **`SupportedOSPlatformVersion` = `10.0.19045.0`**. These are two different knobs and must not be conflated: the TFM platform version selects which Windows API projections are available to compile against, while the supported version is the floor CA1416 enforces. NFR-8 requires Windows 10 **22H2** = build **19045**; letting the supported version default to the TFM's would silently accept installs below the stated floor. Two constraints pin the target to 22621 rather than something closer to the floor: the SDK rejects `SupportedOSPlatformVersion` above `TargetPlatformVersion` (NETSDK1135), and 19045 is not a targeting-pack version in any case — the packs are cut at 19041, 20348, 22000, 22621, 26100. The Windows 11 SDK ≥ 10.0.22621 is already a prerequisite above, so this costs nothing. A bare `net10.0-windows` is wrong for a different reason: it resolves to `net10.0-windows7.0`, and CA1416 would then error on Mica, `MiniDumpWriteDump` and `QueryVideoMemoryInfo` — every Windows-10-era API this app is built on.
  - `Platforms`/`PlatformTarget` = `x64` and `RuntimeIdentifier` = `win-x64`. "x64 only" is asserted in CLAUDE.md and NFR-8 but was previously enforced nowhere except an ad-hoc `-r win-x64` on the publish command.
  - Nullable enable, ImplicitUsings enable, `TreatWarningsAsErrors=true`, `AnalysisLevel=latest-all`, deterministic, `ContinuousIntegrationBuild` on CI.
- `Directory.Packages.props`: central package management, all versions pinned (including `WPF-UI` = 4.3.0 exactly — `16_WPFUI_SYNTAX` §Version hygiene).
- `FrameLedger.Infrastructure` has a build target that invokes the CMake build and copies `FrameLedger.Overlay.dll`, `FrameLedger.VkLayer.dll` (+ manifest), and `FrameLedger.Injector.exe` into the output as content. The native build runs **before** the managed build in the solution's project dependency order.
- **Struct mirror check:** a test in `FrameLedger.Infrastructure.Tests` reads offsets emitted by a tiny generated header dump from the native build and asserts they match the C# `[StructLayout]` mirror. Struct drift between the two layers is the most dangerous silent bug in this architecture; the build catches it.

## Debugging

Launch profiles:
- **"UI + Mock"** — `FL_MOCK=1`, no Agent, no injection. Default for UI work.
- **"UI + Agent"** — both managed processes, real capture against `hook-harness`.
- **"Harness + Overlay"** — starts `hook-harness` under the debugger with the Overlay injected at launch; attach a second native debugger to step hook code.

Native debugging notes: use **`hook-harness`, never a real game**, for step-through debugging — a breakpoint inside a present hook of a real game freezes it in ways anti-cheat and drivers both dislike. Enable the Vulkan validation layers when touching `FrameLedger.VkLayer`. Application Verifier + PageHeap on the harness catches ring-buffer bugs early.

Agent flags: `--serve`, `--console`, `--diag`, `--install-task`, `--uninstall-task`, `--register-vklayer`, `--unregister-vklayer`.

## Bundled assets

- `assets/native/PresentMon.exe` (pinned, SHA-256 verified at build) for the Tier-2 fallback.
- Vulkan layer manifest, written with the installed layer path at install time (Velopack hook), registered under `HKCU` — never `HKLM`, never requiring admin.

## Publish & package

```
cmake --build --preset x64-release
dotnet publish src/FrameLedger.App   -c Release -r win-x64 --self-contained -p:PublishReadyToRun=true -o out/app
dotnet publish src/FrameLedger.Agent -c Release -r win-x64 --self-contained -p:PublishReadyToRun=true -o out/app
vpk pack --packId FrameLedger --packVersion {ver} --packDir out/app --mainExe FrameLedger.exe --icon assets/icon.ico
```

- No trimming (WPF + reflection), no NativeAOT (WPF unsupported), **no obfuscation** (GPLv3 policy, and `19_SAFETY` forbids making our binaries harder to identify).
- `SatelliteResourceLanguages=en;vi;ja`. Expected package ≈ 95–130 MB self-contained.
- Velopack hooks: installed → offer Agent setup + Vulkan layer registration; uninstalled → unregister the layer, remove the scheduled task, ask about the data folder.

## Release-time token substitution

`{{RELEASE_DATE}}` in `legal/*.md` is the **only** placeholder that survives into
the repository, and it is deliberate: the effective date of a legal document is
the date it ships, which is not knowable at authoring time. `release.yml`
substitutes it with the tag date when packaging, and `ci.yml` fails the build if
**any other** `{{` token appears in `README.md` or `legal/*.md` (`13_CI_CD.md`
§ci.yml). Everything else — repository URL, slug, developer identity, contact
address — is resolved in the source files, because an unsubstituted token in a
document the app displays for acceptance (FR-11) is a defect, not a template.

## Local quality gate (pre-push)

`./build.ps1 check`:
1. `cmake --build --preset x64-release` (C++ `/W4 /WX`)
2. native tests (Catch2)
3. `clang-format --dry-run -Werror` over `src/native`
4. `dotnet restore` + build (warnings as errors)
5. `dotnet format --verify-no-changes`
6. `dotnet test` (incl. the struct-mirror check)
7. `tools/rules-validate.ps1` (schema + `anticheat` block sanity — a malformed or empty blocklist is a safety bug)
8. `tools/license-check.ps1` — asserts every vendored third-party has a licence copy in `legal/licenses/`, and that no Intel IGCL / AMD ADLX material has appeared in the tree
9. `tools/resx-audit`
10. Placeholder guard — fails if any `{{` token other than `{{RELEASE_DATE}}` survives in `README.md` or `legal/*.md`

CI runs the identical script (`13_CI_CD`), so local and CI can never disagree.

**Gates skip loudly.** A gate whose tool is not installed (no `cl.exe`) or not
yet written prints `SKIPPED`, is listed again in the summary, and the run ends
with `PASSED WITH N SKIPPED GATE(S)` rather than a clean `ALL GATES PASSED`.
A gate that silently passes because it did nothing is worse than no gate: it
reads as "checked" in CI output when nothing was checked.

**Gates are proven red-green, not just green.** `license-check` and
`rules-validate` were each verified to fail on a planted violation — an IGCL
header dropped into the tree, and an emptied `anticheat.modules` list — before
being wired in. A safety gate that has only ever been observed passing has not
been tested.
