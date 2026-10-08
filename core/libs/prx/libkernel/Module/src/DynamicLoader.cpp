#include "prx/libc/include/General.hpp"
#include "SceTypes.hpp"
#include <nid/NidCompute.hpp>
#include <array>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#else
#include <dlfcn.h>
#endif

namespace {
thread_local std::array<char, 512> loaderError{};
thread_local bool pendingError = false;
void Error(const char* message) {
    std::snprintf(loaderError.data(), loaderError.size(), "%s", message);
    pendingError = true;
}
struct Module {
    void* native = nullptr;
    bool owned = true;
    bool global = false;
    ~Module() {
        if (owned && native) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(native));
#else
            ::dlclose(native);
#endif
        }
    }
};
std::mutex modulesMutex;
std::map<std::uintptr_t, std::shared_ptr<Module>> modules;
std::uintptr_t nextHandle = 0x20000000;
std::map<const void*, std::uintptr_t> imageIds;
void* Symbol(Module& module, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module.native), name));
#else
    return ::dlsym(module.native, name);
#endif
}
void* FindSymbol(Module& module, const char* name) {
    if (auto* symbol = Symbol(module, name)) {
        std::fprintf(stderr, "[libkernel] FindSymbol('%s') -> %p\n", name, symbol);
        return symbol;
    }
    const auto nid = Nid::ComputeNid(name, "");
#ifdef _WIN32
    if (auto* symbol = Symbol(module, nid.c_str())) {
        std::fprintf(stderr, "[libkernel] FindSymbol('%s') -> %p (via NID '%s')\n", name, symbol, nid.c_str());
        return symbol;
    }
#else
    if (auto* symbol = Symbol(module, nid.c_str())) return symbol;
    constexpr char guestSuffix[] = "#guest";
    if (auto* symbol = Symbol(module, (std::string(name) + guestSuffix).c_str())) return symbol;
    auto* symbol = Symbol(module, (nid + guestSuffix).c_str());
    if (symbol) return symbol;
#endif
    std::fprintf(stderr, "[libkernel] FindSymbol('%s') -> NOT FOUND\n", name);
    return nullptr;
}
void* FindInKernel(const char* name) {
#ifdef _WIN32
    const auto module = GetModuleHandleW(L"libkernel.prx");
    if (!module) return nullptr;
    if (auto* symbol = GetProcAddress(module, name)) return reinterpret_cast<void*>(symbol);
    const auto nid = Nid::ComputeNid(name, "");
    return reinterpret_cast<void*>(GetProcAddress(module, nid.c_str()));
#else
    if (auto* symbol = ::dlsym(RTLD_DEFAULT, name)) return symbol;
    const auto nid = Nid::ComputeNid(name, "");
    return ::dlsym(RTLD_DEFAULT, nid.c_str());
#endif
}
void* FindInMain(const char* name) {
#ifdef _WIN32
    const auto module = GetModuleHandleW(nullptr);
    if (!module) return nullptr;
    if (auto* symbol = GetProcAddress(module, name)) return reinterpret_cast<void*>(symbol);
    const auto nid = Nid::ComputeNid(name, "");
    return reinterpret_cast<void*>(GetProcAddress(module, nid.c_str()));
#else
    if (auto* symbol = ::dlsym(RTLD_DEFAULT, name)) return symbol;
    const auto nid = Nid::ComputeNid(name, "");
    return ::dlsym(RTLD_DEFAULT, nid.c_str());
#endif
}

using FmodGetBus = int (APS5_VABI *)(void*, const char*, void**);
using FmodLoadBankFile = int (APS5_VABI *)(void*, const char*, unsigned int, void**);
using FmodFlushCommands = int (APS5_VABI *)(void*);

std::mutex fmodBootstrapMutex;
FmodGetBus fmodGetBus = nullptr;
FmodLoadBankFile fmodLoadBankFile = nullptr;
FmodFlushCommands fmodFlushCommands = nullptr;
std::set<void*> fmodBootstrappedSystems;

int APS5_VABI FmodGetBusWithBankBootstrap(void* system, const char* path, void** bus) {
    const auto getBus = fmodGetBus;
    if (!getBus) return -1;

    const int initialResult = getBus(system, path, bus);
    if (initialResult == 0 || !system || !fmodLoadBankFile) return initialResult;

    constexpr const char* stringsGuestPath = "/app0/Media/StreamingAssets/Master.strings.bank";
    constexpr const char* masterGuestPath = "/app0/Media/StreamingAssets/Master.bank";
    std::error_code error;
    if (!std::filesystem::is_regular_file(ResolvePath_nid_no_patch(stringsGuestPath), error) ||
        !std::filesystem::is_regular_file(ResolvePath_nid_no_patch(masterGuestPath), error)) {
        return initialResult;
    }

    std::lock_guard lock(fmodBootstrapMutex);
    if (fmodBootstrappedSystems.insert(system).second) {
        void* masterBank = nullptr;
        void* stringsBank = nullptr;
        const int masterResult = fmodLoadBankFile(system, masterGuestPath, 0, &masterBank);
        const int stringsResult = fmodLoadBankFile(system, stringsGuestPath, 0, &stringsBank);
        const int flushResult = fmodFlushCommands ? fmodFlushCommands(system) : 0;
        std::fprintf(stderr,
                     "[libkernel] FMOD bank bootstrap after GetBus('%s')=%d: "
                     "Master.bank=%d (%p), Master.strings.bank=%d (%p), flush=%d\n",
                     path ? path : "(null)", initialResult,
                     masterResult, masterBank, stringsResult, stringsBank, flushResult);
    }
    int retryResult = getBus(system, path, bus);
    constexpr std::string_view legacyMasterPrefix = "bus:/Master Bus/";
    if (retryResult != 0 && path && std::string_view(path).starts_with(legacyMasterPrefix)) {
        const std::string normalizedPath = "bus:/" + std::string(path + legacyMasterPrefix.size());
        retryResult = getBus(system, normalizedPath.c_str(), bus);
        std::fprintf(stderr, "[libkernel] FMOD legacy bus path '%s' -> '%s': %d (%p)\n",
                     path, normalizedPath.c_str(), retryResult, bus ? *bus : nullptr);
    } else {
        std::fprintf(stderr, "[libkernel] FMOD GetBus('%s') retry -> %d (%p)\n",
                     path ? path : "(null)", retryResult, bus ? *bus : nullptr);
    }
    return retryResult;
}

void* WrapFmodSymbol(Module& module, const char* name, void* symbol) {
    if (!symbol || std::strcmp(name, "FMOD_Studio_System_GetBus") != 0) return symbol;
    std::lock_guard lock(fmodBootstrapMutex);
    fmodGetBus = reinterpret_cast<FmodGetBus>(symbol);
    if (!fmodLoadBankFile) {
        fmodLoadBankFile = reinterpret_cast<FmodLoadBankFile>(FindSymbol(module, "FMOD_Studio_System_LoadBankFile"));
    }
    if (!fmodFlushCommands) {
        fmodFlushCommands = reinterpret_cast<FmodFlushCommands>(FindSymbol(module, "FMOD_Studio_System_FlushCommands"));
    }
    return fmodLoadBankFile ? reinterpret_cast<void*>(&FmodGetBusWithBankBootstrap) : symbol;
}
}

extern "C" {

void* APS5_VABI scriptingGetMem(std::uint64_t alignment, std::uint64_t size) {
    if (alignment < 0x10u) alignment = 0x10u;
    if ((alignment & (alignment - 1u)) != 0u) return nullptr;
    if (size == 0) size = 1;
#ifdef _WIN32
    void* ptr = _aligned_malloc(size, static_cast<std::size_t>(alignment));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, static_cast<std::size_t>(alignment), size) != 0) ptr = nullptr;
#endif
    std::fprintf(stderr, "[libkernel] scriptingGetMem(alignment=0x%llx, size=0x%llx) -> %p\n",
                 static_cast<unsigned long long>(alignment),
                 static_cast<unsigned long long>(size),
                 ptr);
    return ptr;
}

void APS5_VABI scriptingFreeMem(void* ptr) {
    if (!ptr) return;
    std::fprintf(stderr, "[libkernel] scriptingFreeMem(%p)\n", ptr);
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

void* APS5_VABI ayuoL6Vjz2k(std::uint64_t alignment, std::uint64_t size) {
    return scriptingGetMem(alignment, size);
}

// SDK payloads use handle 1 (or 0x2001) for libkernel before other modules.
int APS5_VABI sceKernelDlsym(KernelModule handle, const char* name, void** address) {
    if (!address || !name || !*name) return -1;
    *address = nullptr;
    try {
        if (handle == 0) {
            if (std::strcmp(name, "scriptingGetMem") == 0 || std::strcmp(name, "ayuoL6Vjz2k") == 0) {
                *address = reinterpret_cast<void*>(&scriptingGetMem);
                std::fprintf(stderr, "[libkernel] sceKernelDlsym(handle=0, name='%s') -> %p (built-in)\n", name, *address);
                return 0;
            }
            if (std::strcmp(name, "scriptingFreeMem") == 0) {
                *address = reinterpret_cast<void*>(&scriptingFreeMem);
                std::fprintf(stderr, "[libkernel] sceKernelDlsym(handle=0, name='%s') -> %p (built-in)\n", name, *address);
                return 0;
            }
            const auto freeNid = Nid::ComputeNid("scriptingFreeMem", "");
            if (std::strcmp(name, freeNid.c_str()) == 0) {
                *address = reinterpret_cast<void*>(&scriptingFreeMem);
                std::fprintf(stderr, "[libkernel] sceKernelDlsym(handle=0, name='%s') -> %p (built-in)\n", name, *address);
                return 0;
            }
            *address = FindInMain(name);
            if (!*address) {
                *address = FindInKernel(name);
            }
        } else if (handle == 1 || handle == 0x2001) {
            if (std::strcmp(name, "scriptingGetMem") == 0 || std::strcmp(name, "ayuoL6Vjz2k") == 0) {
                *address = reinterpret_cast<void*>(&scriptingGetMem);
                std::fprintf(stderr, "[libkernel] sceKernelDlsym(handle=0x%x, name='%s') -> %p (built-in)\n", handle, name, *address);
                return 0;
            }
            if (std::strcmp(name, "scriptingFreeMem") == 0) {
                *address = reinterpret_cast<void*>(&scriptingFreeMem);
                std::fprintf(stderr, "[libkernel] sceKernelDlsym(handle=0x%x, name='%s') -> %p (built-in)\n", handle, name, *address);
                return 0;
            }
            *address = FindInKernel(name);
        } else {
            std::shared_ptr<Module> module;
            {
                std::lock_guard lock(modulesMutex);
                auto found = modules.find(static_cast<std::uint32_t>(handle));
                if (found == modules.end()) {
                    std::fprintf(stderr, "[libkernel] sceKernelDlsym(handle=0x%x, name='%s') -> module not found\n", handle, name);
                    return -1;
                }
                module = found->second;
            }
            *address = WrapFmodSymbol(*module, name, FindSymbol(*module, name));
        }
        std::fprintf(stderr, "[libkernel] sceKernelDlsym(handle=0x%x, name='%s') -> %p\n", handle, name, *address);
        return *address ? 0 : -1;
    } catch (const std::exception&) { return -1; }
}

char* APS5_VABI dlerror_nid_postfix() {
    if (!pendingError) return nullptr;
    pendingError = false;
    return loaderError.data();
}
static std::filesystem::path RelinkedModulePath(const std::filesystem::path& path) {
    std::error_code error;
    const auto pathStr = path.string();
    if (pathStr.ends_with(".guest.prx") && std::filesystem::is_regular_file(path, error)) {
        return path;
    }
    auto relinked = path;
    relinked += ".guest.prx";
    if (std::filesystem::is_regular_file(relinked, error)) {
        return relinked;
    }
    const auto ext = path.extension().string();
    if (ext != ".prx" && ext != ".sprx") {
        auto withPrx = path;
        withPrx += ".prx.guest.prx";
        if (std::filesystem::is_regular_file(withPrx, error)) {
            return withPrx;
        }
    }
    const auto filename = path.filename().string();
    const std::vector<std::filesystem::path> searchDirs = {
        ResolvePath_nid_no_patch("/app0/Media/Plugins"),
        ResolvePath_nid_no_patch("/app0/Media/Modules"),
        ResolvePath_nid_no_patch("/app0/sce_module"),
        ResolvePath_nid_no_patch("/app0"),
        std::filesystem::current_path() / "app0" / "Media" / "Plugins",
        std::filesystem::current_path() / "app0" / "Media" / "Modules",
        std::filesystem::current_path() / "app0" / "sce_module",
        std::filesystem::current_path() / "app0"
    };
    for (const auto& dir : searchDirs) {
        if (!std::filesystem::is_directory(dir, error)) continue;
        if (filename.ends_with(".guest.prx")) {
            auto cand = dir / filename;
            if (std::filesystem::is_regular_file(cand, error)) return cand;
        }
        auto candA = dir / (filename + ".guest.prx");
        if (std::filesystem::is_regular_file(candA, error)) return candA;

        auto candB = dir / (filename + ".prx.guest.prx");
        if (std::filesystem::is_regular_file(candB, error)) return candB;

        if (filename.ends_with(".prx") || filename.ends_with(".sprx")) {
            auto candC = dir / (path.stem().string() + ".guest.prx");
            if (std::filesystem::is_regular_file(candC, error)) return candC;
        }
    }
    return std::filesystem::is_regular_file(relinked, error) ? relinked : path;
}

void* APS5_VABI dlopen_nid_postfix(const char* path, int flags) {
    if ((flags & ~0x103) || (flags & 3) == 0 || (flags & 3) == 3) {
        Error("dlopen: unsupported flags"); return nullptr;
    }
    try {
        auto module = std::make_shared<Module>();
        module->global = (flags & 0x100) != 0 || !path;
#ifdef _WIN32
        std::filesystem::path resolved;
        if (!path) {
            module->native = GetModuleHandleW(nullptr);
            module->owned = false;
        } else {
            if (!*path) { Error("dlopen: empty module path"); return nullptr; }
            resolved = RelinkedModulePath(ResolvePath_nid_no_patch(path));
            std::fprintf(stderr, "[libkernel] dlopen('%s') -> resolved '%ls'\n", path, resolved.c_str());
            module->native = LoadLibraryExW(resolved.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        if (!module->native) {
            const auto err = GetLastError();
            char message[256];
            std::snprintf(message, sizeof(message), "dlopen: Windows loader error %lu for '%ls' (module must be host-compatible)", err, resolved.c_str());
            std::fprintf(stderr, "[libkernel] %s\n", message);
            Error(message); return nullptr;
        }
        std::fprintf(stderr, "[libkernel] dlopen('%s') -> success, native=%p\n", path ? path : "(main)", module->native);
#else
        const auto resolved = path ? RelinkedModulePath(ResolvePath_nid_no_patch(path)).string() : std::string{};
        const int nativeFlags = ((flags & 3) == 1 ? RTLD_LAZY : RTLD_NOW) |
            ((flags & 0x100) ? RTLD_GLOBAL : RTLD_LOCAL);
        module->native = ::dlopen(path ? resolved.c_str() : nullptr, nativeFlags);
        if (!module->native) { Error(::dlerror()); return nullptr; }
#endif
        std::lock_guard lock(modulesMutex);
        const auto handle = nextHandle++;
        modules.emplace(handle, std::move(module));
        return reinterpret_cast<void*>(handle);
    } catch (const std::exception& error) { Error(error.what()); return nullptr; }
}
void* APS5_VABI dlsym_nid_postfix(void* handle, const char* name) {
    if (!name || !*name) { Error("dlsym: empty symbol name"); return nullptr; }
    try {
        if (std::strcmp(name, "scriptingGetMem") == 0 || std::strcmp(name, "ayuoL6Vjz2k") == 0) {
            return reinterpret_cast<void*>(&scriptingGetMem);
        }
        if (std::strcmp(name, "scriptingFreeMem") == 0) {
            return reinterpret_cast<void*>(&scriptingFreeMem);
        }
        std::vector<std::shared_ptr<Module>> search;
        {
            std::lock_guard lock(modulesMutex);
            if (handle == reinterpret_cast<void*>(static_cast<std::intptr_t>(-2))) {
                for (const auto& [key, module] : modules) if (module->global) search.push_back(module);
            } else {
                auto found = modules.find(reinterpret_cast<std::uintptr_t>(handle));
                if (found == modules.end()) { Error("dlsym: invalid or unsupported module handle"); return nullptr; }
                search.push_back(found->second);
            }
        }
        for (const auto& module : search) {
            if (auto* result = FindSymbol(*module, name)) return WrapFmodSymbol(*module, name, result);
        }
        Error("dlsym: symbol not found in supported module scope");
        return nullptr;
    } catch (const std::exception& error) { Error(error.what()); return nullptr; }
}
int APS5_VABI dlclose_nid_postfix(void* handle) {
    std::shared_ptr<Module> module;
    {
        std::lock_guard lock(modulesMutex);
        auto found = modules.find(reinterpret_cast<std::uintptr_t>(handle));
        if (found == modules.end()) { Error("dlclose: invalid module handle"); return -1; }
        module = std::move(found->second);
        modules.erase(found);
    }
    // Unload outside the registry lock: module destructors may call loader APIs.
    module.reset();
    return 0;
}
std::int32_t ModuleIdForImage_nid_no_patch(const void* native) {
    std::lock_guard lock(modulesMutex);
    for (const auto& [handle, module] : modules) {
        if (module->native == native) return static_cast<std::int32_t>(handle);
    }
    const auto [found, inserted] = imageIds.emplace(native, nextHandle);
    if (inserted) ++nextHandle;
    return static_cast<std::int32_t>(found->second);
}
}
