#Requires -Version 7.0
<#
.SYNOPSIS
    FrameLedger local quality gate. CI runs this identical script, so local and
    CI can never disagree (docs/12_BUILD.md §Local quality gate).

.DESCRIPTION
    ./build.ps1 check    the full nine-gate pre-push check
    ./build.ps1 native   native build only
    ./build.ps1 managed  managed build + tests only
    ./build.ps1 format   apply formatting instead of verifying it

    Gates that are not yet implemented SKIP LOUDLY. A gate that silently passes
    because its tool does not exist is worse than no gate: it reads as "checked"
    in CI output when nothing was checked.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('check', 'native', 'managed', 'format')]
    [string]$Task = 'check',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    # Native build needs MSVC. Skip it when only touching managed code.
    [switch]$SkipNative
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$solution = Join-Path $repo 'FrameLedger.slnx'
$nativeDir = Join-Path $repo 'src/native'

$script:Skipped = @()

function Write-Step([string]$Name) {
    Write-Host ''
    Write-Host "=== $Name " -NoNewline -ForegroundColor Cyan
    Write-Host ('=' * [Math]::Max(0, 60 - $Name.Length)) -ForegroundColor Cyan
}

function Skip-Gate([string]$Name, [string]$Why) {
    $script:Skipped += "$Name — $Why"
    Write-Host "SKIPPED: $Name ($Why)" -ForegroundColor Yellow
}

function Invoke-Checked([string]$What, [scriptblock]$Body) {
    & $Body
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit $LASTEXITCODE)" }
}

# --- 1-3. Native ------------------------------------------------------------
function Invoke-Native {
    Write-Step 'Native build (C++ /W4 /WX)'
    if ($SkipNative) { Skip-Gate 'native build' '-SkipNative'; return }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        Skip-Gate 'native build' 'cmake not on PATH'; return
    }
    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        # Do not fall back to another compiler: the Overlay's whole point is
        # the MSVC build profile (/MT, /GS, /guard:cf, /Qspectre).
        Skip-Gate 'native build' 'cl.exe not on PATH — run from a Developer prompt or use ilammy/msvc-dev-cmd'
        return
    }

    $preset = if ($Configuration -eq 'Debug') { 'x64-debug' } else { 'x64-release' }
    Push-Location $nativeDir
    try {
        Invoke-Checked 'cmake configure' { cmake --preset $preset }
        Invoke-Checked 'cmake build' { cmake --build --preset $preset }

        Write-Step 'Native tests'
        Invoke-Checked 'ctest' { ctest --preset $preset }
    }
    finally { Pop-Location }

    Write-Step 'clang-format'
    if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
        Skip-Gate 'clang-format' 'clang-format not on PATH'; return
    }
    $sources = Get-ChildItem $nativeDir -Recurse -Include *.cpp, *.h |
        Where-Object { $_.FullName -notmatch 'third_party|[\\/]build[\\/]' }
    if ($sources) {
        Invoke-Checked 'clang-format' { clang-format --dry-run -Werror @($sources.FullName) }
    }
}

# --- 4-6. Managed -----------------------------------------------------------
function Invoke-Managed([bool]$FixFormat) {
    Write-Step 'Managed restore + build (warnings as errors)'
    Invoke-Checked 'dotnet build' { dotnet build $solution -c $Configuration }

    Write-Step 'dotnet format'
    if ($FixFormat) {
        Invoke-Checked 'dotnet format' { dotnet format $solution }
    }
    else {
        Invoke-Checked 'dotnet format --verify-no-changes' { dotnet format $solution --verify-no-changes }
    }

    Write-Step 'Tests'
    Invoke-Checked 'dotnet test' {
        dotnet test $solution -c $Configuration --no-build --collect:"XPlat Code Coverage"
    }
}

# --- 7-9. Project-specific gates -------------------------------------------
function Invoke-ProjectGates {
    # A malformed or empty anticheat blocklist is a SAFETY bug, not a lint
    # failure (docs/13_CI_CD.md).
    Write-Step 'rules-validate'
    $rulesTool = Join-Path $repo 'tools/rules-validate.ps1'
    if (Test-Path $rulesTool) {
        Invoke-Checked 'rules-validate' { & $rulesTool }
    }
    else {
        Skip-Gate 'rules-validate' 'tools/rules-validate.ps1 not implemented yet'
    }

    Write-Step 'license-check'
    $licenseTool = Join-Path $repo 'tools/license-check.ps1'
    if (Test-Path $licenseTool) {
        Invoke-Checked 'license-check' { & $licenseTool }
    }
    else {
        Skip-Gate 'license-check' 'tools/license-check.ps1 not implemented yet'
    }

    Write-Step 'resx-audit'
    $resxTool = Join-Path $repo 'tools/resx-audit'
    if (Test-Path $resxTool) {
        Invoke-Checked 'resx-audit' { & $resxTool }
    }
    else {
        Skip-Gate 'resx-audit' 'tools/resx-audit not implemented yet (no .resx files exist)'
    }

    Write-Step 'placeholder guard'
    # {{RELEASE_DATE}} is substituted at release time and is the only token
    # allowed to survive. Anything else in a document FR-11 displays for
    # acceptance is a defect (docs/12_BUILD.md §Release-time token substitution).
    $targets = @(Join-Path $repo 'README.md') + (Get-ChildItem (Join-Path $repo 'legal') -Filter *.md).FullName
    $stray = Select-String -Path $targets -Pattern '\{\{(?!RELEASE_DATE\}\})' -AllMatches
    if ($stray) {
        $stray | ForEach-Object { Write-Host "  $($_.Path):$($_.LineNumber): $($_.Line.Trim())" -ForegroundColor Red }
        throw 'Unsubstituted placeholder tokens found in shipped documents'
    }
    Write-Host 'OK — only {{RELEASE_DATE}} remains' -ForegroundColor Green
}

# --- Entry point ------------------------------------------------------------
$sw = [Diagnostics.Stopwatch]::StartNew()
switch ($Task) {
    'native' { Invoke-Native }
    'managed' { Invoke-Managed $false }
    'format' { Invoke-Managed $true }
    'check' {
        Invoke-Native            # native first: the managed build copies its output
        Invoke-Managed $false
        Invoke-ProjectGates
    }
}

Write-Host ''
if ($script:Skipped.Count -gt 0) {
    Write-Host "PASSED WITH $($script:Skipped.Count) SKIPPED GATE(S) in $([int]$sw.Elapsed.TotalSeconds)s" -ForegroundColor Yellow
    $script:Skipped | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    Write-Host 'These gates did not run. Do not read this as a clean check.' -ForegroundColor Yellow
}
else {
    Write-Host "ALL GATES PASSED in $([int]$sw.Elapsed.TotalSeconds)s" -ForegroundColor Green
}
