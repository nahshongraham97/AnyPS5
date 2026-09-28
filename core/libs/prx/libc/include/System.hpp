#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_SYSTEM_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_SYSTEM_HPP

namespace LibcSystem {

template<typename Runner>
int Invoke(const char* command, Runner&& runner) {
    const int result = runner(command);
    if (command == nullptr) return result == 0 ? 0 : 1;
    if (result == -1) return -1;
#ifdef _WIN32
    // The Windows CRT returns a raw exit code; the guest API expects wait status.
    return (result & 0xff) << 8;
#else
    return result;
#endif
}

}

#endif
