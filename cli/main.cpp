// cpp-rcp — RELAY-conformant CLI entry point (spec §11, §13.2).
//
// Thin wrapper around rcp::cli::run(); all logic lives in <rcp/cli.hpp> so it
// can be unit-tested without spawning a subprocess.
#include <rcp/cli.hpp>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return rcp::cli::run(args, std::cin, std::cout, std::cerr);
}
