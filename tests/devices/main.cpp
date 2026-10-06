#include "fixture.hpp"

int main() {
  test::mi_tests();
  test::ri_tests();
  test::rdram_tests();
  test::pi_tests();
  test::timing_tests();
  test::cic_tests();
  test::pif_tests();
  test::gamepad_tests();
  test::rsp_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
