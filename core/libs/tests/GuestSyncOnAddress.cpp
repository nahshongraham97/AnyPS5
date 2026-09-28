#include "prx/libkernel/System/include/SyncOnAddress.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

namespace {
void Require(bool value) { if (!value) std::abort(); }

template<typename Predicate>
void Eventually(Predicate condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!condition() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    Require(condition());
}
}

int main() {
    constexpr int invalid = static_cast<int>(0x80020016u);
    constexpr int timedOut = static_cast<int>(0x8002003cu);
    std::uint32_t value = 7;
    std::uint64_t wide = 9;
    const std::uint32_t zero = 0, shortTimeout = 2000, longTimeout = 1000000;
    Require(sceKernelSyncOnAddressWait(nullptr, 7, nullptr) == invalid);
    Require(sceKernelSyncOnAddressWait64(nullptr, 9, nullptr) == invalid);
    Require(sceKernelSyncOnAddressWake(nullptr, 1) == invalid);
    Require(sceKernelSyncOnAddressWake(&value, -1) == invalid);
    Require(sceKernelSyncOnAddressWake(reinterpret_cast<char*>(&value) + 1, 1) == invalid);
    Require(sceKernelSyncOnAddressWait32(&value, 8, nullptr) == 0);
    Require(sceKernelSyncOnAddressWait32(&value, 7, &zero) == timedOut);
    Require(sceKernelSyncOnAddressWait64(&wide, 9, &shortTimeout) == timedOut);
    Require(SyncOnAddress::Waiting(&wide) == 0);

    std::atomic<int> completed{0};
    std::atomic<int> results{0};
    std::thread first([&] {
        results += sceKernelSyncOnAddressWait(&value, 7, &longTimeout);
        ++completed;
    });
    std::thread second([&] {
        results += sceKernelSyncOnAddressWait32(&value, 7, &longTimeout);
        ++completed;
    });
    Eventually([&] { return SyncOnAddress::Waiting(&value) == 2; });
    Require(sceKernelSyncOnAddressWake(&value, 0) == 0);
    Require(SyncOnAddress::Waiting(&value) == 2);
    Require(sceKernelSyncOnAddressWake(&value, 1) == 0);
    Eventually([&] { return completed.load() == 1 && SyncOnAddress::Waiting(&value) == 1; });
    Require(sceKernelSyncOnAddressWake(&value, 1) == 0);
    first.join();
    second.join();
    Require(completed == 2 && results == 0 && SyncOnAddress::Waiting(&value) == 0);

    std::thread wideWaiter([&] {
        results += sceKernelSyncOnAddressWait64(&wide, 9, &longTimeout);
    });
    Eventually([&] { return SyncOnAddress::Waiting(&wide) == 1; });
    std::atomic_ref<std::uint64_t>(wide).store(10, std::memory_order_release);
    wideWaiter.join();
    Require(results == 0 && SyncOnAddress::Waiting(&wide) == 0);
}
