#include "../support/test.hpp"

int main() {
  test::integer_tests();
  test::memory_tests();
  test::tlb_lookup_tests();
  test::ram_address_tests();
  test::native_address_tests();
  test::linked_load_tests();
  test::merge_store_tests();
  test::control_tests();
  test::timer_phase_tests();
  test::control_dispatch_tests();
  test::cop2_memory_tests();
  test::random_tests();
  test::cache_tests();
  test::cache_coherence_tests();
  test::cache_refill_tests();
  test::frozen_cache_tests();
  test::execution_tests();
  test::native_memory_tests();
  test::native_timing_tests();
  test::native_arithmetic_timing_tests();
  test::native_merge_linked_timing_tests();
  test::native_control_trap_timing_tests();
  test::native_control_noop_timing_tests();
  test::native_byte_order_tests();
  test::native_code_write_tests();
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
