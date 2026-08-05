#Requires -Version 7.0
<#
.SYNOPSIS
    Measures the blast radius of the FrameLedger Vulkan implicit layer.

.DESCRIPTION
    docs/20_OPEN_QUESTIONS.md §R2 calls this the highest blast-radius item in the
    whole spike, and it is: an implicit layer is machine-wide, so a passthrough
    bug loads FrameLedger into EVERY Vulkan process on the system — including
    anything anti-cheat-protected — before a single line of our guard has run.

    §S2's fix is `enable_environment` in the manifest, so the loader does not map
    the layer at all unless the Agent sets the variable when launching a game the
    user opted in. This script measures whether the loader actually behaves that
    way, rather than trusting the manifest documentation.

    THIS IS THE ONLY PLACE THE LAYER IS REGISTERED in this phase, and it
    unregisters in a finally block — including on Ctrl-C or a failed assertion.
    A registered implicit layer is a load path into game processes that no guard
    covers yet (§S2 is still open), so leaving one behind would be the exact
    hazard this script exists to measure.

    Discriminating by construction: the "must not load" case is only meaningful
    if the "must load" case is shown to work in the same run. A loader that never
    loads the layer for an unrelated reason would otherwise look like a pass.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build/native/x64-release'),
    # vulkaninfo.exe ships in System32 with the runtime, so this works without
    # the SDK installed. vulkaninfoSDK.exe is the SDK's own copy; either does.
    [string]$VulkanInfo = 'vulkaninfo.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$layerKey = 'HKCU:\SOFTWARE\Khronos\Vulkan\ImplicitLayers'
$manifest = Join-Path $BuildDir 'FrameLedger.VkLayer/VkLayer_FRAMELEDGER_overlay.json'
$layerDll = Join-Path $BuildDir 'FrameLedger.VkLayer/FrameLedger.VkLayer.dll'
$enableVar = 'FRAMELEDGER_ENABLE_VK_LAYER'
$errors = [System.Collections.Generic.List[string]]::new()

function Test-Prereq {
    if (-not (Test-Path $manifest)) { throw "manifest not found: $manifest — run ./build.ps1 native" }
    if (-not (Test-Path $layerDll)) { throw "layer DLL not found: $layerDll — run ./build.ps1 native" }
    if (-not (Get-Command $VulkanInfo -ErrorAction SilentlyContinue)) {
        throw "$VulkanInfo not on PATH. Install the Vulkan SDK or pass -VulkanInfo <path>."
    }
}

# Run a Vulkan application with the loader's layer logging on, and report
# whether OUR library was loaded. The loader prints the library path it maps,
# so this is an observation of the loader's behaviour, not of ours.
function Test-LayerLoaded([hashtable]$Env) {
    $prev = @{}
    foreach ($k in $Env.Keys) {
        $prev[$k] = [Environment]::GetEnvironmentVariable($k)
        [Environment]::SetEnvironmentVariable($k, $Env[$k])
    }
    $code = $null
    try {
        $out = & $VulkanInfo --summary 2>&1 | Out-String
        $code = $LASTEXITCODE
    }
    catch { $out = "$_" }
    finally {
        foreach ($k in $Env.Keys) { [Environment]::SetEnvironmentVariable($k, $prev[$k]) }
    }

    # DISCOVERY IS NOT LOADING, AND AVAILABILITY IS NOT LOADING EITHER. Two
    # false positives were hit here before this filter was narrow enough, and
    # both would have reported a working gate as broken:
    #
    #   1. With the variable unset the loader still prints
    #        Located json file "...\FrameLedger.VkLayer\VkLayer_..._overlay.json"
    #      because it reads every registered manifest before deciding. Matching
    #      the DLL name hit that PATH — the manifest sits in a directory called
    #      FrameLedger.VkLayer.
    #   2. vulkaninfo's own report LISTS the layer as available, by name, from
    #      the manifest. That is the tool describing the system, not the loader
    #      mapping anything.
    #
    # The unambiguous evidence is the loader saying it INSERTED us into a chain,
    # which it prints with the DLL path alongside:
    #   "Insert instance layer \"VK_LAYER_FRAMELEDGER_overlay\" (...dll)"
    #   "Inserted device layer \"VK_LAYER_FRAMELEDGER_overlay\" (...dll)"
    $signals = @($out -split "`n" |
        Where-Object { $_ -match 'Insert(ed)? (instance|device) layer' } |
        Where-Object { $_ -match 'VK_LAYER_FRAMELEDGER_overlay' })

    return @{
        Loaded    = $signals.Count -gt 0
        Crashed   = ($code -ne 0 -and $null -ne $code)
        ExitCode  = $code
        Output    = $out
    }
}

try {
    Test-Prereq

    # A key that exists but holds no values makes Get-ItemProperty return
    # nothing, and dereferencing that under StrictMode is an error rather than
    # a "no" — so ask the key itself for its value names.
    if (Test-Path $layerKey) {
        $names = @((Get-Item $layerKey -ErrorAction SilentlyContinue).GetValueNames())
        if ($names -contains $manifest) {
            throw "the layer is ALREADY registered at $layerKey — refusing to run, because this script must be the thing that registers and unregisters it"
        }
    }

    Write-Host '=== registering the layer under HKCU (temporarily) ===' -ForegroundColor Cyan
    New-Item -Path $layerKey -Force | Out-Null
    New-ItemProperty -Path $layerKey -Name $manifest -Value 0 -PropertyType DWord -Force | Out-Null
    Write-Host "  $manifest"

    # --- 1. The variable is UNSET: the layer must NOT be loaded --------------
    Write-Host ''
    Write-Host '=== 1. enable variable UNSET — the layer must not load ===' -ForegroundColor Cyan
    $unset = Test-LayerLoaded @{ VK_LOADER_DEBUG = 'layer'; $enableVar = $null }
    if ($unset.Loaded) {
        $errors.Add('BLAST RADIUS: the layer loaded into an unrelated Vulkan process with the enable variable unset')
        Write-Host '  [FAIL] our library WAS mapped' -ForegroundColor Red
    }
    else {
        Write-Host '  [PASS] our library was not mapped' -ForegroundColor Green
    }

    if ($unset.Crashed) {
        $errors.Add("the host application exited $($unset.ExitCode) with the layer registered and the variable unset")
        Write-Host "  [FAIL] the host application did not survive (exit $($unset.ExitCode))" -ForegroundColor Red
    }
    else {
        Write-Host '  [PASS] the host application ran normally' -ForegroundColor Green
    }

    # --- 2. The variable is SET: the layer must load, AND NOT CRASH ---------
    # Without the load half, case 1 proves nothing: a loader that never loads
    # the layer for an unrelated reason looks identical to a working gate.
    #
    # The crash half is here because of what this test actually found. The first
    # version of the layer treated "not in the enable-list" as a reason to
    # return VK_ERROR_INITIALIZATION_FAILED from
    # vkNegotiateLoaderLayerInterfaceVersion. Loader 1.4.357 does not skip a
    # layer that does that — it ACCESS-VIOLATES the host. Every Vulkan
    # application on the machine outside our enable-list would have crashed:
    # a far larger blast radius than the one we were reducing.
    Write-Host ''
    Write-Host '=== 2. enable variable SET — must load AND leave the host alive ===' -ForegroundColor Cyan
    $set = Test-LayerLoaded @{ VK_LOADER_DEBUG = 'layer'; $enableVar = '1' }
    if ($set.Loaded) {
        Write-Host '  [PASS] our layer WAS inserted' -ForegroundColor Green
    }
    else {
        $errors.Add('the layer did not load even with the enable variable set — case 1 therefore proves nothing')
        Write-Host '  [FAIL] our layer was not inserted' -ForegroundColor Red
    }
    if ($set.Crashed) {
        $errors.Add("the host application exited $($set.ExitCode) with our layer loaded — we break applications we are mapped into")
        Write-Host "  [FAIL] the host application did not survive (exit $($set.ExitCode))" -ForegroundColor Red
    }
    else {
        Write-Host '  [PASS] the host application ran normally with our layer in the chain' -ForegroundColor Green
    }

    # --- 3. A NON-MATCHING value must NOT enable the layer -------------------
    # The manifest says FRAMELEDGER_ENABLE_VK_LAYER=1. Whether the loader
    # compares the VALUE or merely checks that the variable EXISTS decides
    # whether a stray `set FRAMELEDGER_ENABLE_VK_LAYER=0` in a user's
    # environment silently enables us in every Vulkan process on the machine.
    #
    # THIS WAS AN OBSERVATION AND IS NOW AN ASSERTION, which is the whole point.
    # It printed in both branches and never touched $errors — it could not fail.
    # That was correct while the answer was unknown: the step existed to DISCOVER
    # the loader's behaviour, and a discovery step that fails is just a step with
    # an opinion. The discovery was made on 2026-08-02 against loader 1.4.357 and
    # is recorded in docs/spike-notes.md §2 as settled: the loader compares the
    # VALUE.
    #
    # Once a measurement becomes a recorded fact, the step that produced it has to
    # become the thing that defends it, or the fact quietly stops being checked
    # while a script that still prints about it reads as coverage. A loader update
    # that switched to existence-checking would widen the highest blast radius in
    # the spike, and the only script that would notice was printing in green
    # either way (20_OPEN_QUESTIONS §S29(d)).
    Write-Host ''
    Write-Host '=== 3. enable variable set to a NON-MATCHING value ===' -ForegroundColor Cyan
    $wrong = Test-LayerLoaded @{ VK_LOADER_DEBUG = 'layer'; $enableVar = '0' }
    if ($wrong.Loaded) {
        $errors.Add("the loader enabled our layer with $enableVar=0 — it checks EXISTENCE, not value, so any stray value of that variable maps FrameLedger into every Vulkan process on the machine. docs/spike-notes.md §2 records the opposite as measured against loader 1.4.357; that measurement no longer holds and the manifest's enable_environment gate is not the gate it is documented to be.")
        Write-Host '  [FAIL] the loader checks EXISTENCE, not value — any value enables the layer' -ForegroundColor Red
    }
    else {
        Write-Host '  [PASS] the loader compares the VALUE — a non-matching value does not enable us' -ForegroundColor Green
    }
}
finally {
    # Unregister unconditionally. A registered implicit layer is a load path
    # into game processes that no guard covers yet; leaving one behind because
    # an assertion failed would be worse than the bug being tested for.
    if (Test-Path $layerKey) {
        Remove-ItemProperty -Path $layerKey -Name $manifest -ErrorAction SilentlyContinue
        $remaining = (Get-Item $layerKey -ErrorAction SilentlyContinue)
        if ($null -ne $remaining -and $remaining.ValueCount -eq 0) {
            Remove-Item $layerKey -ErrorAction SilentlyContinue
        }
    }
    $stillThere = $false
    if (Test-Path $layerKey) {
        $stillThere = @((Get-Item $layerKey -ErrorAction SilentlyContinue).GetValueNames()) -contains $manifest
    }
    Write-Host ''
    if ($stillThere) {
        Write-Host 'ERROR: the layer is STILL REGISTERED. Remove it by hand:' -ForegroundColor Red
        Write-Host "  Remove-ItemProperty -Path '$layerKey' -Name '$manifest'" -ForegroundColor Red
        exit 1
    }
    Write-Host 'unregistered — HKCU is clean' -ForegroundColor DarkGray
}

if ($errors.Count -gt 0) {
    Write-Host ''
    Write-Host 'BLAST-RADIUS CHECK FAILED' -ForegroundColor Red
    $errors | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'blast radius OK — the layer loads only when invited' -ForegroundColor Green
exit 0
