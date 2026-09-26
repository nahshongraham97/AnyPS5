#include <Cli.hpp>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <cerrno>
#include <system_error>
#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace Cli {

int Autorun(const std::string& absPath, bool toWindows) {
    if (!toWindows) {
        std::filesystem::permissions(absPath,
            std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add);
    }

#ifdef _WIN32
    const auto nativePath = std::filesystem::path(absPath).wstring();
    const auto quotedName = L"\"" + nativePath + L"\"";
    const wchar_t* arguments[] = {quotedName.c_str(), nullptr};
    const auto status = _wspawnv(_P_WAIT, nativePath.c_str(), arguments);
    if (status == -1)
        throw std::system_error(errno, std::generic_category(), "Cannot launch output file");
    const int exitCode = static_cast<int>(status);
#else
    char* arguments[] = {const_cast<char*>(absPath.c_str()), nullptr};
    pid_t child;
    const int error = posix_spawn(&child, absPath.c_str(), nullptr, nullptr, arguments, environ);
    if (error != 0)
        throw std::system_error(error, std::generic_category(), "Cannot launch output file");
    int status;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR)
            throw std::system_error(errno, std::generic_category(), "Cannot wait for output file");
    }
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif

    std::cout << "\nExit code: " << exitCode << '\n';

    std::cout << "\nPress Enter to exit...\n";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();

    return exitCode;
}

}
