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
- `FrameLedger.VkLayer` → `FrameLedger.VkLayer.dll` + `VkLayer_FRAMELEDGER_overlay.json`
- `FrameLedger.Injector` → **static lib only.** There is no `FrameLedger.Injector.exe` and none ships (`20_OPEN_QUESTIONS` §S9, decided 2026-08-02). A standalone `LoadLibraryW` injector that users can run is a path into a game process that the guard does not stand in front of — a bypass, and a bad look for a project whose position is "we refuse where we are not welcome". The Agent links the static lib; the guard owns the chokepoint inside it (§S13(b)), so there is no callable entry point that skips the gate. Manual testing uses `hook-harness`, never a real game (§Debugging).
- `FrameLedger.Overlay.Tests` → Catch2 unit tests (ring buffer, record layout, fault filter, seqlock) **and the guard's fail-closed matrix** (`14_TESTING` §Safety-guard tests) — the guard is native, so its tests are too
- `hook-harness` → the dummy **D3D11 + D3D12** app, WARP + composition swapchain so it runs headless on CI (`17_HOOK_ENGINE` §Test harness). D3D12 is device → command queue → swapchain, which is the acquisition asymmetry a D3D11-only fixture cannot exercise. Vulkan and OpenGL modes to follow as those hooks land.
- `fl-probe-hookprofile` → build-profile probes for `/guard:cf` and `-D_HAS_EXCEPTIONS=0` (`20_OPEN_QUESTIONS` §H1/§H3)
- `fl-probe-guard` → measures the Windows APIs the guard is built on, unelevated (`spike-notes.md` §1). Not the guard, and takes no injection rights.

Compiler/linker flags (enforced in CMake, not per-target ad hoc): `/std:c++20 /MT /O2 /GS /guard:cf /Qspectre /GR- /W4 /WX`, `-D_HAS_EXCEPTIONS=0` for the Overlay target. Link `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`.

**MinHook** is fetched by CMake `FetchContent` and built from source, pinned to the **commit** behind tag `v1.3.4` (`c3fcafdc`) rather than to the tag name — a tag can be moved, a commit cannot. FetchContent over a submodule so a fresh clone needs no extra step and CI has nothing to remember. Upstream warnings are not subjected to our `/W4 /WX`: warnings in third-party code are upstream's to fix, and failing our build on them would only tempt someone to patch the fetched copy, which drags it out of "consumed unmodified". The BSD-2-Clause notice ships in `legal/licenses/`, and `tools/license-check.ps1` fails the build if it goes missing while the FetchContent declaration is still present.

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
- **Native output reaches the managed side through `.targets` files imported by `FrameLedger.Agent`**, not through `FrameLedger.Infrastructure`: `/FrameLedger.Guard.targets` (the guard DLL), `/FrameLedger.Overlay.targets` (the payload, §S22) and `/FrameLedger.Rules.targets` (the blocklist seed). Ordering comes from `build.ps1`, which runs the native build first — **not** from the solution: `FrameLedger.slnx` contains no native project.

  > **This bullet was wrong in four ways and is corrected 2026-08-04.** It named a copy of `FrameLedger.Injector.exe`, which §S9 closed and which line 25 above says does not exist, 24 lines earlier in the same document. It attributed the copying to a build target in `FrameLedger.Infrastructure.csproj`, which contains no `<Target>` at all. It said the copies happen there, when the guard DLL was deliberately moved out to a `.targets` file so it would stop flowing to every referencing project (§S18 blocker 3). And it credited the solution with an ordering the solution cannot express.
- **Struct mirror check:** a test in `FrameLedger.Infrastructure.Tests` reads offsets emitted by a tiny generated header dump from the native build and asserts they match the C# `[StructLayout]` mirror. Struct drift between the two layers is the most dangerous silent bug in this architecture; the build catches it.

## Debugging

Launch profiles:
- **"UI + Mock"** — `FL_MOCK=1`, no Agent, no injection. Default for UI work.
- **"UI + Agent"** — both managed processes, real capture against `hook-harness`.
- **"Harness + Overlay"** — starts `hook-harness` under the debugger with the Overlay injected at launch; attach a second native debugger to step hook code.

**Toolchain gotchas, both hit on a real machine and both handled by `build.ps1`:**

- **msys2 / MinGW on `PATH` breaks the native build.** `vcvars64` *prepends* to
  whatever `PATH` it inherits, and MSVC ships `link.exe` — there is no `ld.exe`
  to shadow MinGW's. CMake then picks MinGW's linker and the build dies with
  `cannot find /nologo: No such file or directory`, a failure a long way from
  its cause. `build.ps1` strips only the MinGW entries before importing the
  MSVC environment, so `dotnet`, `git` and `cmake` survive.
- **`vcvars64` exports `Platform=x64`,** which is meaningful for `.vcxproj`
  builds. We have none, and MSBuild reads it as a *solution* platform, so
  `dotnet build FrameLedger.slnx` then fails with `MSB4126 solution
  configuration "Release|x64" is invalid`. `build.ps1` clears it after import;
  project-level x64 comes from `Directory.Build.props`.
- `ninja` and `clang-format` both ship inside the VS C++ workload but are on
  neither the default `PATH` nor the `vcvars` one. `build.ps1` locates both, so
  the C++ workload alone is enough to run every gate.

Native debugging notes: use **`hook-harness`, never a real game**, for step-through debugging — a breakpoint inside a present hook of a real game freezes it in ways anti-cheat and drivers both dislike. Enable the Vulkan validation layers when touching `FrameLedger.VkLayer`. Application Verifier + PageHeap on the harness catches ring-buffer bugs early.

Agent flags: `--serve`, `--console`, `--diag`, `--install-task`, `--uninstall-task`, `--register-vklayer`, `--unregister-vklayer`.

## Bundled assets

- `assets/native/PresentMon.exe` (pinned, SHA-256 verified at build) for the Tier-2 fallback.
- Vulkan layer manifest, **written** with the installed layer path at install time (Velopack hook) — but **not registered there**. Registration is a separate, later act.

### The Vulkan layer is not registered at install time

`20_OPEN_QUESTIONS` §S10. An implicit layer is **machine-wide by nature**: once
registered, the loader maps it into *every* Vulkan process on the system,
including ones the user never added to FrameLedger. Registering it as a side
effect of installation would put our DLL into unrelated Vulkan applications
before the user has enabled a single game — and before any consent dialog has
been shown, which contradicts FR-2.1 and `19_SAFETY` §User-facing consent.

The correct rule is the one `17_HOOK_ENGINE` §Vulkan already states, and it is
now the only rule: **registered only while at least one Vulkan game has hooking
enabled**, unregistered when the last such game is disabled, and unregistered on
uninstall. Under `HKCU` — never `HKLM`, never requiring admin.

This governs all three places that touched registration: the install hook above,
the `--register-vklayer` Agent flag (§Agent flags — a manual repair tool, not the
normal path), and the Settings button (`08_UI` §Settings — it reflects and
repairs state, it does not grant machine-wide reach on its own).

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
6. `dotnet test` — **not** including the struct-mirror check, which does not exist; `build.ps1` declares and skips it loudly (§R10)
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
