#include <relinker/parsing/SelfImage.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Require(bool condition) {
    if (!condition) throw std::runtime_error("SELF image test failed");
}
void Write(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
std::vector<std::uint8_t> Fixture(std::uint64_t flags = (1ull << 20) | 0x800,
                                  std::uint32_t magic = 0x1d3d154f) {
    std::vector<std::uint8_t> self(260);
    Write(self, 0, magic, 4);
    self[6] = 1;
    Write(self, 24, 1, 2);
    Write(self, 32, flags, 8);
    Write(self, 40, 256, 8);
    Write(self, 48, 4, 8);
    Write(self, 56, 4, 8);
    self[256] = 0x12; self[257] = 0x34; self[258] = 0x56; self[259] = 0x78;
    const std::size_t elf = 64;
    Write(self, elf, 0x464c457f, 4);
    self[elf + 4] = 2;
    self[elf + 5] = 1;
    Write(self, elf + 16, 3, 2);
    Write(self, elf + 18, 62, 2);
    Write(self, elf + 32, 64, 8);
    Write(self, elf + 54, 56, 2);
    Write(self, elf + 56, 2, 2);
    Write(self, elf + 64, 1, 4);
    Write(self, elf + 64 + 32, 176, 8);
    Write(self, elf + 120, 1, 4);
    Write(self, elf + 120 + 8, 512, 8);
    Write(self, elf + 120 + 32, 4, 8);
    return self;
}
void Reject(std::vector<std::uint8_t> self, const std::string& expected) {
    try { Relinker::UnwrapSelf(std::move(self)); }
    catch (const std::runtime_error& error) {
        Require(std::string(error.what()).find(expected) != std::string::npos);
        return;
    }
    throw std::runtime_error("Malformed or protected SELF was accepted");
}
}

int main() {
    auto expected = Fixture();
    const auto actual = Relinker::UnwrapSelf(expected);
    Require(actual.size() == 516);
    Require(actual[0] == 0x7f && actual[512] == 0x12 && actual[515] == 0x78);
    Require(Relinker::UnwrapSelf(actual) == actual);
    Require(Relinker::UnwrapSelf(Fixture((1ull << 20) | 0x800, 0xeef51454)) == actual);
    Require(Relinker::UnwrapSelf(Fixture(1ull << 20)) == actual);
    Reject(Fixture((1ull << 20) | 0x800 | 2), "encrypted segment 1");
    Reject(Fixture((1ull << 20) | 0x800 | 8), "compressed segment 1");
    auto digestAndData = Fixture();
    digestAndData.insert(digestAndData.begin() + 64, 32, 0);
    Write(digestAndData, 24, 2, 2);
    Write(digestAndData, 32, (1ull << 20) | 4, 8);
    Write(digestAndData, 40, 260, 8);
    Write(digestAndData, 48, 4, 8);
    Write(digestAndData, 64, (1ull << 20) | 0x800, 8);
    Write(digestAndData, 72, 288, 8);
    Write(digestAndData, 80, 4, 8);
    Write(digestAndData, 88, 4, 8);
    Require(Relinker::UnwrapSelf(digestAndData) == actual);
    auto missingSections = Fixture();
    Write(missingSections, 64 + 40, 9000, 8);
    Write(missingSections, 64 + 58, 64, 2);
    Write(missingSections, 64 + 60, 2, 2);
    Write(missingSections, 64 + 62, 1, 2);
    const auto stripped = Relinker::UnwrapSelf(missingSections);
    for (auto at : {40, 41, 42, 43, 44, 45, 46, 47, 58, 59, 60, 61, 62, 63})
        Require(stripped[at] == 0);
    auto badDecodedSize = Fixture();
    Write(badDecodedSize, 56, 3, 8);
    Reject(std::move(badDecodedSize), "decoded size differs");
    auto missing = Fixture();
    Write(missing, 32, 3ull << 20, 8);
    Reject(std::move(missing), "missing plaintext ELF program segment 1");
    auto outOfBounds = Fixture();
    Write(outOfBounds, 40, 260, 8);
    Reject(std::move(outOfBounds), "outside the file");
    auto badTable = Fixture();
    Write(badTable, 24, 4097, 2);
    Reject(std::move(badTable), "Invalid SELF segment table");
    Reject({1, 2, 3, 4}, "neither a raw ELF nor a recognized SELF");

    // Test PT_NOTE segment (type 4, vaddr 0, memsz 0) does not fail coverage check
    {
        auto withNote = Fixture();
        // Modify second program header at elf+120 to be PT_NOTE (type 4) with vaddr=0 and memsz=0
        const std::size_t elf = 64;
        Write(withNote, elf + 120, 4, 4); // type = PT_NOTE (4)
        Write(withNote, elf + 120 + 16, 0, 8); // vaddr = 0
        Write(withNote, elf + 120 + 40, 0, 8); // memsz = 0
        // Now program segment 1 is a note with vaddr=0, memsz=0. Segment table only covers segment 0.
        // It should succeed without throwing "missing plaintext ELF program segment 1".
        const auto unwrappedNote = Relinker::UnwrapSelf(withNote);
        Require(!unwrappedNote.empty());
    }

    // Test PS5 version segment (type 0x6fffff01) stored past fileSize
    {
        auto withVersion = Fixture((1ull << 20) | 0x800, 0xeef51454);
        const std::size_t elf = 64;
        Write(withVersion, 16, withVersion.size(), 8); // fileSize = current size
        // Add 4 bytes for version segment data past fileSize
        withVersion.push_back('V'); withVersion.push_back('E'); withVersion.push_back('R'); withVersion.push_back('1');
        // Program segment 1 as Ps5VersionSegment at offset 516 (after segment 0 at 512..515)
        Write(withVersion, elf + 120, 0x6fffff01, 4);
        Write(withVersion, elf + 120 + 8, 516, 8); // offset in output ELF
        Write(withVersion, elf + 120 + 32, 4, 8); // size = 4
        const auto unwrappedVer = Relinker::UnwrapSelf(withVersion);
        Require(unwrappedVer.size() >= 520);
        Require(unwrappedVer[516] == 'V' && unwrappedVer[519] == '1');
    }

    // Test conflicting overlapping segment throws error
    {
        auto conflicting = Fixture();
        // Add a 2nd segment entry that writes conflicting bytes to the same range
        conflicting.insert(conflicting.begin() + 64, 32, 0);
        Write(conflicting, 24, 2, 2); // 2 segments
        Write(conflicting, 32, (1ull << 20) | 0x800, 8);
        Write(conflicting, 40, 256, 8); // file offset 256
        Write(conflicting, 48, 4, 8);
        Write(conflicting, 56, 4, 8);
        Write(conflicting, 64, (1ull << 20) | 0x800, 8);
        Write(conflicting, 72, 256 + 32, 8); // points to different data
        Write(conflicting, 80, 4, 8);
        Write(conflicting, 88, 4, 8);
        Reject(std::move(conflicting), "Conflicting overlapping SELF segment data");
    }
}
