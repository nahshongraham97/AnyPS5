# Windows homebrew compatibility checkpoint

## Batch 1: libc dependency lookup

Windows dependency lookup now appends libc.prx for libSceLibcInternal.prx,
libSceLibcInternal.so, and libSceLibcInternal.sprx. Requested names and order
are preserved. An existing libc.prx is not duplicated. This does not rename
libraries or add named exports.

Validation: the regression executable failed before the change and passed
13 cases after it. It is registered with CTest as windows_import_libraries
when BUILD_TESTING is enabled. The test runs on Linux and Windows without
loading guest binaries. Native Windows execution has not been performed here.

## Evidence and remaining work

The user verified hwinfo main completes with exit zero after local hardware
stubs, named libc aliases, and bypassing its SDK entry point. Hardware values
were placeholders. Those local edits are not included in this branch.

The original hwinfo startup crashes while writing through a null pointer
loaded from payload arguments at offset 0x28. The current bootstrap supplies
an argc/argv-style block, not the expected payload argument contract.

The supplied Crispy Doom ELF has entry point 0x40, 249 imports and ten
DT_NEEDED libraries. Normal conversion rejects a direct syscall at 0x16ff0a
inside mprotect. Conversion with syscall checking disabled was performed for
static inspection only; that output was not executed. Disabling the check
is not syscall emulation.

## Next batch

Inspect the SDK startup source matching the supplied binaries. Define the
payload argument and initialization contract before changing bootstrap code.
Add a synthetic startup-contract regression that reproduces the null-pointer
failure. Do not infer full startup support from a direct-to-main test.

Subsequent work includes guest syscall translation, named/NID symbol mapping,
runtime function coverage, and graphics/input/audio validation. No game
compatibility is claimed by this checkpoint.

## Windows test commands

Run in a separate checkout of this branch to preserve the user's working
runtime and local patches. Configure the project with BUILD_TESTING=ON using
the existing MinGW toolchain, then run:

```powershell
cmake --build <build-directory> --target windows_import_libraries_tests
ctest --test-dir <build-directory> -R '^windows_import_libraries$' --output-on-failure
```
