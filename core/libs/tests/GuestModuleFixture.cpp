#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libc/include/general/ExportMacros.hpp"
#include <cstdint>
#include <string_view>
#ifdef _WIN32
#define MODULE_EXPORT __declspec(dllexport)
#else
#define MODULE_EXPORT __attribute__((visibility("default")))
#endif
extern "C" MODULE_EXPORT int APS5_VABI GuestModuleAdd_nid_postfix(int a, int b) {
    return a + b;
}

namespace {
int fmodBankLoadCount = 0;
bool fmodMasterBankLoaded = false;
}

extern "C" MODULE_EXPORT int APS5_VABI FMOD_Studio_System_GetBus_nid_postfix(void*, const char* path, void** bus) {
    if (!fmodMasterBankLoaded || !path || std::string_view(path) != "bus:/Dialogue") return 1;
    if (bus) *bus = reinterpret_cast<void*>(0x4242);
    return 0;
}

extern "C" MODULE_EXPORT int APS5_VABI FMOD_Studio_System_LoadBankFile_nid_postfix(
    void*, const char* path, unsigned int, void** bank) {
    ++fmodBankLoadCount;
    if (path && std::string_view(path).ends_with("/Master.bank")) fmodMasterBankLoaded = true;
    if (bank) *bank = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000 + fmodBankLoadCount));
    return 0;
}

extern "C" MODULE_EXPORT int APS5_VABI GuestModuleFmodLoadCount_nid_postfix() {
    return fmodBankLoadCount;
}
#ifndef _WIN32
extern "C" MODULE_EXPORT int APS5_VABI GuestModuleMul_nid_no_patch(int a, int b) {
    return a * b;
}
APS5_EXPORT("GuestModuleMul#guest", GuestModuleMul_nid_no_patch);
extern "C" MODULE_EXPORT int APS5_VABI GuestModuleSub_nid_no_patch(int a, int b) {
    return a - b;
}
APS5_EXPORT("BOyBJaKwOa8#guest", GuestModuleSub_nid_no_patch);
#endif
