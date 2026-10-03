#pragma once

#include <cstdint>
#include <vector>

namespace Relinker {

// A raw ELF is returned unchanged. PS4/PS5 SELF images with contiguous
// plaintext, uncompressed program segments are reconstructed as raw ELF.
// Encrypted and compressed segments fail explicitly.
std::vector<std::uint8_t> UnwrapSelf(std::vector<std::uint8_t> source);

}
