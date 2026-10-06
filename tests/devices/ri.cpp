#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void ri_tests() {
  RamInterface ri;
  equal(ri.active(), false);
  equal(ri.read_word(8), 6);
  ri.write_word(12, 0x14);
  equal(ri.active(), false);
  ri.write_word(8, 0x12345678);
  equal(ri.active(), true);
  equal(ri.read_word(8), 22);
  ri.write_word(0, 0x12345678);
  equal(ri.read_word(0x20), 0x12345678);
  ri.write_word(4, 0xabcdef01);
  equal(ri.read_word(4), 0xabcdef01);
  ri.acknowledge_error();
  equal(ri.read_word(24), 1);
  equal(ri.read_word(8), 31);
  ri.write_word(24, 0xffffffff);
  equal(ri.read_word(24), 0);
  ri.write_word(28, 0);
  equal(ri.read_word(28), 255);
  ri.power(true);
  equal(ri.active(), true);
  equal(ri.read_word(4), 0xabcdef01);
  ri.power();
  equal(ri.active(), false);
  equal(ri.read_word(28), 0);
}

} // namespace test
