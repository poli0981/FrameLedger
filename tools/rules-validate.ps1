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

    NOTE: this is structural validation, not a full JSON Schema. Writing the
    schema to validator grade is docs/20_OPEN_QUESTIONS.md §S5, and until that
    lands some signal shapes described in docs/19_SAFETY are accepted without
    being checked in depth.
#>
[CmdletBinding()]
param(
    [string]$Path = (Join-Path (Split-Path $PSScriptRoot -Parent) 'rules/detection-rules.json')
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

    # Known families that must never silently disappear. Removing one is a
    # safety change requiring the same review as a security fix.
    $required = @('Easy Anti-Cheat', 'BattlEye', 'Riot Vanguard')
    $present = @()
    foreach ($group in 'modules', 'drivers') {
        if (Test-Member $ac $group) { $present += @($ac.$group | ForEach-Object { $_.family }) }
    }
    foreach ($fam in $required) {
        if ($fam -notin $present) {
            $errors.Add("anticheat blocklist no longer covers '$fam' — removing a family requires an explicit justification in the PR body")
        }
    }
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
exit 0
