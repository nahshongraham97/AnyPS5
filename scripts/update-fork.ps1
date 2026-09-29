<#
.SYNOPSIS
    Update (or create) the local AnyPS5 fork and make sure its scripts exist.

.DESCRIPTION
    Fixes the common clone problems: a half-finished clone, a stale fork that
    predates the scripts, or submodules that were never initialized. Run this
    once, then call the scripts in .\scripts\ directly.

    It never deletes your work: if the tree has local changes it reports them
    and stops instead of discarding anything.

.PARAMETER RepoPath
    Where the AnyPS5 fork lives or should be cloned.
    Default: $env:USERPROFILE\AnyPS5

.PARAMETER Remote
    Fork to clone from when RepoPath is empty.

.PARAMETER Force
    Clone into RepoPath even when it already exists with files (only safe when
    the folder is not a git checkout you care about).

.EXAMPLE
    .\update-fork.ps1
#>
[CmdletBinding()]
param(
    [string]$RepoPath = (Join-Path $env:USERPROFILE 'AnyPS5'),
    [string]$Remote = 'https://github.com/nahshongraham97/AnyPS5.git',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step([string]$text) { Write-Host "`n=== $text ===" -ForegroundColor Cyan }
function Write-Info([string]$text) { Write-Host "  $text" }
function Write-Warn2([string]$text) { Write-Host "  $text" -ForegroundColor Yellow }

$RepoPath = [System.IO.Path]::GetFullPath($RepoPath)
Write-Host "AnyPS5 fork updater" -ForegroundColor Green
Write-Info "Repo: $RepoPath"

$gitDir = Join-Path $RepoPath '.git'
$hasGit = Test-Path -LiteralPath $gitDir

if ($hasGit) {
    Write-Step 'Updating existing checkout'
    Push-Location $RepoPath
    try {
        $dirty = git status --porcelain
        if ($dirty) {
            Write-Warn2 'Local changes present; not touching them. Commit or stash, then re-run.'
            $dirty | ForEach-Object { Write-Warn2 "    $_" }
            throw 'Working tree is dirty.'
        }

        Write-Info "origin: $(git remote get-url origin)"
        git fetch origin
        git checkout main
        git pull --ff-only origin main
        if ($LASTEXITCODE -ne 0) { throw "git pull failed (exit $LASTEXITCODE)." }
        git submodule update --init --recursive
        if ($LASTEXITCODE -ne 0) { throw "git submodule failed (exit $LASTEXITCODE)." }

        Write-Info "HEAD: $(git rev-parse --short HEAD)"
    } finally { Pop-Location }
} else {
    Write-Step 'Creating checkout'
    if (Test-Path -LiteralPath $RepoPath) {
        $existing = @(Get-ChildItem -LiteralPath $RepoPath -Force)
        if ($existing.Count -gt 0 -and -not $Force) {
            Write-Warn2 "$RepoPath exists and is not a git checkout."
            Write-Warn2 'Move it aside, pick another -RepoPath, or pass -Force to clone over it.'
            throw 'RepoPath is not a git checkout.'
        }
        if ($Force) { Remove-Item -LiteralPath $RepoPath -Recurse -Force }
    }
    $parent = Split-Path -Parent $RepoPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    git clone --recurse-submodules --branch main $Remote $RepoPath
    if ($LASTEXITCODE -ne 0) { throw "git clone failed (exit $LASTEXITCODE)." }
}

# --- Verify the scripts are present -----------------------------------------

Write-Step 'Scripts'
$required = @('run-eboot.ps1', 'kyty-bisect.ps1')
$missing  = @()
foreach ($name in $required) {
    $path = Join-Path (Join-Path $RepoPath 'scripts') $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        Write-Info "found  $path"
    } else {
        Write-Warn2 "missing $path"
        $missing += $name
    }
}

if ($missing.Count -gt 0) {
    Write-Step 'Fetching missing scripts'
    $scriptsDir = Join-Path $RepoPath 'scripts'
    New-Item -ItemType Directory -Path $scriptsDir -Force | Out-Null
    foreach ($name in $missing) {
        $url = "https://raw.githubusercontent.com/nahshongraham97/AnyPS5/main/scripts/$name"
        Write-Info "downloading $url"
        Invoke-WebRequest $url -OutFile (Join-Path $scriptsDir $name)
        Write-Info "wrote   $(Join-Path $scriptsDir $name)"
    }
}

Write-Host "`nReady. Run the scripts from $RepoPath, for example:" -ForegroundColor Green
Write-Info "cd `"$RepoPath`""
Write-Info ".\scripts\kyty-bisect.ps1 -Game '<game folder>' -Redownload"
