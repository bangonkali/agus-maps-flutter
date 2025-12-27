#Requires -Version 5.1
<#
.SYNOPSIS
    Bootstrap agus-maps-flutter for Windows development.

.DESCRIPTION
    Sets up everything needed to build and run the agus_maps_flutter
    plugin on Windows x64. This includes:
    1. Fetch CoMaps source code
    2. Apply patches
    3. Build Boost headers
    4. Extract ANGLE libraries from Flutter engine
    5. Copy CoMaps data files

.PARAMETER SkipAngle
    Skip ANGLE library extraction (use if already extracted).

.PARAMETER SkipCoMaps
    Skip CoMaps fetch and patch (use if already set up).

.PARAMETER BuildType
    CMake build type: Debug or Release. Defaults to Release.

.EXAMPLE
    .\scripts\bootstrap_windows.ps1
    .\scripts\bootstrap_windows.ps1 -BuildType Debug
    .\scripts\bootstrap_windows.ps1 -SkipAngle
#>

[CmdletBinding()]
param(
    [switch]$SkipAngle,
    [switch]$SkipCoMaps,
    [ValidateSet("Debug", "Release")]
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

# Get script and root directories
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $RootDir "build"
$AngleDir = Join-Path $BuildDir "angle"
$ComapsDir = Join-Path $RootDir "thirdparty\comaps"

# Colors for output
function Write-Info { param($Message) Write-Host "[INFO] $Message" -ForegroundColor Green }
function Write-Warn { param($Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Err { param($Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }
function Write-Step { param($Step, $Message) Write-Host "`n=== Step $Step`: $Message ===" -ForegroundColor Cyan }

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  agus-maps-flutter Windows Bootstrap  " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check prerequisites
Write-Step 0 "Checking prerequisites"

# Check CMake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Err "CMake not found. Please install CMake 3.22 or later."
    exit 1
}
$cmakeVersion = cmake --version | Select-Object -First 1
Write-Info "CMake: $cmakeVersion"

# Check Ninja
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $ninja) {
    Write-Warn "Ninja not found. Will use default CMake generator (slower)."
    Write-Warn "Install Ninja for faster builds: winget install Ninja-build.Ninja"
}
else {
    $ninjaVersion = ninja --version
    Write-Info "Ninja: $ninjaVersion"
}

# Check Git
$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    Write-Err "Git not found. Please install Git."
    exit 1
}
Write-Info "Git: $(git --version)"

# Check Flutter
$flutter = Get-Command flutter -ErrorAction SilentlyContinue
if (-not $flutter) {
    Write-Err "Flutter not found. Please install Flutter SDK."
    exit 1
}
Write-Info "Flutter: $(flutter --version | Select-Object -First 1)"

# Ensure Flutter Windows desktop is enabled
Write-Info "Ensuring Flutter Windows desktop is enabled..."
flutter config --enable-windows-desktop 2>$null

Write-Info "Prerequisites OK"

# Step 1: Fetch CoMaps source
if (-not $SkipCoMaps) {
    Write-Step 1 "Fetching CoMaps source"
    
    $fetchScript = Join-Path $ScriptDir "fetch_comaps.ps1"
    if (Test-Path $fetchScript) {
        & $fetchScript
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Failed to fetch CoMaps"
            exit 1
        }
    }
    else {
        Write-Err "fetch_comaps.ps1 not found"
        exit 1
    }
}
else {
    Write-Step 1 "Skipping CoMaps fetch (--SkipCoMaps)"
}

# Step 2: Apply patches
if (-not $SkipCoMaps) {
    Write-Step 2 "Applying CoMaps patches"
    
    $patchScript = Join-Path $ScriptDir "apply_comaps_patches.ps1"
    if (Test-Path $patchScript) {
        & $patchScript
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Failed to apply patches"
            exit 1
        }
    }
    else {
        Write-Warn "apply_comaps_patches.ps1 not found, skipping patches"
    }
}
else {
    Write-Step 2 "Skipping patches (--SkipCoMaps)"
}

# Step 3: Build Boost headers
Write-Step 3 "Building Boost headers"

$BoostDir = Join-Path $ComapsDir "3party\boost"
$BoostConfigHpp = Join-Path $BoostDir "boost\config.hpp"

if (Test-Path $BoostConfigHpp) {
    Write-Info "Boost headers already built"
}
elseif (Test-Path $BoostDir) {
    Write-Info "Building Boost headers (this may take a few minutes)..."
    Push-Location $BoostDir
    
    # On Windows, use bootstrap.bat
    $bootstrapBat = Join-Path $BoostDir "bootstrap.bat"
    $bootstrapSh = Join-Path $BoostDir "bootstrap.sh"
    
    if (Test-Path $bootstrapBat) {
        Write-Info "Running bootstrap.bat..."
        & cmd /c "bootstrap.bat"
        if ($LASTEXITCODE -eq 0) {
            Write-Info "Running b2 headers..."
            & .\b2.exe headers
        }
    }
    elseif (Test-Path $bootstrapSh) {
        # Try using Git Bash if available
        $gitBash = Get-Command bash -ErrorAction SilentlyContinue
        if ($gitBash) {
            Write-Info "Running bootstrap.sh via bash..."
            & bash -c "./bootstrap.sh && ./b2 headers"
        }
        else {
            Write-Warn "Cannot build Boost headers - no bootstrap.bat and no bash available"
        }
    }
    else {
        Write-Warn "Boost bootstrap script not found"
    }
    
    Pop-Location
}
else {
    Write-Warn "Boost directory not found at $BoostDir"
}

# Step 4: Extract ANGLE libraries from Flutter
if (-not $SkipAngle) {
    Write-Step 4 "Extracting ANGLE libraries from Flutter engine"
    
    # Create angle directory
    if (-not (Test-Path $AngleDir)) {
        New-Item -ItemType Directory -Path $AngleDir -Force | Out-Null
    }
    
    # Try to find Flutter root
    $flutterRoot = $null
    
    # Method 1: Check FLUTTER_ROOT environment variable
    if ($env:FLUTTER_ROOT -and (Test-Path $env:FLUTTER_ROOT)) {
        $flutterRoot = $env:FLUTTER_ROOT
        Write-Info "Found Flutter via FLUTTER_ROOT: $flutterRoot"
    }
    
    # Method 2: Resolve from flutter command location
    if (-not $flutterRoot) {
        $flutterCmd = Get-Command flutter -ErrorAction SilentlyContinue
        if ($flutterCmd) {
            # flutter.bat is typically in flutter/bin/
            $flutterBin = Split-Path -Parent $flutterCmd.Source
            $flutterRoot = Split-Path -Parent $flutterBin
            
            # Handle case where it might be a shim (e.g., from scoop, chocolatey)
            if (-not (Test-Path (Join-Path $flutterRoot "bin\cache"))) {
                # Try to get actual path from flutter --version output
                $flutterInfo = flutter --version 2>&1 | Out-String
                if ($flutterInfo -match "Flutter\s+\d+\.\d+\.\d+") {
                    # Fallback: check common locations
                    $commonPaths = @(
                        "$env:USERPROFILE\flutter",
                        "$env:LOCALAPPDATA\flutter",
                        "C:\flutter",
                        "C:\src\flutter"
                    )
                    foreach ($path in $commonPaths) {
                        if (Test-Path (Join-Path $path "bin\cache\artifacts\engine")) {
                            $flutterRoot = $path
                            break
                        }
                    }
                }
            }
            
            if ($flutterRoot) {
                Write-Info "Found Flutter via command path: $flutterRoot"
            }
        }
    }
    
    if (-not $flutterRoot) {
        Write-Err "Could not find Flutter SDK root"
        Write-Err "Set FLUTTER_ROOT environment variable or ensure flutter is in PATH"
        exit 1
    }
    
    # Ensure Flutter engine artifacts are downloaded
    Write-Info "Ensuring Flutter Windows engine artifacts are downloaded..."
    flutter precache --windows 2>$null
    
    # Find ANGLE libraries in Flutter engine
    $engineDir = Join-Path $flutterRoot "bin\cache\artifacts\engine\windows-x64"
    
    if (-not (Test-Path $engineDir)) {
        Write-Err "Flutter Windows engine not found at: $engineDir"
        Write-Err "Run 'flutter precache --windows' to download"
        exit 1
    }
    
    Write-Info "Flutter engine directory: $engineDir"
    
    # ANGLE libraries to copy
    $angleLibs = @(
        "libEGL.dll",
        "libGLESv2.dll",
        "d3dcompiler_47.dll"
    )
    
    $copiedCount = 0
    foreach ($lib in $angleLibs) {
        $sourcePath = Join-Path $engineDir $lib
        $destPath = Join-Path $AngleDir $lib
        
        if (Test-Path $sourcePath) {
            Copy-Item -Path $sourcePath -Destination $destPath -Force
            Write-Info "  Copied: $lib"
            $copiedCount++
        }
        else {
            Write-Warn "  Not found: $lib"
        }
    }
    
    # Also copy .lib files if they exist (for linking)
    $angleImportLibs = @(
        "libEGL.dll.lib",
        "libGLESv2.dll.lib"
    )
    
    foreach ($lib in $angleImportLibs) {
        $sourcePath = Join-Path $engineDir $lib
        $destPath = Join-Path $AngleDir $lib
        
        if (Test-Path $sourcePath) {
            Copy-Item -Path $sourcePath -Destination $destPath -Force
            Write-Info "  Copied: $lib"
            $copiedCount++
        }
    }
    
    if ($copiedCount -eq 0) {
        Write-Err "No ANGLE libraries found in Flutter engine"
        Write-Err "This may indicate an incompatible Flutter version"
        exit 1
    }
    
    Write-Info "ANGLE libraries extracted to: $AngleDir"
}
else {
    Write-Step 4 "Skipping ANGLE extraction (--SkipAngle)"
}

# Step 5: Copy CoMaps data files
Write-Step 5 "Checking CoMaps data files"

$exampleAssetsDir = Join-Path $RootDir "example\assets\comaps_data"
if (Test-Path $exampleAssetsDir) {
    Write-Info "CoMaps data files already present in example/assets/comaps_data"
}
else {
    $copyDataScript = Join-Path $ScriptDir "copy_comaps_data.sh"
    if (Test-Path $copyDataScript) {
        Write-Info "Run './scripts/copy_comaps_data.sh' (via Git Bash) to copy data files"
    }
    else {
        Write-Warn "CoMaps data files not found. You may need to copy them manually."
    }
}

# Summary
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Bootstrap Complete!                  " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Info "Next steps:"
Write-Host "  1. cd example" -ForegroundColor White
Write-Host "  2. flutter run -d windows" -ForegroundColor White
Write-Host ""
Write-Info "For debug builds:"
Write-Host "  flutter run -d windows --debug" -ForegroundColor White
Write-Host ""
Write-Info "For release builds:"
Write-Host "  flutter run -d windows --release" -ForegroundColor White
Write-Host ""

if (-not (Test-Path $exampleAssetsDir)) {
    Write-Warn "Remember to copy CoMaps data files before running!"
}
