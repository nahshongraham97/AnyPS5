#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr std::int32_t SCE_OK = 0;
constexpr std::int32_t SCE_REG_MGR_ERROR_INVALID_PARAM = static_cast<std::int32_t>(0x80060001);
constexpr std::int32_t SCE_REG_MGR_ERROR_ENTRY_NOT_FOUND = static_cast<std::int32_t>(0x80060002);

}

extern "C" {

std::int32_t APS5_VABI sceRegMgrNonSysGetInt(std::uint32_t regId, std::int32_t* value) {
    if (!value) return SCE_REG_MGR_ERROR_INVALID_PARAM;
    return SCE_REG_MGR_ERROR_ENTRY_NOT_FOUND;
}

std::int32_t APS5_VABI sceRegMgrNonSysGetStr(std::uint32_t regId, char* str, std::size_t size) {
    if (!str) return SCE_REG_MGR_ERROR_INVALID_PARAM;
    return SCE_REG_MGR_ERROR_ENTRY_NOT_FOUND;
}

std::int32_t APS5_VABI sceRegMgrNonSysGetBin(std::uint32_t regId, void* bin, std::size_t size) {
    if (!bin) return SCE_REG_MGR_ERROR_INVALID_PARAM;
    return SCE_REG_MGR_ERROR_ENTRY_NOT_FOUND;
}

std::int32_t APS5_VABI sceRegMgrNonSysSetInt(std::uint32_t regId, std::int32_t value) {
    return SCE_OK;
}

std::int32_t APS5_VABI sceRegMgrNonSysSetStr(std::uint32_t regId, const char* str) {
    return SCE_OK;
}

std::int32_t APS5_VABI sceRegMgrNonSysSetBin(std::uint32_t regId, const void* bin, std::size_t size) {
    return SCE_OK;
}

std::int32_t APS5_VABI sceRegMgrGetInt(std::uint32_t regId, std::int32_t* value) {
    if (!value) return SCE_REG_MGR_ERROR_INVALID_PARAM;
    return SCE_REG_MGR_ERROR_ENTRY_NOT_FOUND;
}

std::int32_t APS5_VABI sceRegMgrGetStr(std::uint32_t regId, char* str, std::size_t size) {
    if (!str) return SCE_REG_MGR_ERROR_INVALID_PARAM;
    return SCE_REG_MGR_ERROR_ENTRY_NOT_FOUND;
}

std::int32_t APS5_VABI sceRegMgrGetBin(std::uint32_t regId, void* bin, std::size_t size) {
    if (!bin) return SCE_REG_MGR_ERROR_INVALID_PARAM;
    return SCE_REG_MGR_ERROR_ENTRY_NOT_FOUND;
}

std::int32_t APS5_VABI sceRegMgrSetInt(std::uint32_t regId, std::int32_t value) {
    return SCE_OK;
}

std::int32_t APS5_VABI sceRegMgrSetStr(std::uint32_t regId, const char* str) {
    return SCE_OK;
}

std::int32_t APS5_VABI sceRegMgrSetBin(std::uint32_t regId, const void* bin, std::size_t size) {
    return SCE_OK;
}

std::int32_t APS5_VABI sceRegMgrOpenRegistry(std::uint32_t* handle) {
    if (handle) *handle = 1;
    return SCE_OK;
}

std::int32_t APS5_VABI sceRegMgrCloseRegistry(std::uint32_t handle) {
    return SCE_OK;
}

}
