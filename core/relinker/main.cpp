#include <Cli.hpp>
#include <InputDetector.hpp>
#include <PackageStaging.hpp>
#include <domain/Types.hpp>
#include <io/FileReader.hpp>
#include <io/FileWriter.hpp>
#include <elfpatcher/linux/LinuxElfPatcher.hpp>
#include <elfpatcher/general/SegmentFilter.hpp>
#include <elfpatcher/general/EntryStubBuilder.hpp>
#include <elfpatcher/general/ProgramHeaderLayoutBuilder.hpp>
#include <elfpatcher/general/SectionHeaderTableBuilder.hpp>
#include <elfpatcher/windows/WindowsElfPatcher.hpp>
#include <io/ByteWriter.hpp>
#include <relinker/parsing/ElfReader.hpp>
#include <relinker/parsing/SelfImage.hpp>
#include <relinker/analysis/ValidationPolicy.hpp>
#include <relinker/analysis/SyscallScanner.hpp>
#include <relinker/analysis/CallSiteResolver.hpp>
#include <relinker/analysis/UnusedNidFilter.hpp>
#include <relinker/output/SysVDynamicSectionBuilder.hpp>
#include <relinker/output/CallRegistryWriter.hpp>
#include <relinker/pipeline/RelinkerPipeline.hpp>
#include <relinker/guest/GuestImage.hpp>
#include <codegen/IAmd64OnlyConverter.hpp>
#include <map>
#include <codegen/CodegenException.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(const int argc, char* argv[]) {
    Cli::Args args;
    try {
        args = Cli::ParseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }

    try {
        auto extension = std::filesystem::path(args.outputPath).extension().string();
        for (auto& character : extension) if (character >= 'A' && character <= 'Z') character = static_cast<char>(character + ('a' - 'A'));
        if (!args.toWindows && extension == ".exe") std::cerr << "WARNING: Output filename ends with .exe, but --windows was not specified. The output will be a Linux ELF executable.\n";
        Io::FileReader fileReader;
        Io::FileWriter fileWriter;
        const std::string absPath = std::filesystem::absolute(args.outputPath).string();
        const auto inputFsPath = std::filesystem::path(args.inputPath);
        const auto detection = Relinker::InputDetector::Detect(inputFsPath);
        std::cout << "Input detection: " << detection.DiagnosticMessage << '\n';

        std::filesystem::path resolvedExecutable;
        std::filesystem::path stagingApp0;

        if (detection.Format == Relinker::InputFormat::PackageContainer) {
            Relinker::PackageStagingOptions stagingOpts;
            stagingOpts.PackagePath = inputFsPath;
            stagingOpts.OutputDirectory = std::filesystem::path(absPath).parent_path();
            stagingOpts.StagingDirectory = args.stagingDir;
            stagingOpts.ExtractorCommand = args.extractorCommand;
            stagingOpts.Passcode = args.passcode;
            stagingOpts.ImageKey = args.imageKey;

            auto staged = Relinker::PackageStaging::StagePackage(stagingOpts, detection);
            resolvedExecutable = staged.EbootPath;
            stagingApp0 = staged.App0Directory;
            std::cout << "Staged package content: " << staged.ResourceFilesCount << " resources staged to " << stagingApp0.string() << '\n';
        } else if (detection.Format == Relinker::InputFormat::ExtractedDirectory) {
            auto staged = Relinker::PackageStaging::StageExtractedApp(inputFsPath, std::filesystem::path(absPath).parent_path());
            resolvedExecutable = staged.EbootPath;
            stagingApp0 = staged.App0Directory;
            std::cout << "Staged extracted application: " << staged.ResourceFilesCount << " resources staged to " << stagingApp0.string() << '\n';
        } else if (detection.Format == Relinker::InputFormat::RawElf64 ||
                   detection.Format == Relinker::InputFormat::Ps4Self ||
                   detection.Format == Relinker::InputFormat::Ps5Self) {
            resolvedExecutable = detection.ResolvedExecutablePath;
            if (detection.IsEncryptedOrProtected) {
                throw Domain::RelinkerException(detection.DiagnosticMessage);
            }
            const auto parentDir = resolvedExecutable.parent_path();
            const auto outDir = std::filesystem::path(absPath).parent_path();
            if (std::filesystem::exists(parentDir / "sce_sys") && !outDir.empty() && outDir.lexically_normal() != parentDir.lexically_normal()) {
                auto staged = Relinker::PackageStaging::StageExtractedApp(parentDir, outDir, resolvedExecutable);
                stagingApp0 = staged.App0Directory;
            } else {
                stagingApp0 = parentDir;
            }
        } else {
            throw Domain::RelinkerException(detection.DiagnosticMessage.empty() ? "Unrecognized input format" : detection.DiagnosticMessage);
        }

        auto sourceBytes = Relinker::UnwrapSelf(fileReader.Read(resolvedExecutable.string()));

        std::vector<Codegen::TrampolineSite> trampolines;
        if (args.toIntel) {
            const auto codeSegments = Relinker::ElfReader(sourceBytes).ReadCodeSegments();
            auto converted = Codegen::MakeAmd64OnlyConverter()->Convert(std::move(sourceBytes), codeSegments);
            sourceBytes = std::move(converted.Bytes);
            trampolines = std::move(converted.Trampolines);
            std::map<std::string, std::size_t> stubsByName;
            for (const auto& report : converted.Reports) {
                if (report.Lowering == Codegen::Amd64OnlyLowering::Kept)
                    std::cout << "Intel substitution: " << report.InstructionName << " at 0x" << std::hex << report.Offset << std::dec << " (" << report.OriginalLength << " bytes) kept: no room for a jump\n";
                else if (report.InstructionName == "VRSQRTPS" || report.InstructionName == "VRCPPS")
                    ++stubsByName[report.InstructionName];
                else
                    std::cout << "Intel substitution: " << report.InstructionName << " at 0x" << std::hex << report.Offset << std::dec << " (" << report.OriginalLength << " bytes) -> " << (report.Lowering == Codegen::Amd64OnlyLowering::InPlace ? "in place " : "stub ") << report.ReplacementLength << " bytes\n";
            }
            for (const auto& [name, count] : stubsByName)
                std::cout << "Intel substitution: " << name << " -> stub at " << count << " sites\n";
            std::cout << "Intel conversion: " << converted.ReplacedCount << " in place, " << trampolines.size() << " stubs, " << converted.KeptCount << " kept\n";
        }

        auto elfReader = std::make_shared<Relinker::ElfReader>(sourceBytes);
        const std::shared_ptr<Relinker::ISyscallScanner> syscallScanner = args.skipSyscallCheck ? Relinker::MakeNullSyscallScanner() : Relinker::MakeSyscallScanner();

        const auto pipeline = std::make_shared<Relinker::RelinkerPipeline>(
            elfReader,
            syscallScanner,
            Relinker::MakeCallSiteResolver(),
            std::make_shared<Relinker::ValidationPolicy>(),
            std::make_shared<Relinker::SysVDynamicSectionBuilder>(),
            args.unusedFilterLevel == 2 ? Relinker::MakeStrictUnusedNidFilter() : Relinker::MakeUnusedNidFilter(),
            args.unusedFilterLevel
        );

        std::cout << "System: " << (args.toWindows ? "Windows" : "Linux") << "; unused-filter=" << args.unusedFilterLevel << "\n";
        std::cout << "sce_module/sce_modules/prx processing: " << (args.skipSceModule ? "disabled (--skip-sce-module)" : "enabled") << '\n';
        for (const auto& name : args.excludedSceModules) std::cout << "Guest module excluded: " << name << '\n';
        auto result = pipeline->Relink(sourceBytes);
        for (const auto& patch : result.Patches) {
            if (patch.Offset > sourceBytes.size() || patch.Bytes.size() > sourceBytes.size() - patch.Offset)
                throw Domain::RelinkerException("Relinker patch exceeds source image", patch.Offset);
            for (std::size_t index = 0; index < patch.Bytes.size(); ++index) sourceBytes[patch.Offset + index] = patch.Bytes[index];
        }

        std::vector<Relinker::GuestArtifact> guestArtifacts;
        if (!args.skipSceModule) {
            guestArtifacts = Relinker::GuestModuleBuilder().Build(resolvedExecutable, absPath, result.DynamicSection, args.toWindows, args.toIntel, *syscallScanner, args.lazyBinding, args.runPath, args.excludedSceModules);
        }

        if (args.writeRegistry) {
            const std::filesystem::path outFsPath(absPath);
            const std::string registryPath = (outFsPath.parent_path() / (outFsPath.stem().string() + ".registry.json")).string();
            fileWriter.Write(registryPath, std::make_shared<Relinker::CallRegistryWriter>()->WriteCallRegistry(result.RegistryEntries));
        }

        auto byteWriter = std::make_shared<Io::ByteWriter>();

        std::shared_ptr<Elfpatcher::IElfPatcher> patcher;
        if (args.toWindows) {
            std::filesystem::path iconPath;
            if (!stagingApp0.empty() && std::filesystem::exists(stagingApp0 / "sce_sys" / "icon0.png")) {
                iconPath = stagingApp0 / "sce_sys" / "icon0.png";
            } else if (!stagingApp0.empty() && std::filesystem::exists(stagingApp0 / "icon0.png")) {
                iconPath = stagingApp0 / "icon0.png";
            } else {
                iconPath = std::filesystem::path(args.inputPath).parent_path() / "sce_sys" / "icon0.png";
            }
            patcher = std::make_shared<Elfpatcher::Windows::WindowsPePatcher>(args.windowsGui, iconPath);
        } else {
            patcher = std::make_shared<Elfpatcher::Linux::LinuxElfPatcher>(
                std::make_shared<Elfpatcher::EntryStubBuilder>(),
                std::make_shared<Elfpatcher::ProgramHeaderLayoutBuilder>(
                    std::make_shared<Elfpatcher::SegmentFilter>(),
                    byteWriter
                ),
                std::make_shared<Elfpatcher::SectionHeaderTableBuilder>(byteWriter),
                byteWriter
            );
        }

        std::vector<std::uint8_t> executableBytes;
        try {
            executableBytes = patcher->Patch(sourceBytes, result.OriginalHeaders, result.DynamicSection, result.OriginalPltGotVaddr, args.runPath, args.lazyBinding, args.windowsDiagnostics, trampolines);
        } catch (Domain::RelinkerException& error) {
            error.InputPath = args.inputPath;
            throw;
        }
        for (const auto& artifact : guestArtifacts) {
            std::filesystem::create_directories(artifact.Path.parent_path());
            fileWriter.Write(artifact.Path.string(), artifact.Bytes);
            std::cout << "Guest module: " << artifact.Path.string() << '\n';
        }
        fileWriter.Write(absPath, executableBytes);
        std::cout << "External prx references: " << result.RegistryEntries.size() << "\nOutput file: " << absPath << '\n';
        std::cout << "Expected runtime layout (relative to the output executable):\n"
                  << std::filesystem::path(absPath).filename().string() << "\n"
                  << "libs/\n    *.prx\napp0/\n    <game resources>\n";
        for (const auto& artifact : guestArtifacts)
            std::cout << "    " << artifact.Path.lexically_relative(std::filesystem::path(absPath).parent_path() / "app0").generic_string() << '\n';
        std::cout << "Game resources and system libraries must be placed in this layout separately.\n";
        if (args.runPath != "$ORIGIN/libs") std::cout << "Custom library search path (--rpath): " << args.runPath << '\n';

        if (args.autorun) return Cli::Autorun(absPath, args.toWindows);

    } catch (const Domain::RelinkerException& e) {
        std::cerr << "FAIL: " << e.what();
        if (e.FailureOffset != 0) std::cerr << " (offset 0x" << std::hex << e.FailureOffset << ")";
        std::cerr << "\n";
        if (!e.InputPath.empty()) std::cerr << "Input: " << e.InputPath << '\n';
        return 2;
    } catch (const Codegen::CodegenException& e) {
        std::cerr << "FAIL: " << e.what();
        if (e.FailureOffset != 0) std::cerr << " (offset 0x" << std::hex << e.FailureOffset << ")";
        std::cerr << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 2;
    }

    return 0;
}
