#include "prx/libc/include/general/VabiMacros.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
void APS5_VABI srandom_nid_postfix(unsigned int seed);
std::int64_t APS5_VABI random_nid_postfix();
unsigned int APS5_VABI sleep_nid_postfix(unsigned int seconds);
int APS5_VABI __inet_aton_nid_postfix(const char* text, void* address);
std::uint32_t APS5_VABI __inet_addr_nid_postfix(const char* text);
char* APS5_VABI __inet_ntoa_nid_postfix(std::uint32_t address);
}

static void Require(bool condition) {
    if (!condition) std::abort();
}

int main() {
    srandom_nid_postfix(42);
    const auto first = random_nid_postfix();
    const auto second = random_nid_postfix();
    Require(first >= 0 && first <= 0x7fffffff && second >= 0 && second <= 0x7fffffff);
    Require(first != second);
    srandom_nid_postfix(42);
    Require(random_nid_postfix() == first && random_nid_postfix() == second);
    srandom_nid_postfix(0);
    Require(random_nid_postfix() >= 0);

    Require(sleep_nid_postfix(0) == 0);
    const auto before = std::chrono::steady_clock::now();
    Require(sleep_nid_postfix(1) == 0);
    Require(std::chrono::steady_clock::now() - before >= std::chrono::milliseconds(900));

    std::uint8_t bytes[4]{9, 9, 9, 9};
    Require(__inet_aton_nid_postfix("127.0.0.1", bytes) == 1);
    Require(bytes[0] == 127 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1);
    Require(__inet_aton_nid_postfix("256.0.0.1", bytes) == 0 && bytes[0] == 127);
    Require(__inet_aton_nid_postfix("1.2.3.4junk", bytes) == 0);
    Require(__inet_aton_nid_postfix(nullptr, bytes) == 0);
    Require(__inet_aton_nid_postfix("1.2.3.4", nullptr) == 0);
    const std::uint32_t address = __inet_addr_nid_postfix("192.0.2.1");
    Require(std::memcmp(&address, "\xc0\0\x02\x01", 4) == 0);
    Require(std::strcmp(__inet_ntoa_nid_postfix(address), "192.0.2.1") == 0);
    Require(__inet_addr_nid_postfix("invalid") == UINT32_MAX);
    Require(__inet_addr_nid_postfix("255.255.255.255") == UINT32_MAX);
}
