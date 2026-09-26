#include <relinker/parsing/ElfReader.hpp>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void requireFailure(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const Domain::RelinkerException&) {
        return;
    }
    throw std::runtime_error("Malformed ELF range was accepted");
}

template<typename TValue>
void write(std::vector<std::uint8_t>& bytes, std::size_t offset, TValue value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> bytes(512);
    const std::uint8_t ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
    std::memcpy(bytes.data(), ident, sizeof(ident));
    write<std::uint16_t>(bytes, 18, 62);
    write<std::uint64_t>(bytes, 32, 64);
    write<std::uint16_t>(bytes, 54, 56);
    write<std::uint16_t>(bytes, 56, 1);
    write<std::uint32_t>(bytes, 64, 1);
    write<std::uint64_t>(bytes, 72, 256);
    write<std::uint64_t>(bytes, 80, 0x1000);
    write<std::uint64_t>(bytes, 96, 16);
    write<std::uint64_t>(bytes, 104, 32);
    write<std::uint64_t>(bytes, 40, 128);
    write<std::uint16_t>(bytes, 58, 64);
    write<std::uint16_t>(bytes, 60, 2);
    write<std::uint16_t>(bytes, 62, 1);
    write<std::uint64_t>(bytes, 128 + 48, 8);
    write<std::uint64_t>(bytes, 128 + 56, 24);
    write<std::uint64_t>(bytes, 192 + 24, 300);
    write<std::uint64_t>(bytes, 192 + 32, 6);
    std::memcpy(bytes.data() + 300, "\0name", 6);
    return bytes;
}

}

int main() {
    try {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        auto bytes = fixture();
        const Relinker::ElfReader valid(bytes);
        require(valid.TranslateVirtualAddress(0x100f) == 271, "Valid address translation failed");
        requireFailure([&] { valid.TranslateVirtualAddress(0x1010); });
        require(valid.ReadSectionHeaders()[0].EntrySize == 24, "Section entry size was read as alignment");
        Domain::ProgramHeader segment{};
        segment.Offset = maximum;
        segment.FileSize = 2;
        requireFailure([&] { valid.ReadSegment(segment); });
        requireFailure([&] { valid.ReadDynamicTags(segment); });
        Domain::SectionHeader section{};
        section.Offset = maximum;
        section.SectionSize = 2;
        requireFailure([&] { valid.ReadSection(section); });
        section.Offset = bytes.size();
        section.SectionSize = 0;
        require(valid.ReadSection(section).empty(), "Empty range at end of file was rejected");
        for (const auto field : {std::size_t{32}, std::size_t{40}}) {
            bytes = fixture();
            write(bytes, field, maximum);
            const Relinker::ElfReader reader(bytes);
            requireFailure([&] {
                if (field == 32) reader.ReadProgramHeaders();
                else reader.ReadSectionHeaders();
            });
        }
        bytes = fixture();
        write<std::uint16_t>(bytes, 58, 1);
        requireFailure([&] { Relinker::ElfReader(bytes).ReadSectionHeaders(); });
        bytes = fixture();
        write<std::uint16_t>(bytes, 62, 2);
        requireFailure([&] { Relinker::ElfReader(bytes).ReadSectionHeaders(); });
        bytes = fixture();
        write(bytes, 192 + 24, maximum);
        requireFailure([&] { Relinker::ElfReader(bytes).ReadSectionHeaders(); });
        bytes = fixture();
        write<std::uint32_t>(bytes, 128, 6);
        requireFailure([&] { Relinker::ElfReader(bytes).ReadSectionHeaders(); });
        bytes = fixture();
        write<std::uint32_t>(bytes, 128, 1);
        bytes[305] = 'x';
        requireFailure([&] { Relinker::ElfReader(bytes).ReadSectionHeaders(); });
        bytes = fixture();
        write(bytes, 80, maximum - 1);
        requireFailure([&] { Relinker::ElfReader(bytes).ReadProgramHeaders(); });
        std::cout << "ELF bounds tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
