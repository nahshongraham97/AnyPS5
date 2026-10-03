#pragma once

#include <cstdint>
#include <vector>

namespace Relinker {

// A raw ELF is returned unchanged. A SELF with plaintext, uncompressed
// segments is reconstructed as a raw ELF. Protected segments fail explicitly.
std::vector<std::uint8_t> UnwrapSelf(std::vector<std::uint8_t> source);

}
