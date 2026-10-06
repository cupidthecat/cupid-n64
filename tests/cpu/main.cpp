#include "../support/test.hpp"

int main() {
  test::integer_tests();
  test::memory_tests();
  test::control_tests();
  test::cache_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
