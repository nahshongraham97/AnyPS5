#include <Cli.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace Cli {

Args ParseArgs(int argc, char* argv[]) {
    Args args;
    bool unusedFilterSpecified = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--skip-syscall-check") {
            args.skipSyscallCheck = true;
        } else if (arg == "--skip-sce-module") {
            args.skipSceModule = true;
        } else if (arg == "--exclude-sce-module") {
            if (i + 1 >= argc)
                throw std::runtime_error("--exclude-sce-module requires a file name");
            args.excludedSceModules.insert(argv[++i]);
        } else if (arg == "--to-intel") {
            args.toIntel = true;
        } else if (arg.rfind("unused-filter=", 0) == 0) {
            const std::string value = arg.substr(14);
            if (unusedFilterSpecified || value.size() != 1 || value[0] < '0' || value[0] > '2')
                throw std::runtime_error("unused-filter must be specified once with a value of 0, 1 or 2");
            args.unusedFilterLevel = static_cast<std::uint32_t>(value[0] - '0');
            unusedFilterSpecified = true;
        } else if (arg == "--registry") {
            args.writeRegistry = true;
        } else if (arg == "--rpath") {
            if (i + 1 >= argc)
                throw std::runtime_error("--rpath requires a value");
            args.runPath = argv[++i];
        } else if (arg == "--windows") {
            args.toWindows = true;
        } else if (arg == "--lazy-binding") {
            args.lazyBinding = true;
        } else if (arg == "--autorun") {
            args.autorun = true;
        } else if (arg == "--windows-diagnostics") {
            args.windowsDiagnostics = true;
        } else if (arg == "--windows-gui") {
            args.windowsGui = true;
        } else if (arg == "--extractor") {
            if (i + 1 >= argc)
                throw std::runtime_error("--extractor requires a command value");
            args.extractorCommand = argv[++i];
        } else if (arg == "--passcode") {
            if (i + 1 >= argc)
                throw std::runtime_error("--passcode requires a passcode value");
            args.passcode = argv[++i];
        } else if (arg == "--image-key") {
            if (i + 1 >= argc)
                throw std::runtime_error("--image-key requires a key value");
            args.imageKey = argv[++i];
        } else if (arg == "--staging-dir") {
            if (i + 1 >= argc)
                throw std::runtime_error("--staging-dir requires a directory path");
            args.stagingDir = argv[++i];
        } else if (arg.rfind("--", 0) == 0 || arg == "unused-filter") {
            throw std::runtime_error("unknown option: " + arg);
        } else if (args.inputPath.empty()) {
            args.inputPath = arg;
        } else if (args.outputPath.empty()) {
            args.outputPath = arg;
        } else {
            throw std::runtime_error("unexpected argument: " + arg);
        }
    }

    if (args.skipSceModule && !args.excludedSceModules.empty())
        throw std::runtime_error("--exclude-sce-module conflicts with --skip-sce-module");

    if (args.windowsDiagnostics && !args.toWindows)
        throw std::runtime_error("--windows-diagnostics requires --windows");

    if (args.windowsGui && !args.toWindows)
        throw std::runtime_error("--windows-gui requires --windows");

    if (args.inputPath.empty() || args.outputPath.empty())
        throw std::runtime_error(
            "Usage: relinker [--windows] [--windows-diagnostics] [--windows-gui] [--skip-syscall-check] [--skip-sce-module] [--exclude-sce-module <file>]... [--to-intel] [unused-filter=0|1|2] [--registry] [--rpath <path>] [--lazy-binding] [--autorun] [--extractor <cmd>] [--passcode <pass>] [--image-key <key>] [--staging-dir <dir>] <input.elf|eboot.bin|app_dir|package.pkg> <output.elf|output.exe>\n"
            "Supports raw ELF, plaintext PS4/PS5 SELF, extracted app directories, and staged PS4/PS5 PKG/fPKG containers.\n"
            "Protected retail content requires lawfully supplied decryption material (--image-key)."
        );

    return args;
}

}
