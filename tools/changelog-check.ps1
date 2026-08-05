#Requires -Version 7.0
<#
.SYNOPSIS
    Fails a pull request that changes shipped source without changing CHANGELOG.md.

.DESCRIPTION
    PRs #40–#44 landed with no changelog entry. #45 wrote them retrospectively and
    said so at length, in prose, at the top of the file. Then #46 through #52
    landed with no changelog entry either — seven more, including the C# struct
    mirror, the ring reader, the handshake validator, the closed write-read loop
    and check 3's call site.

    That is the finding this script exists for: a retrospective note is not a
    mechanism. The repository already had the sentence asking people to remember,
    and the sentence is what failed, twice, in one week.

    WHAT IT DOES NOT DO. It cannot judge whether an entry is any good, or whether
    it describes the change that was actually made. It asserts one mechanical
    thing: that a diff touching src/ also touched the ledger. A one-word entry
    satisfies it. That is stated here rather than left for someone to discover,
    because a gate whose write-up claims more than it covers is this project's
    own recurring defect (20_OPEN_QUESTIONS, gates-that-cannot-fail).

    FAIL-CLOSED ON AN EMPTY LIST. A pull request with no changed files is not a
    thing; an empty list means the caller's query broke. Reporting "no src/
    changes, therefore fine" on an empty list would be a verdict decided before
    the check looked, so an empty list is a failure instead.

.PARAMETER ChangedFiles
    Repository-relative paths, as GitHub reports them for the pull request. The
    caller supplies them; this script does no git or API work of its own, so the
    logic below is drivable from -SelfTest without a network or a repository.

.PARAMETER SelfTest
    Runs the decision table below in BOTH directions and exits non-zero if any
    case comes out wrong. build.ps1 runs this on every check, so the gate's
    ability to go red is proven on every build rather than assumed — the same
    discipline tools/rules-validate.ps1 applies to its schema canary.
#>
[CmdletBinding(DefaultParameterSetName = 'Check')]
param(
    [Parameter(ParameterSetName = 'Check', Mandatory = $true)]
    [AllowEmptyCollection()]
    [string[]]$ChangedFiles,

    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The ledger this gate protects. Kept as a variable so the self-test and the real
# path cannot disagree about which file counts.
$script:LedgerPath = 'CHANGELOG.md'

<#
    Returns $null when the diff is acceptable, or the reason it is not.

    Deliberately NOT a boolean: "the changelog is missing" and "the file list is
    empty, so nothing was checked" are different failures, and a caller that
    could only see true/false would report the second as the first.
#>
function Test-ChangelogAccompaniesSource {
    param([AllowEmptyCollection()][string[]]$Files)

    # Trim BEFORE filtering, not after. A whitespace-only string is truthy in
    # PowerShell, so `Where-Object { $_ }` keeps '   ' and the count comes out 1 —
    # which made the empty-list refusal above unreachable for that input. Caught
    # by -SelfTest's last case on its first run, which is what that case is for.
    $normalised = @(
        $Files |
            ForEach-Object { if ($null -eq $_) { '' } else { $_.Replace('\', '/').Trim() } } |
            Where-Object { $_.Length -gt 0 }
    )

    if ($normalised.Count -eq 0) {
        return 'the changed-file list is EMPTY. A pull request with no files is not a thing, so this is a broken query, not a clean diff — refusing to report a verdict on a list nobody produced.'
    }

    $source = @($normalised | Where-Object { $_ -like 'src/*' })
    if ($source.Count -eq 0) {
        return $null
    }

    if ($normalised -contains $script:LedgerPath) {
        return $null
    }

    $shown = ($source | Select-Object -First 8) -join ', '
    if ($source.Count -gt 8) { $shown += ", and $($source.Count - 8) more" }
    return "this diff changes $($source.Count) file(s) under src/ and does not touch $($script:LedgerPath): $shown"
}

if ($SelfTest) {
    # Each case names what it would catch. A table that only asserted the green
    # cases would pass against a function that always returned $null.
    $cases = @(
        @{ Name = 'source with a ledger entry passes'; Files = @('src/FrameLedger.Agent/Program.cs', 'CHANGELOG.md'); ExpectFailure = $false }
        @{ Name = 'source WITHOUT a ledger entry fails'; Files = @('src/FrameLedger.Agent/Program.cs'); ExpectFailure = $true }
        @{ Name = 'native source counts as source'; Files = @('src/native/FrameLedger.Overlay/src/dllmain.cpp'); ExpectFailure = $true }
        @{ Name = 'docs-only passes'; Files = @('docs/15_ROADMAP.md', 'README.md'); ExpectFailure = $false }
        @{ Name = 'tests-only passes'; Files = @('tests/FrameLedger.Domain.Tests/Foo.cs'); ExpectFailure = $false }
        @{ Name = 'a path merely CONTAINING src/ does not count'; Files = @('docs/src/notes.md'); ExpectFailure = $false }
        @{ Name = 'backslashes are normalised, not missed'; Files = @('src\FrameLedger.Agent\Program.cs'); ExpectFailure = $true }
        @{ Name = 'an empty list FAILS rather than reading as clean'; Files = @(); ExpectFailure = $true }
        @{ Name = 'a list of blanks is an empty list'; Files = @('', '   '); ExpectFailure = $true }
    )

    $failures = 0
    foreach ($case in $cases) {
        $reason = Test-ChangelogAccompaniesSource -Files $case.Files
        $failed = $null -ne $reason
        if ($failed -eq $case.ExpectFailure) {
            Write-Host "  ok    $($case.Name)" -ForegroundColor DarkGray
        }
        else {
            $expected = if ($case.ExpectFailure) { 'a failure' } else { 'a pass' }
            Write-Host "  FAIL  $($case.Name) — expected $expected, got '$reason'" -ForegroundColor Red
            $failures++
        }
    }

    if ($failures -gt 0) {
        Write-Host "CHANGELOG CHECK SELF-TEST FAILED: $failures of $($cases.Count) cases" -ForegroundColor Red
        exit 1
    }

    Write-Host "changelog-check self-test: $($cases.Count) cases, both directions" -ForegroundColor Green
    exit 0
}

$reason = Test-ChangelogAccompaniesSource -Files $ChangedFiles
if ($null -ne $reason) {
    Write-Host "CHANGELOG CHECK FAILED: $reason" -ForegroundColor Red
    Write-Host ''
    Write-Host '  Add an entry to CHANGELOG.md under [Unreleased].' -ForegroundColor Red
    Write-Host '  This exists because #40-#44 and then #46-#52 each landed with none,' -ForegroundColor Red
    Write-Host '  and the second time the file already carried a note asking for one.' -ForegroundColor Red
    exit 1
}

Write-Host "changelog-check: ok ($($ChangedFiles.Count) changed file(s))" -ForegroundColor Green
exit 0
