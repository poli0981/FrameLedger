#Requires -Version 7.0
<#
.SYNOPSIS
    Asserts that no code path can inject without passing the anti-cheat guard.

.DESCRIPTION
    CLAUDE.md rule 2: "The anti-cheat guard is a hard gate, not a warning...
    There is no override switch." docs/14_TESTING.md calls for a test that
    asserts no code path reaches the injection primitive without a passing
    guard result, and 20_OPEN_QUESTIONS §S8 records that the mechanism
    originally proposed for it — a `sealed` token type only the guard can
    produce — does not work: a token that escapes can simply be IGNORED, because
    a caller can decline to ask for one.

    §S13(b) settled on the stronger shape: the guard OWNS THE CHOKEPOINT. The
    injection primitive has internal linkage inside fl_guard.cpp, so no other
    translation unit has a symbol to call. This script is what keeps that true.

    Two checks, because the first alone is easy to route around:

      1. The primitive's name appears in exactly one file.
      2. The Win32 calls that CONSTITUTE injection — VirtualAllocEx,
         WriteProcessMemory, CreateRemoteThread, and the manual-mapping and
         evasion primitives 19_SAFETY forbids outright — appear nowhere in the
         native tree except that same file.

    Check 2 is the one that matters. Somebody writing a second injector
    elsewhere would never touch the first name, and a reviewer scanning a large
    diff would not necessarily notice.
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [string]$BuildDir = '',
    [bool]$RequireBinaries = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The one file allowed to contain any of this, relative to the native tree.
$chokepoint = 'FrameLedger.Injector/src/fl_guard.cpp'
$nativeRoot = Join-Path $Root 'src/native'

$violations = [System.Collections.Generic.List[string]]::new()

if (-not (Test-Path (Join-Path $nativeRoot $chokepoint))) {
    Write-Host "CHOKEPOINT CHECK FAILED: $chokepoint does not exist." -ForegroundColor Red
    Write-Host '  The guard is the thing this check exists to protect. If it moved, this' -ForegroundColor Red
    Write-Host '  script must move with it — refusing rather than silently checking nothing.' -ForegroundColor Red
    exit 1
}

# Sources only. Third-party code is consumed unmodified and is not ours to gate;
# MinHook legitimately contains process-manipulation primitives.
$sources = @(Get-ChildItem $nativeRoot -Recurse -File -Include '*.cpp', '*.h', '*.hpp' |
        Where-Object { $_.FullName -notmatch '\\(third_party|_deps|build)\\' })

if ($sources.Count -eq 0) {
    Write-Host 'CHOKEPOINT CHECK FAILED: no native sources found — the check would pass vacuously.' -ForegroundColor Red
    exit 1
}

# TWO LISTS, and the split is the point.
#
# The old single list carried both kinds and then did `if ($isChokepoint) {
# continue }` for ALL of them — so the guard's own translation unit was exempt
# from the evasion rules as well as from the injection ones. The comment on that
# line said "only for the primitive itself"; the code did not implement it.
# Appending ZwSetInformationThread(ThreadHideFromDebugger) to fl_guard.cpp passed
# the check, in the one file most likely to be where someone would put it.
#
# Injection is a legitimate thing this project does, in exactly one place.
$chokepointOnly = @(
    @{ Pattern = 'InjectViaLoadLibrary'; Why = 'the injection primitive' },
    @{ Pattern = 'CreateRemoteThread'; Why = 'remote thread creation' },
    @{ Pattern = 'WriteProcessMemory'; Why = 'writing another process''s memory' },
    @{ Pattern = 'VirtualAllocEx'; Why = 'allocating in another process' }
)

# Evasion is not. 19_SAFETY §What we will never build and CLAUDE.md rule 3 rule
# these out PERMANENTLY, which means there is no file in this repository where
# they are acceptable — the chokepoint least of all.
$forbiddenEverywhere = @(
    @{ Pattern = 'NtCreateThreadEx'; Why = 'undocumented remote thread creation' },
    @{ Pattern = 'RtlCreateUserThread'; Why = 'undocumented remote thread creation' },
    @{ Pattern = 'SetWindowsHookEx'; Why = 'hook-based injection' },
    @{ Pattern = 'QueueUserAPC'; Why = 'APC injection' },
    @{ Pattern = 'ThreadHideFromDebugger'; Why = 'thread hiding — evasion, permanently out of scope' },
    @{ Pattern = 'ZwSetInformationThread'; Why = 'thread hiding — evasion, permanently out of scope' }
)

$checkedFile = $false
foreach ($src in $sources) {
    $rel = [IO.Path]::GetRelativePath($nativeRoot, $src.FullName) -replace '\\', '/'
    $isChokepoint = $rel -eq $chokepoint
    if ($isChokepoint) { $checkedFile = $true }

    foreach ($rule in ($chokepointOnly + $forbiddenEverywhere)) {
        # Skip comments: this file, and the guard, describe these APIs at length
        # on purpose. A rule that fired on prose would be turned off within a week.
        $hits = @(Select-String -Path $src.FullName -Pattern $rule.Pattern -SimpleMatch |
                Where-Object { $_.Line.TrimStart() -notmatch '^(//|\*|/\*|;)' })
        if ($hits.Count -eq 0) { continue }

        # The exemption applies to the injection primitives ONLY, and only here.
        if ($isChokepoint -and ($chokepointOnly.Pattern -contains $rule.Pattern)) { continue }

        foreach ($h in $hits) {
            $violations.Add("$rel`:$($h.LineNumber) uses $($rule.Pattern) — $($rule.Why)")
        }
    }
}

# The managed side, which this check could not see at all.
#
# chokepoint-check has always scanned src/native. A P/Invoke declaration reaches
# the same Win32 calls from C#, and nothing looked: adding
# [DllImport("kernel32")] CreateRemoteThread under src/FrameLedger.Infrastructure
# left the build green. CLAUDE.md's "the native layer is reachable only through
# Infrastructure" is about ORGANISATION; this is about the primitives themselves,
# and managed code has no chokepoint to be exempt in.
# src AND tests. The native pass scans all of src/native — including its tests
# and tools, both confirmed to go red — so scoping the managed pass to src/ alone
# was an asymmetry with no justification: a P/Invoke in a test project reaches
# exactly the same Win32 calls, and test code is where "just for a moment" edits
# live.
$managedRoots = @((Join-Path $Root 'src'), (Join-Path $Root 'tests')) | Where-Object { Test-Path $_ }
$managedSources = @(Get-ChildItem $managedRoots -Recurse -File -Include '*.cs' |
        Where-Object { $_.FullName -notmatch '\\(obj|bin)\\' })

if ($managedSources.Count -eq 0) {
    Write-Host 'CHOKEPOINT CHECK FAILED: no managed sources found — the managed pass would be vacuous.' -ForegroundColor Red
    exit 1
}

foreach ($src in $managedSources) {
    $rel = [IO.Path]::GetRelativePath($Root, $src.FullName) -replace '\\', '/'
    foreach ($rule in ($chokepointOnly + $forbiddenEverywhere)) {
        $hits = @(Select-String -Path $src.FullName -Pattern $rule.Pattern -SimpleMatch |
                Where-Object { $_.Line.TrimStart() -notmatch '^(//|///|\*|/\*)' })
        foreach ($h in $hits) {
            $violations.Add("$rel`:$($h.LineNumber) uses $($rule.Pattern) — $($rule.Why) (managed)")
        }
    }
}

if (-not $checkedFile) {
    Write-Host 'CHOKEPOINT CHECK FAILED: the chokepoint file was never examined.' -ForegroundColor Red
    exit 1
}

# --- FL_GUARD_TESTABLE must not reach anything that ships -------------------
# The seam it exposes lets a caller hand the guard all-clean fakes. That is
# necessary for 14_TESTING's fail-closed matrix and unacceptable anywhere else:
# with the injection primitive real, a shipping target that defined this would
# have a route into a game process that consulted no genuine signal at all.
$testTarget = 'src/native/tests/CMakeLists.txt'
$cmakeFiles = @(Get-ChildItem $nativeRoot -Recurse -File -Filter 'CMakeLists.txt' |
        Where-Object { $_.FullName -notmatch '\\(third_party|_deps|build)\\' })
$definers = @()
foreach ($f in $cmakeFiles) {
    $rel = ([IO.Path]::GetRelativePath($Root, $f.FullName)) -replace '\\', '/'
    if (Select-String -Path $f.FullName -Pattern 'FL_GUARD_TESTABLE' -SimpleMatch -Quiet) {
        $definers += $rel
    }
}
foreach ($d in $definers) {
    if ($d -ne $testTarget) {
        $violations.Add("$d defines FL_GUARD_TESTABLE — the evidence seam must exist only in the test target")
    }
}
if ($definers.Count -eq 0) {
    $violations.Add("no CMakeLists defines FL_GUARD_TESTABLE — either the seam moved or this check is now inert")
}

# --- The seam must not EXIST in what ships, not merely be unused ------------
# FL_GUARD_TESTABLE exposes EvaluateWithSources / GuardedInjectWithSources,
# which let a caller hand the guard all-clean fakes. The CMake check above says
# only the test target defines the macro; this says the shipped artifacts carry
# no such symbol, which is the claim that actually matters.
#
# 20_OPEN_QUESTIONS §S15 recorded this as "verified with dumpbin, not assumed" —
# but the verification was a command someone ran once and pasted into a PR body.
# That is prose. A build that stops being true has to fail.
if ($RequireBinaries) {
    if (-not $BuildDir -or -not (Test-Path $BuildDir)) {
        Write-Host "CHOKEPOINT CHECK FAILED: -RequireBinaries but no build at '$BuildDir'." -ForegroundColor Red
        Write-Host '  The symbol half cannot run, and reporting success without it would be' -ForegroundColor Red
        Write-Host '  the exact shape this check exists to prevent.' -ForegroundColor Red
        exit 1
    }
    $dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
    if (-not $dumpbin) {
        Write-Host 'CHOKEPOINT CHECK FAILED: dumpbin not on PATH (it ships with MSVC).' -ForegroundColor Red
        Write-Host '  Refusing rather than skipping: an unrun symbol check is not a passing one.' -ForegroundColor Red
        exit 1
    }

    $artifacts = @(
        (Join-Path $BuildDir 'FrameLedger.Injector/FrameLedger.Injector.lib'),
        (Join-Path $BuildDir 'FrameLedger.Injector/FrameLedger.Guard.dll')
    )
    $inspected = 0
    foreach ($a in $artifacts) {
        if (-not (Test-Path $a)) {
            $violations.Add("expected artifact missing: $a — the symbol check cannot pass on something it did not read")
            continue
        }
        $inspected++
        $syms = & dumpbin /symbols /exports $a 2>&1 | Select-String -Pattern 'WithSources' -SimpleMatch
        foreach ($s in $syms) {
            $violations.Add("$(Split-Path $a -Leaf) exports or defines a *WithSources* symbol: $($s.Line.Trim())")
        }
    }
    if ($inspected -eq 0) {
        $violations.Add('no shipped artifact was inspected — the symbol check would pass vacuously')
    }
}

if ($violations.Count -gt 0) {
    Write-Host 'CHOKEPOINT CHECK FAILED' -ForegroundColor Red
    $violations | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ''
    Write-Host '  Injection happens in exactly one place, behind the guard' -ForegroundColor Red
    Write-Host "  ($chokepoint). CLAUDE.md rule 2: the guard is a hard gate," -ForegroundColor Red
    Write-Host '  and a second path into a game process is an override by another name.' -ForegroundColor Red
    exit 1
}

$symbolNote = if ($RequireBinaries) { '; shipped artifacts carry no *WithSources* symbol' } else { '' }
Write-Host "chokepoint OK — $($sources.Count) native source(s); injection confined to $chokepoint$symbolNote" -ForegroundColor Green
exit 0
