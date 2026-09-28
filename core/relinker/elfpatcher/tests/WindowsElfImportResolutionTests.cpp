#include <elfpatcher/windows/WindowsEntryStubBuilder.hpp>
#include <nid/NidCompute.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace Elfpatcher::Windows;
using Bytes = std::vector<std::uint8_t>;

void require(const bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::uint32_t stringRva(const PeSection& section, const std::string& value) {
    const auto found = std::search(section.Data.begin(), section.Data.end(), value.begin(), value.end());
    require(found != section.Data.end(), "Startup data is missing string: " + value);
    const auto offset = static_cast<std::size_t>(found - section.Data.begin());
    require(offset + value.size() < section.Data.size() && section.Data[offset + value.size()] == 0,
        "Startup data string is not NUL-terminated: " + value);
    return CheckedRva(section.Rva + offset);
}

std::vector<std::size_t> instructionOffsetsTargeting(
    const Bytes& code,
    const std::uint32_t codeRva,
    const std::vector<std::uint8_t>& opcode,
    const std::size_t displacementOffset,
    const std::uint32_t targetRva
) {
    std::vector<std::size_t> offsets;
    for (std::size_t offset = 0; offset + displacementOffset + 4 <= code.size(); ++offset) {
        if (!std::equal(opcode.begin(), opcode.end(), code.begin() + static_cast<std::ptrdiff_t>(offset)))
            continue;
        std::int32_t displacement = 0;
        std::memcpy(&displacement, code.data() + offset + displacementOffset, sizeof(displacement));
        const auto instructionEnd = static_cast<std::int64_t>(codeRva) +
            static_cast<std::int64_t>(offset + displacementOffset + sizeof(displacement));
        if (instructionEnd + displacement == targetRva)
            offsets.push_back(offset);
    }
    return offsets;
}

void requireLookupOrder(
    const Bytes& code,
    const std::uint32_t codeRva,
    const std::uint32_t nidRva,
    const std::uint32_t nameRva,
    const std::uint32_t getProcAddressRva,
    const std::size_t nextImportOffset,
    const std::string& name
) {
    const auto nidLoads = instructionOffsetsTargeting(code, codeRva, {0x48, 0x8d, 0x35}, 3, nidRva);
    const auto nameLoads = instructionOffsetsTargeting(code, codeRva, {0x48, 0x8d, 0x35}, 3, nameRva);
    const auto getProcCalls = instructionOffsetsTargeting(code, codeRva, {0xff, 0x15}, 2, getProcAddressRva);
    require(nidLoads.size() == 1, "NID lookup was not emitted exactly once for " + name);
    require(nameLoads.size() == 1, "Plain-name lookup was not emitted exactly once for " + name);
    require(nidLoads.front() < nameLoads.front(), "Plain-name lookup precedes NID lookup for " + name);
    require(std::count_if(getProcCalls.begin(), getProcCalls.end(), [&](const std::size_t offset) {
        return nidLoads.front() < offset && offset < nameLoads.front();
    }) == 1, "NID lookup does not call GetProcAddress before fallback for " + name);
    require(std::count_if(getProcCalls.begin(), getProcCalls.end(), [&](const std::size_t offset) {
        return nameLoads.front() < offset && offset < nextImportOffset;
    }) == 1, "Plain-name fallback does not call GetProcAddress for " + name);
}

void verifyImportsAcrossSupportedRelocations() {
    constexpr std::uint32_t getProcAddressRva = 0x5000;
    const std::vector<std::string> apiNames = {
        "RaiseException", "lstrlenA", "GetStdHandle", "WriteFile", "FormatMessageA",
        "GetLastError", "GetModuleFileNameA", "LoadLibraryExA", "GetProcAddress", "ExitProcess"
    };
    WindowsImports nativeImports{};
    for (std::size_t index = 0; index < apiNames.size(); ++index)
        nativeImports.Functions.emplace(apiNames[index], apiNames[index] == "GetProcAddress"
            ? getProcAddressRva
            : static_cast<std::uint32_t>(0x4000 + index * 8));

    const std::vector<PeImport> imports = {
        {"sceRegressionAbsolute", 0x2100, 0, -1, 1},
        {"sceRegressionObject", 0x2108, 0, -1, 6},
        {"sceRegressionFunction", 0x2110, 0, -1, 7}
    };
    const std::string library = "libSceRegression.prx";
    const auto startup = WindowsEntryStubBuilder().Build(
        0x10000, 0x1000, nativeImports, {library}, imports, "$ORIGIN", false, false
    );

    std::vector<std::size_t> nidLoadOffsets;
    for (const auto& import : imports) {
        const auto nidRva = stringRva(startup.Data, Nid::ComputeNid(import.Name, ""));
        const auto nidLoads = instructionOffsetsTargeting(startup.Code.Data, startup.Code.Rva, {0x48, 0x8d, 0x35}, 3, nidRva);
        require(nidLoads.size() == 1, "NID lookup was not emitted exactly once for " + import.Name);
        nidLoadOffsets.push_back(nidLoads.front());
    }

    for (std::size_t index = 0; index < imports.size(); ++index) {
        const auto& import = imports[index];
        const auto nid = Nid::ComputeNid(import.Name, "");
        const auto nidRva = stringRva(startup.Data, nid);
        const auto nameRva = stringRva(startup.Data, import.Name);
        const auto nextImportOffset = index + 1 < nidLoadOffsets.size()
            ? nidLoadOffsets[index + 1]
            : startup.Code.Data.size();
        requireLookupOrder(
            startup.Code.Data, startup.Code.Rva, nidRva, nameRva,
            getProcAddressRva, nextImportOffset, import.Name
        );

        const std::string unresolved = "FAIL: unresolved ELF import " + import.Name + "\n";
        stringRva(startup.Data, unresolved);
    }

    require(instructionOffsetsTargeting(
        startup.Code.Data, startup.Code.Rva, {0xff, 0x15}, 2, getProcAddressRva
    ).size() == imports.size() * 2,
        "Each ELF import must perform NID lookup followed by plain-name fallback");
    stringRva(startup.Data, "Searched libraries:\n");
    stringRva(startup.Data, "GetLastError: ");
    stringRva(startup.Data, library);
}

}

int main() {
    try {
        verifyImportsAcrossSupportedRelocations();
        std::cout << "Windows ELF import resolution tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
