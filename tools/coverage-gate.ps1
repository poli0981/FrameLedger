#Requires -Version 7.0
<#
.SYNOPSIS
    Enforces the coverage thresholds docs/14_TESTING.md states as PR-failing.

.DESCRIPTION
    `dotnet test --collect:"XPlat Code Coverage"` has been producing
    coverage.cobertura.xml since the repository was scaffolded and NOTHING HAS
    EVER READ IT. 14_TESTING says ">= 80% on Domain + Application" and "Domain
    metric calculators >= 95% or the PR fails"; neither was enforced anywhere.

    THE GATE IS SELF-ARMING. Domain and Application are empty today (guard logic
    went to C++ under 20_OPEN_QUESTIONS §S13(a)), so there is nothing to measure
    and a threshold would be decorative. Rather than skip - a skipped gate reads
    as a passed one, which is the defect shape this project keeps finding - the
    script checks whether the project has any .cs file. No source => report the
    emptiness explicitly and pass. First .cs file => the threshold applies
    immediately, with no second commit to remember.

    That ordering matters: adding a threshold AFTER the code exists is how the
    number gets negotiated down to whatever the code already scores.

.PARAMETER MinLineRate
    Assembly floor from 14_TESTING (0.80).

.PARAMETER MinCalculatorLineRate
    Floor for Domain metric calculators (0.95).
#>
[CmdletBinding()]
param(
    [double]$MinLineRate = 0.80,
    [double]$MinCalculatorLineRate = 0.95,

    # Overridable so the red-green canary can point at a fixture.
    [string]$Root = (Split-Path $PSScriptRoot -Parent)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$errors = [System.Collections.Generic.List[string]]::new()
$notes = [System.Collections.Generic.List[string]]::new()

# Assemblies 14_TESTING names, and where their source lives.
$targets = @(
    @{ Assembly = 'FrameLedger.Domain'; Source = 'src/FrameLedger.Domain' },
    @{ Assembly = 'FrameLedger.Application'; Source = 'src/FrameLedger.Application' }
)

# Only the NEWEST report per test project. `dotnet test` writes each run into a
# fresh TestResults/<guid>/ and never prunes, so a repository that has been
# built a few times accumulates dozens. Reading them all and taking the best
# rate would mean a project that once scored 95% and now scores 10% still
# passes — a fail-open built out of tidiness rather than logic.
$allReports = @(Get-ChildItem -Path (Join-Path $Root 'tests') -Filter 'coverage.cobertura.xml' -Recurse `
        -ErrorAction SilentlyContinue)
$reports = @(
    $allReports | Group-Object {
        # tests/<Project>/TestResults/<guid>/coverage.cobertura.xml -> <Project>
        Split-Path (Split-Path (Split-Path $_.FullName -Parent) -Parent) -Parent
    } | ForEach-Object { $_.Group | Sort-Object LastWriteTime -Descending | Select-Object -First 1 }
)
if ($allReports.Count -gt $reports.Count) {
    $notes.Add(("ignored {0} stale report(s); using the newest per test project" -f ($allReports.Count - $reports.Count)))
}

# An absent report is a FAILURE, not a skip. "The tests did not run" and "the
# tests ran and everything is covered" must never produce the same outcome.
if ($reports.Count -eq 0) {
    Write-Host 'COVERAGE GATE FAILED: no coverage.cobertura.xml found under tests/.' -ForegroundColor Red
    Write-Host '  Run: dotnet test --collect:"XPlat Code Coverage"' -ForegroundColor Red
    Write-Host '  Refusing rather than reporting success: a missing report is not evidence of coverage.' -ForegroundColor Red
    exit 1
}

# Collect every <package> across every report. Multiple test projects each emit
# one, so the same assembly can appear more than once; take the best rate.
$rates = @{}
$lineCounts = @{}
$classRates = @{}
foreach ($r in $reports) {
    try { $xml = [xml](Get-Content $r.FullName -Raw) }
    catch {
        $errors.Add("unparseable coverage report $($r.FullName): $($_.Exception.Message)")
        continue
    }
    # Navigate defensively. An empty <packages/> or <classes/> is normal for a
    # project with no code, and under Set-StrictMode a missing property throws —
    # which would abort the gate rather than report on it. SelectNodes returns
    # an empty set instead of erroring.
    foreach ($pkg in $xml.SelectNodes('//package')) {
        $name = [string]$pkg.GetAttribute('name')
        if ([string]::IsNullOrWhiteSpace($name)) { continue }
        $rate = [double]$pkg.GetAttribute('line-rate')
        if (-not $rates.ContainsKey($name) -or $rate -gt $rates[$name]) { $rates[$name] = $rate }

        # Coverlet reports an EMPTY assembly as line-rate=1 — 100% of nothing.
        # Taken at face value that is a vacuous pass, and it is what this gate
        # printed for Domain and Application on its first run. Count the actual
        # <line> elements so "fully covered" and "nothing to cover" stay
        # distinguishable; packages carry no lines-valid attribute.
        $lines = $pkg.SelectNodes('.//line').Count
        if (-not $lineCounts.ContainsKey($name) -or $lines -gt $lineCounts[$name]) { $lineCounts[$name] = $lines }

        foreach ($cls in $pkg.SelectNodes('.//class')) {
            $cn = [string]$cls.GetAttribute('name')
            if ([string]::IsNullOrWhiteSpace($cn)) { continue }
            $cr = [double]$cls.GetAttribute('line-rate')
            if (-not $classRates.ContainsKey($cn) -or $cr -gt $classRates[$cn]) { $classRates[$cn] = $cr }
        }
    }
}

foreach ($t in $targets) {
    $srcDir = Join-Path $Root $t.Source
    $hasSource = @(Get-ChildItem -Path $srcDir -Filter '*.cs' -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '\\(bin|obj)\\' }).Count -gt 0

    $measured = $rates.ContainsKey($t.Assembly) -and $lineCounts[$t.Assembly] -gt 0

    if ($measured) {
        $rate = $rates[$t.Assembly]
        if ($rate -lt $MinLineRate) {
            $errors.Add(("{0} line coverage {1:P1} over {2} line(s) is below the {3:P0} floor (docs/14_TESTING.md)" -f `
                        $t.Assembly, $rate, $lineCounts[$t.Assembly], $MinLineRate))
        }
        else {
            $notes.Add(("{0,-28} {1,7:P1} over {2} line(s)  (floor {3:P0})" -f `
                        $t.Assembly, $rate, $lineCounts[$t.Assembly], $MinLineRate))
        }
    }
    elseif ($hasSource) {
        # Source exists but nothing coverable was measured: the assembly is
        # absent from the report, or present with zero lines. Either way the
        # threshold is UNVERIFIED, which is not the same as met.
        $errors.Add("$($t.Assembly) has source files but no coverable lines were measured — the threshold is unverified, not satisfied")
    }
    else {
        $notes.Add(("{0,-28} no source yet — gate arms on the first .cs file" -f $t.Assembly))
    }
}

# Domain metric calculators, 95%. They do not exist yet; when they do, this
# matches on namespace so it needs no maintenance to start biting.
$calcPattern = '^FrameLedger\.Domain\.(Metrics|Calculators)\.'
$calcs = @($classRates.Keys | Where-Object { $_ -match $calcPattern })
if ($calcs.Count -eq 0) {
    $notes.Add(("{0,-28} none found (namespace {1})" -f 'metric calculators', 'FrameLedger.Domain.Metrics.*'))
}
else {
    foreach ($c in $calcs) {
        if ($classRates[$c] -lt $MinCalculatorLineRate) {
            $errors.Add(("metric calculator {0} at {1:P1} is below the {2:P0} floor — 14_TESTING makes this PR-failing" -f `
                        $c, $classRates[$c], $MinCalculatorLineRate))
        }
    }
    $notes.Add(("{0,-28} {1} class(es) at or above {2:P0}" -f 'metric calculators', $calcs.Count, $MinCalculatorLineRate))
}

if ($errors.Count -gt 0) {
    Write-Host 'COVERAGE GATE FAILED' -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "coverage OK — $($reports.Count) report(s)" -ForegroundColor Green
$notes | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
exit 0
