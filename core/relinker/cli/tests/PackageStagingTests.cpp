#include <PackageStaging.hpp>
#include <domain/Types.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#define TEST_ASSERT(cond) do { if (!(cond)) { std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; std::exit(1); } } while(0)

namespace {

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), content.size());
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 5 && std::string(argv[1]) == "--input" && std::string(argv[3]) == "--output") {
        WriteFile(std::filesystem::path(argv[4]) / "eboot.bin", "MOCK_EBOOT_CONTENT");
        return 0;
    }
    const auto tempDir = std::filesystem::temp_directory_path() / "anyps5_test_staging";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    // 1. Retail PKG staging without lawfully supplied key throws descriptive error
    {
        Relinker::DetectionResult detection;
        detection.Format = Relinker::InputFormat::PackageContainer;
        detection.PkgType = Relinker::PackageType::Retail;
        detection.ContentId = "EP4350-PPSA00001_00-0000000000000000";
        detection.IsEncryptedOrProtected = true;

        Relinker::PackageStagingOptions opts;
        opts.PackagePath = tempDir / "retail.pkg";
        opts.ImageKey = ""; // No key supplied

        bool caught = false;
        try {
            Relinker::PackageStaging::StagePackage(opts, detection);
        } catch (const Domain::RelinkerException& e) {
            caught = true;
            const std::string msg = e.what();
            TEST_ASSERT(msg.find("protected retail content") != std::string::npos);
            TEST_ASSERT(msg.find("ANYPS5_IMAGE_KEY") != std::string::npos);
        }
        TEST_ASSERT(caught);
    }

    // 2. PKG staging without extractor throws descriptive error
    {
        Relinker::DetectionResult detection;
        detection.Format = Relinker::InputFormat::PackageContainer;
        detection.PkgType = Relinker::PackageType::FakePkg;
        detection.ContentId = "EP4350-CUSA00001_00-0000000000000000";
        detection.IsEncryptedOrProtected = false;

        Relinker::PackageStagingOptions opts;
        opts.PackagePath = tempDir / "game.fpkg";
        opts.ExtractorCommand = "";

        bool caught = false;
        try {
            Relinker::PackageStaging::StagePackage(opts, detection);
        } catch (const Domain::RelinkerException& e) {
            caught = true;
            const std::string msg = e.what();
            TEST_ASSERT(msg.find("requires an external extractor") != std::string::npos);
        }
        TEST_ASSERT(caught);
    }

    // 3. Extracted App Staging preserves resources, sce_sys, data, and sce_module
    {
        const auto sourceApp = tempDir / "source_app";
        const auto app0Dir = sourceApp / "app0";
        WriteFile(app0Dir / "eboot.bin", "MOCK_EBOOT_CONTENT");
        WriteFile(app0Dir / "sce_sys" / "param.json", "{\"contentId\": \"EP4350-PPSA99999_00-0000000000000000\"}");
        WriteFile(app0Dir / "sce_sys" / "icon0.png", "MOCK_ICON");
        WriteFile(app0Dir / "data" / "level1.bin", "MOCK_LEVEL_DATA");
        WriteFile(app0Dir / "sce_module" / "libgame.prx", "MOCK_PRX");

        const auto outputDir = tempDir / "output_build";
        auto result = Relinker::PackageStaging::StageExtractedApp(sourceApp, outputDir);

        TEST_ASSERT(std::filesystem::exists(outputDir / "app0" / "eboot.bin"));
        TEST_ASSERT(std::filesystem::exists(outputDir / "app0" / "sce_sys" / "param.json"));
        TEST_ASSERT(std::filesystem::exists(outputDir / "app0" / "sce_sys" / "icon0.png"));
        TEST_ASSERT(std::filesystem::exists(outputDir / "app0" / "data" / "level1.bin"));
        TEST_ASSERT(std::filesystem::exists(outputDir / "app0" / "sce_module" / "libgame.prx"));
        TEST_ASSERT(result.ContentId == "EP4350-PPSA99999_00-0000000000000000");
        TEST_ASSERT(result.GuestPrxModules.size() == 1);
        TEST_ASSERT(result.ResourceFilesCount >= 3);
    }

    // 4. Missing executable throws descriptive error
    {
        const auto emptyApp = tempDir / "empty_app";
        std::filesystem::create_directories(emptyApp / "data");
        WriteFile(emptyApp / "data" / "file.txt", "data");
        bool caught = false;
        try {
            Relinker::PackageStaging::StageExtractedApp(emptyApp, tempDir / "out_empty");
        } catch (const Domain::RelinkerException& e) {
            caught = true;
            const std::string msg = e.what();
            TEST_ASSERT(msg.find("does not contain eboot.bin") != std::string::npos);
        }
        TEST_ASSERT(caught);
    }

    // 5. Retail PKG staging with lawfully supplied ANYPS5_IMAGE_KEY env var proceeds past key validation
    {
        Relinker::DetectionResult detection;
        detection.Format = Relinker::InputFormat::PackageContainer;
        detection.PkgType = Relinker::PackageType::Retail;
        detection.ContentId = "EP4350-PPSA00001_00-0000000000000000";
        detection.IsEncryptedOrProtected = true;

        Relinker::PackageStagingOptions opts;
        opts.PackagePath = tempDir / "retail2.pkg";
        opts.ImageKey = ""; // Empty on options, will test environment variable
#ifdef _WIN32
        _putenv("ANYPS5_IMAGE_KEY=0123456789abcdef0123456789abcdef");
#else
        setenv("ANYPS5_IMAGE_KEY", "0123456789abcdef0123456789abcdef", 1);
#endif

        bool caught = false;
        try {
            Relinker::PackageStaging::StagePackage(opts, detection);
        } catch (const Domain::RelinkerException& e) {
            caught = true;
            const std::string msg = e.what();
            // Should pass key validation and fail on extractor command
            TEST_ASSERT(msg.find("requires an external extractor") != std::string::npos);
        }
        TEST_ASSERT(caught);
#ifdef _WIN32
        _putenv("ANYPS5_IMAGE_KEY=");
#else
        unsetenv("ANYPS5_IMAGE_KEY");
#endif
    }

    {
        Relinker::DetectionResult detection;
        detection.Format = Relinker::InputFormat::PackageContainer;
        detection.PkgType = Relinker::PackageType::Unknown;
        const auto extractor = tempDir / (std::string("extract & test") + std::filesystem::path(argv[0]).extension().string());
        std::filesystem::copy_file(argv[0], extractor, std::filesystem::copy_options::overwrite_existing);
        const auto package = tempDir / "sample & name.pkg";
        WriteFile(package, "MOCK_PACKAGE_CONTENT");
        Relinker::PackageStagingOptions opts;
        opts.PackagePath = package;
        opts.StagingDirectory = tempDir / "extract result";
        opts.OutputDirectory = tempDir / "output package";
        opts.ExtractorCommand = extractor.string();
        const auto result = Relinker::PackageStaging::StagePackage(opts, detection);
        TEST_ASSERT(std::filesystem::exists(result.EbootPath));
        TEST_ASSERT(result.EbootPath == opts.OutputDirectory / "app0" / "eboot.bin");
    }

    {
        Relinker::DetectionResult detection;
        detection.PkgType = Relinker::PackageType::Unknown;
        Relinker::PackageStagingOptions opts;
        opts.Passcode = "secret";
        bool caught = false;
        try { Relinker::PackageStaging::StagePackage(opts, detection); }
        catch (const Domain::RelinkerException& error) {
            caught = std::string(error.what()).find("ANYPS5_PASSCODE") != std::string::npos;
        }
        TEST_ASSERT(caught);
    }

    std::filesystem::remove_all(tempDir);
    std::cout << "All PackageStaging tests passed!\n";
    return 0;
}
