# 12 — Build & development environment

Mixed toolchain: .NET 10 for managed projects, MSVC for the native layer.

## Prerequisites

- Windows 11 (dev), **.NET 10 SDK**, **Visual Studio 2026** with: .NET desktop development, **Desktop development with C++** (MSVC v143+, Windows 11 SDK ≥ 10.0.22621), C++ ATL not required.
- **Vulkan SDK** (for the layer + validation layers during development).
- CMake ≥ 3.28 (native projects use CMake; consumed by the solution via a build target — see below).
- Optional: PawnIO for CPU-temperature testing; an NVIDIA GPU for the NVAPI/Reflex paths (dev machine: RTX 5080). AMD/Intel telemetry paths need no SDK — if you have such hardware, testing L1/L2 coverage there is valuable.
- **No admin needed for normal development.** `FL_MOCK=1` is **specified but not implemented** — `grep -rn FL_MOCK src tests tools build.ps1` returns nothing (CLAUDE.md §Dev mode records the same, and this line contradicted it in the present tense until 2026-08-05). When it exists it runs the whole app with a synthetic source and zero injection. Until then the no-injection development path is `hook-harness`, which needs no admin either.

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
- `fl_stub_sl_interposer` → `sl.interposer.dll`, and `fl_stub_sl_common` → `sl.common.dll` (`src/native/tools/vendor-stubs/`). **Fixtures, never shipped**, built under `FL_BUILD_TOOLS`; nothing `12_BUILD` publishes references them. They export the **measured** vendor names from `docs/vendor-exports.json` so the Overlay's symbol resolution is observable, and the second is a **decoy** exporting the same name from a different module — without it, "we resolve module-scoped" is a property no test can falsify. Their **output filenames are the vendor's and their version blocks are ours**: `fl_add_version_resource` takes the real filename so a file called `sl.interposer.dll` in our build tree still says plainly that it is FrameLedger's, which `19_SAFETY` requires of everything we build.
- `fl_stub_ffx_dx12` → `amd_fidelityfx_dx12.dll`, `fl_stub_ffx_upscaler` → `amd_fidelityfx_upscaler_dx12.dll`, `fl_stub_ffx_fg` → `amd_fidelityfx_framegeneration_dx12.dll` — the three AMD **leaves** — and `fl_stub_ffx_loader` → `amd_fidelityfx_loader_dx12.dll`, a **forwarding stand-in** bound to the two 2.x leaves through their FrameLedger-named *direct* entry — the way the real loader was measured to bypass the leaves' exports — so `--hold-presenting-ffx` can push every dispatch through a loader that is hooked along with the leaves and prove the count reads 1× (2026-09-04). Same rules as the Streamline stubs: fixtures, never shipped, vendor filenames with our version blocks.
- **`fl_fidelityfx_headers`** → an INTERFACE target over the five vendored FidelityFX ffx-api headers (`src/native/third_party/fidelityfx/`, tag v2.3.0, MIT by exception). Three include directories because upstream's headers include each other by relative path; include directories only and nothing to link — no `.lib` is vendored — and `hookinventory-check` Pass C reads the Overlay's export table because `ffx_api.h` declares its entry points `__declspec(dllexport)` unconditionally.
- **`fl_streamline_headers`** → an INTERFACE target over the vendored MIT Streamline headers (`src/native/third_party/streamline/`). Include directories only, **never linked**: `SL_API` entry points are `extern "C"` imports, and linking one would make `sl.interposer.dll` a load-time dependency of `FrameLedger.Overlay.dll`, which would stop the Overlay loading in every game that ships no Streamline — in the loader, before any of our code runs, with no message anywhere.

Compiler/linker flags (enforced in CMake, not per-target ad hoc): `/std:c++20 /MT /O2 /GS /guard:cf /Qspectre /GR- /W4 /WX`, `-D_HAS_EXCEPTIONS=0` for the Overlay target. Link `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`.

**MinHook** is fetched by CMake `FetchContent` and built from source, pinned to the **commit** behind tag `v1.3.4` (`c3fcafdc`) rather than to the tag name — a tag can be moved, a commit cannot. FetchContent over a submodule so a fresh clone needs no extra step and CI has nothing to remember. Upstream warnings are not subjected to our `/W4 /WX`: warnings in third-party code are upstream's to fix, and failing our build on them would only tempt someone to patch the fetched copy, which drags it out of "consumed unmodified". The BSD-2-Clause notice ships in `legal/licenses/`, and `tools/license-check.ps1` fails the build if it goes missing while the FetchContent declaration is still present.

**NVAPI SDK** (MIT) **is vendored** at `src/native/third_party/nvapi/` — headers, `amd64/nvapi64.lib`, `License.txt`, consumed through `fl_nvapi` and proven by ctest `fl_nvapi_probe`. `legal/licenses/nvapi-MIT.txt` carries the notice and `license-check.ps1` binds the two bidirectionally.

> **This paragraph said "is not vendored yet" until 2026-08-09**, in the present tense, and added that `src/native/third_party/` *"contains `CMakeLists.txt` and `vulkan-headers` only"*. Both were false from #55 (2026-08-05) onward. The sentence was itself a correction — of an earlier claim that NVAPI *was* vendored when it was not — so this file has now been wrong in both directions about the same fact, which is worth more than the fix: **CLAUDE.md's pinned stack and `18_GPU_VENDOR_APIS` §L3 were updated when the vendoring landed and this one was not.** `legal/` is gated bidirectionally and caught its own version; `docs/` is not gated, and this is what that costs. Link it normally; do **not** resolve NVAPI by ordinal (that was a workaround for a licensing constraint that no longer exists, and it breaks across driver versions). Still guard `NvAPI_Initialize` failure as a normal path — plenty of users have no NVIDIA GPU.

**No AMD or Intel GPU *telemetry* SDK is vendored.** Those vendors' sensors are covered by LibreHardwareMonitor and the DXGI/PDH baseline (`18_GPU_VENDOR_APIS`). A build that pulls in Intel IGCL or AMD ADLX material is a licensing regression, not a feature — CI greps for it. **The AMD FidelityFX ffx-api headers (MIT by exception, tag v2.3.0) ARE vendored since 2026-09-04**, for the Overlay's upscaler hook and for types only — `fl_fidelityfx_headers` above, `18_GPU_VENDOR_APIS` §AMD for the licence reading, and `license-check.ps1` §2d for the per-file gate. Intel's XeSS SDK failed the checklist and nothing of it is here.

**VERSIONINFO is mandatory** on `FrameLedger.Overlay.dll` and `FrameLedger.VkLayer.dll`: real `CompanyName`, `ProductName=FrameLedger`, `FileDescription`, version. Identifiability is a requirement (`19_SAFETY`), and a CI check fails the build if the resource block is missing.

## Managed build & native integration

- **`global.json` pins the SDK band** (`10.0.x`, `rollForward: latestFeature`). Without it `dotnet build` picks the newest installed SDK — on a machine with a .NET 11 preview installed that silently changes analyzer behaviour under `TreatWarningsAsErrors`, and makes local and CI disagree for reasons nobody can see in a diff.
- `Directory.Build.props`:
  - **`TargetFramework` = `net10.0-windows10.0.22621.0`** and **`SupportedOSPlatformVersion` = `10.0.19045.0`**. These are two different knobs and must not be conflated: the TFM platform version selects which Windows API projections are available to compile against, while the supported version is the floor CA1416 enforces. NFR-8 requires Windows 10 **22H2** = build **19045**; letting the supported version default to the TFM's would silently accept installs below the stated floor. Two constraints pin the target to 22621 rather than something closer to the floor: the SDK rejects `SupportedOSPlatformVersion` above `TargetPlatformVersion` (NETSDK1135), and 19045 is not a targeting-pack version in any case — the packs are cut at 19041, 20348, 22000, 22621, 26100. The Windows 11 SDK ≥ 10.0.22621 is already a prerequisite above, so this costs nothing. A bare `net10.0-windows` is wrong for a different reason: it resolves to `net10.0-windows7.0`, and CA1416 would then error on Mica, `MiniDumpWriteDump` and `QueryVideoMemoryInfo` — every Windows-10-era API this app is built on.
  - `Platforms`/`PlatformTarget` = `x64`. "x64 only" is asserted in CLAUDE.md and NFR-8 but was previously enforced nowhere except an ad-hoc `-r win-x64` on the publish command.

    > **`RuntimeIdentifier` is NOT set here, and this bullet claimed it was until 2026-08-06.** The props file's own comment explains why: forcing a RID onto class libraries pulls RID-specific assets into their restore for no benefit, so each executable sets it — `FrameLedger.App`, `FrameLedger.Agent` and now `FrameLedger.CaptureHost`, plus its test project, which needs the same RID to reference a RID'd exe. A new project copying this document rather than the props file would have inherited nothing.
  - Nullable enable, ImplicitUsings enable, `TreatWarningsAsErrors=true`, `AnalysisLevel=latest-all`, deterministic, `ContinuousIntegrationBuild` on CI.
- `Directory.Packages.props`: central package management, all versions pinned (including `WPF-UI` = 4.3.0 exactly — `16_WPFUI_SYNTAX` §Version hygiene).
- **Native output reaches the managed side through `.targets` files imported by `FrameLedger.Agent`**, not through `FrameLedger.Infrastructure`: `/FrameLedger.Guard.targets` (the guard DLL), `/FrameLedger.Overlay.targets` (the payload, §S22) and `/FrameLedger.Rules.targets` (the blocklist seed). Ordering comes from `build.ps1`, which runs the native build first — **not** from the solution: `FrameLedger.slnx` contains no native project.

  > **This bullet was wrong in four ways and is corrected 2026-08-04.** It named a copy of `FrameLedger.Injector.exe`, which §S9 closed and which line 25 above says does not exist, 24 lines earlier in the same document. It attributed the copying to a build target in `FrameLedger.Infrastructure.csproj`, which contains no `<Target>` at all. It said the copies happen there, when the guard DLL was deliberately moved out to a `.targets` file so it would stop flowing to every referencing project (§S18 blocker 3). And it credited the solution with an ordering the solution cannot express.
- **Struct mirror check — ✅ built 2026-08-05.** `ShmLayoutMirrorTests` runs `tools/fl-layout-dump`, reads its JSON, and asserts size plus every field offset against the C# `[StructLayout]` mirrors in `FrameLedger.Shared` — in **both directions**, so a field on either side alone fails. It also asserts blittability, which offsets cannot see. The `struct-mirror` gate reads the run's `.trx` and fails when that test class did not execute, so deleting the test is red as well as breaking it. Struct drift between the two layers is the most dangerous silent bug in this architecture; the build now genuinely catches it.

  > Until 2026-08-05 this bullet ended "the build catches it" in the present tense, while `src/FrameLedger.Shared/` held a `.csproj` and no `.cs` files — **and this document contradicted itself**, because line 138 below said correctly all along that the gate did not exist. `20_OPEN_QUESTIONS` §R10 has the history.

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

- ~~`assets/native/PresentMon.exe` (pinned, SHA-256 verified at build) for the Tier-2 fallback~~ — **DROPPED 2026-08-27, and there are no bundled native assets at all.** §S31 measured PresentMon classifying every frame of a ×4 capture as an application frame (row P2); the owner then dropped it outright. It is not bundled, not fetched, not used, and `tools/frametype-oracle.ps1` — the parser that consumed its output — is deleted with it. `assets/` does not exist and now has no reason to. **And since 2026-08-28 Tier 2 is not a measurement at all**: the ladder is two rungs, `EtwFrameSource` is deleted from the design, and whether a shipped build ever regains a no-injection measurement is `20_OPEN_QUESTIONS` §G.

  The struck text is kept because the *reason* it was written is still live. What it said next, and what remains true:

  > **planned, not present.** `assets/` does not exist, nothing fetches or verifies the binary, and `EtwFrameSource` is unwritten. `20_OPEN_QUESTIONS` §M2 (does the pinned console binary still exist, run unelevated, and emit the 2.x column set?) is unanswered, so this is not merely unpinned — there is nothing to pin yet.
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

> **Exactly two roots, and a gate now says so.** `src/FrameLedger.CaptureHost` — the unshipped
> capture host — is outside the package because neither root references it, and
> `tools/package-closure-check.ps1` is what keeps that true rather than remembered. It matters
> more than an ordinary layering rule: §S27 was closed on the grounds that no shipped binary
> carries an injecting entry point, and the host **is** one. A `ProjectReference` from either root
> would reopen §S27 silently, so the gate names the edge. Proven both directions — a reference
> from the Agent leaves the build green and turns the gate red.
>
> Solution membership is NOT the mechanism and must not be confused with it: `FrameLedger.slnx`
> is never passed to `dotnet publish`, and leaving the host out of it would exclude it from
> `dotnet build`, `dotnet format`, the analyzers and `dotnet test` — a far larger hole than the one
> it would appear to close.
>
> **What the gate does not see, said here rather than left to be discovered.** It resolves
> `<ProjectReference>` and nothing else — not `<Import Project="…targets">`, not
> `Directory.Build.props`. This repository's idiom for putting a foreign binary beside a project's
> output is exactly an imported `.targets` with a `<None … CopyToOutputDirectory>` item, so a
> `.targets` that staged `FrameLedger.CaptureHost.exe` into a publish root would be invisible to it.
> Narrower than it sounds — staging a binary is not referencing a project, the shipped assemblies
> would still hold no code path to it, and the copy would be a visible new `<Import>` in a root
> csproj — but it is a hole, and a gate that overstates its reach is the thing this one exists to
> prevent.

- No trimming (WPF + reflection), no NativeAOT (WPF unsupported), **no obfuscation** (GPLv3 policy, and `19_SAFETY` forbids making our binaries harder to identify).
- `SatelliteResourceLanguages=en;vi;ja`. Expected package ≈ 95–130 MB self-contained.
- Velopack hooks: installed → offer Agent setup + Vulkan layer registration; uninstalled → unregister the layer, remove the scheduled task, ask about the data folder.

## Release-time token substitution

`{{RELEASE_DATE}}` in `legal/*.md` is the **only** placeholder that survives into
the repository, and it is deliberate: the effective date of a legal document is
the date it ships, which is not knowable at authoring time. `release.yml` — **which
does not exist yet; `13_CI_CD` §release.yml records that** —
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
6. `dotnet test --logger trx` — including `ShmLayoutMirrorTests`; with no switches it also runs the
   `Category=Integration` classes, which CI excludes
7. `tools/coverage-gate.ps1` — reads this run's cobertura reports; self-arming, and armed today for
   `FrameLedger.Domain` and `FrameLedger.Application`
8. `tools/rules-validate.ps1` (schema + `anticheat` block sanity — a malformed or empty blocklist is a safety bug)
9. `tools/versioninfo-check.ps1` — reads the built binary, because what ships is what an anti-cheat vendor sees
10. `tools/chokepoint-check.ps1` — injection and evasion primitives confined to one file, native **and** managed, plus the `FL_GUARD_TESTABLE` symbol check against the shipped artifacts
11. **`tools/hookinventory-check.ps1`** — three passes: A resolves every vendor symbol the Overlay names against `docs/vendor-exports.json`, B sweeps for stray literals, C reads the BUILT Overlay's import table and asserts it imports no vendor module. C skips loudly under `-SkipNative`. **Missing from this list until 2026-08-28**, which is §R10 happening again
12. **`tools/package-closure-check.ps1`** — both halves: the self-test (5 cases, 4 of which must go RED) **and** a live pass over this repository. It walks the transitive `ProjectReference` closure of the two publish roots below and fails on anything outside the allowlist, naming the reference edge. `FrameLedger.CaptureHost` is an injecting entry point kept out of the package by construction, and `20_OPEN_QUESTIONS` §S27 is closed on exactly that basis
13. `tools/license-check.ps1` — asserts every vendored third-party has a licence copy in `legal/licenses/`, and that no Intel IGCL / AMD ADLX material has appeared in the tree
14. `tools/changelog-check.ps1 -SelfTest` — nine cases, five expected RED. The live half needs a pull request's changed-file list and is supplied by `ci.yml`
15. `tools/resx-audit` — **skipped loudly; it does not exist, and no `.resx` file does either**
16. **struct-mirror** — reads this run's `.trx` and fails when `ShmLayoutMirrorTests` did not execute, so deleting the mirror test is red as well as breaking it
17. Placeholder guard — fails if any `{{` token other than `{{RELEASE_DATE}}` survives in `README.md` or `legal/*.md`

> **This list was wrong in two ways until 2026-08-06 and both were the same kind of wrong.** Step 6
> said the struct-mirror check *"does not exist; `build.ps1` declares and skips it loudly"* — while
> line 52 of this same document, and `build.ps1` itself, had it as a hard throwing gate since
> 2026-08-05. And the list omitted four gates the script actually runs: `coverage-gate`,
> `versioninfo-check`, `chokepoint-check` and `changelog-check`. A document whose job is to be the
> list of what `check` does was missing 40% of it, which is worse than having no list: a reader
> plans against it. `13_CI_CD.md` repeats the struct-mirror claim and is corrected with it.

CI runs the identical script (`13_CI_CD`), **with `-SkipIntegration`** — so a green CI is not
evidence for anything touching the managed drain, and `./build.ps1 check` with no switches is what a
developer runs before pushing.

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
