#Requires -Version 7.0
<#
.SYNOPSIS
    Validates rules/detection-rules.json, with extra scrutiny on the anticheat
    block that feeds the hard gate.

.DESCRIPTION
    A malformed or empty blocklist is a SAFETY bug, not a lint failure
    (docs/13_CI_CD.md §rules-publish.yml). This runs in build.ps1 check and in
    CI on every change to the rules file.

    Fail-closed is the guiding rule everywhere: if this script cannot prove the
    blocklist is well-formed and non-empty, it fails.

    Two layers, because neither alone is sufficient:

    1. JSON Schema (rules/detection-rules.schema.json), proved DISCRIMINATING by
       a canary before it is trusted — Test-Json fails open on a malformed
       schema (docs/20_OPEN_QUESTIONS.md §S5).
    2. Imperative checks a schema cannot express: required families still
       present IN THE RIGHT GROUP, no case-insensitive duplicate values, and no
       prefix short enough to shadow a system module.
#>
[CmdletBinding()]
param(
    [string]$Path = (Join-Path (Split-Path $PSScriptRoot -Parent) 'rules/detection-rules.json'),

    # docs/19_SAFETY_AND_ANTICHEAT.md §Blocklist seed is NORMATIVE: it is what a
    # maintainer reads before editing the data. When the two disagree, the doc
    # wins in a reader's head and the data wins at runtime — which is how the
    # glob-syntax hole got published. Cross-checked here so they cannot drift.
    [string]$DocPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'docs/19_SAFETY_AND_ANTICHEAT.md')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$errors = [System.Collections.Generic.List[string]]::new()

if (-not (Test-Path $Path)) {
    Write-Host "RULES VALIDATION FAILED: $Path does not exist" -ForegroundColor Red
    exit 1
}

try {
    $rulesRaw = Get-Content $Path -Raw
    $rules = $rulesRaw | ConvertFrom-Json
}
catch {
    Write-Host "RULES VALIDATION FAILED: not valid JSON — $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# --- Schema validation (S5) -------------------------------------------------
# Test-Json FAILS OPEN on a broken schema: measured on PowerShell 7.6.4,
# `-Schema '{'` returns $true while writing "Cannot parse the JSON schema" to
# the error stream. A corrupted or truncated schema file would therefore make
# every rules file "valid", including one with an empty blocklist.
#
# So the schema is proved to be DISCRIMINATING before it is trusted: a canary
# document that MUST fail is validated first. If the canary passes, the schema
# is not doing its job and we refuse rather than continue.
$schemaPath = Join-Path (Split-Path $Path -Parent) 'detection-rules.schema.json'
if (-not (Test-Path $schemaPath)) {
    Write-Host "RULES VALIDATION FAILED: schema missing at $schemaPath" -ForegroundColor Red
    exit 1
}
$schema = Get-Content $schemaPath -Raw

$canaryPassed = $false
try {
    # Missing every required key — no working schema can accept this.
    $canaryPassed = ('{"schemaVersion":"not-a-number"}' | Test-Json -Schema $schema -ErrorAction SilentlyContinue)
}
catch { $canaryPassed = $false }
if ($canaryPassed) {
    Write-Host 'RULES VALIDATION FAILED: the schema accepted a document it must reject.' -ForegroundColor Red
    Write-Host '  The schema is unusable (Test-Json fails open on a malformed schema), so' -ForegroundColor Red
    Write-Host '  a pass here would mean nothing. Refusing rather than reporting success.' -ForegroundColor Red
    exit 1
}

$schemaOk = $false
try { $schemaOk = ($rulesRaw | Test-Json -Schema $schema -ErrorAction Stop) }
catch {
    Write-Host "RULES VALIDATION FAILED: does not match the schema — $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
if (-not $schemaOk) {
    Write-Host 'RULES VALIDATION FAILED: does not match rules/detection-rules.schema.json' -ForegroundColor Red
    exit 1
}

# --- Canary 2: the schema still discriminates on nameFragments (§S19(d)) -----
#
# The canary above is `{"schemaVersion":"not-a-number"}`, which ANY schema still
# pinning schemaVersion rejects. It proves the schema is not entirely inert; it
# proves nothing about the constraints that do the safety work. Delete `minItems`
# from `nameFragments` and that canary still passes, Test-Json still passes, and
# the schema half of the heuristic floor silently ceases to exist.
#
# BE PRECISE ABOUT WHAT THAT WOULD COST, because §S19(d) overstates it: the floor
# would NOT disappear. tools/gen-ac-floor.ps1 hard-errors on an empty fragment
# list and runs as a CMake custom command, so the native build fails. What is
# unguarded is the SCHEMA half — a rules file with an empty list reaching a
# machine, where the compiled-in floor is what saves it.
#
# DERIVED FROM THE SHIPPED DOCUMENT, never hand-written. A hand-written canary is
# a second statement of the schema's shape and drifts from it, which is the defect
# this whole file exists to catch. Mutating the real document means the ONLY
# difference between the passing case and the failing case is the constraint under
# test.
#
# Both directions: the unmodified document has just been asserted to pass, three
# lines above. So a failure here is specifically "the empty list was accepted".
$fragmentCanaryPassed = $false
try {
    $mutant = $rulesRaw | ConvertFrom-Json
    $mutant.anticheat.heuristic.nameFragments = @()
    $fragmentCanaryPassed = ($mutant | ConvertTo-Json -Depth 100 | Test-Json -Schema $schema -ErrorAction SilentlyContinue)
}
catch { $fragmentCanaryPassed = $false }
if ($fragmentCanaryPassed) {
    Write-Host 'RULES VALIDATION FAILED: the schema accepted an EMPTY anticheat.heuristic.nameFragments.' -ForegroundColor Red
    Write-Host '  The unknown-but-suspicious tier is the only coverage for families the seed' -ForegroundColor Red
    Write-Host '  admits it has no data for (Ricochet, VAC). `minItems` on that array is what' -ForegroundColor Red
    Write-Host '  stops a rules push emptying it; if this passes, that constraint is gone.' -ForegroundColor Red
    Write-Host '  See docs/20_OPEN_QUESTIONS.md §S19(d).' -ForegroundColor Red
    exit 1
}

function Test-Member($Object, [string]$Name) {
    return $null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name
}

# --- Top level --------------------------------------------------------------
foreach ($key in 'schemaVersion', 'rulesVersion', 'engines', 'platforms', 'capabilities', 'anticheat') {
    if (-not (Test-Member $rules $key)) { $errors.Add("missing top-level key: $key") }
}

if ((Test-Member $rules 'schemaVersion') -and $rules.schemaVersion -ne 2) {
    $errors.Add("schemaVersion must be 2, got $($rules.schemaVersion)")
}

if ((Test-Member $rules 'rulesVersion') -and $rules.rulesVersion -notmatch '^\d{4}\.\d{2}\.\d+$') {
    $errors.Add("rulesVersion must look like YYYY.MM.N, got '$($rules.rulesVersion)'")
}

# --- Identifier uniqueness --------------------------------------------------
foreach ($section in 'engines', 'platforms', 'capabilities') {
    if (-not (Test-Member $rules $section)) { continue }
    $ids = @($rules.$section | ForEach-Object { $_.id })
    $dupes = $ids | Group-Object | Where-Object Count -gt 1
    foreach ($d in $dupes) { $errors.Add("duplicate id '$($d.Name)' in $section") }
    foreach ($id in $ids) {
        if ([string]::IsNullOrWhiteSpace($id)) { $errors.Add("empty id in $section") }
    }
}

# --- §S23-5: no `$comment` anywhere may enumerate the gate's composition -----
#
# The composition of the pre-injection gate is stated in
# docs/19_SAFETY_AND_ANTICHEAT.md §Pre-injection checks, ONCE. This file's
# top-level `$comment` used to restate it — a fourth copy, in the one artifact
# that ships to users — and that copy was wrong in two ways at once: it omitted
# check 2b (services), the only tier ever MEASURED firing on real anti-cheat, and
# it went stale the moment check 3's executable half was wired.
#
# §S23-4 closed the identical class by REMOVING a restatement rather than
# correcting one. This is what makes the removal stick.
#
# WALKS EVERY `$comment` IN THE DOCUMENT, and that is not thoroughness for its own
# sake — it is a bug fix. The first version of this check was scoped to
# `anticheat.$comment`, and the text it exists to catch lives in the TOP-LEVEL
# `$comment`. It could not fire. Its canary reported green twice: once because a
# backtick inside a double-quoted PowerShell needle silently mangled the search
# string so the mutation never applied, and once for real. A check scoped to the
# wrong object is this file's own signature defect, committed inside the fix for
# it. Walking every `$comment` also means moving the text does not dodge the gate.
#
# WHAT IT DOES NOT FORBID: mentioning a check. "tracked as §S14" and "check 3
# matches nothing" are facts about THIS DATA and belong here. What it forbids is a
# LIST — two or more check numbers in one line is a composition claim, and
# composition claims belong in exactly one document.
function Get-CommentLines($Node, [string]$Path) {
    $out = [System.Collections.Generic.List[object]]::new()
    if ($null -eq $Node) { return $out }
    if ($Node -is [System.Management.Automation.PSCustomObject]) {
        foreach ($prop in $Node.PSObject.Properties) {
            if ($prop.Name -eq '$comment') {
                foreach ($line in @($prop.Value)) {
                    $out.Add([pscustomobject]@{ Where = "$Path.`$comment"; Line = [string]$line })
                }
            }
            else {
                foreach ($item in (Get-CommentLines $prop.Value "$Path.$($prop.Name)")) { $out.Add($item) }
            }
        }
    }
    elseif ($Node -is [System.Object[]]) {
        for ($i = 0; $i -lt $Node.Count; $i++) {
            foreach ($item in (Get-CommentLines $Node[$i] "$Path[$i]")) { $out.Add($item) }
        }
    }
    return $out
}

$commentLines = Get-CommentLines $rules 'rules'
if ($commentLines.Count -eq 0) {
    # Not a style complaint: the file HAS comment blocks, so zero means the walk
    # broke and the check below would report clean having looked at nothing.
    $errors.Add('the $comment walk found no comment lines at all — it is not looking where the comments are, so its verdict means nothing (§S23-5)')
}
foreach ($entry in $commentLines) {
    $hits = [regex]::Matches($entry.Line, '(?i)\bcheck(?:s)?\s+\d')
    $listed = $entry.Line -match '(?i)\bchecks?\s+\d[\dab,\s]*(?:and|,)\s*\d'
    if ($hits.Count -ge 2 -or $listed) {
        $errors.Add("$($entry.Where) enumerates the gate's composition: '$($entry.Line)'. That list lives in docs/19_SAFETY_AND_ANTICHEAT.md §Pre-injection checks, once (§S23-5) — a copy here ships to users and has already been wrong twice.")
    }
}

# --- The anticheat block: fail closed ---------------------------------------
if (Test-Member $rules 'anticheat') {
    $ac = $rules.anticheat

    # §S19(a). A fragment that is a superstring of another can NEVER FIRE: the
    # match is a case-insensitive substring, so `guard` matches everything
    # `gameguard` would, and it matches first. A shipped rule incapable of firing
    # independently is this file's own recurring defect sitting inside the safety
    # gate.
    #
    # It is not merely cosmetic, and the hazard is in the FUTURE: someone removing
    # `guard` while `gameguard` sits below it in the list removes nProtect's fuzzy
    # coverage entirely, and the list still looks like it has a rule for it. This
    # check forces the list to stay minimal, so a removal is visibly a removal.
    #
    # WHAT IT DOES NOT CHECK: whether a fragment is a good idea, or whether the
    # tier covers anything. It answers exactly "can each of these fire on its own".
    if ((Test-Member $ac 'heuristic') -and (Test-Member $ac.heuristic 'nameFragments')) {
        $frags = @($ac.heuristic.nameFragments)
        foreach ($outer in $frags) {
            foreach ($inner in $frags) {
                if ($outer -eq $inner) { continue }
                if ($outer.ToLowerInvariant().Contains($inner.ToLowerInvariant())) {
                    $errors.Add("anticheat.heuristic.nameFragments: '$outer' can never fire — it contains '$inner', which is also in the list and matches first (§S19(a)). Remove the longer one; keeping both makes a future removal of '$inner' look survivable when it is not.")
                }
            }
        }
    }


    foreach ($key in 'modules', 'drivers', 'blockedExecutables', 'blockedStoreIds') {
        if (-not (Test-Member $ac $key)) { $errors.Add("anticheat.$key is missing") }
    }

    # Non-empty is a ship requirement. blockedExecutables/blockedStoreIds may
    # legitimately be empty (they are per-title opt-outs); modules and drivers
    # may not — an empty module list means the guard matches nothing.
    foreach ($key in 'modules', 'drivers') {
        if ((Test-Member $ac $key) -and @($ac.$key).Count -eq 0) {
            $errors.Add("anticheat.$key is EMPTY — the guard would match nothing. This is a fail-closed fixture, not a ship state.")
        }
    }

    $validMatch = @('exact', 'prefix')
    foreach ($group in @('modules', 'drivers')) {
        if (-not (Test-Member $ac $group)) { continue }
        foreach ($entry in $ac.$group) {
            $label = if (Test-Member $entry 'family') { $entry.family } else { '<no family>' }
            foreach ($k in 'family', 'match', 'values') {
                if (-not (Test-Member $entry $k)) { $errors.Add("anticheat.$group entry '$label' missing '$k'") }
            }
            if ((Test-Member $entry 'match') -and $entry.match -notin $validMatch) {
                $errors.Add("anticheat.$group entry '$label' has match '$($entry.match)', expected one of: $($validMatch -join ', ')")
            }
            if ((Test-Member $entry 'values')) {
                if (@($entry.values).Count -eq 0) {
                    $errors.Add("anticheat.$group entry '$label' has no values")
                }
                foreach ($v in $entry.values) {
                    if ([string]::IsNullOrWhiteSpace($v)) {
                        $errors.Add("anticheat.$group entry '$label' has a blank value — would match everything")
                    }
                }
            }
        }
    }

    # --- No case-insensitive duplicate values (S5, imperative) ---------------
    # A schema cannot express this: uniqueItems is per-array and case-sensitive.
    # Two families claiming the same token is not merely untidy — it means one
    # family can be deleted while the blocklist still appears to cover its
    # signal, so the required-family floor below reads as satisfied.
    $seen = @{}
    foreach ($group in 'modules', 'drivers') {
        if (-not (Test-Member $ac $group)) { continue }
        foreach ($entry in $ac.$group) {
            if (-not (Test-Member $entry 'values')) { continue }
            $fam = if (Test-Member $entry 'family') { $entry.family } else { '<no family>' }
            foreach ($v in $entry.values) {
                if ([string]::IsNullOrWhiteSpace($v)) { continue }
                $k = "$group/$($v.ToLowerInvariant())"
                if ($seen.ContainsKey($k)) {
                    $errors.Add("anticheat.$group value '$v' ($fam) duplicates '$($seen[$k])' case-insensitively — matching is case-insensitive, so one of these is dead weight hiding a gap")
                }
                else { $seen[$k] = "$v ($fam)" }
            }
        }
    }

    # --- No prefix short enough to shadow a system module (S5, imperative) ---
    # The schema enforces minLength 3, which is not the same question. A short
    # prefix does not fail open — it refuses EVERY title, which trains users to
    # distrust the gate and is how an override request gets born. Checked
    # against a fixed list rather than a data file so the check cannot be
    # weakened by editing rules data.
    $systemModules = @(
        'ntdll.dll', 'kernel32.dll', 'kernelbase.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll',
        'msvcrt.dll', 'ole32.dll', 'oleaut32.dll', 'shell32.dll', 'ws2_32.dll', 'combase.dll',
        'rpcrt4.dll', 'sechost.dll', 'bcrypt.dll', 'crypt32.dll', 'setupapi.dll', 'version.dll',
        'd3d11.dll', 'd3d12.dll', 'dxgi.dll', 'opengl32.dll', 'vulkan-1.dll', 'nvapi64.dll',
        'ntoskrnl.exe', 'win32k.sys', 'ndis.sys', 'tcpip.sys', 'http.sys', 'fltmgr.sys'
    )
    $minPrefix = 4
    foreach ($group in 'modules', 'drivers') {
        if (-not (Test-Member $ac $group)) { continue }
        foreach ($entry in $ac.$group) {
            if (-not (Test-Member $entry 'match') -or $entry.match -ne 'prefix') { continue }
            if (-not (Test-Member $entry 'values')) { continue }
            $fam = if (Test-Member $entry 'family') { $entry.family } else { '<no family>' }
            foreach ($v in $entry.values) {
                if ([string]::IsNullOrWhiteSpace($v)) { continue }
                if ($v.Length -lt $minPrefix) {
                    $errors.Add("anticheat.$group prefix '$v' ($fam) is shorter than $minPrefix characters — too broad to be a signal")
                    continue
                }
                $shadowed = @($systemModules | Where-Object { $_.StartsWith($v, [StringComparison]::OrdinalIgnoreCase) })
                if ($shadowed.Count -gt 0) {
                    $errors.Add("anticheat.$group prefix '$v' ($fam) shadows system module(s): $($shadowed -join ', ') — this would refuse every title")
                }
            }
        }
    }

    # --- Required families, IN THE RIGHT GROUP (S5, imperative) --------------
    # Family AND group, not family alone. Riot Vanguard is the machine-wide
    # driver gate (19_SAFETY §Pre-injection checks item 2): moving it into
    # 'modules' would satisfy a group-agnostic check while the driver scan lost
    # its only entry — the blocklist would look complete and the machine-wide
    # refusal would silently stop working.
    $required = @(
        @{ family = 'Easy Anti-Cheat'; group = 'modules' },
        @{ family = 'BattlEye'; group = 'modules' },
        @{ family = 'Riot Vanguard'; group = 'drivers' }
    )
    foreach ($req in $required) {
        $present = @()
        if (Test-Member $ac $req.group) { $present = @($ac.($req.group) | ForEach-Object { $_.family }) }
        if ($req.family -notin $present) {
            $errors.Add("anticheat.$($req.group) no longer covers '$($req.family)' — removing a family, or moving it to another group, requires an explicit justification in the PR body")
        }
    }
}

# --- Doc/data drift: 19_SAFETY §Blocklist seed vs the data ------------------
if ((Test-Path $DocPath) -and (Test-Member $rules 'anticheat')) {
    $ac = $rules.anticheat
    $familyGroups = 'modules', 'drivers', 'directories', 'services', 'files'

    # (family, group) pairs the DATA carries.
    $dataPairs = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($g in $familyGroups) {
        if (-not (Test-Member $ac $g)) { continue }
        foreach ($e in $ac.$g) {
            if (Test-Member $e 'family') { [void]$dataPairs.Add("$($e.family)|$g") }
        }
    }

    # (family, group) pairs the TABLE claims. Rows whose group cell is an em
    # dash are the acknowledged "no data yet" entries and are skipped.
    $docPairs = [System.Collections.Generic.HashSet[string]]::new()
    $inTable = $false
    foreach ($line in (Get-Content $DocPath)) {
        if ($line -match '^###\s+Blocklist seed') { $inTable = $true; continue }
        if ($inTable -and $line -match '^###\s') { break }
        if (-not $inTable -or $line -notmatch '^\s*\|') { continue }

        $cells = @($line.Trim().Trim('|') -split '\|' | ForEach-Object { $_.Trim() })
        if ($cells.Count -lt 2) { continue }
        $family = $cells[0].Trim('*', ' ', '`')
        $group = $cells[1].Trim('*', ' ', '`')
        if ($family -in @('Family', '---', '') -or $group -match '^-+$') { continue }
        if ($group -eq [char]0x2014 -or $group -eq '-') { continue }    # em dash: no data yet
        if ($group -notin $familyGroups) { continue }
        [void]$docPairs.Add("$family|$group")
    }

    if ($docPairs.Count -eq 0) {
        $errors.Add("could not parse any rows from the Blocklist seed table in $DocPath — the drift check is inert, which must not read as agreement")
    }
    foreach ($p in $docPairs) {
        if (-not $dataPairs.Contains($p)) {
            $errors.Add("19_SAFETY Blocklist seed lists '$($p -replace '\|', "' in group '")', but the data does not")
        }
    }
    foreach ($p in $dataPairs) {
        if (-not $docPairs.Contains($p)) {
            $errors.Add("data carries '$($p -replace '\|', "' in group '")', but 19_SAFETY Blocklist seed does not list it — an unlisted family is one nobody reviews")
        }
    }
}

# --- Fixture COVERAGE (not evaluation) --------------------------------------
# Every engine and platform id must have a directory in the corpus, and every
# corpus directory must correspond to a live rule id.
#
# THIS SCRIPT DELIBERATELY DOES NOT EVALUATE A RULE. Re-implementing glob, PE and
# strings matching here would be a SECOND EVALUATOR — the same defect shape as a
# second blocklist matcher, and it would drift from RuleEvaluator the moment
# either gained a signal type. The evaluation is RuleFixtureCorpusTests, which
# runs the real evaluator through the real probe, under `build.ps1 check`.
#
# 05_DETECTION and 13_CI_CD both used to claim this script ran rules against
# fixture trees. It never did, and the trees did not exist.
$fixtureRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'tests/fixtures/rules'
if (-not (Test-Path $fixtureRoot)) {
    Write-Host "RULES VALIDATION FAILED: no fixture corpus at $fixtureRoot" -ForegroundColor Red
    Write-Host '  Refusing rather than skipping: a coverage check with nothing to cover is' -ForegroundColor Red
    Write-Host '  a check that passes without looking.' -ForegroundColor Red
    exit 1
}

$fixtureDirs = @(Get-ChildItem $fixtureRoot -Directory -Recurse |
        Where-Object { Test-Path (Join-Path $_.FullName 'expected.json') })
if ($fixtureDirs.Count -eq 0) {
    Write-Host 'RULES VALIDATION FAILED: the fixture corpus contains no fixtures.' -ForegroundColor Red
    exit 1
}
$fixtureNames = @($fixtureDirs | ForEach-Object { $_.Name })

foreach ($section in 'engines', 'platforms') {
    if (-not (Test-Member $rules $section)) { continue }
    foreach ($id in @($rules.$section | ForEach-Object { $_.id })) {
        if ($id -notin $fixtureNames) {
            $errors.Add("$section id '$id' has no fixture under tests/fixtures/rules — a rule nobody evaluates is a rule nobody checks")
        }
    }
}

# NO FIXTURE FILE MAY BE GITIGNORED.
#
# Found the hard way: `tests/fixtures/rules/engines/source/bin/engine.dll` mirrors
# a real Source layout, and `.gitignore`'s `[Bb]in/` swallowed it. The file
# existed on the machine that wrote it, so the fixture passed locally and failed
# on a fresh clone with "engine: null" — and `git add -A` said nothing, because an
# ignored file is not an untracked one. The corpus is DATA; a fixture git refuses
# to carry is a test that only works where it was written.
if (Get-Command git -ErrorAction SilentlyContinue) {
    # ls-files rather than check-ignore --stdin: no round-trip, so no path
    # quoting to undo. (check-ignore echoed the path back with git's own `\r`
    # escape for the CR the pipe added, which then rendered as a literal `/r`.)
    # This lists exactly the files git is currently refusing to carry.
    $repoRoot = Split-Path $PSScriptRoot -Parent
    $ignored = @(& git -C $repoRoot ls-files --others --ignored --exclude-standard -- 'tests/fixtures/rules' 2>$null)
    foreach ($rel in $ignored) {
        if ([string]::IsNullOrWhiteSpace($rel)) { continue }
        $errors.Add("fixture '$("$rel".Trim())' is gitignored — it will not survive a fresh clone, and the test that reads it will pass only on the machine that wrote it")
    }
}
else {
    $errors.Add('git not on PATH — cannot check that fixtures are committable, and an unrun check is not a passing one')
}

# The other direction: a fixture for a rule that no longer exists is dead weight
# that still reports green.
$liveIds = @()
foreach ($section in 'engines', 'platforms', 'capabilities') {
    if (Test-Member $rules $section) { $liveIds += @($rules.$section | ForEach-Object { $_.id }) }
}
# Directories that are deliberately not rule ids: the ordering case and the two
# canaries are named for what they PROVE, not for a rule.
$nonRuleFixtures = @('unity_markers_with_ue_structure', 'no_engine', 'every_engine_marker',
    'dlss_stack', 'fsr_globs')
foreach ($name in $fixtureNames) {
    if ($name -notin $liveIds -and $name -notin $nonRuleFixtures) {
        $errors.Add("fixture '$name' matches no live rule id and is not a named special case — stale fixtures report green forever")
    }
}

# --- Parser capacity: the caps a JSON Schema cannot express -----------------
# Two of the guard's limits are invisible to the schema:
#
#   * kMaxFamilies bounds the SUM of five separate arrays. `maxItems` is
#     per-array, so the schema can only stop one array breaching the total alone.
#   * kRulesTokenBudget bounds the WHOLE FILE, including engines/platforms/
#     capabilities the guard never reads — jsmn tokenises everything before it
#     locates `anticheat`.
#
# Both are checked here as well as in ctest fl_rules_budget, because a rules-only
# PR routes through rules-publish.yml, which never builds native. Neither cap is
# a truncation: exceeding one is ParseResult::kTooLarge for the whole file, which
# the guard turns into "refuse every title on this machine".
#
# THE THRESHOLDS ARE READ OUT OF THE HEADER, never restated. A number copied into
# this script is the third place it can drift, and drift between the schema and
# the parser is the entire defect this check exists to prevent. If the header or
# a constant cannot be read, that FAILS — a capacity check that silently stops
# checking is worse than no check, because it reads as coverage.
$headerPath = Join-Path (Split-Path $PSScriptRoot -Parent) 'src/native/FrameLedger.Injector/include/fl_ac_rules.h'
if (-not (Test-Path $headerPath)) {
    Write-Host "RULES VALIDATION FAILED: cannot find fl_ac_rules.h at $headerPath" -ForegroundColor Red
    Write-Host '  The parser capacities are read from that header. If it moved, this check' -ForegroundColor Red
    Write-Host '  must move with it — refusing rather than silently checking nothing.' -ForegroundColor Red
    exit 1
}
$headerText = Get-Content $headerPath -Raw

function Get-HeaderConstant([string]$Name) {
    # Matches `inline constexpr std::size_t kName = <int>;` and the derived
    # `= kOther / 2;` form, so kRulesTokenBudget is resolved rather than skipped.
    if ($headerText -match "(?m)^\s*inline\s+constexpr\s+std::size_t\s+$Name\s*=\s*([^;]+);") {
        $expr = $Matches[1].Trim()
        if ($expr -match '^\d+$') { return [int]$expr }
        if ($expr -match '^(k\w+)\s*/\s*(\d+)$') {
            $base = Get-HeaderConstant $Matches[1]
            if ($null -eq $base) { return $null }
            return [int]($base / [int]$Matches[2])
        }
        if ($expr -match '^1u?\s*<<\s*(\d+)$') { return [int][math]::Pow(2, [int]$Matches[1]) }
    }
    return $null
}

$maxFamilies = Get-HeaderConstant 'kMaxFamilies'
$tokenBudget = Get-HeaderConstant 'kRulesTokenBudget'
$maxFragments = Get-HeaderConstant 'kMaxNameFragments'
foreach ($c in @(@{ n = 'kMaxFamilies'; v = $maxFamilies }, @{ n = 'kRulesTokenBudget'; v = $tokenBudget },
        @{ n = 'kMaxNameFragments'; v = $maxFragments })) {
    if ($null -eq $c.v) {
        Write-Host "RULES VALIDATION FAILED: could not read $($c.n) from fl_ac_rules.h" -ForegroundColor Red
        Write-Host '  Either the constant was renamed or its form changed. Refusing rather than' -ForegroundColor Red
        Write-Host '  skipping: an unread threshold is a check that passes without looking.' -ForegroundColor Red
        exit 1
    }
}

if (Test-Member $rules 'anticheat') {
    $totalFamilies = 0
    foreach ($g in 'modules', 'drivers', 'directories', 'services', 'files') {
        if (Test-Member $rules.anticheat $g) { $totalFamilies += @($rules.anticheat.$g).Count }
    }
    # §S21. The compiled-in floor is GENERATED from this file, so it holds exactly
    # these families and ParseRules seeds them before reading a byte. A file family
    # identical to a floor entry is then deduplicated — but a file that has DRIFTED
    # from the shipped floor (an updated rules push against an older binary)
    # duplicates none of it, and both copies have to fit.
    #
    # So the bound is 2x, not 1x. Getting this wrong does not fail here; it fails
    # on every client as kTooLarge, which the guard turns into refuse-every-title.
    if (($totalFamilies * 2) -gt $maxFamilies) {
        $errors.Add("$totalFamilies anticheat families needs $($totalFamilies * 2) slots in the worst case (the generated floor plus a fully-drifted file) and fl::guard::kMaxFamilies is $maxFamilies — ParseRules returns kTooLarge and the guard refuses EVERY title")
    }
    $fragmentCount = 0
    if ((Test-Member $rules.anticheat 'heuristic') -and (Test-Member $rules.anticheat.heuristic 'nameFragments')) {
        $fragmentCount = @($rules.anticheat.heuristic.nameFragments).Count
    }
    if (($fragmentCount * 2) -gt $maxFragments) {
        $errors.Add("$fragmentCount nameFragments needs $($fragmentCount * 2) slots in the worst case and fl::guard::kMaxNameFragments is $maxFragments — same failure mode as the family cap")
    }
}

# jsmn's counting rules: object, array, each string (key and value alike) and
# each primitive cost one token.
function Measure-JsmnTokens($Element) {
    switch ($Element.ValueKind) {
        'Object' {
            $n = 1
            foreach ($p in $Element.EnumerateObject()) { $n += 1 + (Measure-JsmnTokens $p.Value) }
            return $n
        }
        'Array' {
            $n = 1
            foreach ($i in $Element.EnumerateArray()) { $n += Measure-JsmnTokens $i }
            return $n
        }
        default { return 1 }
    }
}

$tokenCount = $null
try {
    $doc = [System.Text.Json.JsonDocument]::Parse($rulesRaw)
    $tokenCount = Measure-JsmnTokens $doc.RootElement
}
catch {
    Write-Host "RULES VALIDATION FAILED: could not count tokens — $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
if ($tokenCount -gt $tokenBudget) {
    $errors.Add("$tokenCount jsmn tokens exceeds the guard's budget of $tokenBudget — every engine/platform/capability rule counts, because jsmn tokenises the whole file before it locates the anticheat block")
}

# --- Report -----------------------------------------------------------------
if ($errors.Count -gt 0) {
    Write-Host 'RULES VALIDATION FAILED' -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

$moduleCount = @($rules.anticheat.modules).Count
$driverCount = @($rules.anticheat.drivers).Count
Write-Host "rules OK — schema v$($rules.schemaVersion), rules $($rules.rulesVersion), $moduleCount anticheat module families, $driverCount driver families" -ForegroundColor Green
# Printed on every run, not only on failure: the hazard is a capacity nobody
# looks at until it is already breached.
# The families figure is against the FILE's budget, not kMaxFamilies: printing
# the raw capacity would overstate the headroom by the size of the §S21 floor,
# and this line exists to be read rather than to look reassuring.
# Printed as the WORST case (floor + a fully-drifted file), not as the raw count.
# The raw count against kMaxFamilies would overstate the headroom by exactly the
# size of the floor, and this line exists to be read rather than to reassure.
Write-Host "  capacity — $($totalFamilies * 2)/$maxFamilies family slots worst case, $($fragmentCount * 2)/$maxFragments fragment slots, $tokenCount/$tokenBudget parse tokens" -ForegroundColor DarkGray
Write-Host "  fixtures — $($fixtureDirs.Count) corpus directories cover every engine and platform id" -ForegroundColor DarkGray
exit 0
