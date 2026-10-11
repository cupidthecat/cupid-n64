#include "../../support/test.hpp"
#include <stdexcept>
#include <string_view>

namespace test {
void gpu_crash_reset_tests(bool before_poll);
}

int main(int argc, char **argv) {
  try {
    const bool early = argc == 2 && std::string_view(argv[1]) == "--early";
    if (!early)
      test::gpu_crash_reset_tests(false);
    if (!test::failures)
      test::gpu_crash_reset_tests(true);
  } catch (const std::runtime_error &error) {
    std::cerr << error.what() << '\n';
    return 77;
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
