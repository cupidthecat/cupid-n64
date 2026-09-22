#include "cupid/desktop/application.hpp"

#include <SDL3/SDL_main.h>

#include <exception>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    try {
        std::vector<std::string_view> arguments;
        for (int index = 1; index < argc; ++index)
            arguments.emplace_back(argv[index]);
        std::string error;
        auto options = cupid::desktop::parse_launch_options(arguments, error);
        if (!options) {
            std::cerr << error << '\n' << cupid::desktop::usage();
            return 2;
        }
        if (options->help) {
            std::cout << cupid::desktop::usage() << '\n' << cupid::host::usage();
            return 0;
        }
        return cupid::desktop::run_application(std::move(*options));
    } catch (const std::exception& error) {
        std::cerr << "Cupid-N64: " << error.what() << '\n';
        return 1;
    }
}
