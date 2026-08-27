#Requires -Version 7.0
<#
.SYNOPSIS
    Drives §S31's three-leg capture and hands the two instruments to
    tools/frametype-oracle.ps1. One leg per game launch, by design.

.DESCRIPTION
    §S31 asks one question: IS A DRAINED STREAMLINE BATCH AN APPLICATION FRAME?
    `presents / batch` reads 1.000 / 2.000 / 4.000 against Cyberpunk 2077's own
    off / x2 / x4, but a batch is "a present that drained a Streamline
    evaluation", not an application frame -- the two coincide on that title only
    because Ray Reconstruction happens to be evaluated once per application
    frame, which no independent oracle has confirmed. Three oracles have already
    been tried and all three fell (§S30).

    This script does not decide anything. It COLLECTS the two artifacts each leg
    needs and then runs the comparison. The decision table is §S31, it was
    written before any of this ran, and the operator reads the row off it.

    WHY A SCRIPT AND NOT THREE COMMANDS. A leg costs a game launch and cannot be
    retaken cheaply: when a capture ends, nothing publishes `guardTicks`, the
    Overlay's watchdog hits FL_GUARD_TICK_DEADLINE_MS and calls StopObserving,
    which clears g_observing ONE-WAY for the life of that process. A second
    capture against the same running game returns SupervisionLost with zero
    records. That is designed behaviour (19_SAFETY §During a session), and it
    means a mistyped flag costs a relaunch rather than a retry. Every refusal
    below exists to spend that cost on purpose.

.PARAMETER Leg
    off | x2 | x4 -- the frame-generation setting configured in the GAME'S OWN
    menu before it was launched. THREE SETTINGS, NOT TWO: §S30 already burned a
    draft on the x2 leg, which cannot discriminate because "displayed divided by
    a fixed 2" predicts the same number there.

.PARAMETER Exe
    Full path to the game executable. The consent record is keyed on it, and
    FrameLedger.CaptureHost resolves the running process from it -- there is no
    --pid, deliberately (§S27).

.PARAMETER Seconds
    Capture duration. Default 40, matching every real-title capture in
    spike-notes §8.

.PARAMETER Verify
    Runs tools/frametype-oracle.ps1 over every leg collected so far and prints
    them side by side.

.PARAMETER SelfTest
    Exercises the refusal logic in both directions, without a game, without
    elevation and without PresentMon. It cannot test the capture itself.

.EXAMPLE
    # once, unelevated, in the same build tree you will capture from:
    .\FrameLedger.CaptureHost.exe consent grant --exe "D:\Games\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe"

    # then, per leg, from an ELEVATED shell, with the game already running:
    ./tools/s31-capture.ps1 -Leg off -Exe "D:\Games\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe"

    # after all three:
    ./tools/s31-capture.ps1 -Verify
#>
[CmdletBinding(DefaultParameterSetName = 'Collect')]
param(
    [Parameter(ParameterSetName = 'Collect', Mandatory = $true)]
    [ValidateSet('off', 'x2', 'x4')]
    [string]$Leg,

    [Parameter(ParameterSetName = 'Collect', Mandatory = $true)]
    [string]$Exe,

    [Parameter(ParameterSetName = 'Collect')]
    [ValidateRange(5, 600)]
    [int]$Seconds = 40,

    [Parameter(ParameterSetName = 'Collect')]
    [switch]$Force,

    [Parameter(ParameterSetName = 'Verify', Mandatory = $true)]
    [switch]$Verify,

    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTest,

    [string]$OutDir,
    [string]$PresentMon,
    [string]$CaptureHost
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot

# PINNED BY HASH AND FILENAME, because there is nothing else to pin it by.
# §M2 measured that this binary carries NO VERSIONINFO at all -- FileVersion,
# ProductVersion and CompanyName are all empty -- so a versioninfo-check-style
# gate has nothing to read. "PresentMon 2.x" is not a single artifact, and a
# capture is worthless if nobody can say which binary produced it.
$script:PinnedSha256 = '9BEC3083069F58F911E6A512F4806DB51A27BD096103087BC1D05EF54C80A191'
$script:PinnedName = 'PresentMon-2.5.1-x64.exe'

function Write-Step { param([string]$m) Write-Host "`n== $m ==" -ForegroundColor Cyan }
function Write-Note { param([string]$m) Write-Host "   $m" }
function Deny {
    param([string]$Why, [string[]]$Remedy = @())
    Write-Host "`nREFUSED: $Why" -ForegroundColor Red
    foreach ($r in $Remedy) { Write-Host "         $r" -ForegroundColor Yellow }
    exit 2
}

<#
    The pinned-hash comparison, factored out so the self-test can drive it in
    both directions. Case-insensitive on purpose: Get-FileHash yields upper case
    and the value in spike-notes was transcribed from a different tool.
#>
function Test-PinnedHash {
    param([AllowNull()][string]$Actual, [AllowNull()][string]$Expected)
    if ([string]::IsNullOrWhiteSpace($Actual) -or [string]::IsNullOrWhiteSpace($Expected)) { return $false }
    return [string]::Equals($Actual.Trim(), $Expected.Trim(), [StringComparison]::OrdinalIgnoreCase)
}

<#
    A leg's artifacts, and whether it has already been taken. Returns a shape
    rather than a boolean: "no directory", "directory but no CSV" and "both
    present" are three different states and only the last is a taken leg.
#>
function Get-LegState {
    param([string]$Root, [string]$Name)
    $dir = Join-Path $Root $Name
    $csv = Join-Path $dir 'presentmon.csv'
    $rep = Join-Path $dir 'capturehost-report.txt'
    return [pscustomobject]@{
        Name = $Name; Dir = $dir; Csv = $csv; Report = $rep
        HasCsv = (Test-Path -LiteralPath $csv)
        HasReport = (Test-Path -LiteralPath $rep)
        Taken = (Test-Path -LiteralPath $csv) -and (Test-Path -LiteralPath $rep)
    }
}

<#
    The fingerprint the launcher-update trap needs. Alan Wake 2, 2026-08-20:
    consent was granted at 09:12Z, the executable went from 62,026,752 to
    62,304,768 bytes when the title was launched, and the capture then refused
    with ConsentMissing -- a signal meaning "the dialog has not been accepted",
    which the operator had done forty minutes earlier. Printing size and
    last-write per leg makes that visible ACROSS legs instead of being blamed on
    the gate.
#>
function Get-ExeFingerprint {
    param([string]$Path)
    $f = Get-Item -LiteralPath $Path
    return '{0} bytes, last written {1:u}' -f $f.Length, $f.LastWriteTimeUtc
}

function Test-Elevated {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$id).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

# --- Self-test ---------------------------------------------------------------

if ($SelfTest) {
    $fail = 0
    function Check { param([bool]$ok, [string]$what)
        Write-Host ("  [{0}] {1}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $what)
        if (-not $ok) { $script:fail++ }
    }

    Write-Step 'refusal logic, both directions'
    Check (Test-PinnedHash $script:PinnedSha256 $script:PinnedSha256) 'the pinned hash matches itself'
    Check (Test-PinnedHash $script:PinnedSha256.ToLowerInvariant() $script:PinnedSha256) 'case does not decide it'
    Check (-not (Test-PinnedHash ($script:PinnedSha256 -replace '^9', '8') $script:PinnedSha256)) 'ONE character different is a refusal'
    Check (-not (Test-PinnedHash '' $script:PinnedSha256)) 'an empty hash fails closed'
    Check (-not (Test-PinnedHash $null $script:PinnedSha256)) 'a null hash fails closed'
    Check (-not (Test-PinnedHash $script:PinnedSha256 '')) 'an empty EXPECTATION fails closed too'
    # THE ONE THE GUARD ACTUALLY CARRIES, and it was missing until a canary said so.
    # [string]::Equals already refuses empty-vs-present, so the three cases above pass
    # with the guard REMOVED -- they test Equals, not us. Equals('','') is TRUE, though,
    # which is the shape S23-1 records: two empty build ids compared equal and reported
    # Ok for every process on the machine. This is the case that makes the guard real.
    Check (-not (Test-PinnedHash '' '')) 'BOTH empty is a refusal, not a match'

    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("s31-selftest-" + [Guid]::NewGuid().ToString('N'))
    try {
        $s = Get-LegState -Root $tmp -Name 'x4'
        Check (-not $s.Taken) 'a leg with no directory is not taken'
        New-Item -ItemType Directory -Path $s.Dir -Force | Out-Null
        Set-Content -LiteralPath $s.Csv -Value 'x'
        $s = Get-LegState -Root $tmp -Name 'x4'
        Check (-not $s.Taken) 'a CSV with no report is NOT a taken leg'
        Check $s.HasCsv 'and the CSV is still reported present, not hidden by the verdict'
        Set-Content -LiteralPath $s.Report -Value 'presents/batch= 4.0000'
        $s = Get-LegState -Root $tmp -Name 'x4'
        Check $s.Taken 'both artifacts present is a taken leg'
    }
    finally { if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force } }

    Write-Host ''
    if ($fail -gt 0) { Write-Host "$fail case(s) failed" -ForegroundColor Red; exit 1 }
    Write-Host 's31-capture self-test: 11 cases, both directions' -ForegroundColor Green
    exit 0
}

# --- Shared resolution -------------------------------------------------------

if (-not $OutDir) { $OutDir = Join-Path $env:LOCALAPPDATA 'FrameLedger\s31' }
if (-not $PresentMon) { $PresentMon = Join-Path $RepoRoot "presentmon\$script:PinnedName" }
if (-not $CaptureHost) {
    $CaptureHost = Join-Path $RepoRoot 'src\FrameLedger.CaptureHost\bin\Release\net10.0-windows10.0.22621.0\win-x64\FrameLedger.CaptureHost.exe'
}

# --- Verify ------------------------------------------------------------------

if ($Verify) {
    $oracle = Join-Path $PSScriptRoot 'frametype-oracle.ps1'
    if (-not (Test-Path -LiteralPath $oracle)) { Deny "tools/frametype-oracle.ps1 is missing at $oracle" }

    Write-Step "§S31 · comparing the legs collected under $OutDir"
    $any = $false
    foreach ($name in @('off', 'x2', 'x4')) {
        $s = Get-LegState -Root $OutDir -Name $name
        Write-Host "`n--- leg $name ---" -ForegroundColor Cyan
        if (-not $s.Taken) {
            # An unrun leg is UNRUN. §S31 row P6 says so in as many words: do not
            # infer a retirement from a leg nobody took.
            Write-Note "NOT COLLECTED (csv=$($s.HasCsv) report=$($s.HasReport)) -- an unrun leg is unrun, not a result."
            continue
        }
        $any = $true
        & $oracle -Csv $s.Csv -Report $s.Report
    }

    if (-not $any) { Deny "no leg has both artifacts under $OutDir" @("run -Leg off / -Leg x2 / -Leg x4 first") }

    Write-Step 'Now read the row off §S31 — do not assemble one from these numbers'
    Write-Note 'P1  no FrameType column at all                  -> RETIRE PresentMon as the oracle'
    Write-Note 'P2  FrameType present, every row Application    -> RETIRE. A 1.0 here is an ABSENCE'
    Write-Note 'P3  ratios agree (~2%) at ALL THREE settings    -> a batch IS an application frame'
    Write-Note 'P4  ratios agree at x2 ONLY                     -> NOT an answer. Do not promote'
    Write-Note 'P5  ratios disagree at x4 by more than ~2%      -> stop and localise first'
    Write-Note 'P6  a leg needed elevation nobody granted       -> not a result; an unrun leg is unrun'
    Write-Host ''
    Write-Note 'And the rival §S31 says this table must not be read as excluding: if PresentMon''s'
    Write-Note '`Application` classification is itself derived from a vendor event that fires once'
    Write-Note 'per Streamline evaluation, the two instruments share a mechanism and would agree'
    Write-Note 'for that reason. Agreement makes the reading best-supported; it does not prove it.'
    exit 0
}

# --- Collect -----------------------------------------------------------------

Write-Step "§S31 leg '$Leg' — pre-flight"

if (-not (Test-Elevated)) {
    Deny 'this shell is not elevated, and PresentMon cannot start a trace session without it.' @(
        'Measured 2026-08-20 (spike-notes §11): exit 6, "PresentMon requires either',
        'administrative privileges or to be run by a user in the Performance Log Users',
        'user group". The running PresentMonSharedService does NOT help -- the console',
        'starts its own session.',
        '',
        'Open an elevated PowerShell and re-run. Adding the account to Performance Log',
        'Users (S-1-5-32-559) is the other route and is a system settings change.')
}
Write-Note 'shell is elevated'
Write-Note 'NOTE: this capture therefore runs ELEVATED, which is not the shipped configuration'
Write-Note '      (ADR-9: the default Agent is unelevated). It is a measurement, not a rehearsal.'

if (-not (Test-Path -LiteralPath $PresentMon)) {
    Deny "PresentMon is not at $PresentMon" @('pass -PresentMon <path>, or place it at presentmon\' + $script:PinnedName)
}
$actual = (Get-FileHash -LiteralPath $PresentMon -Algorithm SHA256).Hash
if (-not (Test-PinnedHash $actual $script:PinnedSha256)) {
    Deny 'the PresentMon binary is not the pinned one.' @(
        "expected $script:PinnedSha256",
        "actual   $actual",
        '',
        'This binary carries no VERSIONINFO (§M2), so the hash is the only pin there is.',
        'A capture nobody can attribute to a specific build is not a measurement.')
}
Write-Note "PresentMon pinned hash OK ($script:PinnedName)"

if (-not (Test-Path -LiteralPath $CaptureHost)) {
    Deny "FrameLedger.CaptureHost.exe is not at $CaptureHost" @('build it: ./build.ps1 managed')
}
if (-not (Test-Path -LiteralPath $Exe)) { Deny "the game executable does not exist: $Exe" }

$leaf = Split-Path -Leaf $Exe
$proc = @(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($leaf)) -ErrorAction SilentlyContinue)
if ($proc.Count -eq 0) {
    Deny "no running process named $leaf." @(
        'Launch the game FIRST, set frame generation to the leg you are taking in the',
        "game's own settings menu, and get into a scene before starting the capture.")
}
Write-Note "target running: $leaf (pid $($proc[0].Id))"
Write-Note "fingerprint: $(Get-ExeFingerprint $Exe)"
Write-Note 'If a later leg refuses with ConsentMissing, compare that fingerprint first --'
Write-Note 'a launcher can update the title between the grant and the capture, and the'
Write-Note 'refusal then says something false (HANDOFF §Traps).'

$state = Get-LegState -Root $OutDir -Name $Leg
if ($state.Taken -and -not $Force) {
    Deny "leg '$Leg' already has both artifacts in $($state.Dir)" @(
        'A leg costs a game launch. Pass -Force only if you mean to discard the one you have.')
}
New-Item -ItemType Directory -Path $state.Dir -Force | Out-Null

Write-Step "capturing $Seconds s — leg '$Leg'"
Write-Note "csv    -> $($state.Csv)"
Write-Note "report -> $($state.Report)"

if (Test-Path -LiteralPath $state.Csv) { Remove-Item -LiteralPath $state.Csv -Force }

# PresentMon first: it should be tracing before the capture host starts drawing
# frames of interest. The two ratios are each INTERNAL to one instrument (§S30's
# own correction), so the spans do not have to align -- which is exactly why
# this comparison survives the circularity that killed the previous oracle.
$pmArgs = @(
    '--process_name', $leaf
    '--v2_metrics'
    '--track_frame_type'
    '--output_file', $state.Csv
    '--timed', [string]($Seconds + 5)
    '--terminate_after_timed'
    '--stop_existing_session'
)
Write-Note "PresentMon: $($pmArgs -join ' ')"
$pm = Start-Process -FilePath $PresentMon -ArgumentList $pmArgs -PassThru -NoNewWindow

try {
    & $CaptureHost capture --exe $Exe --seconds $Seconds 2>&1 |
        Tee-Object -FilePath $state.Report
    $hostExit = $LASTEXITCODE
}
finally {
    if (-not $pm.HasExited) {
        Write-Note 'waiting for PresentMon to finish its timed run...'
        $null = $pm.WaitForExit(20000)
    }
    if (-not $pm.HasExited) { $pm.Kill() }
}

Write-Step "leg '$Leg' collected"
Write-Note "CaptureHost exit: $hostExit"
if (-not (Test-Path -LiteralPath $state.Csv)) {
    Write-Host '   PresentMon wrote NO CSV. That is a finding, not a glitch --' -ForegroundColor Yellow
    Write-Host '   read its console output above before retrying, and note that a retry' -ForegroundColor Yellow
    Write-Host '   needs a NEW GAME LAUNCH (the Overlay self-unhooks 65 s after this host exits).' -ForegroundColor Yellow
}
else {
    $rows = (Get-Content -LiteralPath $state.Csv | Measure-Object -Line).Lines
    Write-Note "csv rows (incl. header): $rows"
}

Write-Host ''
Write-Host 'ONE GAME LAUNCH PER CAPTURE. Close the game before the next leg.' -ForegroundColor Yellow
Write-Note 'When this host exited, nothing publishes guardTicks any more; the Overlay hits'
Write-Note 'FL_GUARD_TICK_DEADLINE_MS and disables every hook ONE-WAY for the life of that'
Write-Note 'process. A second capture against the same running game returns SupervisionLost'
Write-Note 'with zero records. Designed behaviour (19_SAFETY §During a session), not a defect.'
Write-Host ''
Write-Note "Legs still to take: $((@('off','x2','x4') | Where-Object { -not (Get-LegState -Root $OutDir -Name $_).Taken }) -join ', ')"
Write-Note 'Then: ./tools/s31-capture.ps1 -Verify'
