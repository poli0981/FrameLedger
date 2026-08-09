#Requires -Version 7.0
<#
.SYNOPSIS
    Every vendor symbol FrameLedger.Overlay resolves by name must exist, in the
    module the Overlay takes it from, in measured data.

.DESCRIPTION
    docs/17_HOOK_ENGINE.md calls a wrong symbol name degrading silently to
    `unknown` "the highest false-confidence risk in the spike". The failure has
    no symptom: a misspelt name resolves to nothing, the Overlay installs
    nothing, and the record it writes is byte-identical to an honest writer's on
    a title with no upscaler. Green everywhere, and wrong.

    THIS IS PREVENTION, AND IT FIXED NOTHING. No drift exists today — every
    symbol in the inventory was resolved against all 34 measured modules before
    this script was written. Saying so matters: a gate whose write-up implies it
    caught something is a gate whose value nobody can audit later.

    WHAT THE ORACLE IS. docs/vendor-exports.json, produced by
    tools/vendor-exports.ps1 running dumpbin over DLLs installed titles actually
    ship. It is MEASURED DATA from one machine, never a vendor SDK and never a
    transcription, which is the whole reason it can be trusted as an oracle here.

    THE LOOKUP IS MODULE-SCOPED, and that is not a detail. Asking "does anything
    export this name" would pass a hook that takes NVSDK_NGX_D3D12_EvaluateFeature
    from any of the SEVEN modules exporting it, and 03_METRICS derives F_app by
    SUBTRACTING that count — so a double-counted evaluation lands on fg_factor,
    the number CLAUDE.md rule 6 exists to protect.

    TWO INDEPENDENT PASSES, because a header-only check has a blind spot. Pass A
    checks the inventory rows against the oracle. Pass B sweeps the Overlay's
    own sources for vendor-shaped string literals that are NOT in the inventory —
    a second resolver written somewhere else would otherwise be invisible to this
    gate while being exactly what it exists to prevent. Same argument
    tools/chokepoint-check.ps1 makes about FL_GUARD_TESTABLE.

.PARAMETER RepoRoot
    Repository root. Defaults to this script's parent directory.

.PARAMETER SelfTest
    Runs the decision table in BOTH directions and exits non-zero if any case
    comes out wrong, so the gate's ability to go red is proven on every build
    rather than assumed. build.ps1 runs this AND the live pass, following
    tools/changelog-check.ps1: a gate wired self-test-only never reads the
    repository, which is the defect it exists to prevent.
#>
[CmdletBinding(DefaultParameterSetName = 'Check')]
param(
    [Parameter(ParameterSetName = 'Check')]
    [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent),

    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- The decision logic, pure so -SelfTest can drive it without a repository --

# Parse FL_HOOK_INVENTORY rows out of the header text.
#
# The row shape is X(L"module.dll", "symbol", family). Requiring the L"..." and
# the quoted symbol is what keeps the macro's own documentation comment —
# "X(module, symbol, family)", unquoted — from parsing as a row.
function Get-InventoryRow([string]$HeaderText) {
    $rows = @()
    foreach ($m in [regex]::Matches($HeaderText, 'X\(\s*L"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\)')) {
        $rows += [pscustomobject]@{
            Module = $m.Groups[1].Value
            Symbol = $m.Groups[2].Value
            Family = $m.Groups[3].Value
        }
    }
    return , $rows
}

# Does THIS module export THIS symbol, in the measured data?
function Test-ModuleExports($Modules, [string]$Module, [string]$Symbol) {
    if (-not $Modules.PSObject.Properties.Name.Contains($Module)) { return $false }
    $entry = $Modules.$Module
    if (-not $entry.PSObject.Properties.Name.Contains('exports')) { return $false }
    return @($entry.exports) -contains $Symbol
}

# Vendor-shaped literals the Overlay must not resolve outside the inventory.
# Deliberately broad on the vendor prefixes and narrow on where it looks.
function Get-StrayVendorLiteral([string]$SourceText) {
    $stray = @()
    foreach ($m in [regex]::Matches($SourceText, '"((?:sl[A-Z]|NVSDK_NGX_|xess[A-Z]|ffx[A-Z])[A-Za-z0-9_]*)"')) {
        $stray += $m.Groups[1].Value
    }
    return , $stray
}

# --- Self-test ---------------------------------------------------------------
if ($SelfTest) {
    $oracle = [pscustomobject]@{
        'sl.interposer.dll' = [pscustomobject]@{ exports = @('slEvaluateFeature', 'slInit') }
        'sl.common.dll'     = [pscustomobject]@{ exports = @('NVSDK_NGX_Parameter_SetUI') }
        'empty.dll'         = [pscustomobject]@{ exports = @() }
    }

    $cases = @(
        @{ Name = 'a row whose module really exports the symbol passes'
           Got  = (Test-ModuleExports $oracle 'sl.interposer.dll' 'slEvaluateFeature'); Want = $true }
        @{ Name = 'a MISSPELT symbol fails'
           Got  = (Test-ModuleExports $oracle 'sl.interposer.dll' 'slEvaluateFeatureX'); Want = $false }
        @{ Name = 'the RIGHT symbol from the WRONG module fails - this is the module-scoping half'
           Got  = (Test-ModuleExports $oracle 'sl.common.dll' 'slEvaluateFeature'); Want = $false }
        @{ Name = 'a module absent from the oracle fails rather than being skipped'
           Got  = (Test-ModuleExports $oracle 'not-measured.dll' 'slEvaluateFeature'); Want = $false }
        @{ Name = 'a module with an empty export list fails'
           Got  = (Test-ModuleExports $oracle 'empty.dll' 'slEvaluateFeature'); Want = $false }
        @{ Name = 'a real row parses'
           Got  = ((Get-InventoryRow '#define FL_HOOK_INVENTORY(X) X(L"a.dll", "sym", fl::FAM)').Count -eq 1); Want = $true }
        @{ Name = 'the macro comment "X(module, symbol, family)" does NOT parse as a row'
           Got  = ((Get-InventoryRow '// X(module, symbol, family) -- explanatory comment').Count -eq 0); Want = $true }
        @{ Name = 'a stray vendor literal in source is found'
           Got  = ((Get-StrayVendorLiteral 'GetProcAddress(h, "slSomethingElse");') -contains 'slSomethingElse'); Want = $true }
        @{ Name = 'an ordinary string is not mistaken for one'
           Got  = ((Get-StrayVendorLiteral 'printf("slept for a while");').Count -eq 0); Want = $true }
    )

    $bad = @()
    foreach ($c in $cases) {
        if ($c.Got -ne $c.Want) { $bad += "$($c.Name): got $($c.Got), wanted $($c.Want)" }
    }
    if ($bad.Count -gt 0) {
        Write-Host 'HOOKINVENTORY SELF-TEST FAILED' -ForegroundColor Red
        $bad | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
        exit 1
    }
    Write-Host "hookinventory self-test OK - $($cases.Count) cases, both directions" -ForegroundColor Green
    exit 0
}

# --- Live pass ---------------------------------------------------------------
$violations = [System.Collections.Generic.List[string]]::new()

$headerPath = Join-Path $RepoRoot 'src/native/FrameLedger.Overlay/include/fl_hook_inventory.h'
$oraclePath = Join-Path $RepoRoot 'docs/vendor-exports.json'

if (-not (Test-Path $headerPath)) {
    Write-Host "HOOKINVENTORY CHECK FAILED" -ForegroundColor Red
    Write-Host "  - the inventory header is missing: $headerPath" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $oraclePath)) {
    Write-Host "HOOKINVENTORY CHECK FAILED" -ForegroundColor Red
    Write-Host "  - the oracle is missing: $oraclePath" -ForegroundColor Red
    exit 1
}

try {
    $json = Get-Content $oraclePath -Raw | ConvertFrom-Json
}
catch {
    Write-Host "HOOKINVENTORY CHECK FAILED" -ForegroundColor Red
    Write-Host "  - docs/vendor-exports.json did not parse: $_" -ForegroundColor Red
    exit 1
}

if (-not $json.PSObject.Properties.Name.Contains('modules')) {
    $violations.Add('docs/vendor-exports.json has no "modules" object, so no symbol can be checked against it')
}
$modules = if ($violations.Count -eq 0) { $json.modules } else { $null }
$moduleCount = if ($null -ne $modules) { @($modules.PSObject.Properties).Count } else { 0 }
if ($moduleCount -eq 0) {
    $violations.Add('docs/vendor-exports.json lists no modules - an "absent" verdict from an empty oracle is a verdict decided before the check looked')
}

# ORACLE DISCRIMINATION, BEFORE ANY VERDICT IS FORMED.
#
# Every failure mode of a lookup like this — a renamed JSON shape, a property
# accessor that quietly returns nothing, a case mismatch — produces the same
# answer as "the symbol is absent". If the oracle cannot be shown to answer YES
# to something true and NO to something false, then a pass below means nothing
# and a failure means nothing either.
if ($violations.Count -eq 0) {
    if (-not (Test-ModuleExports $modules 'sl.common.dll' 'NVSDK_NGX_Parameter_SetUI')) {
        $violations.Add('oracle discrimination failed: sl.common.dll should export NVSDK_NGX_Parameter_SetUI in the measured data and the lookup says it does not - the lookup itself is broken, so no verdict below can be trusted')
    }
    $fabricated = 'FlDefinitelyNotAVendorSymbol_' + 'canary'
    $anyMatched = $false
    foreach ($p in $modules.PSObject.Properties) {
        if (Test-ModuleExports $modules $p.Name $fabricated) { $anyMatched = $true; break }
    }
    if ($anyMatched) {
        $violations.Add("oracle discrimination failed: a fabricated symbol name matched a module, so the lookup says yes to anything")
    }
}

# --- Pass A: every inventory row, module-scoped ------------------------------
$rows = Get-InventoryRow (Get-Content $headerPath -Raw)
if ($rows.Count -eq 0) {
    # NEVER A PASS. A parser that matches nothing reports "no violations found",
    # which is this repository's recurring defect wearing a regex.
    $violations.Add('parsed ZERO rows out of FL_HOOK_INVENTORY - either the inventory is empty or the parser no longer matches its shape, and both must fail rather than report a clean sweep')
}

if ($violations.Count -eq 0) {
    foreach ($r in $rows) {
        if (-not (Test-ModuleExports $modules $r.Module $r.Symbol)) {
            $violations.Add("$($r.Module) does not export '$($r.Symbol)' in docs/vendor-exports.json - the Overlay would resolve nothing, install nothing, and write a record indistinguishable from an honest one on a title with no upscaler")
            continue
        }
        # Multiplicity is legitimate and is NOT a failure — it is the reason the
        # module column exists — but it is worth printing, because a row whose
        # symbol has many exporters is a row where scoping is load-bearing.
        $exporters = @($modules.PSObject.Properties | Where-Object { @($_.Value.exports) -contains $r.Symbol }).Count
        if ($exporters -gt 1) {
            Write-Host "  note: '$($r.Symbol)' is exported by $exporters measured modules; scoped to $($r.Module)" -ForegroundColor DarkGray
        }
    }
}

# --- Pass B: no vendor symbol resolved outside the inventory ------------------
$overlayDir = Join-Path $RepoRoot 'src/native/FrameLedger.Overlay'
$swept = 0
if (Test-Path $overlayDir) {
    $known = @($rows | ForEach-Object { $_.Symbol })
    foreach ($f in Get-ChildItem $overlayDir -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue) {
        if ($f.FullName -eq (Resolve-Path $headerPath).Path) { continue }    # the inventory itself
        $swept++
        foreach ($lit in (Get-StrayVendorLiteral (Get-Content $f.FullName -Raw))) {
            if ($known -notcontains $lit) {
                $violations.Add("$($f.Name) contains the vendor symbol literal '$lit', which is not in FL_HOOK_INVENTORY - a second resolver written outside the table is invisible to the oracle check")
            }
        }
    }
}

# --- Report ------------------------------------------------------------------
if ($violations.Count -gt 0) {
    Write-Host 'HOOKINVENTORY CHECK FAILED' -ForegroundColor Red
    $violations | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ''
    Write-Host 'See docs/17_HOOK_ENGINE.md Hook inventory and docs/vendor-exports.json.' -ForegroundColor Yellow
    exit 1
}

Write-Host "hookinventory OK - $($rows.Count) symbol(s) checked module-scoped against $moduleCount measured modules; $swept Overlay source file(s) swept for strays" -ForegroundColor Green
exit 0
