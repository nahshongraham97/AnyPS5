# `eboot.bin` and SELF inputs

AnyPS5 accepts a raw x86-64 ELF irrespective of its file extension. The
relinker can now reconstruct an ELF from a SELF whose required program
segments are present as plaintext and uncompressed data. It validates the
segment table, ranges, overlaps, and full coverage before running the
existing ELF relinker. This is a file format step; it does not implement a
console kernel, a game's graphics API, or input devices.

SELF segments marked encrypted, compressed, or blocked are rejected with a
named segment ID. An encrypted retail title requires a lawfully obtained
decrypted ELF image before AnyPS5 can relink it. Supplying the original
protected `eboot.bin` alone does not supply plaintext or decryption keys.
An `eboot.bin` from a disc or package may also need its surrounding `app0`
resources, modules, and a supported runtime to reach game startup.

Press Ctrl + A to select all existing text, press Delete, and paste this corrected version:

```powershell
$repo = Join-Path $env:USERPROFILE 'Downloads\crispy-doom-ps5-v1.0-7.1.0\CrispyDoom\payloads\AnyPS5-fork'
$eboot = 'C:\path\to\your\game\eboot.bin'
$output = Join-Path (Split-Path -Parent $eboot) 'eboot-anyps5.exe'
if (-not (Test-Path -LiteralPath $repo -PathType Container)) { throw "Fork checkout not found: $repo" }
if (-not (Test-Path -LiteralPath $eboot -PathType Leaf)) { throw "Eboot not found: $eboot" }
Set-Location -LiteralPath $repo
git status --short --branch
git switch main
if ($LASTEXITCODE -ne 0) { throw 'Cannot switch to fork main' }
git pull --ff-only origin main
if ($LASTEXITCODE -ne 0) { throw 'Git update failed' }
git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw 'Submodule update failed' }
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build build --target relinker self_image_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build --output-on-failure -R '^self_image$'
if ($LASTEXITCODE -ne 0) { throw 'SELF extraction test failed' }
& .\build\core\relinker\relinker.exe --windows --windows-diagnostics $eboot $output
if ($LASTEXITCODE -ne 0) { throw 'Relinker rejected this image; capture the exact FAIL line' }
Write-Host "Converted executable: $output"
```

Replace the `$eboot` path with the actual file. A successful conversion
only verifies that the file format and relinker accepted the image. Deploy
the patched libraries and game resources before any runtime test. Do not
use `--skip-syscall-check` to interpret a conversion as runtime support.
