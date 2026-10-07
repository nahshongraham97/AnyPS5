#include <InputDetector.hpp>
#include <algorithm>
#include <fstream>
#include <vector>

namespace Relinker {

namespace {

constexpr std::uint32_t Ps4SelfMagic = 0x1d3d154f;
constexpr std::uint32_t Ps5SelfMagic = 0xeef51454;
constexpr std::uint32_t ElfMagic = 0x464c457f; // 0x7f, 'E', 'L', 'F' (little-endian)
constexpr std::uint32_t PkgMagic = 0x544e437f; // 0x7f, 'C', 'N', 'T' (little-endian read of big-endian 0x7f434e54)

std::uint32_t ReadU32LE(const std::vector<std::uint8_t>& buf, std::size_t offset) {
    if (offset + 4 > buf.size()) return 0;
    return static_cast<std::uint32_t>(buf[offset]) |
          (static_cast<std::uint32_t>(buf[offset + 1]) << 8) |
          (static_cast<std::uint32_t>(buf[offset + 2]) << 16) |
          (static_cast<std::uint32_t>(buf[offset + 3]) << 24);
}

std::uint16_t ReadU16LE(const std::vector<std::uint8_t>& buf, std::size_t offset) {
    if (offset + 2 > buf.size()) return 0;
    return static_cast<std::uint16_t>(buf[offset]) |
          (static_cast<std::uint16_t>(buf[offset + 1]) << 8);
}

std::uint32_t ReadU32BE(const std::vector<std::uint8_t>& buf, std::size_t offset) {
    if (offset + 4 > buf.size()) return 0;
    return (static_cast<std::uint32_t>(buf[offset]) << 24) |
           (static_cast<std::uint32_t>(buf[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(buf[offset + 2]) << 8) |
            static_cast<std::uint32_t>(buf[offset + 3]);
}

std::uint64_t ReadU64BE(const std::vector<std::uint8_t>& buf, std::size_t offset) {
    if (offset + 8 > buf.size()) return 0;
    std::uint64_t val = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        val = (val << 8) | static_cast<std::uint64_t>(buf[offset + i]);
    }
    return val;
}

std::string ReadAsciiString(const std::vector<std::uint8_t>& buf, std::size_t offset, std::size_t maxLen) {
    if (offset >= buf.size()) return "";
    std::string result;
    const std::size_t end = std::min(buf.size(), offset + maxLen);
    for (std::size_t i = offset; i < end; ++i) {
        char c = static_cast<char>(buf[i]);
        if (c == '\0') break;
        if (c >= 32 && c <= 126) {
            result.push_back(c);
        } else {
            break;
        }
    }
    return result;
}

} // namespace

DetectionResult InputDetector::Detect(const std::filesystem::path& path) {
    DetectionResult result;

    if (!std::filesystem::exists(path)) {
        result.DiagnosticMessage = "Target path does not exist: " + path.string();
        return result;
    }

    if (std::filesystem::is_directory(path)) {
        const std::filesystem::path candidates[] = {
            path / "app0" / "eboot.bin",
            path / "eboot.bin",
            path / "app" / "eboot.bin"
        };

        std::filesystem::path chosenExecutable;
        std::filesystem::path chosenAppRoot;

        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
                chosenExecutable = candidate;
                chosenAppRoot = candidate.parent_path();
                break;
            }
        }

        if (chosenExecutable.empty()) {
            result.Format = InputFormat::Unknown;
            result.DiagnosticMessage = "Directory does not contain eboot.bin, app0/eboot.bin, or app/eboot.bin: " + path.string();
            return result;
        }

        // Inspect the underlying eboot.bin
        DetectionResult inner = Detect(chosenExecutable);
        result.Format = InputFormat::ExtractedDirectory;
        result.Platform = inner.Platform;
        result.IsEncryptedOrProtected = inner.IsEncryptedOrProtected;
        result.ResolvedExecutablePath = chosenExecutable;
        result.AppRootPath = chosenAppRoot;
        result.DiagnosticMessage = "Extracted application directory at " + path.string() + " (executable: " + chosenExecutable.filename().string() + ")";
        return result;
    }

    // Regular file: inspect header without reading whole file
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.DiagnosticMessage = "Cannot open file for reading: " + path.string();
        return result;
    }

    stream.seekg(0, std::ios::end);
    const auto fileSize = static_cast<std::uint64_t>(stream.tellg());
    stream.seekg(0, std::ios::beg);
    result.DeclaredSize = fileSize;

    if (fileSize < 4) {
        result.DiagnosticMessage = "Input is neither a raw ELF nor a recognized SELF image";
        return result;
    }

    const std::size_t headerChunkSize = static_cast<std::size_t>(std::min<std::uint64_t>(fileSize, 8192));
    std::vector<std::uint8_t> header(headerChunkSize);
    stream.read(reinterpret_cast<char*>(header.data()), headerChunkSize);
    const auto bytesRead = static_cast<std::size_t>(stream.gcount());
    header.resize(bytesRead);

    const std::uint32_t magicLE = ReadU32LE(header, 0);

    // 1. Raw ELF Check
    if (magicLE == ElfMagic) {
        result.Format = InputFormat::RawElf64;
        result.ResolvedExecutablePath = path;
        result.Platform = TargetPlatform::Ps4;
        result.DiagnosticMessage = "Raw little-endian x86-64 ELF binary";
        return result;
    }

    // 2. SELF Check (PS4 / PS5)
    if (magicLE == Ps4SelfMagic || magicLE == Ps5SelfMagic) {
        result.Platform = (magicLE == Ps5SelfMagic) ? TargetPlatform::Ps5 : TargetPlatform::Ps4;
        result.Format = (magicLE == Ps5SelfMagic) ? InputFormat::Ps5Self : InputFormat::Ps4Self;
        result.ResolvedExecutablePath = path;

        if (header.size() < 32) {
            result.IsEncryptedOrProtected = true;
            result.DiagnosticMessage = "Truncated SELF header";
            return result;
        }

        if (header[6] != 1) {
            result.IsEncryptedOrProtected = true;
            result.DiagnosticMessage = "Unsupported SELF endianness";
            return result;
        }

        const auto segmentCount = ReadU16LE(header, 24);
        const std::size_t elfOffset = 32 + static_cast<std::size_t>(segmentCount) * 32;

        if (header.size() >= elfOffset + 64) {
            const std::uint32_t embeddedElfMagic = ReadU32LE(header, elfOffset);
            if (embeddedElfMagic == ElfMagic && header[elfOffset + 4] == 2 && header[elfOffset + 5] == 1) {
                result.IsEncryptedOrProtected = false;
                result.DiagnosticMessage = (result.Platform == TargetPlatform::Ps5 ? "Plaintext PS5 FSELF" : "Plaintext PS4 FSELF");
                return result;
            }
        }

        // If embedded ELF header is absent or encrypted
        result.IsEncryptedOrProtected = true;
        result.DiagnosticMessage = (result.Platform == TargetPlatform::Ps5 ? "Encrypted PS5 retail SELF" : "Encrypted PS4 retail SELF") +
                                   std::string(" (requires decrypted ELF or lawful decryption keys)");
        return result;
    }

    // 3. Package Container Check (\x7fCNT)
    // Little-endian read of 0x7f, 'C', 'N', 'T' is 0x544e437f
    if (magicLE == PkgMagic) {
        result.Format = InputFormat::PackageContainer;

        if (header.size() >= 0x80) {
            const std::uint32_t pkgType = ReadU32BE(header, 0x04);
            result.EntryCount = ReadU32BE(header, 0x10);
            const std::uint64_t totalSize = ReadU64BE(header, 0x18);
            if (totalSize > 0) result.DeclaredSize = totalSize;

            const std::string contentId = ReadAsciiString(header, 0x40, 36);
            result.ContentId = contentId;

            if (contentId.find("CUSA") != std::string::npos) {
                result.Platform = TargetPlatform::Ps4;
            } else if (contentId.find("PPSA") != std::string::npos) {
                result.Platform = TargetPlatform::Ps5;
            }

            const std::uint32_t drmType = (header.size() >= 0x68) ? ReadU32BE(header, 0x64) : 0;
            const bool isFake = ((pkgType & 0x80000000u) != 0) || (pkgType == 2) || (drmType == 0xFu);
            result.PkgType = isFake ? PackageType::FakePkg : PackageType::Retail;

            if (result.PkgType == PackageType::Retail) {
                result.IsEncryptedOrProtected = true;
                result.DiagnosticMessage = "Protected retail package (Content ID: " + (contentId.empty() ? "Unknown" : contentId) + ")";
            } else {
                result.IsEncryptedOrProtected = false;
                result.DiagnosticMessage = "Fake / debug package container (Content ID: " + (contentId.empty() ? "Unknown" : contentId) + ")";
            }
            return result;
        }

        result.DiagnosticMessage = "Truncated package container header";
        return result;
    }

    result.DiagnosticMessage = "Input is neither a raw ELF nor a recognized SELF image";
    return result;
}

} // namespace Relinker
