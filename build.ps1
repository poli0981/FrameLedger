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

<#
Clear an ambient Platform variable before any MSBuild invocation.

Anything that sets up MSVC exports Platform=x64 for .vcxproj builds — vcvars64
does, and so does ilammy/msvc-dev-cmd on CI. We have no vcxproj, and MSBuild
reads the variable as a SOLUTION platform, so `dotnet build FrameLedger.slnx`
fails with:

    MSB4126: The specified solution configuration "Release|x64" is invalid

which has no visible connection to having set up a C++ toolchain. Project-level
x64 comes from Directory.Build.props.

This is deliberately its own function, called unconditionally before the managed
build, rather than living at the end of Import-MsvcEnvironment. That function
returns early when cl.exe is already on PATH — which is exactly the CI case,
because the MSVC action ran first. Local runs passed and CI failed on precisely
that difference.
#>
function Clear-VcxprojPlatform {
    Remove-Item Env:Platform -ErrorAction SilentlyContinue
}

<#
Import the MSVC x64 environment into this process so the native build works
from an ordinary shell, not only from a Developer Command Prompt.

Two things this has to get right:

1. Finding the install. vswhere's -requires filter for the C++ workload does
   not match every VS edition/channel — it silently returned nothing on a
   machine that had the tools installed — so fall back to probing for
   vcvars64.bat directly.

2. PATH hygiene. vcvars64 PREPENDS to whatever PATH it inherits. On a machine
   with msys2/MinGW on PATH, CMake then discovers MinGW's `ld.exe` as the
   linker (MSVC ships `link.exe`, so there is nothing to shadow it) and the
   build dies with "cannot find /nologo: No such file or directory" — a
   confusing failure a long way from its cause. Starting from a minimal PATH
   is what a Developer Command Prompt effectively gives you.
#>
function Import-MsvcEnvironment {
    if (Get-Command cl -ErrorAction SilentlyContinue) { return $true }
    # NOTE: this early return is why Clear-VcxprojPlatform is called separately
    # rather than only from the bottom of this function.

    $vcvars = $null
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $root = & $vswhere -latest -prerelease -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null | Select-Object -First 1
        if ($root) { $vcvars = Join-Path $root 'VC\Auxiliary\Build\vcvars64.bat' }
    }
    if (-not $vcvars -or -not (Test-Path $vcvars)) {
        $vcvars = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio' -Recurse -Filter 'vcvars64.bat' `
            -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $vcvars -or -not (Test-Path $vcvars)) { return $false }

    # Drop only the MinGW/msys2 entries — keeping everything else means dotnet,
    # git and cmake survive. Replacing PATH wholesale would remove them and the
    # managed gates would then fail for a reason that has nothing to do with C++.
    $basePath = ($env:PATH -split ';' |
        Where-Object { $_ -and $_ -notmatch 'msys64|mingw(32|64)|Git\\usr\\bin' }) -join ';'

    $dump = cmd /c "set `"PATH=$basePath`" && call `"$vcvars`" >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { return $false }

    foreach ($line in $dump) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }

    Clear-VcxprojPlatform

    # Ninja and clang-format both ship inside VS but neither is on the vcvars
    # PATH. Adding them here means a contributor with the C++ workload gets the
    # native build AND the formatting gate without installing anything extra.
    # vcvars64.bat lives at <VSRoot>\VC\Auxiliary\Build\ — four levels down.
    $vsRoot = Split-Path (Split-Path (Split-Path (Split-Path $vcvars -Parent) -Parent) -Parent) -Parent
    $ninja = Get-ChildItem $vsRoot -Recurse -Filter 'ninja.exe' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($ninja) { $env:PATH = "$($ninja.DirectoryName);$env:PATH" }

    if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
        $cf = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-format.exe'
        if (Test-Path $cf) { $env:PATH = "$(Split-Path $cf -Parent);$env:PATH" }
    }

    return [bool](Get-Command cl -ErrorAction SilentlyContinue)
}

# --- 1-3. Native ------------------------------------------------------------
function Invoke-Native([bool]$FixFormat = $false) {
    Write-Step 'Native build (C++ /W4 /WX)'
    if ($SkipNative) { Skip-Gate 'native build' '-SkipNative'; return }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        Skip-Gate 'native build' 'cmake not on PATH'; return
    }
    if (-not (Import-MsvcEnvironment)) {
        # Do not fall back to another compiler: the Overlay's whole point is
        # the MSVC build profile (/MT, /GS, /guard:cf, /Qspectre).
        Skip-Gate 'native build' 'MSVC not found — install the "Desktop development with C++" workload'
        return
    }
    Write-Host "MSVC: $((Get-Command cl).Source)" -ForegroundColor DarkGray

    $preset = if ($Configuration -eq 'Debug') { 'x64-debug' } else { 'x64-release' }
    Push-Location $nativeDir
    try {
        Invoke-Checked 'cmake configure' { cmake --preset $preset }
        Invoke-Checked 'cmake build' { cmake --build --preset $preset }

        Write-Step 'Native tests'
        Invoke-Checked 'ctest' { ctest --preset $preset }
    }
    finally { Pop-Location }

    Invoke-ClangFormat $FixFormat
}

function Invoke-ClangFormat([bool]$Fix) {
    Write-Step 'clang-format'
    if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
        Skip-Gate 'clang-format' 'clang-format not found (ships with the VS C++ workload)'; return
    }
    $sources = Get-ChildItem $nativeDir -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch 'third_party|[\\/]build[\\/]' }
    if (-not $sources) { return }

    if ($Fix) {
        Invoke-Checked 'clang-format -i' { clang-format -i --style=file @($sources.FullName) }
        Write-Host "formatted $($sources.Count) native file(s)" -ForegroundColor Green
    }
    else {
        Invoke-Checked 'clang-format' { clang-format --dry-run -Werror --style=file @($sources.FullName) }
        Write-Host "OK — $($sources.Count) native file(s)" -ForegroundColor Green
    }
}

# --- 4-6. Managed -----------------------------------------------------------
function Invoke-Managed([bool]$FixFormat) {
    Write-Step 'Managed restore + build (warnings as errors)'
    Clear-VcxprojPlatform
    Invoke-Checked 'dotnet build' { dotnet build $solution -c $Configuration }

    Write-Step 'dotnet format'
    if ($FixFormat) {
        Invoke-Checked 'dotnet format' { dotnet format $solution }
    }
    else {
        Invoke-Checked 'dotnet format --verify-no-changes' { dotnet format $solution --verify-no-changes }
    }

    Write-Step 'Tests'
    # `dotnet test` writes each run into a fresh TestResults/<guid>/ and never
    # prunes. Left alone they accumulate — 24 after a handful of builds — and
    # any gate that reads "the coverage reports" then reads mostly history.
    # Clear them so the gate below sees this run and only this run.
    Get-ChildItem (Join-Path $repo 'tests') -Directory -Filter 'TestResults' -Recurse -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    Invoke-Checked 'dotnet test' {
        dotnet test $solution -c $Configuration --no-build --collect:"XPlat Code Coverage"
    }

    # The cobertura reports above were produced and ignored from the day the
    # repository was scaffolded, while 14_TESTING called its thresholds
    # PR-failing. The gate is self-arming: it reports emptiness explicitly today
    # and starts enforcing the moment Domain or Application gains a .cs file,
    # so the number is never negotiated against code that already exists.
    Write-Step 'coverage-gate'
    $coverageTool = Join-Path $repo 'tools/coverage-gate.ps1'
    if (Test-Path $coverageTool) {
        Invoke-Checked 'coverage-gate' { & $coverageTool }
    }
    else {
        Skip-Gate 'coverage-gate' 'tools/coverage-gate.ps1 not implemented yet'
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

    # Being plainly identifiable to anti-cheat is a requirement, not packaging
    # polish (docs/19_SAFETY_AND_ANTICHEAT.md). Reads the built binary, because
    # what ships is what an anti-cheat vendor sees.
    Write-Step 'versioninfo-check'
    $versionTool = Join-Path $repo 'tools/versioninfo-check.ps1'
    if ((Test-Path $versionTool) -and -not $SkipNative) {
        Invoke-Checked 'versioninfo-check' { & $versionTool -BuildDir (Join-Path $repo "build/native/x64-$($Configuration.ToLower())") }
    }
    elseif ($SkipNative) {
        Skip-Gate 'versioninfo-check' 'native build skipped, so there is no binary to inspect'
    }
    else {
        Skip-Gate 'versioninfo-check' 'tools/versioninfo-check.ps1 not implemented yet'
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
    'native' { Invoke-Native $false }
    'managed' { Invoke-Managed $false }
    'format' { Invoke-Native $true; Invoke-Managed $true }
    'check' {
        Invoke-Native $false     # native first: the managed build copies its output
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
