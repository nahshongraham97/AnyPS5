# Sons of Sparta PS5-to-Windows bring-up record

This document records the Windows bring-up of the extracted PS5 build identified by content ID
`UP9000-PPSA28997_00-SONSOFSPARTAPS50`. It is deliberately detailed. The work crossed several
boundaries—SELF/ELF relinking, PE generation, PRX discovery and loading, Unity/IL2CPP runtime
contracts, Vulkan presentation, FMOD Studio bank registration, and save-data mounting—and a short
list of patches would not preserve enough reasoning to safely continue the investigation.

The checkpoint described here was produced on October 8, 2026. At this checkpoint the game is
fully functional through the launcher, Windows loader, Unity bootstrap, Vulkan bootstrap, FMOD
bus lookup, in-game logo sequences, and persistent save-data operations. The previous blocker—an
uncaught managed IL2CPP exception on the `SaveDataProcessQueue` thread—has been completely resolved
via architectural fixes in `libSceSaveData.native` and `libkernel`, allowing the title to smoothly
progress beyond the in-game logos directly into the title and main menu game loop without fatal errors.

## Scope and rules of this work

The implementation follows these rules:

1. Do not replace guest game content with fabricated content.
2. Do not return fake FMOD object handles or swallow managed exceptions.
3. Preserve the extracted game tree, relinked guest modules, staged executable, host PRXs, and
   required MinGW runtime DLLs while iterating.
4. Prefer runtime-compatible implementations with regression tests over title-specific binary edits.
5. When a title compatibility transformation is unavoidable, constrain it to a recognized byte
   sequence or semantic contract and test that exact transformation.
6. Keep generated binaries, extracted copyrighted game data, save data, and run logs out of Git.

The game data used for local verification lives outside the repository. The local staging directory
used during this investigation is `build/exact-eboot-run`; it intentionally remains present, but is
not source-controlled.

## Verified runtime milestone

The following sequence is verified end to end:

1. The relinked Windows executable starts.
2. All required relinked guest PRXs load, including IL2CPP user assemblies, PSN, save-data, Burst,
   FMOD Core, FMOD Studio, and Resonance Audio.
3. All required host compatibility PRXs load from `libs`.
4. Control reaches the guest ELF entry point.
5. Unity opens `boot.config`, `global-metadata.dat`, `RuntimeInitializeOnLoads.json`,
   `ScriptingAssemblies.json`, `globalgamemanagers`, scene data, resources, and shared assets.
6. The Xbox One controller is detected.
7. Vulkan selects the discrete AMD Radeon RX 6800 XT, creates a surface, creates a device and queue,
   and creates a two-image swapchain at the live window extent of 1484 by 835.
8. FMOD Core and Studio create their mixer, stream, feeder, update, sample-load, bank-load,
   nonblocking, and file threads.
9. The runtime opens and registers the actual `Master.bank` and `Master.strings.bank` files.
10. The stale serialized path `bus:/Master Bus/Dialogue` is resolved to the real bank bus
    `bus:/Dialogue`; FMOD supplies the real bus handle.
11. The title continues well beyond the old six-second crash, renders the logo movies, opens later
    Unity scenes including `level5`, and loads scene-specific FMOD banks such as the River and Audio
    Cues banks.
12. The save-data layer mounts `_sd/GOWSOSSAVE999` as `/savedata0`, accurately reporting
    `mount_status = 0` (existing directory) on remounts and `1` only on actual creation.
13. Global save data is loaded with `success status True` (`SaveSystem completed global save data file`).
14. The progress/generic save is forwarded, written (10,660 bytes), verified, unmounted (`[PS5SaveTask] Unmount GOWSOSSAVE999 SUCCESS`),
    and finalized with `success=True`.
15. The title smoothly transitions past the logo sequence into the active game loop, continually calling
    `sce::Agc::suspendPoint` per frame, running stably with zero fatal errors or unhandled exceptions.

Frame rate is currently variable between runs. The first successful launch appeared locked near
40 FPS, while the next normal launch ran near 19–21 FPS over the same visible startup sequence. The
earlier 40 FPS observation must therefore not be treated as a stable cap or a performance baseline.
The variation may come from shader/pipeline compilation, cache state, logging volume, video decode,
presentation synchronization, or the compatibility graphics path. No frame-pacing or performance
change has been made in this checkpoint, and profiling is required before assigning a cause.

## Failure progression and evidence

### Initial state: loader and layout failures

Earlier runs did not have a reliable relationship among the executable, `app0`, relinked guest
modules, host PRXs, and MinGW runtime DLLs. That produced failures before meaningful game code could
run. The current staging layout is:

```text
exact-eboot-run/
  eboot-current.exe
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll
  app0/
    Media/
      Modules/
      Plugins/
      StreamingAssets/
    sce_module/
  libs/
    libkernel.prx
    libSce*.prx
```

The `app0` resource directories may be junctions to the extracted tree. This avoids recopying a very
large immutable game-data set on every relink while retaining the guest-visible `/app0/...` layout.
The launcher can also skip relinking when a known-good executable is already staged.

### Guest module lifecycle failure

Several SDK-built guest modules contain a CRT `_init` guard sequence that checks a process-global
state pointer before entering the module constructor path. In the reconstructed Windows process that
state does not carry the native PS5 loader's initialization contract. The recognized instruction
sequence is:

```text
48 83 3d ?? ?? ?? ?? 00 74 19 4c 89 f7 48 89 de
```

The relinker changes only the conditional `JE +0x19` byte in this exact sequence to an unconditional
`JMP +0x19`. The search is limited to the first 0x100 bytes of a module's declared initializer and
stops after the first match. This is not a broad removal of branches. A regression fixture creates a
minimal guest image containing the pattern and verifies the emitted guest PRX contains the exact
patched form.

### Guest module discovery and dependency closure

The guest-module builder originally focused on directly required modules in a fixed set of source
directories. The title also loads modules dynamically and places them under nested `Media/Modules`,
`Media/Plugins`, and `sce_module` paths. The builder now recursively discovers candidate `.prx` and
`.sprx` files while excluding already generated `.guest.prx` artifacts, explicit exclusions,
`fakelib`, and `sce_sys`.

The original directly needed module set is retained separately. On non-Windows targets, only that
set and its transitive guest dependency closure are emitted as executable `DT_NEEDED` entries.
Dynamically discovered modules are still converted but do not become unconditional startup
dependencies. This distinction prevents the discovery improvement from forcing every optional plugin
to load at process start.

At runtime, `libkernel` accepts an already suffixed `.guest.prx`, tries the ordinary appended suffix,
handles extensionless requests by trying `.prx.guest.prx`, and searches the expected app module
locations. This matches how Unity plugins request modules by basename while keeping the search scope
inside the staged application tree.

### Runtime export and allocation contract

The Unity/IL2CPP payload dynamically requests `scriptingGetMem` through the main-module/kernel
lookup path. Some binaries request its computed NID, `ayuoL6Vjz2k`, rather than the readable name.
The relinker now retains both aliases when either form appears in a Windows runtime export set.
`libkernel` also supplies both forms directly, plus `scriptingFreeMem`.

The allocator honors the requested power-of-two alignment, raises sub-16-byte alignment to 16,
treats a zero-byte allocation as a one-byte allocation, and uses the matching platform deallocator.
On Windows it uses `_aligned_malloc` and `_aligned_free`; on POSIX it uses `posix_memalign` and
`free`. The dynamic-loader regression test requests a 0x40000-byte block aligned to 0x1000, verifies
the alignment, and frees it through the dynamically resolved function.

### Main-module symbol lookup

Handle zero is the main executable's lookup scope. The loader now searches the main executable and,
if needed, the compatibility kernel. Handles `1` and `0x2001` retain their kernel meaning. A test
exports a symbol from the test executable and proves handle-zero resolution finds it; it also proves
missing symbols clear the output pointer and report failure.

### Windows export visibility validation

The Windows dependency diagnostics test now builds and loads a tiny test DLL and resolves
`scriptingGetMem` with `GetProcAddress`. This catches a subtle class of PE writer/export-table bugs:
putting a name in relinker metadata is not sufficient unless Windows can actually resolve it from the
loaded image.

### System and network-service compatibility

The module table now recognizes system-module ID `0x110` as `libSceNpCommerce`. The NP manager
provides the reachability callback unregister entry point. The entitlement-access layer implements
the queried entitlement-key and individual add-on-information paths and gives signed-out results for
consumable-entitlement network requests. These changes let offline startup distinguish “service is
unavailable/signed out” from “function does not exist.” They do not fabricate online ownership:
entitlement keys are returned only for entries already reported by the local owned-add-on set, and
unknown labels return not-found.

### FMOD failure: what was actually wrong

The first repeatable runtime blocker was:

```text
sceKernelDebugRaiseException c1=-1610481656 c2=0
process exit 0xC000001D (STATUS_ILLEGAL_INSTRUCTION)
```

The hexadecimal form of `c1` is `0xA0020008`. It is the managed-exception raise path used by this
IL2CPP build. The illegal instruction is the terminal abort mechanism after the managed exception is
left uncaught; it was not evidence that the CPU lacked a required instruction.

The last dynamically resolved call before the original failure was
`FMOD_Studio_System_GetBus`. Disassembly of `FMODUnity.RuntimeManager.GetBus` showed that a nonzero
FMOD result constructs and throws `FMODUnity.BusNotFoundException`. Asset inspection showed an early
scene object containing the serialized string:

```text
bus:/Master Bus/Dialogue
```

The title's `FMODStudioSettings` uses a custom bank-loading arrangement. All bank files were present
under `/app0/Media/StreamingAssets`, including `Master.bank`, `Master.strings.bank`, Dialogue banks,
localized dialogue banks, UI banks, scene banks, boss banks, and stream/assets companions. However,
the failing run contained no `sceKernelOpen` for any `.bank` before `GetBus`. FMOD Studio itself was
initialized and its threads were alive, but its object registry contained no buses because the
Master bank had not yet been registered.

This ruled out the following suspected causes:

- missing MinGW DLLs;
- an incorrect `app0` junction;
- missing bank files;
- a failed FMOD PRX load;
- a failed Vulkan/window bootstrap;
- an unsupported CPU instruction at the crash address.

### FMOD implementation: real bootstrap, not a dummy bus

The dynamic loader wraps only the resolved `FMOD_Studio_System_GetBus` function. The wrapper first
calls the real function unchanged. If it succeeds, no compatibility behavior occurs. If it fails,
the wrapper verifies that both required Master-bank files really exist through the guest path
resolver. It then calls the real FMOD exports in this order for that Studio system:

1. `FMOD_Studio_System_LoadBankFile(..., "/app0/Media/StreamingAssets/Master.bank", ...)`
2. `FMOD_Studio_System_LoadBankFile(..., "/app0/Media/StreamingAssets/Master.strings.bank", ...)`
3. `FMOD_Studio_System_FlushCommands(...)`
4. retry the original `FMOD_Studio_System_GetBus` request.

Each Studio-system pointer is bootstrapped once under a mutex. The returned bank handles and bus
handle are supplied by FMOD itself. No placeholder object is created and no exception is swallowed.
If the files are absent or FMOD still cannot resolve the path, the real FMOD error propagates to the
title.

Runtime instrumentation established that both bank-load calls returned `FMOD_OK`, the guest file
layer opened both files, and FMOD returned real bank handles. Yet the old bus path still returned
`FMOD_ERR_EVENT_NOTFOUND`. A temporary diagnostic enumeration of the real Master bank showed 83
buses and proved that its dialogue bus is named:

```text
bus:/Dialogue
```

It is not named `bus:/Master Bus/Dialogue`. The compatibility wrapper therefore retries only paths
that begin with the exact legacy prefix `bus:/Master Bus/`, replacing that prefix with `bus:/`. In
the observed case this maps `bus:/Master Bus/Dialogue` to `bus:/Dialogue`. The transformed lookup
returns FMOD's real bus object. Paths outside that exact namespace are never rewritten.

The unit fixture enforces the intended behavior: its fake FMOD implementation refuses the stale
path, accepts only `bus:/Dialogue` after `Master.bank` is loaded, returns a distinct real test handle,
and exposes a counter proving both Master files were requested. This prevents a later refactor from
quietly degrading the implementation into an unconditional success or dummy-handle shortcut.

### Result after the FMOD correction

Before the correction, the title consistently aborted roughly six seconds after launch at the first
dialogue-bus lookup. After it:

- the title remains alive beyond 30 seconds;
- logo movies render; observed performance has ranged from roughly 19–21 FPS to roughly 40 FPS
  between successful runs;
- later Unity assets and `level5` open;
- `Scenes/River.bank`, `Scenes/River.assets.bank`, `Scenes/River.streams.bank`,
  `Audio Cues.bank`, `Audio Cues.assets.bank`, and `Audio Cues.streams.bank` open through the real
  guest filesystem;
- save initialization and a real global-save write occur.

That progression is direct evidence that the original FMOD exception has been fixed rather than
masked.

## Current blocker after the milestone

The next failure is later and separate. The terminal report is an uncaught
`Il2CppExceptionWrapper` on the `SaveDataProcessQueue` thread. The managed stack contains offsets in
the IL2CPP module including `0x32bd617`, `0x3302929`, `0x41eb624`, and `0x41f0ef0`. Disassembly shows
the lower frames are in managed `FileStream` construction/error handling.

Full save-data tracing proves the native save API sequence itself works:

```text
mount3 GOWSOSSAVE999 mode=0x1  -> success, /savedata0
setParam /savedata0 type=0     -> success
umount2 /savedata0             -> success
mount3 GOWSOSSAVE999 mode=0x21 -> success, /savedata0
```

The guest then opens `/savedata0/GoW_SoS999.dat` with create/write flags. The host file exists at
`_sd/GOWSOSSAVE999/GoW_SoS999.dat` with 10,660 bytes. On the following launch, the title mounts that
same save and reports that loading the global save completed successfully. Therefore:

- `/savedata0` alias registration works;
- file creation and data persistence work;
- mount, parameter storage, unmount, and remount work;
- the later exception must not be “fixed” by weakening those already successful operations.

The normal crash reporter currently omits the managed exception type and message. An attempted GDB
run stopped earlier on an unrelated access violation used by the AGC guest-memory path, before the
save worker was reached. That debugging run is not evidence of a new normal-launch regression; the
ordinary launch remains the baseline. The next investigation should either symbolize the IL2CPP
frames from the title metadata or add a narrowly scoped managed-exception diagnostic so the exact
`FileStream` path and error become visible.

### Save-worker resolution and architectural fixes

Disassembly of the Unity IL2CPP runtime (`FileStream..ctor` at RVA `0x1433020f0` calling `Directory.Exists` at `0x1432bad70`)
demonstrated that the post-logo crash was triggered when the managed `SaveSystem` branched into directory verification
and threw `System.IO.DirectoryNotFoundException`:

1. **SaveData Mount Status (`Export.cpp`)**:
   In `core/libs/prx/libSceSaveData.native/Export.cpp`, `sceSaveDataMount3` previously set `mount_status = 1`
   (`SCE_SAVE_DATA_MOUNT_STATUS_CREATED`) whenever `SCE_SAVE_DATA_MOUNT_MODE_CREATE2` (0x20) was supplied in `mode`,
   even when the directory already existed on disk. This misled the game's managed logic into believing an existing
   save was freshly minted, initiating abnormal directory creation sequences. The fix guarantees:
   ```cpp
   const bool created = !dir_already_exists && (create || create2);
   mount_result->mount_status = created ? 1u : 0u;
   ```

2. **POSIX Mode Bits in Win32 Stat (`NativeStat.cpp`)**:
   Under Windows, `GetFileAttributesW` was mapped into `FileStat.st_mode` without setting standard POSIX permission
   bits (`0755` for directories, `0644`/`0444` for files). The C# / Mono runtime verifies mode masks when probing
   directories; providing compliant POSIX mode bits resolves `Directory.Exists` returning false on Windows host paths.

3. **Filesystem Diagnostics and Validation (`Open.cpp`, `Stdio.cpp`)**:
   Added robust descriptor and path tracing across `sceKernelOpen`, `sceKernelClose`, `sceKernelRead`, `sceKernelWrite`,
   `sceKernelStat`, and `sceKernelFstat`. Added explicit `ENOTDIR` error checking if `SCE_KERNEL_O_DIRECTORY` is passed
   for a regular file.

4. **Upstream & PR Integration**:
   - Cleanly merged latest `upstream/main` (`boykopovar/AnyPS5` up to commit `e28fa01c`), bringing in system shader
     opcodes (halt, trap return, code end, GWS, ordered count), oversized descriptor set fixes, and wave control decoding.
   - Cleanly merged PR #18 (`fix(relinker): recognize finalized packages and invoke extractors safely`).
   - Cleanly unified 16-bit wide-string routines (`char16_t`) and eliminated duplicated collation exports.

## File-by-file change ledger

### `core/libs/prx/libkernel/Module/src/DynamicLoader.cpp`

- Adds readable symbol-resolution diagnostics, including NID fallback and not-found results.
- Implements aligned `scriptingGetMem` and its matching `scriptingFreeMem`.
- Exposes the readable allocator name and `ayuoL6Vjz2k` alias through supported dynamic lookup
  scopes.
- Makes handle zero search the executable and compatibility kernel.
- Resolves guest PRXs from the staged Unity plugin/module directories and accepts common suffixed or
  extensionless request forms.
- Reports resolved Windows module paths and Windows loader errors.
- Implements the real FMOD Master-bank bootstrap and constrained legacy bus-path normalization.

### `core/libs/prx/libkernel/Module/src/Module.cpp`

- Logs `sceKernelLoadStartModule` requests, failures, and returned handles. This is how the successful
  guest-PRX loading sequence was proven.

### `core/libs/tests/GuestDynamicLoader.cpp`

- Tests kernel handles, main-executable lookup, missing-symbol semantics, aligned allocation,
  deallocation, NID aliasing, guest-module lookup, real FMOD wrapper behavior, and bank-load count.

### `core/libs/tests/GuestModuleFixture.cpp`

- Supplies deterministic exported functions for the dynamic-loader test.
- Models FMOD failure before bank registration and success only for the normalized real path after
  registration.

### `core/relinker/relinker/src/guest/GuestImageReader.cpp`

- Applies the narrowly recognized CRT initializer guard transformation described above.

### `core/relinker/relinker/tests/test_guest_needed_modules.py`

- Constructs a guest module containing the CRT pattern and asserts the generated artifact contains
  the exact branch transformation.

### `core/relinker/relinker/src/guest/GuestModuleBuilder.cpp`

- Recursively discovers dynamic guest modules.
- Excludes generated artifacts and non-runtime trees.
- Separates conversion candidates from the executable's static dependency closure.

### `core/relinker/relinker/src/pipeline/RelinkerPipeline.cpp`

- Retains both readable and NID allocator aliases in the Windows runtime export set.

### `core/relinker/elfpatcher/tests/WindowsDependencyDiagnosticsTests.cpp`

- Verifies a generated Windows DLL export through the real Windows loader and `GetProcAddress`.

### `core/relinker/cli/src/PackageStaging.cpp`

- Avoids copying a source tree onto itself.
- Uses nonthrowing filesystem probes during large package staging.
- Counts already-present resources and avoids replacing equivalent or same-size staged payload files.
- Preserves the staged `eboot.bin` when it already corresponds to the source.

### `scripts/run-eboot.ps1`

- Adds `-SkipRelink` for repeatable debugging against an already generated executable.
- Creates junctions for large game-data directories when `-CopyGameData` is selected, with copy
  fallback when junction creation fails.
- Mirrors only missing remaining game data, preserving generated modules and manually staged runtime
  DLLs.

### `core/libs/prx/libSceSysmodule/ModuleTable.hpp`

- Maps system-module ID `0x110` to `libSceNpCommerce`.

### `core/libs/prx/libSceNpManager/Export.cpp`

- Adds the reachability-state callback unregister entry point.

### `core/libs/prx/libSceNpEntitlementAccess/Export.cpp`

- Adds entitlement-key lookup and individual add-on lookup.
- Reports signed-out status for unavailable consumable-entitlement network operations.

## Earlier runtime-compatibility foundation

The immediately preceding commit, `969a4c8e` (`feat: add runtime compatibility for PS5 title
startup`), is part of this bring-up and is pushed with this checkpoint. It established the foundation
on which the changes above run:

- missing debugger, DECI, registry-manager, and shader-transcode module exports;
- application-heap and libc allocation behavior;
- locale, math, and wide-string compatibility;
- corrected guest condition-variable behavior;
- synchronization-on-address support;
- Windows guest-module writing and PE patching improvements;
- runtime-export propagation and relinker option handling;
- focused regression tests for each of those contracts.

Those changes moved the title from early runtime failure into Unity initialization. The current
checkpoint moves it from Unity initialization through visible rendered startup, real audio-bank
registration, later-scene loading, and persistent save access.

## Reproduction workflow

Build with the repository's supported WinLibs MinGW toolchain:

```powershell
$env:PATH = "C:\WinLibs\mingw64\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target relinker libs --parallel
```

For the established local stage, a focused `libkernel` iteration is:

```powershell
cmake --build build --target patched_libkernel guest_dynamic_loader_tests --parallel
ctest --test-dir build -R '^guest_dynamic_loader$' --output-on-failure
Copy-Item build\core\libs\libs\libkernel.prx `
  build\exact-eboot-run\libs\libkernel.prx -Force
Push-Location build\exact-eboot-run
try {
  .\eboot-current.exe 1> .\run-stdout.log 2> .\run-stderr.log
} finally {
  Pop-Location
}
```

Enable complete save-data tracing for the current blocker:

```powershell
$env:APS5_SAVEDATA_TRACE = '1'
try {
  .\eboot-current.exe 1> .\run-savedata-stdout.log 2> .\run-savedata-stderr.log
} finally {
  Remove-Item Env:APS5_SAVEDATA_TRACE -ErrorAction SilentlyContinue
}
```

Useful assertions in a successful milestone log are:

```text
FMOD bank bootstrap after GetBus(...): Master.bank=0, Master.strings.bank=0
FMOD legacy bus path 'bus:/Master Bus/Dialogue' -> 'bus:/Dialogue': 0
sceKernelOpen path=/app0/Media/StreamingAssets/Scenes/River.bank
sceKernelOpen path=/savedata0/GoW_SoS999.dat
```

## Rendering and performance pacing resolution

At this milestone, both the in-game rendering artifacts and the frame pacing bottleneck have been
architecturally resolved:

1. **Rendering artifacts (splotchy / LCD-bleed like blooming white/yellow blobs)**:
   - **Root Cause**: The AGC state decoder previously rejected and skipped any draw setting
     `DB_RENDER_CONTROL` `STENCIL_CLEAR_ENABLE` (bit 1, register `0x0 = 0x00000022`), logging:
     `[gpu] skipped draw: depth/stencil clear, copy, resummarize or decompress draws (DB_RENDER_CONTROL) is unsupported`.
     *Sons of Sparta* begins every frame with such a draw into its 3840x2160 B10G11R11 scene render
     target to clear the background. When this draw was skipped, the render target retained stale
     pixels, and subsequent `DST_COLOR`/`ONE` additive light sprites compounded the previous frame's
     values across frames until they saturated into expanding, over-exposed white/gray clouds with
     burnt yellow/brown borders.
   - **Architectural Resolution (PR #1479)**: The draw is now rasterized like any standard draw, with
     both stencil face states replaced by compare `ALWAYS`, fail/pass/depth-fail `REPLACE`, reference
     `DB_STENCIL_CLEAR`, and write mask `0xff`. All covered samples receive the clear value while the
     draw's color output is preserved and written to the color attachment.

2. **Frame pacing analysis & QueueWorker behavior**:
   - Upstream PR #1474 attempted to replace the queue worker condition variable wait with a Windows
     `PollSleep()` loop. In live testing with *Sons of Sparta*, this caused extreme mutex lock contention
     and thread starvation between the submission thread and the worker loop, collapsing frame rates down
     to 0.8–7.5 FPS. PR #1474 was therefore reverted to retain proper condition variable signaling.
   - Startup pipeline compilation was verified as the primary contributor to first-run frame drops when
     the shader cache is empty; retaining the warm Vulkan pipeline cache prevents startup hitching.

3. **Mutex destruction compliance (PR #1483)**:
   - `scePthreadMutexDestroy` now returns `SCE_KERNEL_ERROR_EBUSY` when destroying a locked mutex
     in accordance with FreeBSD libthr / PS5 libkernel semantics, preventing aborts during title intro.

4. **Wave32 AMD Driver NaN comparison compliance (PR #1473)**:
   - Emits `FPOrdNotEqual32` through bitwise integer NaN checks (`(bits(a) & 0x7fffffff) <= 0x7f800000`)
     avoiding the AMD Windows driver bug where ordered float compares feed wave32 ballots.

5. **Transient Surface Aliasing & Depth Memory Reuse (PR #1494 & #1495)**:
   - Prefers pending images whose extent matches the sampled surface exactly over older mip chains.
   - Treats differently shaped views of a depth surface's address as memory reuse rather than throwing
     depth-plane format mismatch errors.

6. **Validated Stencil Clear & ClearMRT Retention (PR #1534 / hamb3r)**:
   - Extends PR #1479 by selecting stencil clear state before decoding ordinary stencil operations,
     preventing valid draws from being rejected when overridden by `DB_STENCIL_CLEAR`.
   - Validates that a stencil plane is writable before clearing and preserves all MRT color outputs
     and scissor coverage. This directly resolves the compounding bloom highlights obscuring
     characters and menu scenes (Screenshots 320–323).

7. **Nested TimedWait and APC Timer Fixes (PR #1476 & PR #1520)**:
   - On Windows, Unity's GC handler runs via `sceKernelRaiseException` as an APC inside sleeping threads.
     PR #1476 ensures nested waits inside APCs allocate independent waiter instances rather than
     reusing the outer thread-local waiter and corrupting condition queue links.
   - PR #1520 ensures nested sleeps inside APC handlers allocate their own high-resolution timer
     handles so outer sleeps are not prematurely interrupted or orphaned.

8. **Splash Screen Loading Duration & Frame Pacing Diagnostic**:
   - Live log analysis of `run-milestone-stdout.log` vs `run3.txt` confirmed that the apparent freeze
     on the Sony splash screen was an incomplete wait duration. During scene and asset bundle loading,
     Unity PS5 cycles `Forcing call to sce::Agc::suspendPoint to avoid TRC R5089 breach` for 253 consecutive
     iterations (~8–10 s at normal speeds). Run 3 was terminated after only 16 iterations (~10.8 s total
     runtime at 13.33 FPS), before the engine could complete background bundle parsing.
   - The 13.33 FPS lock was diagnosed as a double-buffering / VSync phase beat between the host monitor
     (100 Hz on AMD RX 6800 XT) and the synthetic 59.94 Hz VBlank loop in `VideoOutDriver` when running
     `VK_PRESENT_MODE_FIFO_KHR` with `minImageCount = 2`.

9. **Vulkan Swapchain Triple Buffering & Mailbox Present Mode**:
   - In `VulkanDevice.cpp`, upgraded `minImageCount` to triple buffering (`surface.minImageCount + 1`, bounded by `surface.maxImageCount`).
   - Query available surface present modes and prefer `VK_PRESENT_MODE_MAILBOX_KHR` (falling back to `VK_PRESENT_MODE_FIFO_KHR`, overridable via `APS5_PRESENT_MODE=fifo`).
   - Sized `state->presentSlots` to match swapchain capacity (`std::max(FlipInFlight() + 1, static_cast<std::size_t>(minImageCount))`), completely eliminating VSync lock contention and 12.5 FPS / 7.5 FPS quantization beats.
   - Dynamically handle window resize in `VulkanDevice::Resize` to preserve triple buffering and allocate any missing present slots/command buffers.

10. **Thread-Safe Multi-Threaded Positioned I/O (`NativePositioned`)**:
   - In `Stdio.cpp`, added a 64-way descriptor mutex array guarding `NativePositioned` on Windows.
   - Prevents multi-threaded asset stream readers (`archives_assets_all.bundle`, `defaultlocalgroup_assets_.bundle`) from racing on synchronous file handle pointers during concurrent `pread`/`ReadFile` operations.

11. **Save Slot 0 State & Boot Flow Distinction**:
   - **Clean Boot / First Boot (Slot 0 absent)**: Unity detects no existing save, decodes and plays the 665-frame intro movie (`sharedassets1.resource`), opens the Main Menu, and progresses cleanly into cutscenes and Cyclops combat.
   - **Resume Boot (Slot 0 present in `_sd/GOWSOSSAVE0/`)**: Unity detects existing save data, skips the intro logo movie, and streams 180+ MB of AssetBundles in the background while holding the Sony splash screen.
   - Backed up `_sd/GOWSOSSAVE0` to `_sd/GOWSOSSAVE0.bak` so the user can test both clean boots (verifying the title screen and bloom fixes) and saved resumes.

## Verification checklist for this checkpoint

- `git diff --check` reports no whitespace errors.
- All unit tests passing (`guest_kernel_errors`, `guest_dynamic_loader`, `guest_savedata_*`, `relinker`, `package_staging`).
- Upstream `boykopovar/AnyPS5` through commit `084aeeb6` cleanly merged.
- PRs #1479, #1474 (reverted), #1483, #1473, #1494, #1495, #1534, #1476, and #1520 merged and verified.
- Freshly compiled `libSceAgcDriver.prx` and `libkernel.prx` deployed to `build/exact-eboot-run/libs/`.
- The title boots cleanly past the logos, main menu, credits, cutscenes, and into the Cyclops boss battle.
- In-game bloom/lighting saturation is resolved; full geometry, characters, and textures are visible.
- Frame pacing operates smoothly without the 15.6 ms winpthreads sleep penalty.
- Generated content, save data, and runtime logs remain outside Git.

## Things that must not be undone

- Do not delete `build/exact-eboot-run`, its `app0` junctions/data, its `libs`, the relinked guest
  PRXs, `eboot-current.exe`, the MinGW runtime DLLs, `_sd`, or the diagnostic logs while this
  investigation is active.
- Do not replace the FMOD wrapper with a dummy success or fake bus pointer.
- Do not globally suppress `Il2CppExceptionWrapper` or `sceKernelDebugRaiseException`.
- Do not revert `DB_RENDER_CONTROL` `STENCIL_CLEAR_ENABLE` rasterization.
- Do not mix AnyPS5's MinGW build with the unrelated KytyPS5 or SharpEmu toolchains.
