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

    FOUR PASSES (three independent ones, and a fourth for the census table --
    added 2026-09-03), because each has a blind spot the next covers.
    Pass A checks the inventory rows against the oracle. Pass B sweeps the
    Overlay's own sources for vendor-shaped string literals that are NOT in the
    inventory — a second resolver written somewhere else would otherwise be
    invisible to this gate while being exactly what it exists to prevent. Same
    argument tools/chokepoint-check.ps1 makes about FL_GUARD_TESTABLE.

    PASS C READS THE BUILT BINARY, and it is the only one that can catch the
    failure that matters most. A and B are source checks: they see what we
    RESOLVE. Neither sees what we LINK. Taking the address of an SL_API
    declaration in evaluated code — `&slEvaluateFeature` instead of the runtime
    resolution — makes sl.interposer.dll a LOAD-TIME dependency of
    FrameLedger.Overlay.dll, and the Overlay then fails to load in every game
    that ships no Streamline: inside the loader, before DllMain, with no message
    anywhere and nothing in the ring to explain it. The vendored headers are
    included for types only for exactly this reason
    (src/native/third_party/streamline/README.md).

    PASS C READS THE EXPORT TABLE TOO, since 2026-09-04. The vendored ffx_api.h
    declares its entry points __declspec(dllexport) unconditionally; a definition
    of one inside the Overlay would announce a vendor API we do not implement,
    with every import check green. Same failure class, other table.

    THAT README ASSERTED THIS CHECK EXISTED AND IT DID NOT. Recorded because it
    is the shape this repository keeps finding: a document describing a gate,
    reviewers trusting the document, and nothing behind it. Added 2026-08-14
    after a plan-time audit went looking for the code and found one mention of
    `dumpbin`, in a comment, about a different script.

.PARAMETER RepoRoot
    Repository root. Defaults to this script's parent directory.

.PARAMETER BuildDir
    Native build output, for Pass C. Defaults to build/native/x64-release.

.PARAMETER RequireBinaries
    Makes Pass C mandatory. Without it a missing FrameLedger.Overlay.dll is
    reported and skipped, because `build.ps1 -SkipNative` legitimately produces
    no binary; with it, a missing or unreadable binary is a FAILURE. build.ps1
    passes it whenever the native build ran, following chokepoint-check.

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

    [Parameter(ParameterSetName = 'Check')]
    [string]$BuildDir,

    [Parameter(ParameterSetName = 'Check')]
    [switch]$RequireBinaries,

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

# Parse FL_RUNTIME_CENSUS rows: X(L"module.dll", fl::FL_CENSUS_BIT). Two columns,
# so the three-column inventory regex cannot mistake one for the other.
function Get-CensusRow([string]$HeaderText) {
    $rows = @()
    foreach ($m in [regex]::Matches($HeaderText, 'X\(\s*L"([^"]+)"\s*,\s*((?:fl::)?FL_CENSUS_[A-Z0-9_]+)\s*\)')) {
        $rows += [pscustomobject]@{ Module = $m.Groups[1].Value; Bit = $m.Groups[2].Value }
    }
    return , $rows
}

# Is this module NAME in the measured data at all? The census resolves no symbol,
# so the question is one level up from Test-ModuleExports: has a copy of this name
# ever been seen on disk in an installed title. Case-sensitive on purpose -- the
# loader is not, but the oracle records the on-disk spelling and a row that differs
# from it is a row nobody checked.
function Test-ModuleMeasured($Modules, [string]$Module) {
    if ($null -eq $Modules) { return $false }
    $prop = $Modules.PSObject.Properties[$Module]
    return $null -ne $prop
}

# Vendor-shaped literals the Overlay must not resolve outside the inventory.
# Deliberately broad on the vendor prefixes and narrow on where it looks.
#
# `xefg` is its own alternative and is NOT covered by `xess`: Intel's frame
# generation exports are xefgSwapChainD3D12InitFromSwapChain and friends, which
# share no prefix with xessD3D12CreateContext. Measured from
# docs/vendor-exports.json, where libxess_fg.dll's 31 exports include 28
# xefgSwapChain* names and not one xess* name. Added before the FG hooks land
# rather than after, because the sweep going quiet on a whole vendor family is
# exactly the silence this gate exists to break.
function Get-StrayVendorLiteral([string]$SourceText) {
    $stray = @()
    foreach ($m in [regex]::Matches($SourceText, '"((?:sl[A-Z]|NVSDK_NGX_|xess[A-Z]|xefg[A-Z]|ffx[A-Z])[A-Za-z0-9_]*)"')) {
        $stray += $m.Groups[1].Value
    }
    return , $stray
}

# The DLL names a binary imports, from `dumpbin /dependents` output.
#
# Both the ordinary and the delay-loaded dependency lists are wanted: a
# delay-loaded sl.interposer.dll fails later than a load-time one but fails the
# same way, in a game that has no Streamline to load.
function Get-ImportName([string]$DumpbinText) {
    $names = @()
    foreach ($line in ($DumpbinText -split "`r?`n")) {
        if ($line -match '^\s{4}(\S+\.(?:dll|DLL))\s*$') { $names += $Matches[1] }
    }
    return , $names
}

# Modules FrameLedger.Overlay.dll must never appear to depend on.
#
# ANCHORED, because the failure this catches is about the module the LOADER will
# go looking for, and a substring match would fire on any name that happened to
# contain "ffx". `_?nvngx` covers the driver-store core, which ships as
# _nvngx.dll — an unanchored `nvngx` would have missed it and an anchored one
# without the underscore would have missed it too.
function Get-ForbiddenImport([string[]]$ImportNames) {
    $bad = @()
    foreach ($n in $ImportNames) {
        if ($n -match '^(sl\.|_?nvngx|libxess|ffx_|amd_fidelityfx)') { $bad += $n }
    }
    return , $bad
}

# The names a binary EXPORTS, from `dumpbin /exports` output.
#
# WHY THE EXPORT TABLE TOO (2026-09-04). The vendored ffx_api.h declares its five
# entry points FFX_API_ENTRY, which it defines as __declspec(dllexport) with no
# import switch at all. A declaration exports nothing -- but a DEFINITION anywhere
# in the Overlay would, and the Overlay would then announce `ffxDispatch` from
# inside somebody else's game, an API it does not implement, with every import
# check green. Same failure class Pass C exists for, read from the other table.
function Get-ExportName([string]$DumpbinText) {
    $names = @()
    foreach ($line in ($DumpbinText -split "`r?`n")) {
        # "ordinal hint RVA name" rows: three numeric columns, then the name (a
        # forwarder suffixes " = target", which the \S+ stops before).
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $names += $Matches[1] }
    }
    return , $names
}

# Exported names FrameLedger.Overlay.dll must never carry: the vendor shapes Pass B
# forbids as literals, applied to what the binary announces.
function Get-ForbiddenExport([string[]]$ExportNames) {
    $bad = @()
    foreach ($n in $ExportNames) {
        if ($n -match '^(sl[A-Z]|NVSDK_NGX_|xess[A-Z]|xefg[A-Z]|ffx[A-Z])') { $bad += $n }
    }
    return , $bad
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
        @{ Name = 'an XeFG literal is found - xess[A-Z] does not cover xefg*'
           Got  = ((Get-StrayVendorLiteral 'p = GetProcAddress(h, "xefgSwapChainD3D12InitFromSwapChain");') -contains 'xefgSwapChainD3D12InitFromSwapChain'); Want = $true }

        # --- Pass D: the census rows -------------------------------------------
        @{ Name = 'a census row parses'
           Got  = ((Get-CensusRow 'X(L"nvngx_dlssg.dll", fl::FL_CENSUS_NVNGX_DLSSG)').Count -eq 1); Want = $true }
        @{ Name = 'a census row is NOT an inventory row (two columns, not three)'
           Got  = ((Get-InventoryRow 'X(L"nvngx_dlssg.dll", fl::FL_CENSUS_NVNGX_DLSSG)').Count -eq 0); Want = $true }
        @{ Name = 'an inventory row is NOT a census row'
           Got  = ((Get-CensusRow 'X(L"a.dll", "sym", fl::FAM)').Count -eq 0); Want = $true }
        @{ Name = 'a measured module name passes'
           Got  = (Test-ModuleMeasured $oracle 'sl.interposer.dll'); Want = $true }
        @{ Name = 'an unmeasured module name fails - a misspelling here reads as "not loaded" on every title'
           Got  = (Test-ModuleMeasured $oracle 'sl.interposor.dll'); Want = $false }
        @{ Name = 'a null oracle fails closed'
           Got  = (Test-ModuleMeasured $null 'sl.interposer.dll'); Want = $false }

        # --- Pass C: the import parser, then the predicate over it -----------
        @{ Name = 'dumpbin dependent lines parse into module names'
           Got  = ((Get-ImportName "  Image has the following dependencies:`r`n`r`n    KERNEL32.dll`r`n    d3d12.dll`r`n`r`n  Summary`r`n") -join ',') -eq 'KERNEL32.dll,d3d12.dll'; Want = $true }
        @{ Name = 'the Summary section does not parse as a dependency'
           Got  = ((Get-ImportName "  Summary`r`n`r`n        1000 .data`r`n").Count -eq 0); Want = $true }
        @{ Name = 'a clean import list is clean'
           Got  = ((Get-ForbiddenImport @('KERNEL32.dll', 'd3d11.dll', 'd3d12.dll', 'dxgi.dll')).Count -eq 0); Want = $true }
        @{ Name = 'a LINKED sl.interposer.dll is caught - the whole reason Pass C exists'
           Got  = ((Get-ForbiddenImport @('KERNEL32.dll', 'sl.interposer.dll')) -contains 'sl.interposer.dll'); Want = $true }
        @{ Name = 'the driver-store NGX core is caught despite its leading underscore'
           Got  = ((Get-ForbiddenImport @('_nvngx.dll')) -contains '_nvngx.dll'); Want = $true }
        @{ Name = 'libxess and the FFX modules are caught'
           Got  = ((Get-ForbiddenImport @('libxess_fg.dll', 'ffx_fsr3_x64.dll', 'amd_fidelityfx_dx12.dll')).Count -eq 3); Want = $true }
        @{ Name = 'the match is ANCHORED - an innocent name merely CONTAINING a vendor prefix passes'
           Got  = ((Get-ForbiddenImport @('mysl.dll', 'libffx_helper.dll', 'unvngx.dll')).Count -eq 0); Want = $true }
        @{ Name = 'the SDK 2.x AMD leaves and the loader are all caught as imports'
           Got  = ((Get-ForbiddenImport @('amd_fidelityfx_upscaler_dx12.dll', 'amd_fidelityfx_framegeneration_dx12.dll', 'amd_fidelityfx_loader_dx12.dll')).Count -eq 3); Want = $true }

        # --- AMD: one symbol, several rows ---------------------------------------
        @{ Name = 'three rows with the SAME symbol in different modules parse as three rows'
           Got  = ((Get-InventoryRow 'X(L"a.dll", "ffxDispatch", fl::F) X(L"b.dll", "ffxDispatch", fl::F) X(L"c.dll", "ffxDispatch", fl::F)').Count -eq 3); Want = $true }

        # --- Pass C, the export table --------------------------------------------
        @{ Name = 'dumpbin export rows parse into names, and the header and Summary do not'
           Got  = ((Get-ExportName "    ordinal hint RVA      name`r`n`r`n          1    0 00001000 FlGuardedInject`r`n          2    1 00001040 ffxDispatch`r`n`r`n  Summary`r`n`r`n        1000 .data`r`n") -join ',') -eq 'FlGuardedInject,ffxDispatch'; Want = $true }
        @{ Name = 'a vendor-shaped EXPORT is caught - a definition of an FFX_API_ENTRY name would announce an API we do not implement'
           Got  = ((Get-ForbiddenExport @('FlGuardedInject', 'ffxDispatch')) -join ',') -eq 'ffxDispatch'; Want = $true }
        @{ Name = 'our own exports are not mistaken for vendor ones'
           Got  = ((Get-ForbiddenExport @('FlGuardedInject', 'FlStubEvaluateCount', 'DllMain')).Count -eq 0); Want = $true }
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

# --- Pass D: every census module name is a measured module --------------------
#
# FL_RUNTIME_CENSUS names modules the watchdog asks the loader about. Nothing is
# resolved or called, so Pass A's symbol check does not apply -- but the failure
# mode is the same shape one level up: a name the data has never seen is a name
# that will never be found loaded, and "not loaded" is the answer that makes the
# consumer print "no frame-generation runtime was loaded" about a title that has
# one. Zero rows is never a pass, for the reason Pass A gives.
$censusRows = Get-CensusRow (Get-Content $headerPath -Raw)
if ($censusRows.Count -eq 0) {
    $violations.Add('parsed ZERO rows out of FL_RUNTIME_CENSUS - either the table is empty or the parser no longer matches its shape, and both must fail rather than report a clean sweep')
}
if ($violations.Count -eq 0) {
    foreach ($c in $censusRows) {
        if (-not (Test-ModuleMeasured $modules $c.Module)) {
            $violations.Add("FL_RUNTIME_CENSUS names '$($c.Module)', which no measured title ships (docs/vendor-exports.json) - the loader would never report it and the consumer would read its absence as 'no such runtime was loaded'")
        }
    }
}

# --- Pass C: the built Overlay imports no vendor module -----------------------
#
# A and B are source checks and structurally cannot see this. What is being
# guarded is the IMPORT TABLE: a load-time (or delay-load) dependency on a vendor
# module makes the Overlay unloadable in every game that does not ship it.
$importState = 'skipped'
$importCount = 0
if ($violations.Count -eq 0 -and -not $RequireBinaries) {
    # SKIPPED ON PURPOSE, not for want of a file. -RequireBinaries is the caller
    # saying "the native build ran in THIS invocation", so without it any
    # FrameLedger.Overlay.dll under BuildDir is stale by definition -- left by an
    # earlier build, from sources that may not be the ones on disk now.
    #
    # The first version of this read whatever binary happened to be lying around,
    # which made `build.ps1 check -SkipNative` print a skip line and run the pass
    # anyway, against a binary the run did not produce. A gate reporting on the
    # wrong artefact is worse than one that says it did not look.
    $importState = 'skipped (native build did not run in this invocation; -RequireBinaries makes it mandatory)'
}
elseif ($violations.Count -eq 0) {
    if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot 'build/native/x64-release' }
    $overlayDll = Get-ChildItem $BuildDir -Recurse -Filter 'FrameLedger.Overlay.dll' -ErrorAction SilentlyContinue |
        Select-Object -First 1

    $dumpbin = $null
    if (Get-Command dumpbin -ErrorAction SilentlyContinue) { $dumpbin = 'dumpbin' }
    else {
        $found = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio' -Recurse -Filter 'dumpbin.exe' `
            -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
        if ($found) { $dumpbin = $found.FullName }
    }

    # -RequireBinaries is necessarily set here, so every way of NOT looking is a
    # failure. Refusing beats reporting a clean import list nobody read.
    if (-not $overlayDll) {
        $violations.Add("Pass C: FrameLedger.Overlay.dll not found under $BuildDir, and -RequireBinaries says the native build ran - a missing binary must fail rather than read as 'no forbidden imports'")
    }
    elseif (-not $dumpbin) {
        $violations.Add('Pass C: dumpbin not found, so the import table could not be read - refusing rather than reporting a clean import list nobody looked at')
    }
    else {
        $raw = (& $dumpbin /nologo /dependents $overlayDll.FullName 2>&1 | Out-String)
        $imports = Get-ImportName $raw
        $importCount = $imports.Count

        # ZERO IMPORTS IS NEVER A PASS. Every failure mode of this parse — a
        # dumpbin that errored, an output format change, a path that was not a
        # PE — produces the same empty list as a binary with no vendor imports.
        # Same rule Pass A applies to zero parsed inventory rows.
        if ($importCount -eq 0) {
            $violations.Add("Pass C: parsed ZERO imports from $($overlayDll.Name) - a DLL that imports nothing at all is not a thing, so the parse broke and 'no forbidden imports' would be a verdict decided before the check looked")
        }
        else {
            # DISCRIMINATION, before the verdict. The list must contain something
            # we KNOW is there, or a clean result says only that the parse is
            # broken in a way that happens to look tidy.
            if (@($imports | Where-Object { $_ -match '^(?i)kernel32\.dll$' }).Count -eq 0) {
                $violations.Add("Pass C: the import list does not contain kernel32.dll, which every Win32 DLL imports - the parse is wrong, so no verdict from it can be trusted (got: $($imports -join ', '))")
            }
            foreach ($bad in (Get-ForbiddenImport $imports)) {
                $violations.Add("Pass C: FrameLedger.Overlay.dll IMPORTS '$bad' - the vendored headers are for types only, and a link makes that module a load-time dependency, so the Overlay would fail to load in every game that does not ship it (in the loader, before DllMain, with no message anywhere)")
            }

            # --- and the EXPORT table, with a positive control for the parser ------
            #
            # The Overlay may legitimately export nothing vendor-shaped, so "zero
            # forbidden exports" cannot discriminate a working parser from a broken
            # one on its own. The leaf fixture MUST export ffxDispatch (the stub
            # defines it through the vendored header's own FFX_API_ENTRY), so the
            # same parser is run on it first: if it cannot see that, no verdict on
            # the Overlay's table can be trusted.
            $exports = Get-ExportName (& $dumpbin /nologo /exports $overlayDll.FullName 2>&1 | Out-String)
            $leafStub = Get-ChildItem $BuildDir -Recurse -Filter 'amd_fidelityfx_dx12.dll' -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if (-not $leafStub) {
                $violations.Add("Pass C: the amd_fidelityfx_dx12.dll leaf fixture was not found under $BuildDir, so the export parser has no positive control and 'the Overlay exports no vendor name' would be a verdict decided before the check looked")
            }
            else {
                $stubExports = Get-ExportName (& $dumpbin /nologo /exports $leafStub.FullName 2>&1 | Out-String)
                if ($stubExports -notcontains 'ffxDispatch') {
                    $violations.Add("Pass C: the export parser did not see ffxDispatch on the leaf fixture $($leafStub.FullName), so it reads nothing - refusing rather than reporting a clean export table nobody parsed")
                }
            }
            foreach ($bad in (Get-ForbiddenExport $exports)) {
                $violations.Add("Pass C: FrameLedger.Overlay.dll EXPORTS '$bad' - the vendored headers declare these names __declspec(dllexport), and a definition in the Overlay announces a vendor API it does not implement, from inside somebody else's game")
            }
            $importState = "$importCount import(s) and $($exports.Count) export(s) checked"
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

Write-Host "hookinventory OK - $($rows.Count) symbol(s) checked module-scoped against $moduleCount measured modules; $($censusRows.Count) census name(s) measured; $swept Overlay source file(s) swept for strays; imports: $importState" -ForegroundColor Green
exit 0
