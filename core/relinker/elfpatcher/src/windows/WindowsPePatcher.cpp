#include <elfpatcher/windows/WindowsElfPatcher.hpp>
#include <elfpatcher/windows/WindowsEntryStubBuilder.hpp>
#include <elfpatcher/windows/WindowsIconResourceBuilder.hpp>
#include <elfpatcher/windows/WindowsLoadImage.hpp>
#include <elfpatcher/windows/WindowsPeWriter.hpp>
#include <elfpatcher/windows/WindowsRelocationBuilder.hpp>
#include <elfpatcher/windows/WindowsTlsBuilder.hpp>
#include <elfpatcher/windows/WindowsTrampolineBuilder.hpp>
#include <io/BufferUtils.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <utility>

namespace Elfpatcher::Windows {

namespace {

void writeDiagnosticsImports(const std::vector<PeImport>& imports) {
    const std::filesystem::path path = std::filesystem::absolute("windows-diagnostics-imports.txt");
    std::ofstream stream(path, std::ios::trunc);
    if (!stream)
        throw Domain::RelinkerException("Cannot open windows-diagnostics-imports.txt for writing");
    for (const auto& import : imports)
        stream << import.Name << '\n';
    if (!stream)
        throw Domain::RelinkerException("Cannot write windows-diagnostics-imports.txt");
    stream.close();
    std::cout << "Wrote Windows import diagnostics to " << path.string() << '\n';
}

void writeGotStub(std::vector<PeSection>& sections, const std::uint32_t targetRva, const std::uint32_t stubRva) {
    for (auto& section : sections) {
        if (section.Data.size() < 8 || targetRva < section.Rva || targetRva - section.Rva > section.Data.size() - 8)
            continue;
        Io::WriteU64(section.Data, targetRva - section.Rva, ImageBase + stubRva);
        return;
    }
    throw Domain::RelinkerException("Lazy import GOT slot is not contained in any section", targetRva);
}

}

WindowsPePatcher::WindowsPePatcher(const bool windowsGui, std::filesystem::path iconPath) : _windowsGui(windowsGui), _iconPath(std::move(iconPath)) {
}

std::vector<std::uint8_t> WindowsPePatcher::Patch(const std::vector<std::uint8_t>& sourceElf, const std::vector<Domain::ProgramHeader>& originalHeaders, const Domain::SysVDynamicSection& dynamicSection, const std::uint64_t originalPltGotVaddr, const std::string& runPath, const bool lazyBinding, const bool dependencyDiagnostics, const std::vector<Codegen::TrampolineSite>& trampolines) {
    WindowsLoadImage image(sourceElf, originalHeaders);
    if (originalPltGotVaddr != 0)
        image.GetRva(originalPltGotVaddr, 8);
    const WindowsRelocationBuilder relocationBuilder;
    auto relocations = relocationBuilder.Apply(image, dynamicSection);
    auto sections = image.BuildSections();
    std::array<PeDirectory, 16> directories{};
    auto nextRva = image.GetEndRva();
    bool hasProcessParameters = false;
    for (const auto& header : originalHeaders) {
        if (header.Type != 0x61000001) continue;
        if (hasProcessParameters || header.FileSize < 0x40) throw Domain::RelinkerException("Invalid process parameter segment");
        hasProcessParameters = true;
        std::vector<std::uint8_t> metadata(8);
        Io::WriteU32(metadata, 0, image.GetRva(header.MappedAddress, header.FileSize));
        Io::WriteU32(metadata, 4, CheckedRva(header.FileSize));
        sections.push_back({".procpar", nextRva, SectionRead | 0x40u, std::move(metadata)});
        nextRva = AlignRva(nextRva + sections.back().Data.size());
    }
    for (const auto& header : originalHeaders) {
        if (header.Type != 0x6474e550) continue;
        std::vector<std::uint8_t> metadata(4);
        Io::WriteU32(metadata, 0, image.GetRva(header.MappedAddress, header.FileSize));
        sections.push_back({".ehmeta", nextRva, SectionRead | 0x40u, std::move(metadata)});
        nextRva = AlignRva(nextRva + sections.back().Data.size());
    }
    directories[9] = WindowsTlsBuilder().Build(sourceElf, originalHeaders, image, sections, relocations.BaseRelocations, nextRva);
    WindowsTrampolineBuilder().Build(trampolines, image, sections, nextRva);
    if (!dynamicSection.RuntimeExports.empty()) {
        std::map<std::string, std::uint32_t> exports;
        for (const auto& symbol : dynamicSection.RuntimeExports) {
            const auto rva = image.GetRva(symbol.Value, std::max<std::uint64_t>(symbol.Size, 1));
            const auto [found, inserted] = exports.emplace(symbol.Name, rva);
            if (!inserted && found->second != rva)
                throw Domain::RelinkerException("Duplicate main export with conflicting RVA: " + symbol.Name);
        }
        if (exports.size() > 65535) throw Domain::RelinkerException("Too many main PE exports");
        PeSection exportSection{".edata", nextRva, SectionRead | 0x40u, std::vector<std::uint8_t>(40)};
        auto& data = exportSection.Data;
        const auto count = CheckedRva(exports.size());
        const auto functions = data.size();
        data.resize(data.size() + count * 4);
        const auto names = data.size();
        data.resize(data.size() + count * 4);
        const auto ordinals = data.size();
        data.resize(data.size() + count * 2);
        Io::WriteU32(data, 12, CheckedRva(nextRva + data.size()));
        Io::AppendString(data, "eboot.bin.exe");
        Io::WriteU32(data, 16, 1);
        Io::WriteU32(data, 20, count);
        Io::WriteU32(data, 24, count);
        Io::WriteU32(data, 28, CheckedRva(nextRva + functions));
        Io::WriteU32(data, 32, CheckedRva(nextRva + names));
        Io::WriteU32(data, 36, CheckedRva(nextRva + ordinals));
        std::size_t index = 0;
        for (const auto& [name, rva] : exports) {
            Io::WriteU32(data, functions + index * 4, rva);
            Io::WriteU32(data, names + index * 4, CheckedRva(nextRva + data.size()));
            Io::WriteU16(data, ordinals + index * 2, static_cast<std::uint16_t>(index));
            Io::AppendString(data, name);
            ++index;
        }
        directories[0] = {nextRva, CheckedRva(data.size())};
        nextRva = AlignRva(nextRva + data.size());
        sections.push_back(std::move(exportSection));
    }
    const WindowsImportBuilder importBuilder;
    auto nativeImports = importBuilder.Build(nextRva);
    directories[1] = nativeImports.Directory;
    directories[12] = nativeImports.AddressTable;
    nextRva = AlignRva(nextRva + nativeImports.Section.Data.size());
    auto libraries = importBuilder.ReadLibraries(dynamicSection);
    std::vector<std::string> guestPaths;
    for (std::size_t index = 0; index < dynamicSection.GuestModules.size(); ++index) {
        const auto& module = dynamicSection.GuestModules[index];
        guestPaths.push_back(module.Path);
        for (const auto& import : module.Imports) relocations.Imports.push_back({import.Name, import.TargetRva, import.Addend, static_cast<std::int32_t>(index), import.RelocationType, import.Library});
    }
    libraries.insert(libraries.begin(), guestPaths.begin(), guestPaths.end());
    if (dependencyDiagnostics)
        writeDiagnosticsImports(relocations.Imports);
    auto entry = WindowsEntryStubBuilder().Build(nextRva, image.GetEntryRva(), nativeImports, libraries, relocations.Imports, runPath, lazyBinding, dependencyDiagnostics, dynamicSection.GuestModules);
    directories[3] = entry.ExceptionDirectory;
    const auto entryRva = entry.Code.Rva;
    nextRva = AlignRva(entry.Code.Rva + entry.Code.Data.size());
    sections.push_back(std::move(nativeImports.Section));
    sections.push_back(std::move(entry.Data));
    sections.push_back(std::move(entry.Code));
    for (const auto& lazyStub : entry.LazyStubs) {
        writeGotStub(sections, lazyStub.TargetRva, lazyStub.StubRva);
        relocations.BaseRelocations.push_back(lazyStub.TargetRva);
    }
    auto relocationData = relocationBuilder.BuildBaseRelocations(relocations.BaseRelocations);
    if (!relocationData.empty()) {
        directories[5] = {nextRva, CheckedRva(relocationData.size())};
        sections.push_back({".reloc", nextRva, SectionRead | 0x02000040u, std::move(relocationData)});
        nextRva = AlignRva(nextRva + sections.back().Data.size());
    }
    directories[2] = WindowsIconResourceBuilder().Build(_iconPath, sections, nextRva);
    return WindowsPeWriter().Write(sections, entryRva, directories, _windowsGui);
}

}
