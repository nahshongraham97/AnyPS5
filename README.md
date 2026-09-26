# About

Tool for automatic executables porting to Linux and Windows.

Includes a [relinker](core/relinker) that converts executable to the target system's native format and implementations of [system prx libraries](core/libs/prx) suitable for dynamic linking. No emulation or separate runtime process.

Releases will be published after the first full successful launch of at least one game.

## Status

Execution reaches `_start`, [stack unwinding](core/libs/prx/libc/src/exception/Unwind.cpp) and exception handling tables are built, reaches main. Unsupported or unexpected states strictly throw `std::runtime_error`. `what()` is printed to stderr and the process terminates.

The [shader recompiler](core/shader/recompiler/Recompiler.cpp) successfully produces validated via [Spirv-Tools](3rdparty/SPIRV-Tools) SPIR-V.

The real game reaches the logo, main menu, and [gameplay](https://gist.github.com/user-attachments/assets/81d28e9b-c237-4545-b2ca-720071129816) with audio.

[Technical debt of the project](docs/TechnicalDebt.md), [code style conventions](docs/CONVENTIONS.md)

## Build

The relinker uses only the C++20 standard library and should build with any conforming compiler.

[libc.prx](core/libs/prx/libc) implementations contain compiler-specific code. Linux builds work with GCC; on Windows, MinGW-w64 GCC 15.2.0 (`winlibs-gcc15`, `x86_64-ucrt-posix-seh`) is currently required.

The project targets maximum compiler portability. Support for additional compilers will be addressed after the first successful game launch.

### Relinker regression tests

The relinker can be built independently without downloading the runtime or graphics submodules. Tests additionally require Python 3.

```sh
cmake -S . -B build-relinker -DANYPS5_RELINKER_ONLY=ON -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-relinker --config Debug --parallel 2
ctest --test-dir build-relinker -C Debug --output-on-failure
```

For a GCC or Clang sanitizer build, add `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` when configuring a separate build directory. The regression suite generates synthetic ELF files; no game files are needed. It covers malformed ranges, instruction handling, Linux/Windows output generation, and native child-process exit status. Windows builds also run the existing dependency-diagnostics tests.

### Intel conversion limitations

`--to-intel` preserves the executable's operating-system format. The previous AMD-only replacement table contained the original instructions, so its reported replacements did not provide Intel compatibility. These placeholders are now rejected before writing output, with the instruction name and file offset. The check also rejects the recognized SSE4a instructions. A file with no recognized unsupported instructions is copied unchanged and exits successfully; this is not a guarantee of compatibility with every Intel CPU or every executable. Actual instruction substitutions remain unimplemented.

Relinker tests do not establish game compatibility. Graphics, audio, input, saving, and guest runtime behavior still require end-to-end testing on the target hardware and operating system.

## Disclaimer

This project is intended for interoperability, research, preservation, and compatibility purposes. It does not include, distribute, or require copyrighted software, firmware, cryptographic keys, or proprietary libraries. Users are responsible for ensuring that any binaries used with this project are obtained and used in accordance with applicable laws and their respective license terms.

## License

This project is licensed under the GNU General Public License version 2 only.
