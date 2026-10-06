#include "../support/test.hpp"

int main() {
  test::integer_tests();
  test::memory_tests();
  test::control_tests();
  test::cache_tests();
  test::execution_tests();
  test::native_memory_tests();
  test::native_loop_tests();
  test::native_branch_tests();
  test::native_entry_tests();
  test::fpu_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
