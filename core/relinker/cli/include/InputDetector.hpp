#ifndef CORE_RELINKER_CLI_INCLUDE_INPUTDETECTOR_HPP
#define CORE_RELINKER_CLI_INCLUDE_INPUTDETECTOR_HPP

#include <cstdint>
#include <filesystem>
#include <string>

namespace Relinker {

enum class InputFormat {
    Unknown,
    RawElf64,
    Ps4Self,
    Ps5Self,
    PackageContainer,
    ExtractedDirectory
};

enum class PackageType {
    Unknown,
    Retail,
    FakePkg
};

enum class TargetPlatform {
    Unknown,
    Ps4,
    Ps5
};

struct DetectionResult {
    InputFormat Format = InputFormat::Unknown;
    TargetPlatform Platform = TargetPlatform::Unknown;
    PackageType PkgType = PackageType::Unknown;
    bool IsEncryptedOrProtected = false;
    std::string ContentId;
    std::uint64_t DeclaredSize = 0;
    std::uint32_t EntryCount = 0;
    std::filesystem::path ResolvedExecutablePath;
    std::filesystem::path AppRootPath;
    std::string DiagnosticMessage;
};

class InputDetector {
public:
    static DetectionResult Detect(const std::filesystem::path& path);
};

} // namespace Relinker

#endif
