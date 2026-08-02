#Requires -Version 7.0
<#
.SYNOPSIS
    Asserts every shipped native binary carries a populated VERSIONINFO block.

.DESCRIPTION
    docs/19_SAFETY_AND_ANTICHEAT.md §What we will never build: the DLL ships
    with its real filename, real exports, and a populated version block. An
    anti-cheat vendor looking at a module inside their game must be able to tell
    instantly what it is and who wrote it. Being identifiable is the design
    principle, not a side effect — anything that made these fields vaguer,
    absent or randomised would be evasion, which CLAUDE.md rule 3 rejects.

    src/native/FrameLedger.Overlay/CMakeLists.txt carried the comment
    "VERSIONINFO is mandatory and CI fails the build without it" directly above
    a TODO to add it, while no .rc file existed anywhere and nothing checked.
    This script is what makes that sentence true.

    Reads the BUILT BINARY rather than the .rc source: what ships is what
    matters, and a resource that fails to compile in would leave the source
    looking correct.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build/native/x64-release')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Every binary that can end up inside a process we do not own.
$required = @('FrameLedger.Overlay.dll', 'FrameLedger.VkLayer.dll')

if (-not (Test-Path $BuildDir)) {
    Write-Host "VERSIONINFO CHECK FAILED: no native build at $BuildDir" -ForegroundColor Red
    Write-Host '  Build first: ./build.ps1 native' -ForegroundColor Red
    exit 1
}

$errors = [System.Collections.Generic.List[string]]::new()
$checked = 0

foreach ($name in $required) {
    $file = Get-ChildItem -Path $BuildDir -Filter $name -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $file) {
        $errors.Add("$name was not produced by the native build — cannot verify it is identifiable")
        continue
    }

    $vi = $file.VersionInfo
    # A missing resource yields an object whose fields are all null/empty, not
    # an exception. "No version block" and "a version block full of blanks" are
    # the same failure for our purposes: neither identifies the binary.
    $fields = [ordered]@{
        CompanyName     = $vi.CompanyName
        ProductName     = $vi.ProductName
        FileDescription = $vi.FileDescription
        FileVersion     = $vi.FileVersion
    }
    $missing = @($fields.GetEnumerator() | Where-Object { [string]::IsNullOrWhiteSpace($_.Value) } |
            ForEach-Object { $_.Key })

    if ($missing.Count -gt 0) {
        $errors.Add("$name has no usable VERSIONINFO — empty: $($missing -join ', ')")
        continue
    }

    # The product name is the one an anti-cheat vendor greps for. A binary that
    # calls itself something else is the beginning of hiding.
    if ($vi.ProductName -ne 'FrameLedger') {
        $errors.Add("$name ProductName is '$($vi.ProductName)', expected 'FrameLedger' — the binary must name the project it belongs to")
    }
    if ($vi.OriginalFilename -and $vi.OriginalFilename -ne $name) {
        $errors.Add("$name OriginalFilename is '$($vi.OriginalFilename)' — a renamed binary is harder to identify, which is the wrong direction")
    }

    ++$checked
    Write-Host ("  {0,-28} {1} / {2} / v{3}" -f $name, $vi.CompanyName, $vi.ProductName, $vi.FileVersion) -ForegroundColor DarkGray
}

if ($errors.Count -gt 0) {
    Write-Host 'VERSIONINFO CHECK FAILED' -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host '  docs/19_SAFETY_AND_ANTICHEAT.md requires every shipped native binary to identify itself.' -ForegroundColor Red
    exit 1
}

Write-Host "versioninfo OK — $checked native binary(ies) identify themselves" -ForegroundColor Green
exit 0
