#include <relinker/parsing/SelfImage.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace Relinker {
namespace {
constexpr std::uint64_t MaxReconstructedSize = 2ull * 1024 * 1024 * 1024;
constexpr std::uint64_t Ps4SelfMagic = 0x1d3d154f;
constexpr std::uint64_t Ps5SelfMagic = 0xeef51454;
constexpr std::uint64_t HasBlocks = 0x800;
constexpr std::uint32_t Ps5VersionSegment = 0x6fffff01;

bool Fits(const std::vector<std::uint8_t>& bytes, std::uint64_t offset, std::uint64_t size) {
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::uint64_t Read(const std::vector<std::uint8_t>& bytes, std::uint64_t offset, unsigned width) {
    if (!Fits(bytes, offset, width)) throw std::runtime_error("Truncated SELF or ELF header");
    std::uint64_t value = 0;
    for (unsigned index = 0; index < width; ++index)
        value |= std::uint64_t(bytes[static_cast<std::size_t>(offset + index)]) << (index * 8);
    return value;
}

struct Program { std::uint32_t type; std::uint64_t offset, size; };
struct Segment { std::uint64_t flags, offset, size, decodedSize; };
}

std::vector<std::uint8_t> UnwrapSelf(std::vector<std::uint8_t> source) {
    if (Fits(source, 0, 4) && source[0] == 0x7f && source[1] == 'E' &&
        source[2] == 'L' && source[3] == 'F') return source;
    if (!Fits(source, 0, 4) ||
        (Read(source, 0, 4) != Ps4SelfMagic && Read(source, 0, 4) != Ps5SelfMagic))
        throw std::runtime_error("Input is neither a raw ELF nor a recognized SELF image");
    if (!Fits(source, 0, 32)) throw std::runtime_error("Truncated SELF header");
    if (source[6] != 1) throw std::runtime_error("Unsupported SELF endianness");
    const auto count = Read(source, 24, 2);
    if (count == 0 || count > 4096 || !Fits(source, 32, count * 32))
        throw std::runtime_error("Invalid SELF segment table");
    const auto elfOffset = 32 + count * 32;
    if (!Fits(source, elfOffset, 64) || Read(source, elfOffset, 4) != 0x464c457f ||
        source[static_cast<std::size_t>(elfOffset + 4)] != 2 ||
        source[static_cast<std::size_t>(elfOffset + 5)] != 1 ||
        Read(source, elfOffset + 18, 2) != 62)
        throw std::runtime_error("SELF does not contain a supported little-endian x86-64 ELF header");
    const auto phoff = Read(source, elfOffset + 32, 8);
    const auto phentsize = Read(source, elfOffset + 54, 2);
    const auto phcount = Read(source, elfOffset + 56, 2);
    if (phentsize != 56 || phcount == 0 || phcount > 4096 ||
        phoff > std::numeric_limits<std::uint64_t>::max() - elfOffset ||
        !Fits(source, elfOffset + phoff, phcount * phentsize))
        throw std::runtime_error("Invalid SELF embedded ELF program headers");

    std::vector<Program> programs;
    std::uint64_t outputSize = std::max<std::uint64_t>(64, phoff + phcount * phentsize);
    for (std::uint64_t index = 0; index < phcount; ++index) {
        const auto at = elfOffset + phoff + index * phentsize;
        const auto type = static_cast<std::uint32_t>(Read(source, at, 4));
        const auto offset = Read(source, at + 8, 8);
        const auto size = Read(source, at + 32, 8);
        if (offset > MaxReconstructedSize || size > MaxReconstructedSize - offset)
            throw std::runtime_error("SELF ELF program segment exceeds the reconstruction limit");
        programs.push_back({type, offset, size});
        outputSize = std::max(outputSize, offset + size);
    }
    if (outputSize > MaxReconstructedSize)
        throw std::runtime_error("SELF ELF exceeds the reconstruction limit");

    std::vector<Segment> segments;
    for (std::uint64_t index = 0; index < count; ++index) {
        const auto at = 32 + index * 32;
        segments.push_back({Read(source, at, 8), Read(source, at + 8, 8),
                            Read(source, at + 16, 8), Read(source, at + 24, 8)});
    }
    std::vector<std::uint8_t> elf(static_cast<std::size_t>(outputSize), 0);
    std::vector<std::uint8_t> covered(elf.size(), 0);
    const auto copy = [&](std::uint64_t destination, std::uint64_t offset, std::uint64_t size) {
        if (!Fits(source, offset, size) || destination > elf.size() || size > elf.size() - destination)
            throw std::runtime_error("SELF segment range is outside the file or ELF image");
        for (std::uint64_t i = 0; i < size; ++i) {
            const auto target = static_cast<std::size_t>(destination + i);
            const auto value = source[static_cast<std::size_t>(offset + i)];
            if (covered[target] && elf[target] != value)
                throw std::runtime_error("Conflicting overlapping SELF segment data");
            elf[target] = value;
            covered[target] = 1;
        }
    };
    copy(0, elfOffset, 64);
    copy(phoff, elfOffset + phoff, phcount * phentsize);
    // The relinker consumes program headers and dynamic tags, not sections.
    // A SELF may retain stale section offsets without embedding that table.
    // Drop the optional table rather than copying unrelated SELF metadata.
    std::fill(elf.begin() + 40, elf.begin() + 48, 0);
    std::fill(elf.begin() + 58, elf.begin() + 64, 0);
    // A version segment can be stored after the logical end of a SELF.
    const auto fileSize = Read(source, 16, 8);
    for (const auto& program : programs)
        if (program.type == Ps5VersionSegment && program.size &&
            Fits(source, fileSize, program.size))
            copy(program.offset, fileSize, program.size);
    for (const auto& segment : segments) {
        const auto id = (segment.flags >> 20) & 0xffff;
        if (id >= programs.size() || !programs[id].size) continue;
        // Entries without HasBlocks commonly describe signatures or digests,
        // not program bytes. Retain the older plain-entry layout only when
        // its stored and decoded sizes match the entire program segment.
        if (!(segment.flags & HasBlocks)) {
            const bool hasDataEntry = std::any_of(segments.begin(), segments.end(), [id](const Segment& other) {
                return (other.flags & HasBlocks) && ((other.flags >> 20) & 0xffff) == id;
            });
            if (hasDataEntry || segment.size != programs[id].size ||
                (segment.decodedSize && segment.decodedSize != programs[id].size)) continue;
        }
        if (segment.flags & 2) throw std::runtime_error("SELF contains encrypted segment " + std::to_string(id) + "; supply a legally decrypted ELF image");
        if (segment.flags & 8) throw std::runtime_error("SELF contains compressed segment " + std::to_string(id) + "; extraction is unsupported");
        if (segment.size != programs[id].size)
            throw std::runtime_error("SELF segment " + std::to_string(id) + " size differs from its ELF program header");
        if (segment.decodedSize && segment.decodedSize != programs[id].size)
            throw std::runtime_error("SELF segment " + std::to_string(id) + " decoded size differs from its ELF program header");
        copy(programs[id].offset, segment.offset, segment.size);
    }
    for (std::size_t index = 0; index < programs.size(); ++index) {
        const auto& program = programs[index];
        if (!std::all_of(covered.begin() + static_cast<std::ptrdiff_t>(program.offset),
                         covered.begin() + static_cast<std::ptrdiff_t>(program.offset + program.size),
                         [](std::uint8_t value) { return value != 0; }))
            throw std::runtime_error("SELF is missing plaintext ELF program segment " + std::to_string(index));
    }
    return elf;
}
}
