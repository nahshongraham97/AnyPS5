<#
.SYNOPSIS
    Run a game on several KytyPS5 builds and capture each run's log.

.DESCRIPTION
    KytyPS5 aborts during the language-selection step for some titles. This
    downloads a list of known-good/known-bad KytyPS5 builds side by side, runs
    each against the same game folder, and keeps the console output plus the
    emulator's own log so the failure can be compared across builds.

    Each build extracts to <Root>\builds\<tag> and its logs stay inside that
    folder, so nothing overwrites anything else.

    Kyty is standalone: this does not touch AnyPS5, the relinker, or the game
    files. It only launches kyty_emulator.exe from each extracted build.

.PARAMETER Game
    Game directory (the folder holding eboot.bin), an ELF, or a .zar archive.

.PARAMETER Root
    Where builds are downloaded and logs are written.
    Default: $env:USERPROFILE\KytyPS5-builds

.PARAMETER Tag
    One or more KytyPS5 release tags to test, newest first. Defaults to a set
    that brackets the 2026-09-21 change to shader resource tracking.

.PARAMETER ExtraArgs
    Extra arguments passed through to kyty_emulator.exe, e.g.
    @('--present-mode','Fifo').

.PARAMETER VulkanValidation
    Pass --vulkan-validation true. Off by default; Kyty treats any Vulkan
    validation error as fatal and aborts on benign swapchain issues.

.PARAMETER Redownload
    Re-extract a build even when its folder already exists.

.EXAMPLE
    .\kyty-bisect.ps1 -Game 'D:\Games\SAROS-PPSA07631' -Redownload
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Game,

    [string]$Root = (Join-Path $env:USERPROFILE 'KytyPS5-builds'),

    [string[]]$Tag = @(
        'KytyPS5-2026-09-29-6799ecb',
        'KytyPS5-2026-09-22-8dc9b9d',
        'KytyPS5-2026-09-20-fe4f942',
        'KytyPS5-2026-09-12-d3d7bd3'
    ),

    [string[]]$ExtraArgs = @(),
    [switch]$VulkanValidation,
    [switch]$Redownload
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step([string]$text) { Write-Host "`n=== $text ===" -ForegroundColor Cyan }
function Write-Info([string]$text) { Write-Host "  $text" }

# --- Resolve the game input -------------------------------------------------

if (-not (Test-Path -LiteralPath $Game)) { throw "Game not found: $Game" }
$Game = (Resolve-Path -LiteralPath $Game).Path

$buildRoot = Join-Path $Root 'builds'
$logRoot   = Join-Path $Root 'logs'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

Write-Host "KytyPS5 build sweep" -ForegroundColor Green
Write-Info "Game   : $Game"
Write-Info "Builds : $($Tag.Count)"

$summary = @()

foreach ($current in $Tag) {
    Write-Step "$current"

    $target = Join-Path $buildRoot $current
    $exe    = Join-Path $target 'kyty_emulator.exe'

    if ($Redownload -and (Test-Path -LiteralPath $target)) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }

    if (-not (Test-Path -LiteralPath $exe)) {
        $url = "https://github.com/KytyPS5/KytyPS5/releases/download/$current/$current-Windows-x64.zip"
        $zip = Join-Path $buildRoot "$current.zip"
        Write-Info "Downloading $url"
        try {
            Invoke-WebRequest $url -OutFile $zip
            Expand-Archive $zip -DestinationPath $target -Force
            Remove-Item -LiteralPath $zip -Force
        } catch {
            Write-Host "  Download/extract failed: $($_.Exception.Message)" -ForegroundColor Yellow
            $summary += [pscustomobject]@{ Build = $current; Result = 'download failed' }
            continue
        }
    } else {
        Write-Info "Using existing build"
    }

    if (-not (Test-Path -LiteralPath $exe)) {
        Write-Host "  kyty_emulator.exe missing after extract" -ForegroundColor Yellow
        $summary += [pscustomobject]@{ Build = $current; Result = 'exe missing' }
        continue
    }

    # Kyty writes its own logs (and _PipelineCache) relative to the working
    # directory, so run from the build folder and copy the output out after.
    $arguments = @('--game', $Game)
    if (-not $VulkanValidation) { $arguments += @('--vulkan-validation', 'false') }
    if ($ExtraArgs.Count -gt 0) { $arguments += $ExtraArgs }

    Write-Info ("kyty_emulator.exe " + ($arguments -join ' '))
    $capture = Join-Path $logRoot "$current.console.txt"
    Push-Location $target
    try {
        & $exe @arguments 2>&1 | Tee-Object -FilePath $capture
        $exit = $LASTEXITCODE
    } finally { Pop-Location }

    $logFile = Get-ChildItem -LiteralPath $target -Recurse -File -Include '*.txt', '*.log' -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1

    $errorLine = Select-String -Path $capture -Pattern '^--- Error ---$' -Context 0, 2 -ErrorAction SilentlyContinue |
                 Select-Object -First 1

    $reason = if ($errorLine) { ($errorLine.Context.PostContext -join ' ').Trim() } else { '(no --- Error --- block)' }

    $summary += [pscustomobject]@{
        Build     = $current
        ExitCode  = $exit
        Result    = $reason
        Console   = $capture
        EmuLog    = if ($logFile) { $logFile.FullName } else { '' }
    }

    if ($logFile) {
        Copy-Item -LiteralPath $logFile.FullName -Destination (Join-Path $logRoot "$current.emulator.log") -Force
    }
}

Write-Step 'Summary'
$summary | Format-List

Write-Info "Console captures and emulator logs: $logRoot"
Write-Host "`nDone." -ForegroundColor Green
