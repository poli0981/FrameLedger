#Requires -Version 7.0
<#
.SYNOPSIS
    The accuracy blocks in README.md and legal/DISCLAIMER.md are one text, kept in
    legal/ACCURACY.md, and this gate fails when any copy has drifted from it.

.DESCRIPTION
    docs/20_OPEN_QUESTIONS.md §S23-6: the accuracy blocks were hand-maintained prose
    that nothing verified, and DISCLAIMER's went stale four times in one direction
    and once in the other. Single-sourcing them does not make them true — that is
    still a human's job, dated in the block itself — but it makes there be exactly
    one place to get them wrong, and a build that notices when a copy is not that
    place.

    Each target file embeds the block between
        <!-- accuracy-block:begin -->
        <!-- accuracy-block:end -->
    and the text between the markers must equal legal/ACCURACY.md with its own
    `<!-- … -->` comment lines removed, after line-ending normalisation and
    trimming. Missing markers fail; a second pair of markers fails; whitespace-only
    drift is tolerated because CRLF/LF and a trailing newline are not what this
    gate is for.

    -SelfTest proves the gate discriminates, both directions, on temporary copies:
    the shipped files pass, a one-character mutation inside a block fails, and a
    file with its markers removed fails. A gate that has never been red is a gate
    nobody has seen work.
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent),
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$beginMarker = '<!-- accuracy-block:begin -->'
$endMarker = '<!-- accuracy-block:end -->'

function Get-CanonicalBlock([string]$Text) {
    # Whole HTML comments go, wherever their lines start: the source file opens with a
    # multi-line comment explaining itself, and a copy may carry the markers' own.
    $stripped = [regex]::Replace(($Text -replace "`r`n", "`n"), '(?s)<!--.*?-->', '')
    $kept = foreach ($l in ($stripped -split "`n")) { $l.TrimEnd() }
    return (($kept -join "`n").Trim())
}

# Returns $null when the file embeds exactly one block, else the reason it does not.
function Test-Embedded([string]$Path, [string]$Expected) {
    if (-not (Test-Path $Path)) { return "missing file $Path" }
    $text = Get-Content $Path -Raw
    $begins = [regex]::Matches($text, [regex]::Escape($beginMarker)).Count
    $ends = [regex]::Matches($text, [regex]::Escape($endMarker)).Count
    if ($begins -ne 1 -or $ends -ne 1) {
        return "$Path must embed the block exactly once (begin markers: $begins, end markers: $ends)"
    }
    $start = $text.IndexOf($beginMarker) + $beginMarker.Length
    $stop = $text.IndexOf($endMarker)
    if ($stop -lt $start) { return "$Path has its end marker before its begin marker" }
    $inner = Get-CanonicalBlock $text.Substring($start, $stop - $start)
    if ($inner -ne $Expected) {
        return "$Path's accuracy block differs from legal/ACCURACY.md — edit the source and re-embed, never the copy"
    }
    return $null
}

function Invoke-Check([string]$Root) {
    $source = Join-Path $Root 'legal/ACCURACY.md'
    if (-not (Test-Path $source)) { return @("legal/ACCURACY.md is missing") }
    $expected = Get-CanonicalBlock (Get-Content $source -Raw)
    if ($expected.Length -eq 0) { return @("legal/ACCURACY.md carries no block (only comments)") }
    $problems = @()
    foreach ($rel in 'README.md', 'legal/DISCLAIMER.md') {
        $why = Test-Embedded (Join-Path $Root $rel) $expected
        if ($null -ne $why) { $problems += $why }
    }
    return $problems
}

if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("fl-accuracy-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path (Join-Path $tmp 'legal') -Force | Out-Null
    try {
        foreach ($rel in 'legal/ACCURACY.md', 'README.md', 'legal/DISCLAIMER.md') {
            Copy-Item (Join-Path $RepoRoot $rel) (Join-Path $tmp $rel)
        }
        $cases = 0
        # 1. The shipped files pass.
        $p = @(Invoke-Check $tmp)
        if ($p.Count -ne 0) { throw "self-test: the shipped files should pass, got: $($p -join '; ')" }
        $cases++
        # 2. One character changed inside README's block fails, naming README.
        $readme = Join-Path $tmp 'README.md'
        $orig = Get-Content $readme -Raw
        $s = $orig.IndexOf($beginMarker) + $beginMarker.Length + 40
        $mutated = $orig.Substring(0, $s) + 'X' + $orig.Substring($s + 1)
        Set-Content $readme $mutated -NoNewline
        $p = @(Invoke-Check $tmp)
        if ($p.Count -ne 1 -or $p[0] -notmatch 'README\.md') { throw "self-test: a mutated README block should fail naming README, got: $($p -join '; ')" }
        $cases++
        Set-Content $readme $orig -NoNewline
        # 3. Markers removed from DISCLAIMER fails, naming it.
        $disc = Join-Path $tmp 'legal/DISCLAIMER.md'
        $origD = Get-Content $disc -Raw
        Set-Content $disc ($origD -replace [regex]::Escape($beginMarker), '') -NoNewline
        $p = @(Invoke-Check $tmp)
        if ($p.Count -ne 1 -or $p[0] -notmatch 'DISCLAIMER\.md') { throw "self-test: a DISCLAIMER without markers should fail naming it, got: $($p -join '; ')" }
        $cases++
        Set-Content $disc $origD -NoNewline
        # 4. A source with only comments fails rather than matching an empty block.
        Set-Content (Join-Path $tmp 'legal/ACCURACY.md') "<!-- nothing -->`n" -NoNewline
        $p = @(Invoke-Check $tmp)
        if ($p.Count -ne 1 -or $p[0] -notmatch 'no block') { throw "self-test: an empty source should fail, got: $($p -join '; ')" }
        $cases++
        Write-Host "accuracy-check self-test OK - $cases cases, both directions"
    }
    finally {
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    exit 0
}

$problems = @(Invoke-Check $RepoRoot)
if ($problems.Count -ne 0) {
    Write-Host 'ACCURACY CHECK FAILED:' -ForegroundColor Red
    foreach ($p in $problems) { Write-Host "  $p" -ForegroundColor Red }
    Write-Host '  legal/ACCURACY.md is the source; README.md and legal/DISCLAIMER.md embed it between the markers.' -ForegroundColor Red
    exit 1
}
Write-Host 'accuracy-check OK - README.md and legal/DISCLAIMER.md carry legal/ACCURACY.md verbatim'
exit 0
