#Requires -Version 5.1
<#
.SYNOPSIS
    Apply CoMaps patches for Windows development.

.DESCRIPTION
    Applies all patch files from patches/comaps/ to the CoMaps source
    in thirdparty/comaps/. This is the Windows PowerShell equivalent
    of scripts/apply_comaps_patches.sh.

.PARAMETER PatchDir
    Directory containing patch files. Defaults to patches/comaps.

.PARAMETER DryRun
    If specified, only checks if patches can be applied without applying them.

.EXAMPLE
    .\scripts\apply_comaps_patches.ps1
    .\scripts\apply_comaps_patches.ps1 -DryRun
#>

[CmdletBinding()]
param(
    [string]$PatchDir,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

# Get script and root directories
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$ComapsDir = Join-Path $RootDir "thirdparty\comaps"

if (-not $PatchDir) {
    $PatchDir = Join-Path $RootDir "patches\comaps"
}

# Colors for output
function Write-Info { param($Message) Write-Host "[INFO] $Message" -ForegroundColor Green }
function Write-Warn { param($Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Err { param($Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

Write-Info "Applying CoMaps patches"
Write-Info "Patch directory: $PatchDir"
Write-Info "Target directory: $ComapsDir"

# Verify directories exist
if (-not (Test-Path $ComapsDir)) {
    Write-Err "CoMaps directory not found at $ComapsDir"
    Write-Err "Run fetch_comaps.ps1 first"
    exit 1
}

if (-not (Test-Path $PatchDir)) {
    Write-Warn "Patch directory not found at $PatchDir"
    Write-Info "No patches to apply"
    exit 0
}

# Get all patch files sorted by name
$patchFiles = Get-ChildItem -Path $PatchDir -Filter "*.patch" | Sort-Object Name

if ($patchFiles.Count -eq 0) {
    Write-Info "No patch files found in $PatchDir"
    exit 0
}

Write-Info "Found $($patchFiles.Count) patch file(s)"

# Change to CoMaps directory
Push-Location $ComapsDir

$appliedCount = 0
$skippedCount = 0
$failedCount = 0

foreach ($patch in $patchFiles) {
    $patchName = $patch.Name
    $patchPath = $patch.FullName
    
    Write-Host "  Processing: $patchName" -NoNewline
    
    if ($DryRun) {
        # Check if patch can be applied
        $result = git apply --check $patchPath 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host " [CAN APPLY]" -ForegroundColor Green
            $appliedCount++
        } else {
            # Check if already applied (reverse check)
            $reverseResult = git apply --check --reverse $patchPath 2>&1
            if ($LASTEXITCODE -eq 0) {
                Write-Host " [ALREADY APPLIED]" -ForegroundColor Yellow
                $skippedCount++
            } else {
                Write-Host " [WOULD FAIL]" -ForegroundColor Red
                $failedCount++
            }
        }
    } else {
        # Try to apply the patch
        $result = git apply --check $patchPath 2>&1
        if ($LASTEXITCODE -eq 0) {
            # Patch can be applied
            git apply $patchPath
            if ($LASTEXITCODE -eq 0) {
                Write-Host " [APPLIED]" -ForegroundColor Green
                $appliedCount++
            } else {
                Write-Host " [FAILED]" -ForegroundColor Red
                $failedCount++
            }
        } else {
            # Check if already applied
            $reverseResult = git apply --check --reverse $patchPath 2>&1
            if ($LASTEXITCODE -eq 0) {
                Write-Host " [ALREADY APPLIED]" -ForegroundColor Yellow
                $skippedCount++
            } else {
                Write-Host " [CONFLICT]" -ForegroundColor Red
                Write-Warn "Patch may partially apply or have conflicts"
                $failedCount++
            }
        }
    }
}

Pop-Location

Write-Host ""
Write-Info "Patch summary:"
Write-Host "  Applied: $appliedCount" -ForegroundColor Green
Write-Host "  Skipped (already applied): $skippedCount" -ForegroundColor Yellow
if ($failedCount -gt 0) {
    Write-Host "  Failed: $failedCount" -ForegroundColor Red
}

if ($failedCount -gt 0) {
    Write-Err "Some patches failed to apply"
    exit 1
}

Write-Info "Patch application complete"
