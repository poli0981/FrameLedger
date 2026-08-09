#Requires -Version 7.0
<#
.SYNOPSIS
    Asserts that nothing outside a known allowlist can reach the shipped package.

.DESCRIPTION
    docs/12_BUILD.md §Publish & package publishes exactly two roots —
    src/FrameLedger.App and src/FrameLedger.Agent — into one out/app. Everything
    else in src/ is outside the package only because neither root references it,
    and until now nothing checked that: `build.ps1` never runs `dotnet publish`,
    and .github/workflows/ has no release job, so out/app's real contents have
    never been asserted by anything.

    That mattered the moment FrameLedger.CaptureHost appeared. 20_OPEN_QUESTIONS
    §S27 says the real consent gate arrives with the `games` table "not before",
    and it was closed on the strength of there being no injecting entry point on
    any shipped binary. The capture host IS an injecting entry point; what keeps
    §S27 closed is that it is not shipped. A ProjectReference from either publish
    root would reopen it silently.

    So this walks the transitive <ProjectReference> closure of the two roots and
    fails on anything outside the allowlist, naming the reference edge that
    reached it.

    WHAT IT DOES NOT SEE, stated rather than left to be discovered. It reads the
    two root .csproj files and matches <ProjectReference Include="..."> out of
    them. It does NOT resolve <Import Project="...targets"> and does NOT read
    Directory.Build.props — and this repository's established idiom for putting a
    foreign binary beside a project's output is exactly an imported .targets with
    a <None ... CopyToOutputDirectory> item (see FrameLedger.Guard.targets and
    FrameLedger.DrainFixtures.targets). A .targets that copied
    FrameLedger.CaptureHost.exe into a publish root's output would therefore be
    invisible here.

    That is a narrower hole than it sounds: staging a binary is not the same as
    referencing a project, the shipped assembly would still contain no code path
    to it, and the copy would be a visible new <Import> in a root csproj. But it
    is a hole, and a gate that overstates its reach is the shape this file exists
    to prevent. Widening it means resolving Imports transitively and failing on
    <None>/<Content> items naming a non-allowlisted project's output.

    TWO INVOCATIONS, AND BOTH ARE WIRED IN build.ps1.

    -SelfTest exercises the LOGIC against temp fixtures, five cases of which two
    must come back RED. The live pass (no switch, the DEFAULT parameter set)
    resolves this repository. Running only the self-test would be a gate that
    never looks at the repository — the exact defect it exists to prevent, and
    the shape tools/changelog-check.ps1 has for a reason that does not apply
    here: that script needs a pull request's changed-file list, which only CI
    has, so ci.yml supplies its live half. Nothing would ever supply this one.
#>
[CmdletBinding(DefaultParameterSetName = 'Live')]
param(
    [Parameter(ParameterSetName = 'Live')]
    [string]$Root = (Split-Path $PSScriptRoot -Parent),

    [Parameter(Mandatory, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The publish roots, verbatim from docs/12_BUILD.md §Publish & package.
$publishRoots = @(
    'src/FrameLedger.App/FrameLedger.App.csproj',
    'src/FrameLedger.Agent/FrameLedger.Agent.csproj'
)

# What is allowed inside the package. A project not on this list is either new and
# needs a deliberate decision, or is something that must not ship.
$allowed = @(
    'FrameLedger.App',
    'FrameLedger.Agent',
    'FrameLedger.Application',
    'FrameLedger.Domain',
    'FrameLedger.Infrastructure',
    'FrameLedger.Shared'
)

function Get-ProjectReferences([string]$csproj) {
    # Regex rather than an XML parse on purpose: MSBuild conditions, Sdk-implicit
    # items and $(MSBuildThisFileDirectory) all make a "correct" parse a project of
    # its own, and every ProjectReference in this repository is a literal path.
    if (-not (Test-Path -LiteralPath $csproj)) { return @() }
    $dir = Split-Path -Parent $csproj
    $text = Get-Content -LiteralPath $csproj -Raw
    $out = @()
    foreach ($m in [regex]::Matches($text, '<ProjectReference\s+Include="([^"]+)"')) {
        $rel = $m.Groups[1].Value -replace '\\', [IO.Path]::DirectorySeparatorChar
        $out += [IO.Path]::GetFullPath((Join-Path $dir $rel))
    }
    return $out
}

function Get-Closure([string]$rootCsproj) {
    $seen = [System.Collections.Generic.Dictionary[string, string]]::new()
    $queue = [System.Collections.Generic.Queue[object]]::new()
    $full = [IO.Path]::GetFullPath($rootCsproj)
    $seen[$full] = '(publish root)'
    $queue.Enqueue($full)
    while ($queue.Count -gt 0) {
        $cur = $queue.Dequeue()
        foreach ($ref in (Get-ProjectReferences $cur)) {
            if (-not $seen.ContainsKey($ref)) {
                $seen[$ref] = [IO.Path]::GetFileNameWithoutExtension($cur)
                $queue.Enqueue($ref)
            }
        }
    }
    return $seen
}

function Test-Closure([string]$repoRoot, [string[]]$roots, [string[]]$allowlist) {
    $problems = [System.Collections.Generic.List[string]]::new()
    $inspected = 0

    foreach ($rootRel in $roots) {
        $rootPath = Join-Path $repoRoot $rootRel
        if (-not (Test-Path -LiteralPath $rootPath)) {
            $problems.Add("publish root missing: $rootRel — the closure cannot be checked on a project that is not there")
            continue
        }

        $closure = Get-Closure $rootPath
        $inspected++

        # VACUITY GUARD, and it lives in the LIVE path rather than only in the
        # self-test. A regex that stopped matching would report an empty closure,
        # and an empty closure contains nothing forbidden. The idiom is
        # tools/chokepoint-check.ps1's "no sources found — the check would pass
        # vacuously".
        if ($closure.Count -lt 3) {
            $problems.Add("closure of $rootRel resolved only $($closure.Count) project(s) — that is not a real closure, and a check that resolves nothing passes vacuously")
        }

        foreach ($proj in $closure.Keys) {
            $name = [IO.Path]::GetFileNameWithoutExtension($proj)
            if ($allowlist -notcontains $name) {
                $problems.Add("$rootRel reaches $name (via $($closure[$proj])) — that puts it inside out/app")
            }
        }
    }

    if ($inspected -eq 0) {
        $problems.Add('no publish root was inspected — the check would pass vacuously')
    }

    return , $problems
}

if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("fl-closure-" + [Guid]::NewGuid().ToString('N'))
    $cases = 0
    $red = 0
    $failures = [System.Collections.Generic.List[string]]::new()

    function New-Csproj([string]$path, [string[]]$refs) {
        $dir = Split-Path -Parent $path
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $items = ($refs | ForEach-Object { "    <ProjectReference Include=`"$_`" />" }) -join "`n"
        Set-Content -LiteralPath $path -Value "<Project Sdk=`"Microsoft.NET.Sdk`">`n  <ItemGroup>`n$items`n  </ItemGroup>`n</Project>"
    }

    function Invoke-Case([string]$name, [bool]$expectRed, [scriptblock]$arrange) {
        $script:cases++
        $caseRoot = Join-Path $tmp ([Guid]::NewGuid().ToString('N'))
        & $arrange $caseRoot
        $problems = Test-Closure $caseRoot @('src/A/A.csproj', 'src/B/B.csproj') @('A', 'B', 'Lib', 'Lib2')
        $isRed = $problems.Count -gt 0
        if ($isRed) { $script:red++ }
        if ($isRed -ne $expectRed) {
            $script:failures.Add("$name — expected $(if ($expectRed) { 'RED' } else { 'GREEN' }), got $(if ($isRed) { 'RED' } else { 'GREEN' }): $($problems -join '; ')")
        }
        else {
            Write-Host "  $name : $(if ($isRed) { 'RED' } else { 'GREEN' }) as expected" -ForegroundColor DarkGray
        }
    }

    try {
        # GREEN FIRST. Every RED case below is satisfied by a check that refuses
        # everything, so the suite needs one arrangement that must PASS or it proves
        # only that nothing works. Three projects deep, because the vacuity guard
        # refuses a closure smaller than that and the fixture has to clear it.
        Invoke-Case 'both roots reach only allowlisted projects' $false {
            param($r)
            New-Csproj (Join-Path $r 'src/A/A.csproj') @('..\Lib\Lib.csproj')
            New-Csproj (Join-Path $r 'src/B/B.csproj') @('..\Lib\Lib.csproj')
            New-Csproj (Join-Path $r 'src/Lib/Lib.csproj') @('..\Lib2\Lib2.csproj')
            New-Csproj (Join-Path $r 'src/Lib2/Lib2.csproj') @()
        }

        Invoke-Case 'a root references an unshipped project DIRECTLY' $true {
            param($r)
            New-Csproj (Join-Path $r 'src/A/A.csproj') @('..\Lib\Lib.csproj')
            New-Csproj (Join-Path $r 'src/B/B.csproj') @('..\Lib\Lib.csproj', '..\Host\Host.csproj')
            New-Csproj (Join-Path $r 'src/Lib/Lib.csproj') @('..\Lib2\Lib2.csproj')
            New-Csproj (Join-Path $r 'src/Lib2/Lib2.csproj') @()
            New-Csproj (Join-Path $r 'src/Host/Host.csproj') @()
        }

        Invoke-Case 'a root reaches an unshipped project TRANSITIVELY' $true {
            param($r)
            New-Csproj (Join-Path $r 'src/A/A.csproj') @('..\Lib\Lib.csproj')
            New-Csproj (Join-Path $r 'src/B/B.csproj') @('..\Lib\Lib.csproj')
            New-Csproj (Join-Path $r 'src/Lib/Lib.csproj') @('..\Lib2\Lib2.csproj', '..\Host\Host.csproj')
            New-Csproj (Join-Path $r 'src/Lib2/Lib2.csproj') @()
            New-Csproj (Join-Path $r 'src/Host/Host.csproj') @()
        }

        Invoke-Case 'a publish root is missing' $true {
            param($r)
            New-Csproj (Join-Path $r 'src/A/A.csproj') @('..\Lib\Lib.csproj')
            New-Csproj (Join-Path $r 'src/Lib/Lib.csproj') @('..\Lib2\Lib2.csproj')
            New-Csproj (Join-Path $r 'src/Lib2/Lib2.csproj') @()
        }

        # The regex stops matching, or a rename empties the graph: the closure comes
        # back tiny and a tiny closure contains nothing forbidden.
        Invoke-Case 'a root whose closure is too small to be real' $true {
            param($r)
            New-Csproj (Join-Path $r 'src/A/A.csproj') @()
            New-Csproj (Join-Path $r 'src/B/B.csproj') @('..\Lib\Lib.csproj')
            New-Csproj (Join-Path $r 'src/Lib/Lib.csproj') @('..\Lib2\Lib2.csproj')
            New-Csproj (Join-Path $r 'src/Lib2/Lib2.csproj') @()
        }
    }
    finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }

    if ($failures.Count -gt 0) {
        Write-Host 'PACKAGE-CLOSURE SELF-TEST FAILED' -ForegroundColor Red
        $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
        exit 1
    }

    if ($red -lt 4) {
        Write-Host "PACKAGE-CLOSURE SELF-TEST FAILED: only $red of $cases cases went red — a gate that never refuses is not a gate." -ForegroundColor Red
        exit 1
    }

    Write-Host "package-closure self-test OK — $cases cases, $red RED as expected" -ForegroundColor Green
    exit 0
}

$problems = Test-Closure $Root $publishRoots $allowed
if ($problems.Count -gt 0) {
    Write-Host 'PACKAGE-CLOSURE CHECK FAILED' -ForegroundColor Red
    $problems | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ''
    Write-Host '  docs/12_BUILD.md publishes FrameLedger.App and FrameLedger.Agent into one' -ForegroundColor Red
    Write-Host '  out/app. A project either root can reach SHIPS. FrameLedger.CaptureHost is' -ForegroundColor Red
    Write-Host '  an injecting entry point kept out of the package by construction, and' -ForegroundColor Red
    Write-Host '  20_OPEN_QUESTIONS §S27 is closed on exactly that basis.' -ForegroundColor Red
    exit 1
}

Write-Host "package-closure OK — $($publishRoots.Count) publish root(s); nothing outside the allowlist is reachable" -ForegroundColor Green
exit 0
