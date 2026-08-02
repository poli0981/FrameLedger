#Requires -Version 7.0
<#
.SYNOPSIS
    Licence guard. Fails the build on a licensing regression.

.DESCRIPTION
    Two checks, both from docs/18_GPU_VENDOR_APIS.md and
    legal/THIRD_PARTY_NOTICES.md:

    1. No Intel IGCL or AMD ADLX material anywhere in the tree.

       These are rejected on licence grounds, not preference. IGCL ships under
       the Intel Software License Agreement, which permits use "solely for use
       on Intel platforms", imposes broad indemnification, and adds its own
       termination trigger — each a further restriction on downstream
       recipients that GPL-3.0 §10 forbids and §7 does not permit.

       Re-declaring the API by hand is explicitly NOT an approved workaround:
       struct layouts must match byte for byte, so an "independent"
       implementation is indistinguishable from a copy.

       Licensing regressions are silent and hard to unwind later. That is why
       this runs at PR time rather than at release.

    2. Every vendored third-party component has a licence copy in
       legal/licenses/.

    Exit 0 clean, 1 on any violation.
#>
[CmdletBinding()]
param([string]$RepoRoot = (Split-Path $PSScriptRoot -Parent))

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$violations = [System.Collections.Generic.List[string]]::new()

# --- 1. Rejected vendor SDKs ------------------------------------------------
# Distinctive identifiers, not bare product names: "adlx" alone would match
# prose in the very docs that explain why we do not use it.
$forbidden = @(
    @{ Pattern = 'ctl_api\.h|ctlApiInit|CTL_RESULT_SUCCESS|igcl_api'; Name = 'Intel IGCL' }
    @{ Pattern = 'IADLXHelper|ADLXHelper\.h|adlx\.h|ADLX_RESULT'; Name = 'AMD ADLX' }
    @{ Pattern = 'adl_sdk\.h|ADL2_Main_Control_Create'; Name = 'AMD ADL' }
)

$scanRoots = @('src', 'tools', 'tests') |
    ForEach-Object { Join-Path $RepoRoot $_ } |
    Where-Object { Test-Path $_ }

if ($scanRoots) {
    $files = Get-ChildItem -Path $scanRoots -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Extension -in '.c', '.cpp', '.h', '.hpp', '.cs', '.txt', '.json', '.props', '.targets' -and
            $_.FullName -notmatch '[\\/](bin|obj|build|TestResults)[\\/]'
        }

    foreach ($rule in $forbidden) {
        $hits = $files | Select-String -Pattern $rule.Pattern -List -ErrorAction SilentlyContinue
        foreach ($hit in $hits) {
            $rel = [IO.Path]::GetRelativePath($RepoRoot, $hit.Path)
            $violations.Add("$($rule.Name) material found: ${rel}:$($hit.LineNumber)")
        }
    }
}

# --- 2. Vendored components have licence copies -----------------------------
# Keyed by the directory that would exist if the component were vendored, so a
# missing licence is only reported once the component is actually present.
$vendored = @(
    @{ Path = 'src/native/third_party/minhook'; Licence = 'minhook-BSD-2-Clause.txt'; Name = 'MinHook' }
    @{ Path = 'src/native/third_party/nvapi'; Licence = 'nvapi-MIT.txt'; Name = 'NVIDIA NVAPI SDK' }
)

$licenceDir = Join-Path $RepoRoot 'legal/licenses'
foreach ($v in $vendored) {
    if (Test-Path (Join-Path $RepoRoot $v.Path)) {
        $copy = Join-Path $licenceDir $v.Licence
        if (-not (Test-Path $copy)) {
            $violations.Add("$($v.Name) is vendored at $($v.Path) but legal/licenses/$($v.Licence) is missing")
        }
    }
}

# NVAPI's SPDX headers must survive vendoring — the MIT grant travels with them.
$nvapiDir = Join-Path $RepoRoot 'src/native/third_party/nvapi'
if (Test-Path $nvapiDir) {
    $headers = Get-ChildItem $nvapiDir -Recurse -Filter *.h -ErrorAction SilentlyContinue
    $stripped = $headers | Where-Object {
        -not (Select-String -Path $_.FullName -Pattern 'SPDX-License-Identifier:\s*MIT' -Quiet)
    }
    foreach ($h in $stripped) {
        $violations.Add("NVAPI header lost its SPDX-License-Identifier: MIT block: $($h.Name)")
    }
}

# --- Report -----------------------------------------------------------------
if ($violations.Count -gt 0) {
    Write-Host 'LICENCE CHECK FAILED' -ForegroundColor Red
    $violations | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ''
    Write-Host 'See docs/18_GPU_VENDOR_APIS.md §Vendor SDKs we deliberately do not use.' -ForegroundColor Red
    exit 1
}

Write-Host 'licence check OK — no rejected vendor SDK material, vendored licences present' -ForegroundColor Green
exit 0
