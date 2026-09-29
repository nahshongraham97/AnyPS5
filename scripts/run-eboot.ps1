<#
.SYNOPSIS
    Get the latest AnyPS5 fork, then prepare and/or run a PS5 eboot.bin.

.DESCRIPTION
    Everything runs on Windows. One thing is unavoidable: AnyPS5's relinker is
    built here and used here, so this script must build AnyPS5 first.

    What it does, in order:
      1. Updates (or clones) the fork main branch with all submodules.
      2. Builds the AnyPS5 relinker + patched system libraries with WinLibs MinGW.
      3. Converts the guest ELF/eboot into a native Windows executable.
      4. Deploys the patched PRX/SPRX libraries next to it.
      5. Launches the result with AnyPS5 (native), SharpEmu, and/or KytyPS5.

    SharpEmu and KytyPS5 are separate projects. This script does not download
    them; point the -SharpEmu / -Kyty switches at builds or release extracts you
    already have. AnyPS5 itself needs no emulator: the relinker produces a native
    Windows executable.

.PARAMETER Eboot
    Path to the guest file you want to run (eboot.bin or a converted .elf).

.PARAMETER RepoPath
    Where the AnyPS5 fork lives, or where to clone it. Default: $env:USERPROFILE\AnyPS5.

.PARAMETER WorkDir
    Where build output and the payload staging directory go.
    Default: $RepoPath\ps5-run.

.PARAMETER WinLibs
    WinLibs MinGW-w64 bin directory. Default: C:\winlibs\mingw64\bin.
    Required for a build; ignored when -SkipBuild is used.

.PARAMETER SharpEmu
    Path to SharpEmu.exe. Providing it runs the eboot through SharpEmu.

.PARAMETER Kyty
    Path to kyty_emulator.exe. Providing it runs the eboot through KytyPS5.

.PARAMETER Backend
    auto | anyps5 | sharpemu | kyty | all.
      auto    - kyty if -Kyty given, else sharpemu if -SharpEmu given, else anyps5
      anyps5  - run the relinked native executable (default when no emulator given)
      sharpemu- run via SharpEmu; requires -SharpEmu
      kyty    - run via KytyPS5; requires -Kyty
      all     - run every backend you supplied a path for
    Default: auto.

.PARAMETER SkipBuild
    Reuse the existing build; do not reconfigure or rebuild AnyPS5.

.PARAMETER SkipPull
    Do not touch git; use the working tree as-is.

.PARAMETER SkipSceModule
    Pass --skip-sce-module to the relinker. By default sce_module is scanned
    for syscalls and kept in the payload, which new titles rely on.

.PARAMETER Diagnostics
    Pass --windows-diagnostics to the relinker for extra loader diagnostics.

.PARAMETER Force
    Remove an existing payload directory before staging.

.PARAMETER CopyGameData
    Copy the game's other files (sce_sys, data folders, ...) into the payload's
    app0 folder, mirroring the console layout. The native AnyPS5 executable
    expects that layout at runtime. Default: $true. SharpEmu and KytyPS5 read
    the original eboot folder directly and do not need this.

.EXAMPLE
    .\run-eboot.ps1 -Eboot 'D:\Games\MyGame\eboot.bin' -WinLibs 'C:\winlibs\mingw64\bin'

.EXAMPLE
    .\run-eboot.ps1 -Eboot 'D:\Games\MyGame\eboot.bin' `
        -SharpEmu 'C:\SharpEmu\SharpEmu.exe' -Kyty 'C:\KytyPS5\kyty_emulator.exe' `
        -Backend all
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Eboot,

    [string]$RepoPath = (Join-Path $env:USERPROFILE 'AnyPS5'),
    [string]$WorkDir,
    [string]$WinLibs = 'C:\winlibs\mingw64\bin',
    [string]$SharpEmu,
    [string]$Kyty,
    [ValidateSet('auto', 'anyps5', 'sharpemu', 'kyty', 'all')]
    [string]$Backend = 'auto',
    [switch]$SkipBuild,
    [switch]$SkipPull,
    [switch]$SkipSceModule,
    [switch]$Diagnostics,
    [switch]$Force,
    [bool]$CopyGameData = $true
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Pinned clone target: the fork's main carries the upstream sync and its
# conflict resolution. Do not point this at a random branch.
$RepoUrl   = 'https://github.com/nahshongraham97/AnyPS5.git'
$RepoBranch = 'main'

# WinLibs MinGW used by the fork's CI. Keep the compiler and any runtime DLLs
# you copy from the same installation.
$WinLibsRelease = '15.2.0posix-14.0.0-ucrt-r7'
$WinLibsZip     = 'winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-14.0.0-r7.zip'

function Write-Step([string]$text) { Write-Host "`n=== $text ===" -ForegroundColor Cyan }
function Write-Info([string]$text) { Write-Host "  $text" }
function Assert-LastExit([string]$what) {
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)." }
}

# --- Resolve inputs ---------------------------------------------------------

if (-not (Test-Path -LiteralPath $Eboot -PathType Leaf)) {
    throw "Eboot not found: $Eboot"
}
$Eboot = (Resolve-Path -LiteralPath $Eboot).Path

if (-not $WorkDir) { $WorkDir = Join-Path $RepoPath 'ps5-run' }
$WorkDir = [System.IO.Path]::GetFullPath($WorkDir)

$payloadRoot   = Join-Path $WorkDir 'payload'
$librariesDir  = Join-Path $payloadRoot 'libs'

$relinkerExe   = Join-Path $RepoPath 'build/core/relinker/relinker.exe'
$patchedDir    = Join-Path $RepoPath 'build/core/libs/libs'
$convertedExe  = Join-Path $payloadRoot ((Split-Path -Leaf $Eboot) + '.exe')

if ($Backend -eq 'auto') {
    if     ($Kyty)     { $Backend = 'kyty' }
    elseif ($SharpEmu) { $Backend = 'sharpemu' }
    else               { $Backend = 'anyps5' }
}
if ($Backend -in @('sharpemu', 'all', 'kyty') ) {
    if ($Backend -in @('sharpemu', 'all') -and -not $SharpEmu) { throw "-Backend $Backend needs -SharpEmu." }
    if ($Backend -in @('kyty', 'all')     -and -not $Kyty)     { throw "-Backend $Backend needs -Kyty." }
}

Write-Host "AnyPS5 runner" -ForegroundColor Green
Write-Info "Eboot    : $Eboot"
Write-Info "Repo     : $RepoPath"
Write-Info "Work dir : $WorkDir"
Write-Info "Backend  : $Backend"

# --- 1. Get the latest fork -------------------------------------------------

if (-not $SkipPull) {
    Write-Step "Updating fork ($RepoBranch)"
    if (Test-Path -LiteralPath (Join-Path $RepoPath '.git')) {
        Push-Location $RepoPath
        try {
            git fetch origin
            git checkout $RepoBranch
            git pull --ff-only origin $RepoBranch
            git submodule update --init --recursive
            Assert-LastExit 'git update'
        } finally { Pop-Location }
    } else {
        if (Test-Path -LiteralPath $RepoPath) {
            $existing = @(Get-ChildItem -LiteralPath $RepoPath -Force)
            if ($existing.Count -gt 0) { throw "RepoPath exists and is not empty: $RepoPath" }
        }
        $parent = Split-Path -Parent $RepoPath
        if ($parent -and -not (Test-Path -LiteralPath $parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        git clone --recurse-submodules --branch $RepoBranch $RepoUrl $RepoPath
        Assert-LastExit 'git clone'
    }
    Push-Location $RepoPath
    try {
        Write-Info ("HEAD: " + (git rev-parse --short HEAD))
        Write-Info ("Status: " + ((git status --short --branch) -join '; '))
    } finally { Pop-Location }
} else {
    Write-Step "Skipping git update (-SkipPull)"
}

# --- 2. Build AnyPS5 --------------------------------------------------------

if (-not $SkipBuild) {
    Write-Step "Building AnyPS5 (relinker + patched libraries)"
    if (-not (Test-Path -LiteralPath (Join-Path $WinLibs 'g++.exe'))) {
        throw "MinGW g++.exe not found in $WinLibs. Install WinLibs '$WinLibsRelease' or pass -WinLibs."
    }
    $env:PATH = "$WinLibs;$env:PATH"

    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if (-not $ninja) {
        throw "ninja not found on PATH. Install it (e.g. from the same WinLibs toolchain) or add it to PATH."
    }

    Push-Location $RepoPath
    try {
        cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
        Assert-LastExit 'cmake configure'
        cmake --build build --target relinker libs --parallel
        Assert-LastExit 'cmake build'
    } finally { Pop-Location }
} else {
    Write-Step "Skipping build (-SkipBuild)"
}

if (-not (Test-Path -LiteralPath $relinkerExe -PathType Leaf)) {
    throw "Relinker not built: $relinkerExe. Run without -SkipBuild first."
}

# --- 3. Stage the payload ---------------------------------------------------

Write-Step "Staging payload"
if ($Force -and (Test-Path -LiteralPath $payloadRoot)) {
    Remove-Item -LiteralPath $payloadRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
New-Item -ItemType Directory -Path $librariesDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $payloadRoot 'app0') -Force | Out-Null

# --windows makes the relinker emit a native PE executable. Without it the
# output is a Linux ELF and Windows cannot start it.
$relinkerArgs = @('--windows')
if ($Diagnostics)     { $relinkerArgs += '--windows-diagnostics' }
if ($SkipSceModule)   { $relinkerArgs += '--skip-sce-module' }
$relinkerArgs += @($Eboot, $convertedExe)

Write-Info ("relinker " + ($relinkerArgs -join ' '))
& $relinkerExe @relinkerArgs
Assert-LastExit 'relinker'

if (-not (Test-Path -LiteralPath $convertedExe -PathType Leaf)) {
    throw "Relinker did not produce $convertedExe."
}

# Deploy patched PRX/SPRX next to the executable. Do NOT use libs\unpatched:
# those lack the NID patching guest imports need.
$libraries = @(Get-ChildItem -LiteralPath $patchedDir -Filter '*.prx' -File -ErrorAction SilentlyContinue)
if ($libraries.Count -eq 0) {
    throw "No patched PRX files in $patchedDir. Did 'cmake --build build --target libs' run?"
}
foreach ($library in $libraries) {
    $name = [IO.Path]::GetFileNameWithoutExtension($library.Name)
    foreach ($extension in @('prx', 'sprx')) {
        Copy-Item -LiteralPath $library.FullName -Destination (Join-Path $librariesDir "$name.$extension") -Force
    }
}
Write-Info "Deployed $($libraries.Count) patched libraries to $librariesDir"

# The patched PRX are PE DLLs. If they were not linked with a static runtime
# they need the MinGW runtime DLLs beside them. Copy whatever exists; missing
# files are fine when the build used -static-libgcc/-static-libstdc++.
if (Test-Path -LiteralPath $WinLibs -PathType Container) {
    foreach ($runtime in @('libwinpthread-1.dll', 'libgcc_s_seh-1.dll', 'libstdc++-6.dll')) {
        $source = Join-Path $WinLibs $runtime
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $payloadRoot $runtime) -Force
            Copy-Item -LiteralPath $source -Destination (Join-Path $librariesDir $runtime) -Force
            Write-Info "Copied runtime $runtime"
        }
    }
}

# Mirror the game folder into app0 so the native executable finds its data.
# The eboot itself is the relinked executable, so it is not copied again.
if ($CopyGameData) {
    $gameDir = Split-Path -Parent $Eboot
    $app0Dir = Join-Path $payloadRoot 'app0'
    Write-Step "Copying game data into app0"
    $ebootLeaf = Split-Path -Leaf $Eboot
    Get-ChildItem -LiteralPath $gameDir -Force | ForEach-Object {
        if ($_.Name -eq $ebootLeaf) { return }
        Copy-Item -LiteralPath $_.FullName -Destination $app0Dir -Recurse -Force
    }
    Write-Info "Copied $(Split-Path -Leaf $gameDir) contents (minus $ebootLeaf) to $app0Dir"
}

# --- 4. Run -----------------------------------------------------------------

function Start-AnyPS5 {
    Write-Step "Running with AnyPS5 (native)"
    Write-Info "Payload root must contain libs\ and app0\: $payloadRoot"
    Push-Location $payloadRoot
    try { & $convertedExe } finally { Pop-Location }
}

function Start-SharpEmu {
    if (-not (Test-Path -LiteralPath $SharpEmu -PathType Leaf)) { throw "SharpEmu not found: $SharpEmu" }
    Write-Step "Running with SharpEmu"
    & $SharpEmu $Eboot --log-to-file
}

function Start-Kyty {
    if (-not (Test-Path -LiteralPath $Kyty -PathType Leaf)) { throw "KytyPS5 not found: $Kyty" }
    Write-Step "Running with KytyPS5"
    # The parent folder of the file becomes /app0.
    & $Kyty --game (Split-Path -Parent $Eboot)
}

switch ($Backend) {
    'anyps5'   { Start-AnyPS5 }
    'sharpemu' { Start-SharpEmu }
    'kyty'     { Start-Kyty }
    'all'      {
        Start-AnyPS5
        Start-SharpEmu
        Start-Kyty
    }
}

Write-Host "`nDone." -ForegroundColor Green
