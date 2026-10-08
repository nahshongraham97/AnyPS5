#include "prx/libkernel/System/include/SyncOnAddress.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <new>
#include <limits>
#include <unordered_map>
#include <vector>

extern "C" int* APS5_VABI __error_nid_postfix();

namespace {

constexpr int INVALID_ARGUMENT = static_cast<int>(0x80020016u);
constexpr int TIMED_OUT = static_cast<int>(0x8002003cu);
constexpr int OUT_OF_MEMORY = static_cast<int>(0x8002000cu);
constexpr auto POLL_INTERVAL = std::chrono::milliseconds(10);

struct GuestTimespec {
    std::int64_t seconds;
    std::int64_t nanoseconds;
};

struct Waiter {
    std::condition_variable condition;
    bool woken = false;
};

std::mutex registryMutex;
std::unordered_map<std::uintptr_t, std::vector<Waiter*>> registry;

template<typename T>
T Load(const volatile T* address) {
    return std::atomic_ref<T>(*const_cast<T*>(address)).load(std::memory_order_acquire);
}

template<typename T>
int Wait(volatile T* address, T expected, const std::uint32_t* timeoutMicros) {
    if (!address || (reinterpret_cast<std::uintptr_t>(address) % std::atomic_ref<T>::required_alignment) != 0)
        return INVALID_ARGUMENT;
    if (Load(address) != expected) return 0;
    const bool finite = timeoutMicros != nullptr;
    if (finite && *timeoutMicros == 0) return TIMED_OUT;
    const auto deadline = finite
        ? std::chrono::steady_clock::now() + std::chrono::microseconds(*timeoutMicros)
        : std::chrono::steady_clock::time_point::max();

    Waiter waiter;
    const auto key = reinterpret_cast<std::uintptr_t>(address);
    std::unique_lock lock(registryMutex);
    registry[key].push_back(&waiter);
    int result = 0;
    for (;;) {
        if (waiter.woken || Load(address) != expected) break;
        const auto now = std::chrono::steady_clock::now();
        if (finite && now >= deadline) { result = TIMED_OUT; break; }
        const auto next = finite ? std::min(deadline, now + POLL_INTERVAL) : now + POLL_INTERVAL;
        waiter.condition.wait_until(lock, next);
    }
    auto& waiters = registry.at(key);
    waiters.erase(std::find(waiters.begin(), waiters.end(), &waiter));
    if (waiters.empty()) registry.erase(key);
    return result;
}

}

std::size_t SyncOnAddress::Waiting(const volatile void* address) {
    std::lock_guard lock(registryMutex);
    const auto found = registry.find(reinterpret_cast<std::uintptr_t>(address));
    return found == registry.end() ? 0 : found->second.size();
}

extern "C" {

int APS5_VABI sceKernelSyncOnAddressWait(volatile std::uint32_t* address,
                                        std::uint32_t expected, const std::uint32_t* timeoutMicros,
                                        const char* name) {
    (void)name;
    try { return Wait(address, expected, timeoutMicros); }
    catch (const std::bad_alloc&) { return OUT_OF_MEMORY; }
    catch (...) { return INVALID_ARGUMENT; }
}

int APS5_VABI sceKernelSyncOnAddressWait32(volatile std::uint32_t* address,
                                          std::uint32_t expected, const std::uint32_t* timeoutMicros) {
    try { return Wait(address, expected, timeoutMicros); }
    catch (const std::bad_alloc&) { return OUT_OF_MEMORY; }
    catch (...) { return INVALID_ARGUMENT; }
}

int APS5_VABI sceKernelSyncOnAddressWait64(volatile std::uint64_t* address,
                                          std::uint64_t expected, const std::uint32_t* timeoutMicros) {
    try { return Wait(address, expected, timeoutMicros); }
    catch (const std::bad_alloc&) { return OUT_OF_MEMORY; }
    catch (...) { return INVALID_ARGUMENT; }
}

int APS5_VABI sceKernelSyncOnAddressWake(volatile void* address, std::int32_t count) {
    if (!address || (reinterpret_cast<std::uintptr_t>(address) % alignof(std::uint32_t)) != 0 || count < 0)
        return INVALID_ARGUMENT;
    if (count == 0) return 0;
    std::lock_guard lock(registryMutex);
    const auto found = registry.find(reinterpret_cast<std::uintptr_t>(address));
    if (found == registry.end()) return 0;
    for (Waiter* waiter : found->second) {
        if (waiter->woken) continue;
        waiter->woken = true;
        waiter->condition.notify_one();
        if (--count == 0) break;
    }
    return 0;
}

int APS5_VABI _umtx_op_nid_postfix(void* object, int operation, std::uint64_t value,
                                   void* timeoutSize, void* timeout) {
    constexpr int Wait = 2;
    constexpr int Wake = 3;
    constexpr int CvSignal = 9;
    constexpr int CvBroadcast = 10;
    constexpr int WaitUint = 11;
    constexpr int WaitUintPrivate = 15;
    constexpr int WakePrivate = 16;

    std::uint32_t timeoutMicros;
    const std::uint32_t* timeoutPointer = nullptr;
    if (timeout != nullptr) {
        const auto size = reinterpret_cast<std::uintptr_t>(timeoutSize);
        if (size < sizeof(GuestTimespec)) { *__error_nid_postfix() = 22; return -1; }
        const auto& duration = *static_cast<const GuestTimespec*>(timeout);
        if (duration.seconds < 0 || duration.nanoseconds < 0 || duration.nanoseconds >= 1000000000) {
            *__error_nid_postfix() = 22;
            return -1;
        }
        const auto micros = static_cast<std::uint64_t>(duration.seconds) * UINT64_C(1000000) +
            (static_cast<std::uint64_t>(duration.nanoseconds) + 999) / 1000;
        timeoutMicros = static_cast<std::uint32_t>(std::min<std::uint64_t>(micros, UINT32_MAX));
        timeoutPointer = &timeoutMicros;
    }

    int result;
    if (operation == Wait) {
        result = sceKernelSyncOnAddressWait64(static_cast<volatile std::uint64_t*>(object), value, timeoutPointer);
    } else if (operation == WaitUint || operation == WaitUintPrivate) {
        result = sceKernelSyncOnAddressWait32(static_cast<volatile std::uint32_t*>(object),
            static_cast<std::uint32_t>(value), timeoutPointer);
    } else if (operation == Wake || operation == WakePrivate ||
               operation == CvSignal || operation == CvBroadcast) {
        const auto requested = operation == CvSignal ? UINT64_C(1) :
            operation == CvBroadcast ? static_cast<std::uint64_t>(INT32_MAX) : value;
        const auto count = static_cast<std::int32_t>(std::min<std::uint64_t>(requested, INT32_MAX));
        result = sceKernelSyncOnAddressWake(object, count);
    } else {
        *__error_nid_postfix() = 78;
        return -1;
    }

    if (result == 0) return 0;
    if (result == TIMED_OUT) *__error_nid_postfix() = 60;
    else if (result == OUT_OF_MEMORY) *__error_nid_postfix() = 12;
    else *__error_nid_postfix() = 22;
    return -1;
}

}
