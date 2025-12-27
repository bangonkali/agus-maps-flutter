#Requires -Version 5.1
<#
.SYNOPSIS
    Regenerate CoMaps patches from current modifications.

.DESCRIPTION
    Generates patch files from modifications made to thirdparty/comaps/.
    Each modified file gets its own numbered patch file in patches/comaps/.
    This is the Windows PowerShell equivalent of scripts/regenerate_patches.sh.

.PARAMETER OutputDir
    Directory to write patch files. Defaults to patches/comaps.

.EXAMPLE
    .\scripts\regenerate_patches.ps1
#>

[CmdletBinding()]
param(
    [string]$OutputDir,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Get script and root directories
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$ComapsDir = Join-Path $RootDir "thirdparty\comaps"

if (-not $OutputDir) {
    $OutputDir = Join-Path $RootDir "patches\comaps"
}

# Colors for output
function Write-Info { param($Message) Write-Host "[INFO] $Message" -ForegroundColor Green }
function Write-Warn { param($Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Err { param($Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

Write-Info "Regenerating CoMaps patches"
Write-Info "Source directory: $ComapsDir"
Write-Info "Output directory: $OutputDir"

# Verify CoMaps directory exists
if (-not (Test-Path $ComapsDir)) {
    Write-Err "CoMaps directory not found at $ComapsDir"
    exit 1
}

# Create output directory if needed
if (-not (Test-Path $OutputDir)) {
    Write-Info "Creating patch directory"
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

if ($Clean) {
    Write-Warn "Cleaning existing patch files in: $OutputDir"
    Get-ChildItem -Path $OutputDir -Filter "*.patch" -ErrorAction SilentlyContinue | Remove-Item -Force
}

# Change to CoMaps directory
Push-Location $ComapsDir

# Get list of modified files (excluding submodules)
# Disable safecrlf warnings so PowerShell doesn't treat stderr output as an error record.
$modifiedFiles = git -c core.safecrlf=false diff --name-only HEAD 2>$null
if (-not $modifiedFiles) {
    Write-Info "No modifications found in CoMaps directory"
    Pop-Location
    exit 0
}

# Convert to array if single file
if ($modifiedFiles -is [string]) {
    $modifiedFiles = @($modifiedFiles)
}

Write-Info "Found $($modifiedFiles.Count) modified file(s)"

# Build a set of submodule paths so we can skip top-level gitlink entries.
# Note: `git diff --name-only` can include submodule paths when submodules have
# modified content; those entries are not patchable via `git diff HEAD -- <path>`.
$submodulePaths = @{}
$gitmodules = git -c core.safecrlf=false config --file .gitmodules --get-regexp path 2>$null
if ($gitmodules) {
    foreach ($line in $gitmodules) {
        # Format: submodule.<name>.path <path>
        $parts = $line -split "\s+", 2
        if ($parts.Count -eq 2 -and $parts[1]) {
            $submodulePaths[$parts[1]] = $true
        }
    }
}

# Get existing patch files to determine next number
$existingPatches = Get-ChildItem -Path $OutputDir -Filter "*.patch" -ErrorAction SilentlyContinue | Sort-Object Name
$nextPatchNum = 1

if ($existingPatches) {
    # Extract highest patch number
    foreach ($patch in $existingPatches) {
        if ($patch.Name -match '^(\d{4})-') {
            $num = [int]$Matches[1]
            if ($num -ge $nextPatchNum) {
                $nextPatchNum = $num + 1
            }
        }
    }
}

Write-Info "Next patch number: $nextPatchNum"

# Generate patches for each modified file
$generatedCount = 0

foreach ($file in $modifiedFiles) {
    # Skip submodule gitlink entries.
    if ($submodulePaths.ContainsKey($file)) {
        Write-Warn "Skipping submodule entry: $file"
        continue
    }

    # Skip paths that live inside a submodule checkout.
    # In practice, the superproject typically reports only the submodule path itself,
    # but this check is made robust and avoids false positives like skipping `3party/CMakeLists.txt`.
    foreach ($submodulePath in $submodulePaths.Keys) {
        if ($file.StartsWith("$submodulePath/", [System.StringComparison]::Ordinal) -or
            $file.StartsWith("$submodulePath\\", [System.StringComparison]::Ordinal)) {
            Write-Warn "Skipping file inside submodule: $file"
            continue 2
        }
    }
    
    # Create patch name from file path
    $safeName = $file -replace '[/\\]', '-' -replace '[^a-zA-Z0-9\-\.]', ''
    $patchName = "{0:D4}-{1}.patch" -f $nextPatchNum, $safeName
    $patchPath = Join-Path $OutputDir $patchName
    
    Write-Host "  Generating: $patchName" -NoNewline
    
    # Generate patch for this file
    $patchContent = git -c core.safecrlf=false diff HEAD -- $file
    
    if ($patchContent) {
        # Write patch file with Unix line endings for git compatibility.
        # Ensure the patch ends with a trailing newline, otherwise `git apply` can report
        # "corrupt patch at line ..." for the last hunk line.
        ($patchContent -join "`n") + "`n" | Set-Content -Path $patchPath -Encoding UTF8
        Write-Host " [OK]" -ForegroundColor Green
        $generatedCount++
        $nextPatchNum++
    } else {
        Write-Host " [EMPTY]" -ForegroundColor Yellow
    }
}

Pop-Location

Write-Host ""
Write-Info "Generated $generatedCount patch file(s)"
Write-Info "Patches written to: $OutputDir"
