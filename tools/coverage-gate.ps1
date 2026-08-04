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
# one, so the same assembly appears more than once — three times here, for
# FrameLedger.Domain, at 74.4%, 68.7% and 89.1% over the identical 422 lines.
#
# UNION THE LINES, do not pick a report. The previous code maximised the rate and
# the line count INDEPENDENTLY, so it could pair 100%-of-5-lines from one report
# with 500-lines from another and report a figure no run produced — and the
# empty-assembly defence below is exactly what that recombination defeats.
#
# Picking the single report with the most lines is no better: with equal line
# counts the choice is arbitrary, and it throws away real coverage, because a
# line exercised only by Application.Tests is still exercised. Measured: that
# approach reported 68.7% for an assembly whose union is higher.
#
# So merge at LINE level — a line is covered if any report saw it hit — and
# derive the rate from the merged set. The pair is then self-consistent by
# construction rather than by a rule about which report to trust.
$lineHits = @{}       # assembly -> @{ "file:line" = maxHits }
$classHits = @{}      # class    -> @{ "file:line" = maxHits }
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
        if (-not $lineHits.ContainsKey($name)) { $lineHits[$name] = @{} }

        foreach ($cls in $pkg.SelectNodes('.//class')) {
            $cn = [string]$cls.GetAttribute('name')
            $file = [string]$cls.GetAttribute('filename')
            $trackClass = -not [string]::IsNullOrWhiteSpace($cn)
            if ($trackClass -and -not $classHits.ContainsKey($cn)) { $classHits[$cn] = @{} }

            # Key on the CLASS NAME, not the filename. Measured: the three
            # reports over FrameLedger.Domain spell the same file two ways —
            # 'FrameLedger.Domain/Detection/CapabilityRule.cs' in two of them and
            # 'Detection/CapabilityRule.cs' in the third — so a filename-keyed
            # union counted 211 real lines as 422 and diluted the rate with
            # phantom uncovered lines. Class names are assembly-qualified and
            # identical across all three.
            $unit = if ($trackClass) { $cn } else { $file }

            # `.//line` from <class> also matches the per-method <lines>, so the
            # same line appears twice inside ONE report. That is why the old
            # `SelectNodes('.//line').Count` printed 422 for an assembly with 211
            # lines. Keying deduplicates it.
            foreach ($ln in $cls.SelectNodes('.//line')) {
                $id = "$unit`:$($ln.GetAttribute('number'))"
                $h = [int]$ln.GetAttribute('hits')
                if (-not $lineHits[$name].ContainsKey($id) -or $h -gt $lineHits[$name][$id]) {
                    $lineHits[$name][$id] = $h
                }
                if ($trackClass) {
                    if (-not $classHits[$cn].ContainsKey($id) -or $h -gt $classHits[$cn][$id]) {
                        $classHits[$cn][$id] = $h
                    }
                }
            }
        }
    }
}

# Coverlet reports an EMPTY assembly as line-rate=1 — 100% of nothing. Taken at
# face value that is a vacuous pass, and it is what this gate printed for Domain
# and Application on its first run. Deriving the rate from a counted line set
# keeps "fully covered" and "nothing to cover" distinguishable: an empty set
# yields 0 lines, and the caller treats 0 lines as not measured.
foreach ($name in $lineHits.Keys) {
    $total = $lineHits[$name].Count
    $lineCounts[$name] = $total
    $covered = @($lineHits[$name].Values | Where-Object { $_ -gt 0 }).Count
    $rates[$name] = if ($total -gt 0) { $covered / $total } else { 0 }
}
foreach ($cn in $classHits.Keys) {
    $total = $classHits[$cn].Count
    $covered = @($classHits[$cn].Values | Where-Object { $_ -gt 0 }).Count
    $classRates[$cn] = if ($total -gt 0) { $covered / $total } else { 0 }
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
