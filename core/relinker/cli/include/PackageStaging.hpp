#ifndef CORE_RELINKER_CLI_INCLUDE_PACKAGESTAGING_HPP
#define CORE_RELINKER_CLI_INCLUDE_PACKAGESTAGING_HPP

#include <InputDetector.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Relinker {

struct PackageStagingOptions {
    std::filesystem::path PackagePath;
    std::filesystem::path OutputDirectory;
    std::filesystem::path StagingDirectory;
    std::string ExtractorCommand;
    std::string Passcode;
    std::string ImageKey;
};

struct StagedPackageResult {
    std::filesystem::path App0Directory;
    std::filesystem::path EbootPath;
    std::filesystem::path SceModuleDirectory;
    std::vector<std::filesystem::path> GuestPrxModules;
    std::size_t ResourceFilesCount = 0;
    std::string ContentId;
};

class PackageStaging {
public:
    static StagedPackageResult StagePackage(const PackageStagingOptions& options, const DetectionResult& detection);
    static StagedPackageResult StageExtractedApp(const std::filesystem::path& appDir, const std::filesystem::path& outputDir);
};

} // namespace Relinker

#endif
