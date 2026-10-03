#ifndef CORE_LIBS_PRX_LIBKERNEL_SYSTEM_SYNC_ON_ADDRESS_HPP
#define CORE_LIBS_PRX_LIBKERNEL_SYSTEM_SYNC_ON_ADDRESS_HPP

#include <cstddef>
#include <cstdint>
#include "SceTypes.hpp"

namespace SyncOnAddress {

// Diagnostic count for the native wait registry; also lets tests synchronize
// with registration instead of racing a fixed delay.
std::size_t Waiting(const volatile void* address);

}

extern "C" {
int APS5_VABI sceKernelSyncOnAddressWait(volatile std::uint32_t* address,
                                        std::uint32_t expected, const std::uint32_t* timeoutMicros,
                                        const char* name = nullptr);
int APS5_VABI sceKernelSyncOnAddressWait32(volatile std::uint32_t* address,
                                          std::uint32_t expected, const std::uint32_t* timeoutMicros);
int APS5_VABI sceKernelSyncOnAddressWait64(volatile std::uint64_t* address,
                                          std::uint64_t expected, const std::uint32_t* timeoutMicros);
int APS5_VABI sceKernelSyncOnAddressWake(volatile void* address, std::int32_t count);
}

#endif
