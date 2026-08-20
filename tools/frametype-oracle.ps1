#Requires -Version 7.0
<#
.SYNOPSIS
    Compares PresentMon 2.x's FrameType classification against FrameLedger's
    presents-per-batch proxy, as two DIMENSIONLESS RATIOS.

.DESCRIPTION
    HANDOFF item 3 rests on a premise nothing in this repository has verified: that
    a drained Streamline batch IS an application frame. Three oracles have already
    been tried and all three fell (20_OPEN_QUESTIONS S30) -- fl-baseline-probe by
    its own written falsifier, and two readings of an in-process present overlay.
    PresentMon 2.x's FrameType column was pre-committed as the next measurement
    because it classifies each present from ETW and divides by nothing.

    RATIOS, NOT RATES, and that is the correction S30 records rather than a style
    choice. Comparing rates needs a shared span, the span is where the previous
    draft went circular, and each ratio here is internal to one instrument:
    PresentMon's `displayed / application` against our `presents / batch`. Neither
    side needs to know when the other started.

    WHAT THIS SCRIPT HAS NEVER SEEN, stated first because it is the largest
    weakness. No real PresentMon CSV has ever been fed to it. On the machine it
    was written on, PresentMon 2.5.1 refuses to start a trace session at all --
    exit 6, "PresentMon requires either administrative privileges or to be run by
    a user in the Performance Log Users user group" -- so the 2.x column set and
    the FrameType vocabulary are UNMEASURED (spike-notes.md S11). Everything below
    is therefore written to fail LOUDLY on anything it does not recognise rather
    than to guess:

      - Columns are resolved BY NAME from the header, never by position.
      - A missing FrameType column is a refusal that prints the columns it did
        find. 03_METRICS S13 already specifies exactly this: "reports an explicit
        capability loss when FrameType is absent rather than silently reporting
        fg_mode = none".
      - The FrameType VOCABULARY is not assumed. The script prints every distinct
        value it saw, and counts as application frames only the rows spelled
        `Application`. If nothing is spelled that way it refuses instead of
        dividing by zero -- a wrong spelling must not become a wrong number.
      - It does NOT implement RFC-4180 quoting. A quoted comma would misalign the
        split, the FrameType column would not be found by name, and the script
        would refuse. That is the safe direction and it is why the name lookup
        happens after the split rather than before.

    AND THE FALSIFIER, WRITTEN BEFORE THE RUN. `--track_frame_type` is a BETA
    option in 2.5.1 and its own help says it "requires application and/or driver
    instrumentation using Intel-PresentMon provider". So it is not a general
    classification of any present: it depends on the vendor emitting events. If a
    DLSS-G capture comes back with no FrameType column, or with every row spelled
    `Application` while frame generation was demonstrably on, then PresentMon is
    NOT an application-frame oracle for NVIDIA frame generation and must be
    retired in the same S31 row rather than quietly relied on -- exactly as
    fl-baseline-probe was. This script reports that case as a RETIREMENT and never
    as a ratio of 1.0.

.PARAMETER Csv
    Path to a PresentMon 2.x CSV (`--output_file`, `--v2_metrics
    --track_frame_type`).

.PARAMETER Report
    Optional. Path to a saved FrameLedger.CaptureHost report. The script reads the
    `presents/batch=` figure out of it so the two ratios can be printed together.

.PARAMETER SelfTest
    Runs the decision table below in BOTH directions and exits non-zero if any
    case comes out wrong. build.ps1 runs this on every check, so the one thing
    standing behind an unvalidated parser is itself proven able to go red.
#>
[CmdletBinding(DefaultParameterSetName = 'Compare')]
param(
    [Parameter(ParameterSetName = 'Compare', Mandatory = $true)]
    [string]$Csv,

    [Parameter(ParameterSetName = 'Compare')]
    [string]$Report,

    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The column the whole comparison rests on, and the one value that means "the
# application produced this frame". Named once so the self-test and the live path
# cannot disagree about the spelling under test.
$script:FrameTypeColumn = 'FrameType'
$script:ApplicationValue = 'Application'

<#
    Turns CSV text into a census, or into the reason there is none.

    Deliberately NOT a boolean and never a bare number: "no FrameType column",
    "no rows at all" and "every row is an application frame" are three different
    findings, and only the last is a measurement. Collapsing them is how a
    capability loss becomes a factor of 1.0.
#>
function New-Census {
    # ONE SHAPE FOR BOTH OUTCOMES. Set-StrictMode turns a missing key into a
    # terminating error, so a refusal that omitted the numeric fields would make
    # every caller's `$c.Refusal` test throw before it could read the refusal --
    # a check that dies before reaching the thing it checks, which is the shape
    # gates-that-cannot-fail is about.
    #
    # $Refusal IS DELIBERATELY UNTYPED. Declaring it [string] makes PowerShell
    # coerce $null to the empty string, so `$null -eq $c.Refusal` is false on a
    # SUCCESSFUL census and every caller reads a success as a refusal. Measured
    # here, not reasoned about: the four success cases in the table below went red
    # while every refusal case stayed green, which is the signature.
    param(
        $Refusal = $null,
        [int]$Total = 0,
        [int]$Application = 0,
        [AllowNull()]$ByType = $null
    )
    return @{
        Refusal        = $Refusal
        Total          = $Total
        Application    = $Application
        Generated      = $Total - $Application
        ByType         = $(if ($null -eq $ByType) { [ordered]@{} } else { $ByType })
        Ratio          = $(if ($Application -gt 0) { $Total / $Application } else { $null })
        AllApplication = ($Application -gt 0 -and $Application -eq $Total)
    }
}

function Get-FrameTypeCensus {
    param([AllowNull()][string[]]$Lines)

    $rows = @()
    if ($null -ne $Lines) {
        $rows = @($Lines | Where-Object { $_ -ne $null -and $_.Trim() -ne '' })
    }

    # FAIL-CLOSED ON EMPTY. No header is not "a clean capture"; it means the
    # caller's file is not what they think it is.
    if ($rows.Count -eq 0) {
        return New-Census -Refusal 'the CSV is empty — PresentMon wrote no header, so nothing was captured'
    }

    $header = @($rows[0].Split(',') | ForEach-Object { $_.Trim().Trim('"') })
    $index = -1
    for ($i = 0; $i -lt $header.Count; $i++) {
        if ($header[$i] -ieq $script:FrameTypeColumn) { $index = $i; break }
    }

    if ($index -lt 0) {
        return New-Census -Refusal ("no '$($script:FrameTypeColumn)' column — this is a CAPABILITY LOSS, not a " +
            "measurement. --track_frame_type is a beta option and needs vendor instrumentation of the " +
            "Intel-PresentMon provider. Columns present: $($header -join ', ')")
    }

    if ($rows.Count -lt 2) {
        return New-Census -Refusal 'the CSV has a header and no rows — no present was recorded'
    }

    $byType = [ordered]@{}
    $total = 0
    foreach ($line in $rows[1..($rows.Count - 1)]) {
        $fields = @($line.Split(','))
        if ($fields.Count -le $index) { continue }
        $value = $fields[$index].Trim().Trim('"')
        if ($value -eq '') { $value = '<blank>' }
        if (-not $byType.Contains($value)) { $byType[$value] = 0 }
        $byType[$value] = [int]$byType[$value] + 1
        $total++
    }

    if ($total -eq 0) {
        return New-Census -Refusal 'every row was shorter than the FrameType column — the split did not line up'
    }

    $application = 0
    foreach ($key in $byType.Keys) {
        if ($key -ieq $script:ApplicationValue) { $application += [int]$byType[$key] }
    }

    # A WRONG SPELLING MUST NOT BECOME A WRONG NUMBER. The vocabulary is
    # unmeasured, so a zero here means "we do not recognise what this build emits"
    # far more likely than "this title produced no application frames".
    if ($application -eq 0) {
        return New-Census -Refusal ("no row is spelled '$($script:ApplicationValue)', so the denominator would " +
            "be zero. The values this capture actually contains are: $($byType.Keys -join ', ')") -ByType $byType
    }

    # AllApplication is THE FALSIFIER, carried as a flag rather than as a ratio of 1.0.
    return New-Census -Total $total -Application $application -ByType $byType
}

<#
    Pulls `presents/batch=N` out of a CaptureHost report. Returns $null when the
    report does not carry one, which is a real state: the host prints it only when
    a batch was drained.
#>
function Get-PresentsPerBatch {
    param([AllowNull()][string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    $m = [regex]::Match($Text, 'presents/batch=\s*([0-9]+(?:\.[0-9]+)?)')
    if (-not $m.Success) { return $null }
    return [double]$m.Groups[1].Value
}

# --- Self-test ---------------------------------------------------------------

function Invoke-SelfTest {
    $cases = @(
        @{ Name = 'a FrameType column with generated frames yields the ratio'
           Lines = @('SwapChain,FrameType,MsBetweenPresents',
                     'a,Application,1', 'a,Repeated,1', 'a,Application,1', 'a,Repeated,1')
           Expect = { param($r) $null -eq $r.Refusal -and $r.Total -eq 4 -and $r.Application -eq 2 -and
                                [math]::Abs($r.Ratio - 2.0) -lt 1e-9 -and -not $r.AllApplication } }

        @{ Name = 'the column is found BY NAME wherever it sits'
           Lines = @('MsBetweenPresents,SwapChain,FrameType',
                     '1,a,Application', '1,a,Repeated', '1,a,Repeated', '1,a,Repeated')
           Expect = { param($r) $null -eq $r.Refusal -and [math]::Abs($r.Ratio - 4.0) -lt 1e-9 } }

        @{ Name = 'an UNKNOWN generated spelling still counts as generated'
           Lines = @('FrameType', 'Application', 'Intel XEFG', 'Intel XEFG', 'AMD AFMF')
           Expect = { param($r) $null -eq $r.Refusal -and [math]::Abs($r.Ratio - 4.0) -lt 1e-9 -and
                                $r.ByType.Contains('Intel XEFG') } }

        @{ Name = 'NO FrameType column is a refusal that names the columns it saw'
           Lines = @('SwapChain,MsBetweenPresents', 'a,1', 'a,1')
           Expect = { param($r) $r.Refusal -match 'CAPABILITY LOSS' -and $r.Refusal -match 'SwapChain' } }

        @{ Name = 'every row Application fires the FALSIFIER rather than reporting 1.0'
           Lines = @('FrameType', 'Application', 'Application', 'Application')
           Expect = { param($r) $null -eq $r.Refusal -and $r.AllApplication -and $r.Generated -eq 0 } }

        @{ Name = 'a vocabulary with no Application refuses instead of dividing by zero'
           Lines = @('FrameType', 'Repeated', 'Repeated')
           Expect = { param($r) $r.Refusal -match 'denominator would be zero' -and $r.Refusal -match 'Repeated' } }

        @{ Name = 'a header with no rows is a refusal, not a clean capture'
           Lines = @('SwapChain,FrameType')
           Expect = { param($r) $r.Refusal -match 'no rows' } }

        @{ Name = 'an EMPTY file fails closed'
           Lines = @()
           Expect = { param($r) $r.Refusal -match 'empty' } }

        @{ Name = 'a null input fails closed too'
           Lines = $null
           Expect = { param($r) $r.Refusal -match 'empty' } }
    )

    $failed = 0
    foreach ($c in $cases) {
        $result = Get-FrameTypeCensus -Lines $c.Lines
        $ok = & $c.Expect $result
        Write-Host ("  {0}  {1}" -f $(if ($ok) { 'ok  ' } else { 'FAIL' }), $c.Name)
        if (-not $ok) { $failed++ }
    }

    # The report side, both directions.
    $reportCases = @(
        @{ Name = 'presents/batch is read out of a report line'
           Text = '    FG counts: presents=100 batches=25 span=1.2s  presents/batch=4  evaluations/batch=N/A'
           Expect = { param($v) $null -ne $v -and [math]::Abs($v - 4.0) -lt 1e-9 } }
        @{ Name = 'a report WITHOUT one yields null rather than a default'
           Text = '    FG counts: presents=100 batches=0'
           Expect = { param($v) $null -eq $v } }
        @{ Name = 'an empty report yields null'
           Text = ''
           Expect = { param($v) $null -eq $v } }
    )
    foreach ($c in $reportCases) {
        $value = Get-PresentsPerBatch -Text $c.Text
        $ok = & $c.Expect $value
        Write-Host ("  {0}  {1}" -f $(if ($ok) { 'ok  ' } else { 'FAIL' }), $c.Name)
        if (-not $ok) { $failed++ }
    }

    $count = $cases.Count + $reportCases.Count
    if ($failed -gt 0) {
        Write-Host "frametype-oracle self-test: $failed of $count case(s) FAILED" -ForegroundColor Red
        exit 1
    }
    Write-Host "frametype-oracle self-test: $count cases, both directions"
    exit 0
}

if ($SelfTest) { Invoke-SelfTest }

# --- Live comparison ---------------------------------------------------------

if (-not (Test-Path -LiteralPath $Csv)) {
    Write-Host "FAILED: no such CSV: $Csv" -ForegroundColor Red
    exit 2
}

$census = Get-FrameTypeCensus -Lines (Get-Content -LiteralPath $Csv)

Write-Host ''
Write-Host "PresentMon CSV: $Csv"

if ($null -ne $census.Refusal) {
    Write-Host "  NOT AN ORACLE: $($census.Refusal)" -ForegroundColor Yellow
    Write-Host '  Record this in 20_OPEN_QUESTIONS S31 as a retirement, in the same row, rather than'
    Write-Host '  re-running until it answers. That is what fl-baseline-probe earned.'
    exit 3
}

Write-Host "  frames classified : $($census.Total)"
foreach ($key in $census.ByType.Keys) {
    Write-Host ("    {0,-24} {1}" -f $key, $census.ByType[$key])
}
Write-Host "  displayed / application = $([math]::Round($census.Ratio, 4))"

if ($census.AllApplication) {
    Write-Host ''
    Write-Host '  FALSIFIER FIRED: every classified frame is an application frame.' -ForegroundColor Yellow
    Write-Host '  If frame generation was demonstrably ON for this capture, PresentMon did not see it,'
    Write-Host '  and it is NOT an application-frame oracle for this vendor. --track_frame_type is a beta'
    Write-Host '  option that needs the vendor to instrument the Intel-PresentMon provider. Retire it in'
    Write-Host '  S31 rather than reading 1.0 as a measurement.'
    exit 4
}

if ($PSBoundParameters.ContainsKey('Report')) {
    if (-not (Test-Path -LiteralPath $Report)) {
        Write-Host "FAILED: no such report: $Report" -ForegroundColor Red
        exit 2
    }
    $ours = Get-PresentsPerBatch -Text (Get-Content -LiteralPath $Report -Raw)
    Write-Host ''
    Write-Host "FrameLedger report: $Report"
    if ($null -eq $ours) {
        Write-Host '  the report carries no presents/batch — nothing to compare against' -ForegroundColor Yellow
        exit 5
    }
    Write-Host "  presents / batch        = $ours"
    $delta = [math]::Abs($census.Ratio - $ours) / $ours * 100.0
    Write-Host ''
    Write-Host ("  TWO DIMENSIONLESS RATIOS: {0:N4} against {1:N4} — {2:N2}% apart." -f $census.Ratio, $ours, $delta)
    Write-Host '  Neither needs a shared span, and neither is derived from the other. If they agree at'
    Write-Host '  THREE settings, a drained Streamline batch is an application frame; if they agree only'
    Write-Host '  at x2, the rival that PresentMon is halving something is not excluded (S30).'
}

exit 0
