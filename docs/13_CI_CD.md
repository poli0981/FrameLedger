# 13 — CI/CD

FrameLedger uses the **`poli0981/.github` ops repo** where its templates fit, and repo-local workflows where they do not. `ci.yml` is repo-local **by necessity, not preference**: the ops repo's `reusable-desktop-csharp.yml` runs `dotnet restore` / `build` / `test` directly and exposes no input for a native pre-step, MSVC setup, or the Vulkan SDK. FrameLedger's build is mixed-toolchain and native-first, and `12_BUILD.md` §Local quality gate commits to CI running *the identical script* as local — a promise a pure-managed template cannot keep. CodeQL and release still call the ops repo.

> ⚠ Known gotcha (learned on earlier migrations): caller stubs **must declare explicit `permissions:` blocks** — permissions do not inherit into reusable workflows. Every stub below lists its own.

## Workflows (`.github/workflows/`)

### `ci.yml` — push to `main` + all PRs · **repo-local**
- `runs-on: windows-latest`, .NET SDK pinned by `global.json` via `actions/setup-dotnet`, MSVC via `ilammy/msvc-dev-cmd`, NuGet cached.
- Single step of substance: **`./build.ps1 check -SkipIntegration`** — the same script a developer runs before pushing. The gate list lives in `12_BUILD` §Local quality gate, **once**, rather than being restated here where it goes stale.
- **`-SkipIntegration` is the one place local and CI deliberately differ**, and the reason is a measured property of the hard gate rather than flakiness: §S16 puts the injecting process's ancestors in the scan set, a .NET test host can load `System.Security.Cryptography.ProtectedData.dll`, and the guard's `protect` fragment then refuses our own harness (§S19(b)). So a green CI is **not** evidence for anything touching the managed drain or the capture host's end-to-end behaviour.
- Uploads coverage artifacts.
- **Changelog gate**, on pull requests only: the changed-file list comes from GitHub's own view of the PR — not `git diff`, so a shallow checkout cannot silently produce a short one — and an empty list is refused rather than read as "no `src/` changes". It is a **step of the required `check` job** and deliberately not a job of its own: `main`'s required contexts are exactly `check` and the two `analyze` jobs, so a new job would go red while the merge button stayed green, which is what already makes `Rules / validate` advisory (§S23-2).

> **Three claims in this section were false and are corrected 2026-08-06.** There is no Vulkan SDK
> step and there will not be one — the Khronos headers are vendored. `resx-audit` does not exist and
> is skipped loudly, so no artifact of it is uploaded. And the struct-mirror parenthesis — *"that
> gate does not exist and `build.ps1` skips it loudly"* — has been false since 2026-08-05:
> `build.ps1` implements it as a hard throwing gate that reads the run's `.trx` and fails when
> `ShmLayoutMirrorTests` did not execute. `12_BUILD` carried the identical stale sentence and is
> corrected with it; restating the gate list in two documents is what let one of them rot.
- **Licence guard:** `tools/license-check` fails the build if a vendored dependency is missing its licence copy, or if Intel IGCL / AMD ADLX headers appear anywhere in the tree (`docs/18_GPU_VENDOR_APIS.md` §Vendor SDKs we deliberately do not use). Licensing regressions are silent and hard to unwind later — catch them at PR time.
- **Placeholder guard:** fails if any `{{` token survives in `README.md` or `legal/*.md`. Those are shipped, legally operative documents (FR-11 displays them in the first-run Legal Gate); an unsubstituted `{{DEVELOPER_NAME}}` in an EULA is not a cosmetic defect.
- `permissions: contents: read`.

### `codeql.yml` — push, PR, weekly cron · **repo-local**

> **Not a caller stub, which this heading said until 2026-08-06.** `codeql.yml`'s own header says
> the opposite: the ops repo has `codeql-mixed.yml`, but C++ needs manual build mode driven by the
> CMake preset and that input surface has not been verified. The paragraph below already anticipated
> exactly this outcome; the heading was never updated when it happened.
- Languages: `csharp` **and `cpp`** (manual build mode for C++, driven by the CMake preset). The native layer is where memory-safety bugs would live; excluding it would defeat the purpose. Verify the mixed template exposes a C++ build-command input; if it does not, this one goes repo-local too.
- `permissions: security-events: write, contents: read`.

### `release.yml` — on tag `v*` · **PLANNED, NOT PRESENT**

> `.github/workflows/` contains `ci.yml`, `codeql.yml` and `rules-publish.yml`. There is no release
> automation at all: the publish commands exist only as a fenced block in `12_BUILD` §Publish &
> package, and **nothing in this repository has ever run `dotnet publish`** — which is why
> `out/app`'s real contents had never been asserted by anything until
> `tools/package-closure-check.ps1` started walking the reference closure statically. Recorded
> 2026-08-06 rather than left describing a workflow that does not exist.
- Build + test (same script, native first) → publish App+Agent self-contained → ~~verify PresentMon SHA-256~~ *(retired 2026-08-27 — the console binary is not bundled; owner decision, `20_OPEN_QUESTIONS` §G)* → **verify VERSIONINFO present on `FrameLedger.Overlay.dll` and `FrameLedger.VkLayer.dll`** (identifiability is a safety requirement, `19_SAFETY`) → `vpk pack` → generate `SHA256SUMS.txt` → create GitHub Release with Velopack assets + checksums, release notes from `CHANGELOG.md` section.
- Optional final step: submit installer hash to VirusTotal and append the report link to release notes (helps unsigned-binary trust).
- `permissions: contents: write`.

### `rules-publish.yml` — on change to `rules/detection-rules.json` in `main`
- Runs `tools/rules-validate` and fails on anti-cheat removals. The raw file on `main` **is** the distribution endpoint (05_DETECTION), so this workflow only gates correctness.

  > **There is no `rulesVersion` bump check, and this line used to claim one.**
  > `rules-validate.ps1` checks the field's *format* and nothing compares it
  > against the merge base, so a blocklist edit ships with the version untouched —
  > measured on this repository's own history, that is what every commit that
  > changed the `anticheat` block actually did, while the one commit that bumped
  > `rulesVersion` changed the block not at all.
  >
  > Recorded rather than built, because nothing depends on the ordering: §S20's
  > seeder deliberately uses **provenance** (a hash of what it installed) rather
  > than version comparison, precisely because that history falsifies the
  > comparison. A monotonicity gate is still worth having before anything else
  > starts trusting the field — an unbuilt gate described as existing is the
  > defect this note replaces.

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
