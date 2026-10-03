#include <relinker/parsing/SelfImage.hpp>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Write(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
std::vector<std::uint8_t> Fixture(std::uint64_t flags = 1ull << 20) {
    std::vector<std::uint8_t> self(260);
    Write(self, 0, 0x1d3d154f, 4);
    Write(self, 24, 1, 2);
    Write(self, 32, flags, 8);
    Write(self, 40, 256, 8);
    Write(self, 48, 4, 8);
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
        assert(std::string(error.what()).find(expected) != std::string::npos);
        return;
    }
    assert(false && "The malformed or protected SELF was accepted");
}
}

int main() {
    auto expected = Fixture();
    const auto actual = Relinker::UnwrapSelf(expected);
    assert(actual.size() == 516);
    assert(actual[0] == 0x7f && actual[512] == 0x12 && actual[515] == 0x78);
    assert(Relinker::UnwrapSelf(actual) == actual);
    Reject(Fixture((1ull << 20) | 2), "encrypted segment 1");
    Reject(Fixture((1ull << 20) | 8), "compressed or blocked segment 1");
    Reject(Fixture((1ull << 20) | 0x800), "compressed or blocked segment 1");
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
}
