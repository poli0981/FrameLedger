# 13 — CI/CD

FrameLedger uses the **`poli0981/.github` ops repo** where its templates fit, and repo-local workflows where they do not. `ci.yml` is repo-local **by necessity, not preference**: the ops repo's `reusable-desktop-csharp.yml` runs `dotnet restore` / `build` / `test` directly and exposes no input for a native pre-step, MSVC setup, or the Vulkan SDK. FrameLedger's build is mixed-toolchain and native-first, and `12_BUILD.md` §Local quality gate commits to CI running *the identical script* as local — a promise a pure-managed template cannot keep. CodeQL and release still call the ops repo.

> ⚠ Known gotcha (learned on earlier migrations): caller stubs **must declare explicit `permissions:` blocks** — permissions do not inherit into reusable workflows. Every stub below lists its own.

## Workflows (`.github/workflows/`)

### `ci.yml` — push to `main` + all PRs · **repo-local**
- `runs-on: windows-latest`, .NET SDK pinned by `global.json` via `actions/setup-dotnet`, MSVC via `ilammy/msvc-dev-cmd`, CMake + Vulkan SDK cached.
- Single step of substance: **`./build.ps1 check`** — the same script a developer runs before pushing, so local and CI cannot disagree. It covers the native build (`/W4 /WX`), Catch2 tests, `clang-format --dry-run -Werror`, managed restore/build with warnings as errors, `dotnet format --verify-no-changes`, `dotnet test` including the struct-mirror check, `rules-validate`, `license-check`, and `resx-audit`.
- Uploads coverage + resx-audit artifacts.
- **Licence guard:** `tools/license-check` fails the build if a vendored dependency is missing its licence copy, or if Intel IGCL / AMD ADLX headers appear anywhere in the tree (`docs/18_GPU_VENDOR_APIS.md` §Vendor SDKs we deliberately do not use). Licensing regressions are silent and hard to unwind later — catch them at PR time.
- **Placeholder guard:** fails if any `{{` token survives in `README.md` or `legal/*.md`. Those are shipped, legally operative documents (FR-11 displays them in the first-run Legal Gate); an unsubstituted `{{DEVELOPER_NAME}}` in an EULA is not a cosmetic defect.
- `permissions: contents: read`.

### `codeql.yml` — push, PR, weekly cron · caller stub → `poli0981/.github` `codeql-mixed.yml`
- Languages: `csharp` **and `cpp`** (manual build mode for C++, driven by the CMake preset). The native layer is where memory-safety bugs would live; excluding it would defeat the purpose. Verify the mixed template exposes a C++ build-command input; if it does not, this one goes repo-local too.
- `permissions: security-events: write, contents: read`.

### `release.yml` — on tag `v*`
- Build + test (same script, native first) → publish App+Agent self-contained → verify PresentMon SHA-256 → **verify VERSIONINFO present on `FrameLedger.Overlay.dll` and `FrameLedger.VkLayer.dll`** (identifiability is a safety requirement, `19_SAFETY`) → `vpk pack` → generate `SHA256SUMS.txt` → create GitHub Release with Velopack assets + checksums, release notes from `CHANGELOG.md` section.
- Optional final step: submit installer hash to VirusTotal and append the report link to release notes (helps unsigned-binary trust).
- `permissions: contents: write`.

### `rules-publish.yml` — on change to `rules/detection-rules.json` in `main`
- Runs `tools/rules-validate` → bumps `rulesVersion` check → the raw file on `main` **is** the distribution endpoint (05_DETECTION), so this workflow only gates correctness.

  > **The validator does not evaluate rules**, and this line used to say it ran
  > "against fixtures". It checks the schema, the imperative constraints, the
  > parser's capacity bounds, and **fixture coverage** — that every rule id has a
  > fixture and every fixture has a rule. The evaluation is
  > `RuleFixtureCorpusTests` in `ci.yml`'s `check` job, which drives the real
  > evaluator through the real probe. Both halves run on the same PR; only the
  > coverage half fires on a rules-only change, which is why the two are
  > described separately rather than as one gate.
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

- [ ] Add repo-local `ci.yml`; add caller stubs for CodeQL + release pointing at `poli0981/.github` with the explicit permissions above
- [ ] Enable Dependabot alerts + security updates
- [ ] Add `bug_report.yml`, `feature_request.yml` issue forms; PR template referencing CLAUDE.md definition-of-done
- [ ] Branch protection as above
- [ ] Repo topics: `windows`, `wpf`, `benchmark`, `fps`, `presentmon`, `game-performance`
