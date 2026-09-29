# AGENTS.md

## Repository layout (important)

This checkout is **AnyPS5 only**. It is one of three unrelated projects, each
with its own repo, language, and toolchain. Do not mix their build systems.

| Project | Upstream | User fork | Language | Toolchain |
| --- | --- | --- | --- | --- |
| AnyPS5 (this repo) | `boykopovar/AnyPS5` | `nahshongraham97/AnyPS5` | C++ | CMake + Ninja + **WinLibs MinGW-w64** (CI uses GCC 15.2.0 posix-seh-ucrt) |
| KytyPS5 | `KytyPS5/KytyPS5` | `nahshongraham97/KytyPS5` | C++ | CMake + Ninja + **clang-cl** (MSVC 2022 toolchain). `cl.exe` and MinGW `g++` are rejected. Needs `glslangValidator`; Qt 6 only for the launcher |
| SharpEmu | `sharpemu/sharpemu` | `nahshongraham97/sharpemu` | **C# / .NET** | `dotnet build` / `dotnet publish` |

- AnyPS5's relinker produces a native Windows executable; it does not need an
  emulator.
- `scripts/run-eboot.ps1` only *launches* SharpEmu and KytyPS5 binaries. It must
  never build or patch them.
- Kyty tooling (`run-kyty.ps1`, `kyty-bisect.ps1`) lives in
  `nahshongraham97/KytyPS5` on branch `tools`, kept off `main` so `main` stays a
  clean mirror of upstream.

## Build (AnyPS5, Windows)

```powershell
$env:PATH = "C:\winlibs\mingw64\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target relinker libs --parallel
```

- `relinker` output: `build/core/relinker/relinker.exe`
- patched libraries: `build/core/libs/libs` (use these, **not** `libs/unpatched`)

## Known constraints

- `relinker` requires a raw ELF (`7F 45 4C 46`). Retail SELF-wrapped `eboot.bin`
  is rejected with `Invalid ELF magic number`; there is no SELF/decrypt support.
- Relinked executables need `libs\` and `app0\` as siblings at runtime.
- KytyPS5 aborts the process on *any* Vulkan validation error, so run it with
  `--vulkan-validation false` unless debugging graphics.

## Git

- `origin` = `nahshongraham97/AnyPS5` (fork), `upstream` = `boykopovar/AnyPS5`.
- Fork `main` carries the upstream sync and its conflict resolution. Sync with
  `git fetch upstream && git merge --ff-only upstream/main`.
- The fork's `main` is periodically a fast-forward of upstream; check
  `git merge-base --is-ancestor main upstream/main` before assuming a rebase is
  needed. A fork with no unique commits has nothing to rebase.
- Verify CI after pushing; the `Progress` workflow failure is pre-existing and
  unrelated to content changes.
