#include "prx/libc/include/general/VabiMacros.hpp"
#include "SceTypes.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
extern "C" {
void* APS5_VABI dlopen_nid_postfix(const char*, int);
void* APS5_VABI dlsym_nid_postfix(void*, const char*);
int APS5_VABI dlclose_nid_postfix(void*);
char* APS5_VABI dlerror_nid_postfix();
int APS5_VABI sceKernelDlsym(KernelModule, const char*, void**);
int APS5_VABI _sceKernelRtldThreadAtexitIncrement_nid_postfix(const void*);
int APS5_VABI _sceKernelRtldThreadAtexitDecrement_nid_postfix(const void*);
}

#ifdef _WIN32
#define EXPORT_MAIN __declspec(dllexport)
#else
#define EXPORT_MAIN __attribute__((visibility("default")))
#endif

extern "C" {
EXPORT_MAIN void MainExportedTestFunction() {}
}

static void Require(bool value) { if (!value) std::abort(); }
template<typename TFunction>
static bool ThrowsRuntimeError(TFunction function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}
int main(int argc, char** argv) {
    Require(argc == 2);
    Require(dlerror_nid_postfix() == nullptr);
    Require(dlopen_nid_postfix(argv[1], 0x2000) == nullptr);
    Require(dlerror_nid_postfix() != nullptr);
    Require(dlerror_nid_postfix() == nullptr);
    Require(dlopen_nid_postfix("anyps5-missing-module-for-test.prx", 2) == nullptr);
    Require(dlerror_nid_postfix() != nullptr);
    void* executable = dlopen_nid_postfix(nullptr, 2);
    Require(executable != nullptr && dlclose_nid_postfix(executable) == 0);
    void* module = dlopen_nid_postfix(argv[1], 2 | 0x100);
    Require(module != nullptr);
    using Add = int (APS5_VABI *)(int, int);
    auto add = reinterpret_cast<Add>(dlsym_nid_postfix(module, "GuestModuleAdd"));
    Require(add && add(17, 25) == 42);
    void* resolved = nullptr;
    Require(sceKernelDlsym(1, "sceKernelDlsym", &resolved) == 0 && resolved != nullptr);
    resolved = nullptr;
    Require(sceKernelDlsym(0x2001, "sceKernelDlsym", &resolved) == 0 && resolved != nullptr);
    Require(sceKernelDlsym(1, "anyps5_missing_symbol", &resolved) != 0 && resolved == nullptr);
    Require(sceKernelDlsym(1, "sceKernelDlsym", nullptr) != 0);
    resolved = nullptr;
    Require(sceKernelDlsym(0, "MainExportedTestFunction", &resolved) == 0 && resolved != nullptr);

    // Verify scriptingGetMem resolution and execution via sceKernelDlsym handle 0
    resolved = nullptr;
    Require(sceKernelDlsym(0, "scriptingGetMem", &resolved) == 0 && resolved != nullptr);
    using ScriptingGetMemFn = void* (APS5_VABI *)(std::uint64_t, std::uint64_t);
    using ScriptingFreeMemFn = void (APS5_VABI *)(void*);
    auto getMem = reinterpret_cast<ScriptingGetMemFn>(resolved);
    void* allocated = getMem(0x1000, 0x40000);
    Require(allocated != nullptr);
    Require((reinterpret_cast<std::uintptr_t>(allocated) & 0xfff) == 0);

    // Verify scriptingFreeMem
    void* freeResolved = nullptr;
    Require(sceKernelDlsym(0, "scriptingFreeMem", &freeResolved) == 0 && freeResolved != nullptr);
    auto freeMem = reinterpret_cast<ScriptingFreeMemFn>(freeResolved);
    freeMem(allocated);

    // Verify NID resolution for ayuoL6Vjz2k (NID of scriptingGetMem)
    void* nidResolved = nullptr;
    Require(sceKernelDlsym(0, "ayuoL6Vjz2k", &nidResolved) == 0 && nidResolved == resolved);

    resolved = reinterpret_cast<void*>(1);
    Require(sceKernelDlsym(0, "nonexistent_main_symbol", &resolved) != 0 && resolved == nullptr);
    Require(sceKernelDlsym(0, "MainExportedTestFunction", nullptr) != 0);
    resolved = nullptr;
    Require(sceKernelDlsym(static_cast<KernelModule>(reinterpret_cast<std::uintptr_t>(module)),
                           "GuestModuleAdd", &resolved) == 0 && resolved == reinterpret_cast<void*>(add));

    const auto streamingAssets = std::filesystem::path("app0") / "Media" / "StreamingAssets";
    std::filesystem::create_directories(streamingAssets);
    std::ofstream(streamingAssets / "Master.strings.bank", std::ios::binary).put('\0');
    std::ofstream(streamingAssets / "Master.bank", std::ios::binary).put('\0');
    resolved = nullptr;
    Require(sceKernelDlsym(static_cast<KernelModule>(reinterpret_cast<std::uintptr_t>(module)),
                           "FMOD_Studio_System_GetBus", &resolved) == 0 && resolved != nullptr);
    using GetBus = int (APS5_VABI *)(void*, const char*, void**);
    void* bus = nullptr;
    Require(reinterpret_cast<GetBus>(resolved)(reinterpret_cast<void*>(0x1234), "bus:/Master Bus/Dialogue", &bus) == 0);
    Require(bus == reinterpret_cast<void*>(0x4242));
    void* loadCountAddress = nullptr;
    Require(sceKernelDlsym(static_cast<KernelModule>(reinterpret_cast<std::uintptr_t>(module)),
                           "GuestModuleFmodLoadCount", &loadCountAddress) == 0);
    using LoadCount = int (APS5_VABI *)();
    Require(reinterpret_cast<LoadCount>(loadCountAddress)() == 2);
    std::filesystem::remove(streamingAssets / "Master.strings.bank");
    std::filesystem::remove(streamingAssets / "Master.bank");
    std::filesystem::remove(streamingAssets);
    std::filesystem::remove(streamingAssets.parent_path());
    std::filesystem::remove(streamingAssets.parent_path().parent_path());
    Require(dlsym_nid_postfix(reinterpret_cast<void*>(-2), "GuestModuleAdd") == reinterpret_cast<void*>(add));
#ifndef _WIN32
    auto mul = reinterpret_cast<Add>(dlsym_nid_postfix(module, "GuestModuleMul"));
    Require(mul && mul(6, 7) == 42);
    auto sub = reinterpret_cast<Add>(dlsym_nid_postfix(module, "GuestModuleSub"));
    Require(sub && sub(50, 8) == 42);
#endif
    Require(dlsym_nid_postfix(module, "missing_symbol") == nullptr);
    std::thread other([] { Require(dlerror_nid_postfix() == nullptr); });
    other.join();
    Require(dlerror_nid_postfix() != nullptr);
    Require(dlerror_nid_postfix() == nullptr);
    void* second = dlopen_nid_postfix(argv[1], 1);
    Require(second && second != module);
    Require(dlclose_nid_postfix(module) == 0);
    Require(dlsym_nid_postfix(module, "GuestModuleAdd") == nullptr);
    add = reinterpret_cast<Add>(dlsym_nid_postfix(second, "GuestModuleAdd"));
    Require(add && add(2, 3) == 5);
    Require(dlclose_nid_postfix(second) == 0);
    Require(dlclose_nid_postfix(second) == -1);

    const std::filesystem::path guest = "anyps5-relinked-module-for-test.prx";
    auto relinked = guest;
    relinked += ".guest.prx";
    {
        std::ofstream elf(guest, std::ios::binary);
        elf.write("\x7f" "ELF", 4);
    }
    std::filesystem::copy_file(argv[1], relinked, std::filesystem::copy_options::overwrite_existing);
    void* redirected = dlopen_nid_postfix(guest.string().c_str(), 2);
    Require(redirected != nullptr);
    add = reinterpret_cast<Add>(dlsym_nid_postfix(redirected, "GuestModuleAdd"));
    Require(add && add(40, 2) == 42);
    Require(dlclose_nid_postfix(redirected) == 0);
    std::filesystem::remove(guest);
    std::filesystem::remove(relinked);

    void* pinned = dlopen_nid_postfix(argv[1], 2);
    add = reinterpret_cast<Add>(dlsym_nid_postfix(pinned, "GuestModuleAdd"));
    Require(add && _sceKernelRtldThreadAtexitIncrement_nid_postfix(reinterpret_cast<const void*>(add)) == 0);
    Require(_sceKernelRtldThreadAtexitIncrement_nid_postfix(reinterpret_cast<const void*>(add)) == 0);
    Require(dlclose_nid_postfix(pinned) == 0);
    Require(add(20, 22) == 42);
    Require(_sceKernelRtldThreadAtexitDecrement_nid_postfix(reinterpret_cast<const void*>(add)) == 0);
    Require(add(40, 2) == 42);
    Require(_sceKernelRtldThreadAtexitDecrement_nid_postfix(reinterpret_cast<const void*>(add)) == 0);
    Require(_sceKernelRtldThreadAtexitIncrement_nid_postfix(reinterpret_cast<const void*>(&ThrowsRuntimeError<void (*)()>)) == 0);
    Require(_sceKernelRtldThreadAtexitDecrement_nid_postfix(reinterpret_cast<const void*>(&ThrowsRuntimeError<void (*)()>)) == 0);
    Require(ThrowsRuntimeError([] { _sceKernelRtldThreadAtexitDecrement_nid_postfix(reinterpret_cast<const void*>(&Require)); }));
    int local = 0;
    Require(ThrowsRuntimeError([&] { _sceKernelRtldThreadAtexitIncrement_nid_postfix(&local); }));
}
