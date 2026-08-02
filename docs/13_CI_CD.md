# 13 — CI/CD

FrameLedger consumes the centralized reusable workflows from the **`poli0981/.github` ops repo**, template family **`desktop-csharp`**, with repo-local caller stubs.

> ⚠ Known gotcha (learned on earlier migrations): caller stubs **must declare explicit `permissions:` blocks** — permissions do not inherit into reusable workflows. Every stub below lists its own.

## Workflows (`.github/workflows/`)

### `ci.yml` — push to `main` + all PRs
- `runs-on: windows-latest`, .NET 10 SDK via `actions/setup-dotnet`, MSVC via `ilammy/msvc-dev-cmd`, CMake + Vulkan SDK cached.
- Steps: checkout → NuGet cache → `./build.ps1 check` (restore, build `-warnaserror`, `dotnet format --verify-no-changes`, `dotnet test --collect:"XPlat Code Coverage"`, rules-validate, resx-audit) → upload coverage + resx-audit artifacts.
- **Licence guard:** `tools/license-check` fails the build if a vendored dependency is missing its licence copy, or if Intel IGCL / AMD ADLX headers appear anywhere in the tree (`docs/18_GPU_VENDOR_APIS.md` §Vendor SDKs we deliberately do not use). Licensing regressions are silent and hard to unwind later — catch them at PR time.
- `permissions: contents: read`.

### `codeql.yml` — push, PR, weekly cron
- Languages: `csharp` **and `cpp`** (manual build mode for C++, driven by the CMake preset). The native layer is where memory-safety bugs would live; excluding it would defeat the purpose.
- `permissions: security-events: write, contents: read`.

### `release.yml` — on tag `v*`
- Build + test (same script, native first) → publish App+Agent self-contained → verify PresentMon SHA-256 → **verify VERSIONINFO present on `FrameLedger.Overlay.dll` and `FrameLedger.VkLayer.dll`** (identifiability is a safety requirement, `19_SAFETY`) → `vpk pack` → generate `SHA256SUMS.txt` → create GitHub Release with Velopack assets + checksums, release notes from `CHANGELOG.md` section.
- Optional final step: submit installer hash to VirusTotal and append the report link to release notes (helps unsigned-binary trust).
- `permissions: contents: write`.

### `rules-publish.yml` — on change to `rules/detection-rules.json` in `main`
- Runs `tools/rules-validate` against fixtures → bumps `rulesVersion` check → the raw file on `main` **is** the distribution endpoint (05_DETECTION), so this workflow only gates correctness.
- **The `anticheat` block gets extra scrutiny:** schema-valid, non-empty, no entry removed without a justification in the PR body. Removing a blocklist entry is a safety change and requires the same review as a security fix.
- `permissions: contents: read`.

## Dependabot (`.github/dependabot.yml`)

```yaml
version: 2
updates:
  - package-ecosystem: nuget
    directory: /
    schedule: { interval: weekly }
    groups: { minor-and-patch: { update-types: [minor, patch] } }
  - package-ecosystem: github-actions
    directory: /
    schedule: { interval: weekly }
```

Central package management makes Dependabot PRs single-file diffs.

## Branch & release policy

- `main` protected: CI + CodeQL required, linear history, no direct pushes.
- Release: update `CHANGELOG.md` (Keep a Changelog format) → tag `vX.Y.Z` → `release.yml` does the rest → smoke-test the produced installer on a clean Win 10 VM + Win 11 (manual checklist in 14_TESTING §Release smoke).
- Pre-releases: `vX.Y.Z-beta.N` tags mark the GitHub release as pre-release; Velopack stable channel ignores them (explicit beta channel is v2 backlog).

## Repo hygiene checklist (one-time setup)

- [ ] Add caller stubs pointing at `poli0981/.github` reusable workflows with the explicit permissions above
- [ ] Enable Dependabot alerts + security updates
- [ ] Add `bug_report.yml`, `feature_request.yml` issue forms; PR template referencing CLAUDE.md definition-of-done
- [ ] Branch protection as above
- [ ] Repo topics: `windows`, `wpf`, `benchmark`, `fps`, `presentmon`, `game-performance`
