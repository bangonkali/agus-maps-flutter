#Requires -Version 5.1
<#
.SYNOPSIS
    Fetch CoMaps source code for Windows development.

.DESCRIPTION
    Clones the CoMaps repository to thirdparty/comaps at a pinned tag
    and initializes submodules. This is the Windows PowerShell equivalent
    of scripts/fetch_comaps.sh.

.PARAMETER Tag
    The git tag to checkout. Defaults to v2025.12.11-2.

.PARAMETER Force
    If specified, removes existing thirdparty/comaps and re-clones.

.EXAMPLE
    .\scripts\fetch_comaps.ps1
    .\scripts\fetch_comaps.ps1 -Tag "v2025.12.11-2" -Force
#>

[CmdletBinding()]
param(
    [string]$Tag = "v2025.12.11-2",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# Get script and root directories
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$ThirdpartyDir = Join-Path $RootDir "thirdparty"
$ComapsDir = Join-Path $ThirdpartyDir "comaps"

# Colors for output (using Write-Host)
function Write-Info { param($Message) Write-Host "[INFO] $Message" -ForegroundColor Green }
function Write-Warn { param($Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Err { param($Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

Write-Info "Fetching CoMaps source (tag: $Tag)"
Write-Info "Target directory: $ComapsDir"

# Check if already exists
if (Test-Path $ComapsDir) {
    if ($Force) {
        Write-Warn "Removing existing CoMaps directory (--Force specified)"
        Remove-Item -Recurse -Force $ComapsDir
    } else {
        # Check if it's the right tag
        Push-Location $ComapsDir
        try {
            $currentTag = git describe --tags --exact-match 2>$null
            if ($currentTag -eq $Tag) {
                Write-Info "CoMaps already at tag $Tag, skipping clone"
                Write-Info "Ensuring submodules are initialized..."
                git submodule update --init --recursive --depth 1
                Pop-Location
                Write-Info "CoMaps fetch complete"
                exit 0
            } else {
                Write-Warn "CoMaps exists but at different version ($currentTag), use -Force to re-clone"
                Pop-Location
                exit 0
            }
        } catch {
            Write-Warn "Could not determine current tag, continuing..."
            Pop-Location
        }
    }
}

# Create thirdparty directory if needed
if (-not (Test-Path $ThirdpartyDir)) {
    Write-Info "Creating thirdparty directory"
    New-Item -ItemType Directory -Path $ThirdpartyDir | Out-Null
}

# Clone CoMaps
Write-Info "Cloning CoMaps repository..."
$repoUrl = "https://github.com/comaps/comaps.git"

git clone --branch $Tag --depth 1 $repoUrl $ComapsDir
if ($LASTEXITCODE -ne 0) {
    Write-Err "Failed to clone CoMaps repository"
    exit 1
}

# Initialize submodules
Write-Info "Initializing submodules (this may take a while)..."
Push-Location $ComapsDir
git submodule update --init --recursive --depth 1
if ($LASTEXITCODE -ne 0) {
    Write-Err "Failed to initialize submodules"
    Pop-Location
    exit 1
}
Pop-Location

Write-Info "CoMaps fetch complete"
Write-Info "Source directory: $ComapsDir"
