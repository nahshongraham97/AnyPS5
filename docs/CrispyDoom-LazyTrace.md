# Crispy Doom: strict binding versus first-use trace

The supplied PS5 ELF is not a native Windows game. As of the September 28
integration, conversion and PRX loading have advanced, but no guest frame or
controller input has been verified. A successful import audit is only a binding
check. In particular, `--skip-syscall-check` does not implement the SDK's raw
FreeBSD/PS5 `syscall` instructions or its kernel-dependent startup contract.
The [PS5 payload SDK entrypoint documentation](https://github.com/PS5Dev/PS5SDK/blob/main/README.md#entrypoint)
describes the six fields expected by this style of payload.

Strict mode fails at the first missing import before the ELF entry point. Use
that result to track completeness. The relinker's `--lazy-binding` option is a
**diagnostic only**: it installs a named fail-fast trap for each unresolved
import, allowing startup to proceed until the guest actually invokes one.
It never supplies fake successful behavior. A crash before any named import
trap may instead indicate the guest entry/arguments, a raw syscall, an ABI
mismatch, or another runtime defect.

For this exact ELF, the startup argument mismatch is already observable in
machine code: `_start` at ELF address `0x40` calls the function pointer at
`payload_args + 0`, `__kernel_init` reads pointers at offsets `0x08` and
`0x10` and kernel addresses at `0x18` and `0x20`, and `_start` writes the
result through the pointer at `0x28`. The current Windows entry builder
instead provides an argc/argv-like block starting with the integer `1`.
Thus lazy binding is expected to expose a startup failure before `main`,
not produce video. A future startup bridge must supply the actual six-field
SDK contract and modeled callback, pipe, and kernel-memory semantics; a
correct layout alone would not make the raw guest syscalls safe or functional.

The following expects the fork checkout inside the Crispy Doom payload folder,
as in the earlier Windows instructions. It builds, deploys patched PRXs, and
creates *two fresh executables*. Run from a PowerShell window using the same
WinLibs MinGW toolchain that configured `build`. `system()` can execute guest
commands in your host shell; run only payloads you trust.

Press Ctrl + A to select all existing text, press Delete, and paste this corrected version:

```powershell
$ErrorActionPreference = 'Stop'
$repo = Join-Path $env:USERPROFILE 'Downloads\crispy-doom-ps5-v1.0-7.1.0\CrispyDoom\payloads\AnyPS5-fork'
$payload = Split-Path -Parent $repo
$elf = Join-Path $payload 'crispy-doom.elf'
$traceScript = Join-Path $payload 'trace.gdb'
foreach ($required in @($repo, $elf, $traceScript)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing: $required" }
}
Set-Location -LiteralPath $repo
if (git status --porcelain) { throw 'Save or commit local edits before updating this checkout.' }
git fetch origin main
if ($LASTEXITCODE -ne 0) { throw 'Fetch failed.' }
git switch main
if ($LASTEXITCODE -ne 0) { throw 'Could not switch to main.' }
git pull --ff-only origin main
if ($LASTEXITCODE -ne 0) { throw 'Fast-forward update failed.' }
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build build --target libs relinker --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$patched = Join-Path $repo 'build\core\libs\libs'
$destination = Join-Path $payload 'libs'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$libraries = @(Get-ChildItem -LiteralPath $patched -File -Filter '*.prx')
if ($libraries.Count -eq 0) { throw "No patched PRXs in $patched" }
foreach ($library in $libraries) {
    $name = [IO.Path]::GetFileNameWithoutExtension($library.Name)
    foreach ($extension in @('prx', 'sprx')) {
        $target = Join-Path $destination "$name.$extension"
        Copy-Item -LiteralPath $library.FullName -Destination $target -Force
        if ((Get-FileHash -LiteralPath $library.FullName -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
            throw "Deployment hash mismatch: $target"
        }
    }
}
$strictExe = Join-Path $payload 'crispy-doom-strict-current.exe'
$lazyExe = Join-Path $payload 'crispy-doom-lazy-current.exe'
$relinker = Join-Path $repo 'build\core\relinker\relinker.exe'
& $relinker --windows --windows-diagnostics --skip-syscall-check $elf $strictExe
if ($LASTEXITCODE -ne 0) { throw 'Strict conversion failed.' }
& $relinker --windows --windows-diagnostics --skip-syscall-check --lazy-binding $elf $lazyExe
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic conversion failed.' }
$log = Join-Path $env:USERPROFILE 'Downloads\crispy-doom-anyps5-lazy-trace.txt'
Push-Location -LiteralPath $payload
try {
    & 'C:\WinLibs\mingw64\bin\gdb.exe' -batch -q -x $traceScript $lazyExe 2>&1 |
        Tee-Object -FilePath $log
} finally {
    Pop-Location
}
Write-Host "Diagnostic trace: $log"
```

Attach the full trace and `windows-diagnostics-imports.txt` from the fork
checkout (the relinker's working directory). If the trace reaches `Transferring control to ELF entry point`, note
the next named import trap, signal, exception address and backtrace. If a
window appears, report whether `sceVideoOutOpen`/flip and pad calls occur;
seeing a window is not by itself proof of a rendered frame or working input.
For the production/strict test, rerun GDB with `$strictExe` only after the
ELF import audit reports zero missing exports.

## Startup bridge acceptance criteria

The loader work needed for frames is more than filling import names:

1. Identify SDK-style payload entry points and pass the six-field
   `payload_args` structure, with valid callback and result storage; retain
   the existing argv-style entry for ordinary ELFs. A test payload should
   exercise all six fields without dereferencing address `1` or `nullptr`.
2. Resolve the requested libkernel symbols through the guest callback, and
   route its syscall entry to a modeled guest dispatcher rather than the
   host Windows `syscall` instruction. Test the actual Crispy Doom CRT's
   `sceKernelDlsym` and `getpid` requests and invalid-symbol failure cases.
3. Supply correctly owned pipe/socket descriptors and a bounded guest
   kernel-memory model for the CRT's initialization, or stop with a named
   unsupported-firmware/operation error. Never treat a fabricated host
   pointer as the PS5 kernel data base.
4. Trace entry, first video-output open, first submitted flip, and first
   pad read independently. Test a real buffer-to-flip sequence before
   claiming that a visible window represents a rendered game frame.
