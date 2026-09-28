#include "prx/libc/include/System.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>

extern "C" int APS5_VABI system_nid_postfix(const char* command);

namespace {

void Require(bool condition) {
    if (!condition) std::abort();
}

const char* receivedCommand;
int availabilityResult;

int FakeSystemRunner(const char* command) {
    receivedCommand = command;
    return availabilityResult;
}

int FakeStatusRunner(const char* command) {
    receivedCommand = command;
#ifdef _WIN32
    return 7;
#else
    return 7 << 8;
#endif
}

int FakeFailureRunner(const char*) {
    errno = EACCES;
    return -1;
}

}

int main() {
    availabilityResult = 42;
    Require(LibcSystem::Invoke(nullptr, FakeSystemRunner) == 1);
    Require(receivedCommand == nullptr);
    availabilityResult = 0;
    Require(LibcSystem::Invoke(nullptr, FakeSystemRunner) == 0);

    constexpr char quotedCommand[] = "fixture \"quoted argument\"";
    Require(LibcSystem::Invoke(quotedCommand, FakeStatusRunner) == (7 << 8));
    Require(receivedCommand == quotedCommand);
    Require(std::strcmp(receivedCommand, quotedCommand) == 0);
    errno = 0;
    Require(LibcSystem::Invoke(quotedCommand, FakeFailureRunner) == -1);
    Require(errno == EACCES);

    Require(system_nid_postfix(nullptr) != 0);
#ifdef _WIN32
    constexpr char benignCommand[] = "cmd /d /c exit 7";
#else
    constexpr char benignCommand[] = "sh -c 'exit 7'";
#endif
    Require(system_nid_postfix(benignCommand) == (7 << 8));
}
