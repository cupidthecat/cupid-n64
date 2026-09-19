#include "test.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    const std::string_view filter = argc > 1 ? argv[1] : "";
    unsigned passed = 0;
    unsigned failed = 0;
    for (const auto& test : test::cases()) {
        if (test.name.find(filter) == std::string::npos)
            continue;
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "FAIL " << test.name << ": unexpected exception\n";
        }
    }
    std::cout << "Tests: " << passed + failed << ", passed: " << passed << ", failed: " << failed << '\n';
    if (passed + failed == 0) {
        std::cerr << "No tests matched the requested filter.\n";
        return 2;
    }
    return failed == 0 ? 0 : 1;
}
