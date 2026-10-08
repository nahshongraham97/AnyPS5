#include <InputDetector.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond) do { if (!(cond)) { std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; std::exit(1); } } while(0)

namespace {

void WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

std::vector<std::uint8_t> MakeRawElf() {
    std::vector<std::uint8_t> bytes(64, 0);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 2; // 64-bit
    bytes[5] = 1; // Little-endian
    bytes[6] = 1; // ELF version
    bytes[18] = 62; bytes[19] = 0; // x86-64 machine
    return bytes;
}

std::vector<std::uint8_t> MakePlaintextSelf(std::uint32_t magic) {
    // SELF header: 32 bytes + segment table (1 entry = 32 bytes) + ELF header (64 bytes)
    std::vector<std::uint8_t> bytes(128, 0);
    // Magic LE
    bytes[0] = static_cast<std::uint8_t>(magic & 0xff);
    bytes[1] = static_cast<std::uint8_t>((magic >> 8) & 0xff);
    bytes[2] = static_cast<std::uint8_t>((magic >> 16) & 0xff);
    bytes[3] = static_cast<std::uint8_t>((magic >> 24) & 0xff);
    bytes[6] = 1; // Endianness (little-endian)
    bytes[24] = 1; bytes[25] = 0; // 1 segment
    // Segment entry at offset 32: 32 bytes
    // Embedded ELF at offset 64:
    bytes[64] = 0x7f; bytes[65] = 'E'; bytes[66] = 'L'; bytes[67] = 'F';
    bytes[68] = 2; bytes[69] = 1; bytes[70] = 1;
    bytes[82] = 62; bytes[83] = 0;
    return bytes;
}

std::vector<std::uint8_t> MakeEncryptedSelf(std::uint32_t magic) {
    // SELF header: 32 bytes + segment table without valid ELF
    std::vector<std::uint8_t> bytes(64, 0);
    bytes[0] = static_cast<std::uint8_t>(magic & 0xff);
    bytes[1] = static_cast<std::uint8_t>((magic >> 8) & 0xff);
    bytes[2] = static_cast<std::uint8_t>((magic >> 16) & 0xff);
    bytes[3] = static_cast<std::uint8_t>((magic >> 24) & 0xff);
    bytes[6] = 1;
    bytes[24] = 1; bytes[25] = 0;
    return bytes;
}

std::vector<std::uint8_t> MakePkg(std::uint32_t type, const std::string& contentId, std::uint64_t declaredSize) {
    std::vector<std::uint8_t> bytes(512, 0);
    // Magic \x7fCNT
    bytes[0] = 0x7f; bytes[1] = 'C'; bytes[2] = 'N'; bytes[3] = 'T';
    // Type BE
    bytes[4] = static_cast<std::uint8_t>((type >> 24) & 0xff);
    bytes[5] = static_cast<std::uint8_t>((type >> 16) & 0xff);
    bytes[6] = static_cast<std::uint8_t>((type >> 8) & 0xff);
    bytes[7] = static_cast<std::uint8_t>(type & 0xff);
    // Entry count
    bytes[16] = 0; bytes[17] = 0; bytes[18] = 0; bytes[19] = 10;
    // Declared size BE
    for (int i = 0; i < 8; ++i) {
        bytes[24 + i] = static_cast<std::uint8_t>((declaredSize >> ((7 - i) * 8)) & 0xff);
    }
    // Content ID at 0x40
    for (std::size_t i = 0; i < contentId.size() && i < 36; ++i) {
        bytes[0x40 + i] = static_cast<std::uint8_t>(contentId[i]);
    }
    return bytes;
}

} // namespace

int main() {
    const auto tempDir = std::filesystem::temp_directory_path() / "anyps5_test_detector";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    // 1. Non-existent path
    {
        auto res = Relinker::InputDetector::Detect(tempDir / "does_not_exist.bin");
        TEST_ASSERT(res.Format == Relinker::InputFormat::Unknown);
    }

    // 2. Raw ELF
    {
        const auto elfPath = tempDir / "game.elf";
        WriteFile(elfPath, MakeRawElf());
        auto res = Relinker::InputDetector::Detect(elfPath);
        TEST_ASSERT(res.Format == Relinker::InputFormat::RawElf64);
        TEST_ASSERT(!res.IsEncryptedOrProtected);
    }

    // 3. PS4 Plaintext SELF
    {
        const auto selfPath = tempDir / "ps4_eboot.bin";
        WriteFile(selfPath, MakePlaintextSelf(0x1d3d154f));
        auto res = Relinker::InputDetector::Detect(selfPath);
        TEST_ASSERT(res.Format == Relinker::InputFormat::Ps4Self);
        TEST_ASSERT(res.Platform == Relinker::TargetPlatform::Ps4);
        TEST_ASSERT(!res.IsEncryptedOrProtected);
    }

    // 4. PS5 Plaintext SELF
    {
        const auto selfPath = tempDir / "ps5_eboot.bin";
        WriteFile(selfPath, MakePlaintextSelf(0xeef51454));
        auto res = Relinker::InputDetector::Detect(selfPath);
        TEST_ASSERT(res.Format == Relinker::InputFormat::Ps5Self);
        TEST_ASSERT(res.Platform == Relinker::TargetPlatform::Ps5);
        TEST_ASSERT(!res.IsEncryptedOrProtected);
    }

    // 5. PS4 Encrypted SELF
    {
        const auto selfPath = tempDir / "ps4_retail_eboot.bin";
        WriteFile(selfPath, MakeEncryptedSelf(0x1d3d154f));
        auto res = Relinker::InputDetector::Detect(selfPath);
        TEST_ASSERT(res.Format == Relinker::InputFormat::Ps4Self);
        TEST_ASSERT(res.IsEncryptedOrProtected);
    }

    // 6. PS5 Encrypted SELF
    {
        const auto selfPath = tempDir / "ps5_retail_eboot.bin";
        WriteFile(selfPath, MakeEncryptedSelf(0xeef51454));
        auto res = Relinker::InputDetector::Detect(selfPath);
        TEST_ASSERT(res.Format == Relinker::InputFormat::Ps5Self);
        TEST_ASSERT(res.IsEncryptedOrProtected);
    }

    // 7. PS4 Fake PKG
    {
        const auto pkgPath = tempDir / "game.fpkg";
        WriteFile(pkgPath, MakePkg(0x80000001, "EP4350-CUSA00001_00-0000000000000000", 1024 * 1024));
        auto res = Relinker::InputDetector::Detect(pkgPath);
        TEST_ASSERT(res.Format == Relinker::InputFormat::PackageContainer);
        TEST_ASSERT(res.Platform == Relinker::TargetPlatform::Ps4);
        TEST_ASSERT(res.PkgType == Relinker::PackageType::FakePkg);
        TEST_ASSERT(!res.IsEncryptedOrProtected);
    }

    // 8. PS5 Retail PKG (large sparse size: 50GB declared in header)
    {
        const auto pkgPath = tempDir / "game_ps5.pkg";
        const std::uint64_t declared50Gb = 50ULL * 1024 * 1024 * 1024;
        WriteFile(pkgPath, MakePkg(0x00000001, "EP4350-PPSA00001_00-0000000000000000", declared50Gb));
        auto res = Relinker::InputDetector::Detect(pkgPath);
        TEST_ASSERT(res.Format == Relinker::InputFormat::PackageContainer);
        TEST_ASSERT(res.Platform == Relinker::TargetPlatform::Ps5);
        TEST_ASSERT(res.PkgType == Relinker::PackageType::Retail);
        TEST_ASSERT(res.IsEncryptedOrProtected);
        TEST_ASSERT(res.DeclaredSize == declared50Gb);
    }

    // 9. Extracted App Directory
    {
        const auto appDir = tempDir / "extracted_app";
        const auto ebootInApp = appDir / "app0" / "eboot.bin";
        WriteFile(ebootInApp, MakePlaintextSelf(0xeef51454));
        auto res = Relinker::InputDetector::Detect(appDir);
        TEST_ASSERT(res.Format == Relinker::InputFormat::ExtractedDirectory);
        TEST_ASSERT(res.Platform == Relinker::TargetPlatform::Ps5);
        TEST_ASSERT(res.ResolvedExecutablePath == ebootInApp);
    }

    // 10. Retail package with .fpkg extension: must NOT equate extension with plaintext archive
    {
        const auto retailNamedFpkg = tempDir / "fake_named_retail.fpkg";
        WriteFile(retailNamedFpkg, MakePkg(0x00000001, "EP4350-CUSA00001_00-0000000000000000", 1024 * 1024));
        auto res = Relinker::InputDetector::Detect(retailNamedFpkg);
        TEST_ASSERT(res.Format == Relinker::InputFormat::PackageContainer);
        TEST_ASSERT(res.PkgType == Relinker::PackageType::Retail);
        TEST_ASSERT(res.IsEncryptedOrProtected);
    }

    // 11. Debug / No DRM package recognized via drm_type == 0xF
    {
        const auto debugPkg = tempDir / "debug.pkg";
        auto bytes = MakePkg(0x00000000, "EP4350-CUSA00001_00-0000000000000000", 1024 * 1024);
        // Set drm_type at offset 0x64 to 0xF (No DRM / Debug)
        bytes[0x67] = 0x0f;
        WriteFile(debugPkg, bytes);
        auto res = Relinker::InputDetector::Detect(debugPkg);
        TEST_ASSERT(res.Format == Relinker::InputFormat::PackageContainer);
        TEST_ASSERT(res.PkgType == Relinker::PackageType::FakePkg);
        TEST_ASSERT(!res.IsEncryptedOrProtected);
    }

    // 12. Truncated SELF (< 32 bytes)
    {
        const auto truncSelf = tempDir / "trunc.self";
        std::vector<std::uint8_t> bytes(20, 0);
        bytes[0] = 0x4f; bytes[1] = 0x15; bytes[2] = 0x3d; bytes[3] = 0x1d;
        WriteFile(truncSelf, bytes);
        auto res = Relinker::InputDetector::Detect(truncSelf);
        TEST_ASSERT(res.IsEncryptedOrProtected);
        TEST_ASSERT(res.DiagnosticMessage.find("Truncated") != std::string::npos);
    }

    // 13. Unsupported SELF endianness
    {
        const auto badEndianSelf = tempDir / "bad_endian.self";
        auto bytes = MakePlaintextSelf(0x1d3d154f);
        bytes[6] = 2; // Big-endian (unsupported)
        WriteFile(badEndianSelf, bytes);
        auto res = Relinker::InputDetector::Detect(badEndianSelf);
        TEST_ASSERT(res.IsEncryptedOrProtected);
        TEST_ASSERT(res.DiagnosticMessage.find("endianness") != std::string::npos);
    }

    {
        const auto path = tempDir / "UP9000-PPSA28997_00-SONSOFSPARTAPS50.pkg";
        std::vector<std::uint8_t> bytes(256);
        bytes[0] = 0x7f; bytes[1] = 'F'; bytes[2] = 'I'; bytes[3] = 'H';
        bytes[4] = 1; bytes[6] = 3;
        WriteFile(path, bytes);
        std::filesystem::resize_file(path, 9ULL * 1024 * 1024 * 1024);
        auto res = Relinker::InputDetector::Detect(path);
        TEST_ASSERT(res.Format == Relinker::InputFormat::PackageContainer);
        TEST_ASSERT(res.Platform == Relinker::TargetPlatform::Ps5);
        TEST_ASSERT(res.PkgType == Relinker::PackageType::Unknown);
        TEST_ASSERT(res.DeclaredSize == 9ULL * 1024 * 1024 * 1024);
        TEST_ASSERT(res.DiagnosticMessage.find("Finalized") != std::string::npos);
    }

    std::filesystem::remove_all(tempDir);
    std::cout << "All InputDetector tests passed!\n";
    return 0;
}
