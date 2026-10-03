# About

Tool for automatic executables porting to Linux and Windows.

Includes a [relinker](core/relinker) that converts executable to the target system's native format and implementations of [system prx libraries](core/libs/prx) suitable for dynamic linking. No emulation or separate runtime process.

[Usage](docs/user/USAGE.md), [Build instructions](docs/dev/BUILD.md), [Technical debt of the project](docs/dev/TechnicalDebt.md), [code style conventions](docs/dev/CONVENTIONS.md), [contributing](CONTRIBUTING.md)

## Status

[![libraries](https://boykopovar.github.io/AnyPS5/badge-libraries.svg)](https://boykopovar.github.io/AnyPS5/) [![shaders](https://boykopovar.github.io/AnyPS5/badge-shaders.svg)](https://boykopovar.github.io/AnyPS5/)

[![progress map](https://boykopovar.github.io/AnyPS5/progress.svg)](https://boykopovar.github.io/AnyPS5/)

<sub>* System libraries: percentage of the functions known to the project so far (declared in [core/libs/prx](core/libs/prx)), not of every PS5 system function. The total grows as more functions are declared.</sub>

[List of verified games](docs/user/COMPATIBILITY.md)

Dreaming Sarah (2D platformer) runs at a stable 60 fps on a GTX 1050 Ti / i5-7500 3.4GHz.

Unsupported or unexpected states strictly throw `std::runtime_error`. `what()` is printed to stderr and the process terminates.

The [shader recompiler](core/shader/recompiler/Recompiler.cpp) successfully produces SPIR-V (validated via [Spirv-Tools](3rdparty/SPIRV-Tools) when built with `ANYPS5_ENABLE_SPIRV_TOOLS`).

The real game reaches the logo, main menu, and [gameplay](https://gist.github.com/user-attachments/assets/81d28e9b-c237-4545-b2ca-720071129816) with audio.

The libc `system()` compatibility export executes its command string through the
host command shell. **A guest title can therefore execute arbitrary host
commands with the permissions of the user running AnyPS5.** Only run guest
binaries you trust.

[Technical debt of the project](docs/dev/TechnicalDebt.md), [code style conventions](docs/dev/CONVENTIONS.md)

## Build

For a reproducible Windows checkout, library build, deployment, and loader
diagnostics, see [Windows payload build and deployment](docs/WindowsPayloadBuild.md).

To audit an ELF against the NID-patched Windows libraries, run
`python scripts/audit-elf-imports.py path/to/game.elf build/core/libs/libs`.
To inspect an SDK payload's entry contract and possible raw syscall sites,
run `python scripts/audit-payload-startup.py path/to/payload.elf`.
It reports missing imports and exits with status 1 when any are missing.
The audit does not establish that an ELF will run: exports must also match the
guest ABI and semantics. In particular, PS5 SDK payloads receive a
`payload_args` pointer at their entry point, including a dynamic symbol
resolver, pipe descriptors, kernel addresses, and a result pointer. The
current Windows entry stub does not supply that contract, and a host cannot
substitute arbitrary pointers for real PS5 kernel facilities. Payload support
requires a separate startup bridge and an explicitly modeled kernel interface;
merely adding exports is insufficient.

The `GuestKernelModel` provides bounded guest memory, synthetic kernel memory,
pipe endpoints, and a dispatcher for FreeBSD syscall numbers 3, 4, 6, 20, and
542. Unsupported calls return FreeBSD `ENOSYS` (78). Its test covers the
initial `getpid`/`dynlib_get_obj_member` sequence used by the public PS5 SDK
CRT and verifies that the latter remains unsupported. This component is not
yet connected to guest machine-code syscall sites; it must not be interpreted
as a working PS5 kernel or as a successful payload launch.

The relinker uses only the C++20 standard library and should build with any conforming compiler.

[libc.prx](core/libs/prx/libc) implementations contain compiler-specific code. Linux builds work with GCC; the Windows CI build uses WinLibs MinGW-w64 GCC 16.2.0 (POSIX threads, SEH, MSVCRT).

The project targets maximum compiler portability. Support for additional compilers will be addressed after the first successful game launch.

## Compatibility

See the [game compatibility list](docs/user/COMPATIBILITY.md) for tested games and known issues.

## Input mapping

SDL-mapped game controllers are supported, including analog sticks and triggers. Keyboard and mouse controls can be configured with an `anyps5-input.ini` file. See [input mapping](docs/user/INPUT_MAPPING.md) for the supported devices and configuration format.

## Disclaimer

This project is intended for interoperability, research, preservation, and compatibility purposes. It does not include, distribute, or require copyrighted software, firmware, cryptographic keys, or proprietary libraries. Users are responsible for ensuring that any binaries used with this project are obtained and used in accordance with applicable laws and their respective license terms.

## License

This project is licensed under the GNU General Public License version 2 only.
