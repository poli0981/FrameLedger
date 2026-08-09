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
    @{ Path = 'src/native/third_party/nvapi'; Licence = 'nvapi-MIT.txt'; Name = 'NVIDIA NVAPI SDK' },

    # NVIDIA Streamline headers. MIT, verified against upstream license.txt on
    # 2026-08-09 — none of docs/18_GPU_VENDOR_APIS.md §Checklist step 2's needles,
    # so step 1 terminates on a pass. NOT to be confused with NVIDIA NGX/DLSS,
    # which is the proprietary RTX SDKs Licence and may be neither vendored nor
    # re-declared; sl.common.dll exporting NGX-named symbols is what makes that
    # confusion easy.
    @{ Path = 'src/native/third_party/streamline'; Licence = 'streamline-MIT.txt'; Name = 'NVIDIA Streamline headers' },

    # Khronos Vulkan headers, copied from SDK 1.4.357.0 rather than fetched, so
    # CI needs no ~1 GB SDK install and we compile against the same revision the
    # blast-radius test runs against. Apache-2.0 OR MIT; we ship the Apache text.
    @{ Path = 'src/native/third_party/vulkan-headers'; Licence = 'apache-2.0.txt'; Name = 'Khronos Vulkan headers' }
)

$licenceDir = Join-Path $RepoRoot 'legal/licenses'

# MinHook arrives via FetchContent, so no directory appears under third_party/
# to key on — but its code is compiled into a DLL we ship, which is exactly
# when BSD-2-Clause requires the notice to travel with the binary. Key on the
# CMake declaration instead of a path that will never exist.
$fetched = @(
    @{ Marker = 'src/native/third_party/CMakeLists.txt'; Needle = 'minhook'
       Licence = 'minhook-BSD-2-Clause.txt'; Name = 'MinHook' },

    # jsmn is compiled into FrameLedger.Injector, which the Agent links and we
    # ship — so MIT's notice requirement travels with the binary, same as
    # MinHook's.
    @{ Marker = 'src/native/third_party/CMakeLists.txt'; Needle = 'jsmn'
       Licence = 'jsmn-MIT.txt'; Name = 'jsmn' },

    # Catch2 is test-only and never shipped. Listed anyway: BSL-1.0 costs
    # nothing to honour, and a dependency that is "not shipped today" is one
    # refactor away from being shipped tomorrow.
    @{ Marker = 'src/native/third_party/CMakeLists.txt'; Needle = 'catch2'
       Licence = 'catch2-BSL-1.0.txt'; Name = 'Catch2' }
)
foreach ($f in $fetched) {
    $marker = Join-Path $RepoRoot $f.Marker
    if ((Test-Path $marker) -and (Select-String -Path $marker -Pattern $f.Needle -Quiet)) {
        $copy = Join-Path $licenceDir $f.Licence
        if (-not (Test-Path $copy)) {
            $violations.Add("$($f.Name) is built into a shipped binary but legal/licenses/$($f.Licence) is missing")
        }
    }
}

foreach ($v in $vendored) {
    if (Test-Path (Join-Path $RepoRoot $v.Path)) {
        $copy = Join-Path $licenceDir $v.Licence
        if (-not (Test-Path $copy)) {
            $violations.Add("$($v.Name) is vendored at $($v.Path) but legal/licenses/$($v.Licence) is missing")
        }
    }
}

# --- 2b. Packages whose licence text WE are obliged to ship ------------------
# Some NuGet packages declare an SPDX expression and ship no licence file of
# their own. LibreHardwareMonitorLib 0.9.6 is one: verified by unpacking the
# .nupkg — 46 entries, no licence file. MPL-2.0 §3.1 makes distributing the
# text our obligation, not a courtesy, so the copy in legal/licenses/ is
# load-bearing and must not be deleted as "unused".
$packagesProps = Join-Path $RepoRoot 'Directory.Packages.props'
if (Test-Path $packagesProps) {
    $props = Get-Content $packagesProps -Raw
    $obliged = @(
        @{ Package = 'LibreHardwareMonitorLib'; Licence = 'mpl-2.0.txt'; Spdx = 'MPL-2.0' }
    )
    foreach ($o in $obliged) {
        if ($props -match [regex]::Escape($o.Package)) {
            $copy = Join-Path $licenceDir $o.Licence
            if (-not (Test-Path $copy)) {
                $violations.Add("$($o.Package) is a dependency and its package ships no licence file; $($o.Spdx) requires legal/licenses/$($o.Licence) to be distributed with the app")
            }
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

# --- 2b. Streamline: the grant, and the two things its own licence file excludes
#
# NOT an SPDX check. Streamline carries no SPDX line anywhere — the grant lives
# as comment text in license.txt, and two of the vendored headers
# (sl_appidentity.h, sl_device_wrappers.h) carry no licence header at all. A
# per-file SPDX grep copied from the NVAPI rule above would fire on those two
# forever, and a check that is always red gets deleted.
#
# THE RISK HERE IS A DIFFERENT ONE, and it is the one worth gating. Streamline's
# include/ and source/ are MIT, but the SAME license.txt carries a second,
# proprietary block — "NSight Perf SDK License, Version 2023.3" — naming
# sl_nvperf.h and sl_nvperf.dll, and upstream's external/ngx-sdk/ is the NVIDIA
# RTX SDKs Licence, which docs/18_GPU_VENDOR_APIS.md §Checklist step 3 forbids
# vendoring outright. Neither is here today. Both arrive the moment somebody
# re-syncs by copying the tree instead of the closure, which is the obvious way
# to do it and the wrong one.
$slDir = Join-Path $RepoRoot 'src/native/third_party/streamline'
if (Test-Path $slDir) {
    $slLicence = Join-Path $slDir 'license.txt'
    if (-not (Test-Path $slLicence)) {
        $violations.Add('Streamline is vendored but src/native/third_party/streamline/license.txt is missing — MIT requires the notice to travel with the copy')
    }
    elseif (-not (Select-String -Path $slLicence -Pattern 'Permission is hereby granted, free of charge' -Quiet)) {
        $violations.Add('src/native/third_party/streamline/license.txt no longer contains the MIT permission grant')
    }

    # Named files, not a glob on the licence text: the licence names them, so we
    # assert against the names it names.
    $nvperf = Get-ChildItem $slDir -Recurse -Filter 'sl_nvperf.*' -ErrorAction SilentlyContinue
    foreach ($f in $nvperf) {
        $violations.Add("Streamline's NSight Perf SDK Licence covers $($f.Name) and it is NOT MIT — it must not be vendored: $($f.Name)")
    }

    $ext = Join-Path $slDir 'external'
    if (Test-Path $ext) {
        $violations.Add("src/native/third_party/streamline/external/ exists — upstream's external/ngx-sdk is the NVIDIA RTX SDKs Licence, which §Checklist step 3 forbids vendoring")
    }
}

# --- 2c. The notices file's BUNDLING claim must match the filesystem ---------
#
# Every other check here is keyed on the directory a component WOULD occupy, so
# it fires on vendored-without-a-licence and is structurally blind to the
# reverse: legal/THIRD_PARTY_NOTICES.md claiming material the tree does not
# contain. Both live claims were false when this was written (2026-08-05) —
# NVAPI ("Yes — headers and import library vendored. Verified 2026-08-02") and
# Intel PresentMon ("Bundled as a pinned native binary; SHA-256 verified at
# build"). Neither directory exists. That is a gate whose verdict is decided
# before it looks, inside the one file the EULA incorporates by reference, and
# shipping a notice for material we do not distribute is over-disclosure — a
# defect in the same way an omission is.
#
# BIDIRECTIONAL ON PURPOSE. A check that only caught "claimed but absent" would
# go quiet the day somebody vendors a component and forgets the notice, which is
# this same defect wearing the other face.
#
# Only TABLE ROWS are read (lines starting with '|'), so the prose accuracy
# block above the table — which necessarily quotes the old wording — cannot
# satisfy or trip the check.
$noticesPath = Join-Path $RepoRoot 'legal/THIRD_PARTY_NOTICES.md'
if (-not (Test-Path $noticesPath)) {
    $violations.Add('legal/THIRD_PARTY_NOTICES.md is missing, so no bundling claim can be checked')
}
else {
    $rows = @(Get-Content $noticesPath | Where-Object { $_.TrimStart().StartsWith('|') })
    $claims = @(
        @{ Marker = 'NVIDIA NVAPI SDK'; Path = 'src/native/third_party/nvapi' },
        @{ Marker = 'Intel PresentMon'; Path = 'assets/native/PresentMon.exe' },
        @{ Marker = 'Vulkan headers'; Path = 'src/native/third_party/vulkan-headers' },
        @{ Marker = 'NVIDIA Streamline'; Path = 'src/native/third_party/streamline' }
    )
    foreach ($c in $claims) {
        $matched = @($rows | Where-Object { $_ -match [regex]::Escape($c.Marker) })
        if ($matched.Count -eq 0) {
            # Fail rather than skip: a renamed row must break the build, not
            # silently retire the check (the rules-validate precedent).
            $violations.Add("No table row in THIRD_PARTY_NOTICES.md mentions '$($c.Marker)', so its bundling claim cannot be checked")
            continue
        }
        $present = Test-Path (Join-Path $RepoRoot $c.Path)
        $saysNotYet = @($matched | Where-Object { $_ -match 'Not yet' })
        if ($present -and $saysNotYet.Count -gt 0) {
            $violations.Add("'$($c.Marker)' exists at $($c.Path) but THIRD_PARTY_NOTICES.md still says 'Not yet' — the notice under-discloses material we now ship")
        }
        elseif (-not $present -and $saysNotYet.Count -ne $matched.Count) {
            $violations.Add("THIRD_PARTY_NOTICES.md claims '$($c.Marker)' is bundled but $($c.Path) does not exist — a legal document asserting material this repository does not contain")
        }
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
