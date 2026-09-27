#include <elfpatcher/windows/WindowsImportBuilder.hpp>
#include <io/BufferUtils.hpp>
#include <iostream>
#include <stdexcept>

using namespace Elfpatcher::Windows;

static void Check(const std::vector<std::string>& input, const std::vector<std::string>& expected) {
    Domain::SysVDynamicSection dynamic{};
    for (const auto& name : input) {
        Io::AppendU64(dynamic.DynamicSegmentData, 1);
        Io::AppendU64(dynamic.DynamicSegmentData, dynamic.DynStrData.size());
        Io::AppendString(dynamic.DynStrData, name);
    }
    if (WindowsImportBuilder().ReadLibraries(dynamic) != expected)
        throw std::runtime_error("Unexpected Windows dependency search order");
}

int main() {
    try {
        for (const auto* suffix : {".prx", ".so", ".sprx"}) {
            const auto name = std::string("libSceLibcInternal") + suffix;
            Check({name, "libkernel_sys.so"}, {name, "libkernel_sys.so", "libc.prx"});
            Check({name, "libc.prx"}, {name, "libc.prx"});
            Check({"libc.prx", name}, {"libc.prx", name});
        }
        Check({}, {});
        Check({"libkernel_sys.so"}, {"libkernel_sys.so"});
        Check({"libSceLibcInternal.so.backup"}, {"libSceLibcInternal.so.backup"});
        Check({"libSceLibcInternal.so", "libSceLibcInternal.sprx"},
              {"libSceLibcInternal.so", "libSceLibcInternal.sprx", "libc.prx"});
        std::cout << "13 Windows dependency cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
