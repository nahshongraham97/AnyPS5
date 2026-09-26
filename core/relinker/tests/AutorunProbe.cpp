#include <Cli.hpp>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc == 1) {
        if (std::getenv("ANYPS5_TEST_CHILD_SIGNAL") != nullptr)
            std::raise(SIGTERM);
        return 7;
    }
    try {
        return Cli::Autorun(argv[1], false);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
