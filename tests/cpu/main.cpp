#include "../support/test.hpp"

int main() {
  test::integer_tests();
  test::memory_tests();
  test::linked_load_tests();
  test::merge_store_tests();
  test::control_tests();
  test::control_dispatch_tests();
  test::cop2_memory_tests();
  test::random_tests();
  test::cache_tests();
  test::cache_coherence_tests();
  test::cache_refill_tests();
  test::execution_tests();
  test::native_memory_tests();
  test::native_timing_tests();
  test::native_fpu_memory_tests();
  test::native_fpu_tests();
  test::native_loop_tests();
  test::native_boundary_tests();
  test::native_branch_tests();
  test::native_entry_tests();
  test::section_invalidation_tests();
  test::fpu_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
