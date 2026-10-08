#include <PackageStaging.hpp>
#include <domain/Types.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#include <vector>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace Relinker {

namespace {

#if defined(_WIN32)
std::wstring QuoteWindowsArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted += ch;
            slashes = 0;
        } else {
            quoted.append(slashes, L'\\');
            quoted += ch;
            slashes = 0;
        }
    }
    quoted.append(slashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}
#endif

int InvokeExtractor(const std::filesystem::path& executable, const std::filesystem::path& input,
                    const std::filesystem::path& output) {
    if (!std::filesystem::is_regular_file(executable))
        throw Domain::RelinkerException("Package extractor is not an executable file: " + executable.string());
#if defined(_WIN32)
    const auto exe = std::filesystem::absolute(executable).wstring();
    const auto package = input.wstring();
    const auto destination = output.wstring();
    auto commandLine = QuoteWindowsArgument(exe) + L" --input " + QuoteWindowsArgument(package) +
                       L" --output " + QuoteWindowsArgument(destination);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &process))
        throw Domain::RelinkerException("Could not start package extractor: " + executable.string());
    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    const BOOL gotExitCode = GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (waitResult != WAIT_OBJECT_0 || !gotExitCode)
        throw Domain::RelinkerException("Could not wait for package extractor: " + executable.string());
    return static_cast<int>(exitCode);
#else
    const auto exe = std::filesystem::absolute(executable).string();
    const auto package = input.string();
    const auto destination = output.string();
    char* const arguments[] = {const_cast<char*>(exe.c_str()), const_cast<char*>("--input"),
                               const_cast<char*>(package.c_str()), const_cast<char*>("--output"),
                               const_cast<char*>(destination.c_str()), nullptr};
    pid_t pid = 0;
    const auto started = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, arguments, environ);
    if (started != 0) throw Domain::RelinkerException("Could not start package extractor: " + exe);
    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) throw Domain::RelinkerException("Could not wait for package extractor: " + exe);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
}

void CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination, std::size_t& fileCount) {
    if (!std::filesystem::exists(source)) return;
    std::filesystem::create_directories(destination);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        const auto relative = std::filesystem::relative(entry.path(), source);
        const auto target = destination / relative;

        if (entry.is_directory()) {
            std::filesystem::create_directories(target);
        } else if (entry.is_regular_file()) {
            if (!std::filesystem::exists(target) || !std::filesystem::equivalent(entry.path(), target)) {
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing);
                ++fileCount;
            }
        }
    }
}

} // namespace

StagedPackageResult PackageStaging::StagePackage(const PackageStagingOptions& options, const DetectionResult& detection) {
    if (!options.Passcode.empty() || !options.ImageKey.empty())
        throw Domain::RelinkerException("Passcodes and image keys must be provided through ANYPS5_PASSCODE or ANYPS5_IMAGE_KEY in the extractor environment, not command-line arguments");
    std::string imageKey = options.ImageKey;
    if (imageKey.empty()) {
        const char* envKey = std::getenv("ANYPS5_IMAGE_KEY");
        if (envKey && *envKey) {
            imageKey = envKey;
        }
    }

    if (detection.PkgType == PackageType::Retail && imageKey.empty()) {
        throw Domain::RelinkerException(
            "Package is protected retail content (Content ID: " +
            (detection.ContentId.empty() ? "Unknown" : detection.ContentId) +
            "). Decryption requires a lawfully supplied ANYPS5_IMAGE_KEY environment value. Retail packages cannot be extracted without valid keys."
        );
    }

    if (options.ExtractorCommand.empty()) {
        throw Domain::RelinkerException(
            "Package container (" + options.PackagePath.filename().string() +
            ") requires an external extractor executable. Specify --extractor <path> (with --input and --output arguments; secrets inherited from the environment) or supply an extracted app directory."
        );
    }

    std::filesystem::path stagingRoot = options.StagingDirectory;
    if (stagingRoot.empty()) {
        if (!options.OutputDirectory.empty()) {
            stagingRoot = options.OutputDirectory;
        } else {
            stagingRoot = options.PackagePath.parent_path() / (options.PackagePath.stem().string() + "_staged");
        }
    }

    const std::filesystem::path app0Target = stagingRoot / "app0";
    if (std::filesystem::exists(app0Target) && !std::filesystem::is_empty(app0Target))
        throw Domain::RelinkerException("Package staging app0 directory is not empty: " + app0Target.string());
    std::filesystem::create_directories(app0Target);

    std::cout << "Executing package extractor for " << options.PackagePath.filename().string() << " into " << app0Target.string() << '\n';

    const int exitCode = InvokeExtractor(options.ExtractorCommand, options.PackagePath, app0Target);
    if (!imageKey.empty()) {
        imageKey.replace(0, imageKey.size(), imageKey.size(), '\0');
    }
    if (exitCode != 0) {
        throw Domain::RelinkerException("Package extractor failed with exit code " + std::to_string(exitCode));
    }

    return StageExtractedApp(stagingRoot, options.OutputDirectory);
}

StagedPackageResult PackageStaging::StageExtractedApp(const std::filesystem::path& appDir, const std::filesystem::path& outputDir, const std::filesystem::path& preferredExecutable) {
    if (!std::filesystem::exists(appDir)) {
        throw Domain::RelinkerException("Application directory does not exist: " + appDir.string());
    }

    std::filesystem::path sourceEboot;
    std::filesystem::path sourceAppRoot;

    if (!preferredExecutable.empty() && std::filesystem::exists(preferredExecutable)) {
        sourceEboot = preferredExecutable;
        sourceAppRoot = preferredExecutable.parent_path();
    } else {
        const std::filesystem::path ebootCandidates[] = {
            appDir / "app0" / "eboot.bin",
            appDir / "eboot.bin",
            appDir / "app" / "eboot.bin"
        };

        for (const auto& candidate : ebootCandidates) {
            if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
                sourceEboot = candidate;
                sourceAppRoot = candidate.parent_path();
                break;
            }
        }
    }

    if (sourceEboot.empty()) {
        for (const auto& entry : std::filesystem::directory_iterator(appDir)) {
            if (entry.is_regular_file()) {
                const auto ext = entry.path().extension().string();
                if (ext == ".elf" || ext == ".bin") {
                    sourceEboot = entry.path();
                    sourceAppRoot = appDir;
                    break;
                }
            }
        }
    }

    if (sourceEboot.empty()) {
        throw Domain::RelinkerException("Application directory does not contain eboot.bin, app0/eboot.bin, or an executable ELF: " + appDir.string());
    }

    StagedPackageResult result;
    result.EbootPath = sourceEboot;
    result.App0Directory = sourceAppRoot;

    // Check for sce_sys and param.json for Content ID
    const std::filesystem::path paramJson = sourceAppRoot / "sce_sys" / "param.json";
    if (std::filesystem::exists(paramJson)) {
        std::ifstream f(paramJson);
        std::string line;
        while (std::getline(f, line)) {
            auto pos = line.find("\"contentId\"");
            if (pos != std::string::npos) {
                auto firstQuote = line.find('"', pos + 11);
                if (firstQuote != std::string::npos) {
                    auto secondQuote = line.find('"', firstQuote + 1);
                    if (secondQuote != std::string::npos) {
                        result.ContentId = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                        break;
                    }
                }
            }
        }
    }

    // Stage resources to output directory if specified and different from source
    if (!outputDir.empty()) {
        const std::filesystem::path destApp0 = outputDir / "app0";
        if (destApp0.lexically_normal() != sourceAppRoot.lexically_normal()) {
            std::filesystem::create_directories(destApp0);

            // Copy sce_sys if present
            const auto sourceSceSys = sourceAppRoot / "sce_sys";
            if (std::filesystem::exists(sourceSceSys)) {
                CopyDirectoryRecursive(sourceSceSys, destApp0 / "sce_sys", result.ResourceFilesCount);
            }

            // Copy other asset files and directories beside eboot
            for (const auto& entry : std::filesystem::directory_iterator(sourceAppRoot)) {
                const auto name = entry.path().filename().string();
                if (name == "sce_sys" || name == "eboot.bin") continue;
                if (entry.is_directory()) {
                    CopyDirectoryRecursive(entry.path(), destApp0 / name, result.ResourceFilesCount);
                } else if (entry.is_regular_file()) {
                    const auto targetFile = destApp0 / name;
                    if (!std::filesystem::exists(targetFile) || !std::filesystem::equivalent(entry.path(), targetFile)) {
                        std::filesystem::copy_file(entry.path(), targetFile, std::filesystem::copy_options::overwrite_existing);
                        ++result.ResourceFilesCount;
                    }
                }
            }

            result.App0Directory = destApp0;
            result.EbootPath = destApp0 / "eboot.bin";
            if (std::filesystem::exists(sourceEboot) && (!std::filesystem::exists(result.EbootPath) || !std::filesystem::equivalent(sourceEboot, result.EbootPath))) {
                std::filesystem::copy_file(sourceEboot, result.EbootPath, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    // Find sce_module / sce_modules
    const std::filesystem::path moduleDirs[] = {
        result.App0Directory / "sce_module",
        result.App0Directory / "sce_modules",
        sourceAppRoot / "sce_module",
        sourceAppRoot / "sce_modules"
    };

    for (const auto& dir : moduleDirs) {
        if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
            result.SceModuleDirectory = dir;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    const auto ext = entry.path().extension().string();
                    if (ext == ".prx" || ext == ".sprx" || ext.empty()) {
                        result.GuestPrxModules.push_back(entry.path());
                    }
                }
            }
            break;
        }
    }

    return result;
}

} // namespace Relinker
