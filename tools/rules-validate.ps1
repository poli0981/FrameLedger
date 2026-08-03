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

# --- The anticheat block: fail closed ---------------------------------------
if (Test-Member $rules 'anticheat') {
    $ac = $rules.anticheat

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
foreach ($c in @(@{ n = 'kMaxFamilies'; v = $maxFamilies }, @{ n = 'kRulesTokenBudget'; v = $tokenBudget })) {
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
    if ($totalFamilies -gt $maxFamilies) {
        $errors.Add("$totalFamilies anticheat families across all groups exceeds fl::guard::kMaxFamilies ($maxFamilies) — ParseRules returns kTooLarge and the guard refuses EVERY title")
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
Write-Host "  capacity — $totalFamilies/$maxFamilies families, $tokenCount/$tokenBudget parse tokens" -ForegroundColor DarkGray
Write-Host "  fixtures — $($fixtureDirs.Count) corpus directories cover every engine and platform id" -ForegroundColor DarkGray
exit 0
