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
    @{ Path = 'src/native/third_party/vulkan-headers'; Licence = 'apache-2.0.txt'; Name = 'Khronos Vulkan headers' },

    # AMD FidelityFX SDK headers, tag v2.3.0. MIT BY EXCEPTION: upstream's
    # docs/license.md is a binary-only default licence followed by an exception
    # list that places the named files under MIT -- and the list names every file
    # in the tree (845 of 845 on 2026-09-04). §2d below asserts, per vendored
    # file, that it is on that list and carries the grant inline; this row only
    # asserts the licence copy exists.
    @{ Path = 'src/native/third_party/fidelityfx'; Licence = 'fidelityfx-MIT.txt'; Name = 'AMD FidelityFX SDK headers' },

    # AMD FidelityFX SDK -- the FSR 3.0 HOST API headers, tag fsr3-v3.0.4. A
    # DIFFERENT licence shape from the row above: a root LICENSE.txt that IS the
    # MIT grant verbatim, with every header carrying it inline -- which is why
    # the directory is separate and §2e below (not §2d) walks it. The same MIT
    # text from the same project, so the same legal/licenses copy is the notice.
    @{ Path = 'src/native/third_party/fidelityfx-fsr3'; Licence = 'fidelityfx-MIT.txt'; Name = 'AMD FidelityFX SDK 3.0 host headers' }
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

# --- 2d. AMD FidelityFX: MIT BY EXCEPTION, asserted file by file --------------
#
# THE SHAPE OF THE RISK. Upstream has no root LICENSE. docs/license.md opens with
# a binary-only, no-reverse-engineering licence that "applies to all files except
# as noted below", and the MIT grant lives in the exception list that follows.
# So the question for any file vendored from that tree is not "what does the
# root licence say" but "is THIS PATH on the list" -- and a re-sync that adds a
# header without checking is how a file under the wrong licence arrives with the
# directory's licence copy sitting beside it looking fine. Both halves are
# asserted per file: on the list, and carrying the grant in its own banner.
#
# The list happens to cover the whole tree today (845 of 845 blobs). That is a
# fact about one commit, not a property to rely on: the check reads the list.
$ffxDir = Join-Path $RepoRoot 'src/native/third_party/fidelityfx'
if (Test-Path $ffxDir) {
    $ffxLicence = Join-Path $ffxDir 'license.md'
    if (-not (Test-Path $ffxLicence)) {
        $violations.Add('AMD FidelityFX is vendored but src/native/third_party/fidelityfx/license.md is missing — the MIT grant for these files is the exception list inside it, so nothing else can stand in for it')
    }
    else {
        $lic = Get-Content $ffxLicence -Raw
        if ($lic -notmatch 'except as noted below') {
            $violations.Add('src/native/third_party/fidelityfx/license.md no longer has the "except as noted below" shape — if upstream changed its licensing model, re-run docs/18_GPU_VENDOR_APIS.md §Checklist rather than trusting this gate')
        }
        if ($lic -notmatch 'Permission is hereby granted, free of charge') {
            $violations.Add('src/native/third_party/fidelityfx/license.md no longer contains the MIT permission grant')
        }
        $listed = @([regex]::Matches($lic, '(?m)^- `([^`]+)`\s*$') | ForEach-Object { $_.Groups[1].Value })
        if ($listed.Count -eq 0) {
            # NEVER A PASS: a parser matching nothing would report every vendored
            # file as off the list, which is the right failure -- but say why.
            $violations.Add('parsed ZERO entries from the MIT exception list in fidelityfx/license.md — either the list is gone or its shape changed, and both must fail rather than read as "nothing is licensed"')
        }

        $kits = Join-Path $ffxDir 'Kits'
        $vendoredFiles = @(if (Test-Path $kits) { Get-ChildItem $kits -Recurse -File -ErrorAction SilentlyContinue })
        if ($vendoredFiles.Count -eq 0) {
            $violations.Add('src/native/third_party/fidelityfx/Kits holds no files — a vendoring directory with a licence copy and nothing under it is a claim about nothing (18_GPU_VENDOR_APIS: nothing is vendored ahead of its consumer, and nothing is disclosed ahead of being vendored)')
        }
        foreach ($f in $vendoredFiles) {
            # Upstream spells the list with backslashes relative to the repository
            # root, and this directory mirrors upstream's layout from Kits/ down.
            $rel = [IO.Path]::GetRelativePath($ffxDir, $f.FullName).Replace('/', '\')
            if ($listed -notcontains $rel) {
                $violations.Add("fidelityfx/$($rel.Replace('\', '/')) is NOT on license.md's MIT exception list — the binary-only default licence would apply to it, and it may not be vendored")
            }
            if ($f.Extension -in '.dll', '.lib', '.exe', '.cpp', '.hlsl') {
                $violations.Add("fidelityfx/$($rel.Replace('\', '/')) is a $($f.Extension) — only declarations are vendored from this SDK; binaries and sources are excluded whatever the list says (third_party/fidelityfx/README.md)")
            }
            if ($f.Extension -eq '.h' -and -not (Select-String -Path $f.FullName -Pattern 'Permission is hereby granted, free of charge' -Quiet)) {
                $violations.Add("fidelityfx/$($rel.Replace('\', '/')) does not carry the MIT grant in its own banner — every header of interest did on 2026-09-04, so this is a file that was not checked")
            }
        }
    }
}

# --- 2e. AMD FidelityFX, the FSR 3.0 HOST API at tag fsr3-v3.0.4: MIT at the root
# AND inline, asserted file by file ----------------------------------------------
#
# A DIFFERENT SHAPE FROM §2d, which is why it is a different directory and a
# different section rather than a second walk of the same one: at this tag the
# root LICENSE.txt IS the MIT grant (no exception list to look a path up in), and
# every header carries the same grant in its own banner. So the per-file question
# is "does THIS header carry the grant", and the per-directory question is "is the
# root grant present" -- both asserted, because a re-sync from a later tag would
# bring the 2.x exception-list shape in under this directory's name and §2d would
# never look here.
$fsr3Dir = Join-Path $RepoRoot 'src/native/third_party/fidelityfx-fsr3'
if (Test-Path $fsr3Dir) {
    $fsr3Licence = Join-Path $fsr3Dir 'LICENSE.txt'
    if (-not (Test-Path $fsr3Licence)) {
        $violations.Add('AMD FidelityFX 3.0 host headers are vendored but src/native/third_party/fidelityfx-fsr3/LICENSE.txt is missing — at tag fsr3-v3.0.4 that file IS the MIT grant, so nothing else can stand in for it')
    }
    elseif ((Get-Content $fsr3Licence -Raw) -notmatch 'Permission is hereby granted, free of charge') {
        $violations.Add('src/native/third_party/fidelityfx-fsr3/LICENSE.txt does not contain the MIT permission grant — if upstream changed its licensing model at this tag, re-run docs/18_GPU_VENDOR_APIS.md §Checklist rather than trusting this gate')
    }

    $sdk = Join-Path $fsr3Dir 'sdk'
    $fsr3Files = @(if (Test-Path $sdk) { Get-ChildItem $sdk -Recurse -File -ErrorAction SilentlyContinue })
    if ($fsr3Files.Count -eq 0) {
        $violations.Add('src/native/third_party/fidelityfx-fsr3/sdk holds no files — a vendoring directory with a licence copy and nothing under it is a claim about nothing')
    }
    foreach ($f in $fsr3Files) {
        $rel = [IO.Path]::GetRelativePath($fsr3Dir, $f.FullName).Replace('\', '/')
        if ($f.Extension -ne '.h') {
            $violations.Add("fidelityfx-fsr3/$rel is a $($f.Extension) — only the host API's header declarations are vendored from this tag; binaries, sources, shaders and helpers are excluded (third_party/fidelityfx-fsr3/README.md)")
        }
        elseif (-not (Select-String -Path $f.FullName -Pattern 'Permission is hereby granted, free of charge' -Quiet)) {
            $violations.Add("fidelityfx-fsr3/$rel does not carry the MIT grant in its own banner — every one of the ten did at fsr3-v3.0.4, so this is a file that was not checked")
        }
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
        @{ Marker = 'Vulkan headers'; Path = 'src/native/third_party/vulkan-headers' },
        @{ Marker = 'NVIDIA Streamline'; Path = 'src/native/third_party/streamline' },
        @{ Marker = 'AMD FidelityFX'; Path = 'src/native/third_party/fidelityfx' },
        # A marker the row above cannot satisfy by accident: the two directories are
        # two claims, and one row must not stand in for both.
        @{ Marker = 'FidelityFX SDK 3.0 host headers'; Path = 'src/native/third_party/fidelityfx-fsr3' }
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
