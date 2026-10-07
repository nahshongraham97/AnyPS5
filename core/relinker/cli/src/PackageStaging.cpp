#include <PackageStaging.hpp>
#include <domain/Types.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace Relinker {

namespace {

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
            "). Decryption requires a lawfully supplied image key (--image-key <hex>). Retail packages cannot be extracted without valid keys."
        );
    }

    if (options.ExtractorCommand.empty()) {
        throw Domain::RelinkerException(
            "Package container (" + options.PackagePath.filename().string() +
            ") requires an external extractor. Specify --extractor <cmd> (with --input, --output, --passcode, --image-key contract) or supply an extracted app directory."
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
    std::filesystem::create_directories(app0Target);

    std::string cmd = options.ExtractorCommand;
    cmd += " --input \"" + options.PackagePath.string() + "\"";
    cmd += " --output \"" + app0Target.string() + "\"";

    if (!options.Passcode.empty()) {
        cmd += " --passcode \"" + options.Passcode + "\"";
    }
    if (!imageKey.empty()) {
        cmd += " --image-key \"" + imageKey + "\"";
    }

    std::cout << "Executing package extractor for " << options.PackagePath.filename().string() << " into " << app0Target.string() << '\n';

    const int exitCode = std::system(cmd.c_str());
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
