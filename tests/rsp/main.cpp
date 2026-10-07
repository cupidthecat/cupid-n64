#include "fixture.hpp"

int main() {
  test::rsp_scalar_tests();
  test::rsp_vector_tests();
  test::rsp_simd_tests();
  test::rsp_native_tests();
  test::rsp_block_tests();
  test::rsp_memory_tests();
  test::rsp_pipeline_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
