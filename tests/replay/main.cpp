#include "check.hpp"
#include <iterator>

namespace test::machine_replay {
void cpu_tests();
void timing_tests();
void rsp_tests();
void input_tests();
} // namespace test::machine_replay

int main(int argc, char **argv) {
  using namespace test::machine_replay;
  if (argc == 3 && std::string_view(argv[1]) == "--replay") {
    std::ifstream file(argv[2], std::ios::binary);
    if (!file)
      return 2;
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                          std::istreambuf_iterator<char>()};
    try {
      const auto difference = compare(decode(bytes));
      if (difference)
        print(*difference);
      return difference ? 1 : 0;
    } catch (const cupid::n64::state::InvalidState &) {
      return 2;
    }
  }
  input_tests();
  timing_tests();
  if (!test::failures)
    cpu_tests();
  if (!test::failures)
    rsp_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
